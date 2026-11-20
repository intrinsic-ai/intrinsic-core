// Copyright 2026 Intrinsic Innovation LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "intrinsic/geometry/api/unpack_renderable.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "intrinsic/util/status/status_macros.h"
#include "meshoptimizer.h"
#include "nlohmann/json.hpp"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"

namespace intrinsic::geo {

namespace {

using json = ::nlohmann::json;

// GLB integers are little-endian and are read and written with memcpy.
static_assert(std::endian::native == std::endian::little,
              "GLB parsing and writing assume a little-endian host");

constexpr uint32_t kGlbHeaderMagic = 0x46546C67;  // "glTF"
constexpr uint32_t kGlbVersion = 2;
constexpr uint32_t kGlbChunkJson = 0x4E4F534A;  // "JSON"
constexpr uint32_t kGlbChunkBin = 0x004E4942;   // "BIN\0"
constexpr uint64_t kGlbHeaderSize = 12;
constexpr uint64_t kGlbChunkHeaderSize = 8;
// The GLB header stores the total file length as uint32.
constexpr uint64_t kGlbMaxSize = std::numeric_limits<uint32_t>::max();
// Trailing bytes of the JSON chunk that are ignored. The specification pads
// with spaces, but some writers pad with NUL bytes, which JSON parsers reject.
constexpr absl::string_view kJsonChunkPadding(" \t\n\r\0", 5);

constexpr char kKhrMeshopt[] = "KHR_meshopt_compression";
constexpr char kKhrMeshQuantization[] = "KHR_mesh_quantization";
constexpr char kExtMeshopt[] = "EXT_meshopt_compression";
constexpr char kExtMeshGpuInstancing[] = "EXT_mesh_gpu_instancing";
constexpr char kKhrAnimationPointer[] = "KHR_animation_pointer";
constexpr char kKhrMaterialsVariants[] = "KHR_materials_variants";
constexpr char kKhrTextureTransform[] = "KHR_texture_transform";

uint64_t AlignTo4(uint64_t value) { return (value + 3) & ~uint64_t{3}; }

uint32_t ReadU32(absl::string_view data, uint64_t offset) {
  uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

// Returns true iff [offset, offset + length) lies within [0, size), without
// overflowing.
bool IsRangeInBounds(uint64_t offset, uint64_t length, uint64_t size) {
  return length <= size && offset <= size - length;
}

struct GlbChunks {
  absl::string_view json;
  absl::string_view bin;
};

// Splits a GLB container into its JSON and (optional) BIN chunks following
// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#binary-gltf-layout
absl::StatusOr<GlbChunks> SplitGlbChunks(absl::string_view glb) {
  if (glb.size() < kGlbHeaderSize) {
    return absl::InvalidArgumentError(
        "Invalid GLB buffer: data smaller than GLB header");
  }
  if (ReadU32(glb, 0) != kGlbHeaderMagic) {
    return absl::InvalidArgumentError("Invalid GLB buffer: magic is not glTF");
  }
  if (const uint32_t version = ReadU32(glb, 4); version != kGlbVersion) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Unsupported GLB version: %d", version));
  }
  if (const uint32_t total_length = ReadU32(glb, 8);
      total_length != glb.size()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Invalid GLB buffer: header length %d does not match buffer size %d",
        total_length, glb.size()));
  }

  GlbChunks chunks;
  bool has_json = false;
  uint64_t offset = kGlbHeaderSize;
  for (int chunk_index = 0; offset < glb.size(); ++chunk_index) {
    if (glb.size() - offset < kGlbChunkHeaderSize) {
      return absl::InvalidArgumentError(
          "Invalid GLB buffer: truncated chunk header");
    }
    const uint32_t chunk_length = ReadU32(glb, offset);
    const uint32_t chunk_type = ReadU32(glb, offset + 4);
    const uint64_t data_offset = offset + kGlbChunkHeaderSize;
    if (chunk_length % 4 != 0) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Invalid GLB buffer: chunk %d length %d is not 4-byte aligned",
          chunk_index, chunk_length));
    }
    if (!IsRangeInBounds(data_offset, chunk_length, glb.size())) {
      return absl::InvalidArgumentError(
          "Invalid GLB buffer: chunk length extends beyond buffer size");
    }

    const absl::string_view data = glb.substr(data_offset, chunk_length);
    if (chunk_index == 0) {
      if (chunk_type != kGlbChunkJson) {
        return absl::InvalidArgumentError(
            "Invalid GLB buffer: first chunk is not JSON");
      }
      chunks.json = data;
      has_json = true;
    } else if (chunk_type == kGlbChunkJson) {
      return absl::InvalidArgumentError(
          "Invalid GLB buffer: multiple JSON chunks");
    } else if (chunk_type == kGlbChunkBin) {
      if (chunk_index != 1) {
        return absl::InvalidArgumentError(
            "Invalid GLB buffer: BIN chunk must be the second chunk");
      }
      chunks.bin = data;
    }
    // Chunks of unknown type are ignored, as required by the specification.
    offset = data_offset + chunk_length;
  }

  if (!has_json) {
    return absl::InvalidArgumentError("Invalid GLB buffer: missing JSON chunk");
  }
  const size_t json_end = chunks.json.find_last_not_of(kJsonChunkPadding);
  chunks.json = json_end == absl::string_view::npos
                    ? absl::string_view()
                    : chunks.json.substr(0, json_end + 1);
  if (chunks.json.empty()) {
    return absl::InvalidArgumentError(
        "Invalid GLB buffer: JSON chunk is empty");
  }
  return chunks;
}

// Assembles a GLB container. The BIN chunk is omitted when `bin_chunk` is
// empty.
absl::StatusOr<std::string> AssembleGlb(absl::string_view json_chunk,
                                        absl::string_view bin_chunk) {
  const uint64_t json_size = AlignTo4(json_chunk.size());
  const uint64_t bin_size = AlignTo4(bin_chunk.size());
  const uint64_t total_length =
      kGlbHeaderSize + kGlbChunkHeaderSize + json_size +
      (bin_chunk.empty() ? 0 : kGlbChunkHeaderSize + bin_size);
  if (total_length > kGlbMaxSize) {
    return absl::OutOfRangeError(absl::StrFormat(
        "Unpacked GLB size %d exceeds the GLB limit of %d bytes", total_length,
        kGlbMaxSize));
  }

  std::string glb;
  glb.reserve(total_length);
  const auto append_u32 = [&glb](uint32_t value) {
    glb.append(reinterpret_cast<const char*>(&value), sizeof(value));
  };
  append_u32(kGlbHeaderMagic);
  append_u32(kGlbVersion);
  append_u32(static_cast<uint32_t>(total_length));
  append_u32(static_cast<uint32_t>(json_size));
  append_u32(kGlbChunkJson);
  glb.append(json_chunk);
  glb.append(json_size - json_chunk.size(), ' ');
  if (!bin_chunk.empty()) {
    append_u32(static_cast<uint32_t>(bin_size));
    append_u32(kGlbChunkBin);
    glb.append(bin_chunk);
    glb.append(bin_size - bin_chunk.size(), '\0');
  }
  return glb;
}

// Reads a non-negative integer property. Returns `default_value` if the
// property is absent, or an error if it is absent and no default is given.
absl::StatusOr<uint64_t> GetUint(const json& object, const std::string& key,
                                 std::optional<uint64_t> default_value) {
  const auto it = object.find(key);
  if (it == object.end()) {
    if (default_value.has_value()) {
      return *default_value;
    }
    return absl::InvalidArgumentError(
        absl::StrCat("Missing required property '", key, "'"));
  }
  if (!it->is_number_unsigned()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Property '", key,
                     "' must be a non-negative integer, got ", it->dump()));
  }
  return it->get<uint64_t>();
}

absl::StatusOr<std::string> GetString(
    const json& object, const std::string& key,
    std::optional<std::string> default_value) {
  const auto it = object.find(key);
  if (it == object.end()) {
    if (default_value.has_value()) {
      return *std::move(default_value);
    }
    return absl::InvalidArgumentError(
        absl::StrCat("Missing required property '", key, "'"));
  }
  if (!it->is_string()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Property '", key, "' must be a string, got ", it->dump()));
  }
  return it->get<std::string>();
}

// Returns true if `extension` is declared in `extensionsUsed` or attached to
// any bufferView.
bool UsesExtension(const json& gltf, const std::string& extension) {
  if (const auto used = gltf.find("extensionsUsed");
      used != gltf.end() && used->is_array()) {
    for (const json& name : *used) {
      if (name == extension) {
        return true;
      }
    }
  }
  if (const auto views = gltf.find("bufferViews");
      views != gltf.end() && views->is_array()) {
    for (const json& view : *views) {
      if (!view.is_object()) {
        continue;
      }
      const auto exts = view.find("extensions");
      if (exts != view.end() && exts->is_object() &&
          exts->contains(extension)) {
        return true;
      }
    }
  }
  return false;
}

void RemoveFromArray(json& parent, const std::string& key,
                     const std::string& value) {
  const auto it = parent.find(key);
  if (it == parent.end() || !it->is_array()) {
    return;
  }
  json& array = *it;
  for (auto element = array.begin(); element != array.end();) {
    element = (*element == value) ? array.erase(element) : std::next(element);
  }
  if (array.empty()) {
    parent.erase(key);
  }
}

// Returns true if `buffer_index` refers to the GLB-stored buffer, i.e. the
// first buffer without a `uri`.
bool IsGlbBinBuffer(const json& gltf, uint64_t buffer_index) {
  if (buffer_index != 0) {
    return false;
  }
  const auto buffers = gltf.find("buffers");
  if (buffers == gltf.end() || !buffers->is_array() || buffers->empty()) {
    return false;
  }
  const json& buffer = buffers->front();
  return buffer.is_object() && !buffer.contains("uri");
}

// ---------------------------------------------------------------------------
// KHR_meshopt_compression.
// ---------------------------------------------------------------------------

enum class MeshoptMode { kAttributes, kTriangles, kIndices };
enum class MeshoptFilter {
  kNone,
  kOctahedral,
  kQuaternion,
  kExponential,
  kColor
};

struct MeshoptParams {
  uint64_t buffer = 0;
  uint64_t byte_offset = 0;
  uint64_t byte_length = 0;
  uint64_t byte_stride = 0;
  uint64_t count = 0;
  MeshoptMode mode = MeshoptMode::kAttributes;
  MeshoptFilter filter = MeshoptFilter::kNone;

  // Identifies the decoded content, so that aliasing views are decoded once.
  auto Key() const {
    return std::make_tuple(byte_offset, byte_length, byte_stride, count, mode,
                           filter);
  }
};

absl::StatusOr<MeshoptParams> ParseMeshoptParams(const json& ext) {
  if (!ext.is_object()) {
    return absl::InvalidArgumentError(
        absl::StrCat(kKhrMeshopt, " extension must be an object"));
  }
  MeshoptParams params;
  INTR_ASSIGN_OR_RETURN(params.buffer, GetUint(ext, "buffer", std::nullopt));
  INTR_ASSIGN_OR_RETURN(params.byte_offset, GetUint(ext, "byteOffset", 0));
  INTR_ASSIGN_OR_RETURN(params.byte_length,
                        GetUint(ext, "byteLength", std::nullopt));
  INTR_ASSIGN_OR_RETURN(params.byte_stride,
                        GetUint(ext, "byteStride", std::nullopt));
  INTR_ASSIGN_OR_RETURN(params.count, GetUint(ext, "count", std::nullopt));

  INTR_ASSIGN_OR_RETURN(const std::string mode,
                        GetString(ext, "mode", std::nullopt));
  if (mode == "ATTRIBUTES") {
    params.mode = MeshoptMode::kAttributes;
  } else if (mode == "TRIANGLES") {
    params.mode = MeshoptMode::kTriangles;
  } else if (mode == "INDICES") {
    params.mode = MeshoptMode::kIndices;
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Unsupported meshopt compression mode: ", mode));
  }

  INTR_ASSIGN_OR_RETURN(const std::string filter,
                        GetString(ext, "filter", "NONE"));
  if (filter == "NONE") {
    params.filter = MeshoptFilter::kNone;
  } else if (filter == "OCTAHEDRAL") {
    params.filter = MeshoptFilter::kOctahedral;
  } else if (filter == "QUATERNION") {
    params.filter = MeshoptFilter::kQuaternion;
  } else if (filter == "EXPONENTIAL") {
    params.filter = MeshoptFilter::kExponential;
  } else if (filter == "COLOR") {
    params.filter = MeshoptFilter::kColor;
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Unsupported meshopt filter: ", filter));
  }
  return params;
}

// Returns the smallest encoded size that the meshoptimizer decoders accept for
// `p.count` elements (for ATTRIBUTES, a lower bound over all codec versions).
// Checking it before allocating the output bounds the decoded size by the
// compressed size, so that small inputs cannot request huge allocations.
uint64_t MinMeshoptEncodedSize(const MeshoptParams& p) {
  switch (p.mode) {
    case MeshoptMode::kAttributes: {
      // Mirrors meshoptimizer's vertex codec: blocks of up to 256 vertices
      // (and 8 KiB), each with at least byteStride / 4 control bytes, plus a
      // 1-byte header and a tail of at least max(byteStride, 24) bytes.
      const uint64_t block_size =
          std::min<uint64_t>(256, (8192 / p.byte_stride) & ~uint64_t{15});
      const uint64_t blocks = (p.count + block_size - 1) / block_size;
      return 1 + blocks * (p.byte_stride / 4) +
             std::max<uint64_t>(p.byte_stride, 24);
    }
    case MeshoptMode::kTriangles:
      // Header, at least one code byte per triangle and a 16-byte tail.
      return 1 + p.count / 3 + 16;
    case MeshoptMode::kIndices:
      // Header, at least one byte per index and a 4-byte tail.
      return 1 + p.count + 4;
  }
  return 0;
}

// Validates the extension properties against the KHR_meshopt_compression
// specification and the meshoptimizer decoder preconditions, which are only
// enforced by debug assertions inside meshoptimizer.
absl::Status ValidateMeshoptParams(const MeshoptParams& p,
                                   uint64_t view_byte_length,
                                   std::optional<uint64_t> view_byte_stride) {
  const uint64_t stride = p.byte_stride;
  if (p.count == 0) {
    return absl::InvalidArgumentError("meshopt 'count' must be positive");
  }
  switch (p.mode) {
    case MeshoptMode::kAttributes:
      if (stride == 0 || stride > 256 || stride % 4 != 0) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "meshopt ATTRIBUTES requires byteStride to be a multiple of 4 in "
            "[4, 256], got %d",
            stride));
      }
      break;
    case MeshoptMode::kTriangles:
      if (p.count % 3 != 0) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "meshopt TRIANGLES requires count to be a multiple of 3, got %d",
            p.count));
      }
      [[fallthrough]];
    case MeshoptMode::kIndices:
      if (stride != 2 && stride != 4) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "meshopt index modes require byteStride 2 or 4, got %d", stride));
      }
      if (p.filter != MeshoptFilter::kNone) {
        return absl::InvalidArgumentError(
            "meshopt filters are only allowed in ATTRIBUTES mode");
      }
      break;
  }
  switch (p.filter) {
    case MeshoptFilter::kNone:
    case MeshoptFilter::kExponential:  // Covered by the ATTRIBUTES check.
      break;
    case MeshoptFilter::kOctahedral:
    case MeshoptFilter::kColor:
      if (stride != 4 && stride != 8) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "meshopt OCTAHEDRAL/COLOR filters require byteStride 4 or 8, got "
            "%d",
            stride));
      }
      break;
    case MeshoptFilter::kQuaternion:
      if (stride != 8) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "meshopt QUATERNION filter requires byteStride 8, got %d", stride));
      }
      break;
  }
  if (p.count > kGlbMaxSize / stride) {
    return absl::OutOfRangeError(absl::StrFormat(
        "meshopt decoded size (count %d * byteStride %d) exceeds the GLB "
        "limit",
        p.count, stride));
  }
  if (view_byte_length != p.count * stride) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "bufferView byteLength %d does not match meshopt count %d * "
        "byteStride %d",
        view_byte_length, p.count, stride));
  }
  if (view_byte_stride.has_value() && *view_byte_stride != stride) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "bufferView byteStride %d does not match meshopt byteStride %d",
        *view_byte_stride, stride));
  }
  if (const uint64_t min_size = MinMeshoptEncodedSize(p);
      p.byte_length < min_size) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "meshopt byteLength %d is smaller than the minimum encoded size %d "
        "for count %d",
        p.byte_length, min_size, p.count));
  }
  return absl::OkStatus();
}

// Decodes `src` into `dst`, which must hold `p.count * p.byte_stride` bytes.
// See
// https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_meshopt_compression
absl::Status DecodeMeshopt(const MeshoptParams& p, absl::string_view src,
                           char* dst) {
  const auto* src_data = reinterpret_cast<const unsigned char*>(src.data());
  int rc = -1;
  switch (p.mode) {
    case MeshoptMode::kAttributes:
      rc = meshopt_decodeVertexBuffer(dst, p.count, p.byte_stride, src_data,
                                      src.size());
      break;
    case MeshoptMode::kTriangles:
      rc = meshopt_decodeIndexBuffer(dst, p.count, p.byte_stride, src_data,
                                     src.size());
      break;
    case MeshoptMode::kIndices:
      rc = meshopt_decodeIndexSequence(dst, p.count, p.byte_stride, src_data,
                                       src.size());
      break;
  }
  if (rc != 0) {
    return absl::InvalidArgumentError(
        absl::StrFormat("meshopt decoder failed with code %d", rc));
  }

  switch (p.filter) {
    case MeshoptFilter::kNone:
      break;
    case MeshoptFilter::kOctahedral:
      meshopt_decodeFilterOct(dst, p.count, p.byte_stride);
      break;
    case MeshoptFilter::kQuaternion:
      meshopt_decodeFilterQuat(dst, p.count, p.byte_stride);
      break;
    case MeshoptFilter::kExponential:
      meshopt_decodeFilterExp(dst, p.count, p.byte_stride);
      break;
    case MeshoptFilter::kColor:
      meshopt_decodeFilterColor(dst, p.count, p.byte_stride);
      break;
  }
  return absl::OkStatus();
}

// ---------------------------------------------------------------------------
// bufferViews.
// ---------------------------------------------------------------------------

// A bufferView of the input and the location of its data.
struct ViewInfo {
  uint64_t length = 0;
  std::optional<uint64_t> stride;
  // Set for KHR_meshopt_compression views, whose data decodes to `length`
  // bytes.
  std::optional<MeshoptParams> meshopt;
  // Offset of uncompressed data in the input BIN chunk.
  uint64_t source_offset = 0;
};

// Parses and validates all bufferViews against the input BIN chunk `bin`.
absl::StatusOr<std::vector<ViewInfo>> ParseBufferViews(const json& gltf,
                                                       absl::string_view bin) {
  std::vector<ViewInfo> infos;
  const auto views_it = gltf.find("bufferViews");
  if (views_it == gltf.end()) {
    return infos;
  }
  if (!views_it->is_array()) {
    return absl::InvalidArgumentError("'bufferViews' must be an array");
  }
  infos.reserve(views_it->size());
  for (const json& view : *views_it) {
    if (!view.is_object()) {
      return absl::InvalidArgumentError("bufferView must be an object");
    }
    ViewInfo info;
    INTR_ASSIGN_OR_RETURN(const uint64_t view_buffer,
                          GetUint(view, "buffer", std::nullopt));
    INTR_ASSIGN_OR_RETURN(info.length,
                          GetUint(view, "byteLength", std::nullopt));
    if (view.contains("byteStride")) {
      INTR_ASSIGN_OR_RETURN(info.stride,
                            GetUint(view, "byteStride", std::nullopt));
    }

    const json* ext = nullptr;
    if (const auto exts = view.find("extensions"); exts != view.end()) {
      if (!exts->is_object()) {
        return absl::InvalidArgumentError(
            "bufferView 'extensions' must be an object");
      }
      if (const auto it = exts->find(kKhrMeshopt); it != exts->end()) {
        ext = &*it;
      }
    }

    if (ext != nullptr) {
      INTR_ASSIGN_OR_RETURN(const MeshoptParams params,
                            ParseMeshoptParams(*ext));
      INTR_RETURN_IF_ERROR(
          ValidateMeshoptParams(params, info.length, info.stride));
      if (!IsGlbBinBuffer(gltf, params.buffer)) {
        return absl::UnimplementedError(absl::StrFormat(
            "Compressed data in buffer %d is not supported; only the GLB "
            "BIN chunk (buffer 0 without uri) is supported",
            params.buffer));
      }
      if (!IsRangeInBounds(params.byte_offset, params.byte_length,
                           bin.size())) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Compressed bufferView out of range: offset %d, length %d, "
            "BIN size %d",
            params.byte_offset, params.byte_length, bin.size()));
      }
      info.meshopt = params;
    } else {
      if (!IsGlbBinBuffer(gltf, view_buffer)) {
        return absl::UnimplementedError(absl::StrFormat(
            "bufferView data in buffer %d is not supported; only the GLB "
            "BIN chunk (buffer 0 without uri) is supported",
            view_buffer));
      }
      INTR_ASSIGN_OR_RETURN(info.source_offset, GetUint(view, "byteOffset", 0));
      if (!IsRangeInBounds(info.source_offset, info.length, bin.size())) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Uncompressed bufferView out of range: offset %d, length %d, "
            "BIN size %d",
            info.source_offset, info.length, bin.size()));
      }
    }
    infos.push_back(std::move(info));
  }
  return infos;
}

// Pads `bin` so that its size is congruent to `phase` modulo 4 and grows it by
// `length` bytes. Returns the offset of the newly reserved region.
//
// glTF only requires `bufferView.byteOffset + accessor.byteOffset` to be a
// multiple of the component size, so relocated bufferViews keep the original
// offset modulo 4 to keep their accessors aligned.
absl::StatusOr<uint64_t> ReserveAligned(std::string& bin, uint64_t length,
                                        uint64_t phase = 0) {
  const uint64_t size = bin.size();
  const uint64_t offset = size + ((phase - size) & 3);
  if (length > kGlbMaxSize || offset > kGlbMaxSize - length) {
    return absl::OutOfRangeError(
        "Unpacked binary data exceeds the GLB size limit");
  }
  bin.resize(offset + length, '\0');
  return offset;
}

// ---------------------------------------------------------------------------
// KHR_mesh_quantization.
// ---------------------------------------------------------------------------

constexpr uint64_t kComponentTypeByte = 5120;
constexpr uint64_t kComponentTypeUnsignedByte = 5121;
constexpr uint64_t kComponentTypeShort = 5122;
constexpr uint64_t kComponentTypeUnsignedShort = 5123;
constexpr uint64_t kComponentTypeFloat = 5126;
constexpr uint64_t kTargetArrayBuffer = 34962;

// Returns true for attribute semantics that core glTF 2.0 restricts to float
// (or, for TEXCOORD_n, to float and normalized unsigned integers) but that
// KHR_mesh_quantization allows to be stored as (normalized) integers.
bool IsQuantizableSemantic(absl::string_view name) {
  return name == "POSITION" || name == "NORMAL" || name == "TANGENT" ||
         absl::StartsWith(name, "TEXCOORD_");
}

absl::StatusOr<int> ComponentCount(absl::string_view type) {
  if (type == "VEC2") {
    return 2;
  }
  if (type == "VEC3") {
    return 3;
  }
  if (type == "VEC4") {
    return 4;
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Unsupported accessor type for dequantization: ", type));
}

absl::StatusOr<int> QuantizedComponentSize(uint64_t component_type) {
  switch (component_type) {
    case kComponentTypeByte:
    case kComponentTypeUnsignedByte:
      return 1;
    case kComponentTypeShort:
    case kComponentTypeUnsignedShort:
      return 2;
    default:
      return absl::InvalidArgumentError(absl::StrFormat(
          "Unsupported component type for dequantization: %d", component_type));
  }
}

// Calls `f(std::type_identity<T>())` with the C++ type `T` of a component
// type accepted by `QuantizedComponentSize`.
template <typename F>
decltype(auto) VisitComponentType(uint64_t component_type, F&& f) {
  switch (component_type) {
    case kComponentTypeByte:
      return f(std::type_identity<int8_t>());
    case kComponentTypeUnsignedByte:
      return f(std::type_identity<uint8_t>());
    case kComponentTypeShort:
      return f(std::type_identity<int16_t>());
    default:
      return f(std::type_identity<uint16_t>());
  }
}

// Maps a component value of type `T` to float. Normalized values follow glTF
// 2.0, e.g. max(c / 127, -1) for signed bytes; IEEE float division is
// correctly rounded, so the result is the nearest float to the exact value.
template <typename T>
float ToFloat(float value, bool normalized) {
  if (!normalized) {
    return value;
  }
  const float result =
      value / static_cast<float>(std::numeric_limits<T>::max());
  if constexpr (std::is_signed_v<T>) {
    return std::max(result, -1.0f);
  } else {
    return result;
  }
}

template <typename T, bool kNormalized>
void DequantizeElements(const char* src, uint64_t count, int components,
                        uint64_t stride, char* dst) {
  for (uint64_t i = 0; i < count; ++i) {
    const char* element = src + i * stride;
    for (int c = 0; c < components; ++c) {
      T raw;
      std::memcpy(&raw, element + c * sizeof(T), sizeof(T));
      const float value = ToFloat<T>(raw, kNormalized);
      std::memcpy(dst, &value, sizeof(value));
      dst += sizeof(value);
    }
  }
}

// A validated accessor of a quantized attribute.
struct QuantizedAccessor {
  uint64_t index = 0;
  uint64_t component_type = 0;
  int components = 0;
  uint64_t count = 0;
  bool normalized = false;
  // The bufferView, or nullopt for accessors without data (all zeros).
  std::optional<uint64_t> view;
  // Offset and element stride of the quantized data within `view`.
  uint64_t byte_offset = 0;
  uint64_t stride = 0;

  uint64_t FloatElementSize() const { return components * sizeof(float); }
  uint64_t FloatSize() const { return count * FloatElementSize(); }
};

absl::StatusOr<QuantizedAccessor> ParseQuantizedAccessor(
    const json& accessor, uint64_t index, const std::vector<ViewInfo>& views) {
  if (accessor.contains("sparse")) {
    return absl::UnimplementedError(
        "Dequantization of sparse accessors is not supported");
  }
  QuantizedAccessor a;
  a.index = index;
  INTR_ASSIGN_OR_RETURN(a.component_type,
                        GetUint(accessor, "componentType", std::nullopt));
  INTR_ASSIGN_OR_RETURN(const int component_size,
                        QuantizedComponentSize(a.component_type));
  INTR_ASSIGN_OR_RETURN(const std::string type,
                        GetString(accessor, "type", std::nullopt));
  INTR_ASSIGN_OR_RETURN(a.components, ComponentCount(type));
  INTR_ASSIGN_OR_RETURN(a.count, GetUint(accessor, "count", std::nullopt));
  if (a.count == 0) {
    return absl::InvalidArgumentError("accessor 'count' must be positive");
  }
  if (const auto it = accessor.find("normalized"); it != accessor.end()) {
    if (!it->is_boolean()) {
      return absl::InvalidArgumentError(
          "accessor 'normalized' must be a boolean");
    }
    a.normalized = it->get<bool>();
  }
  for (const char* key : {"min", "max"}) {
    const auto it = accessor.find(key);
    if (it == accessor.end()) {
      continue;
    }
    if (!it->is_array() || it->size() != static_cast<size_t>(a.components)) {
      return absl::InvalidArgumentError(absl::StrCat("accessor '", key,
                                                     "' must be an array of ",
                                                     a.components, " numbers"));
    }
    if (!std::all_of(it->begin(), it->end(),
                     [](const json& value) { return value.is_number(); })) {
      return absl::InvalidArgumentError(
          absl::StrCat("accessor '", key, "' must contain numbers"));
    }
  }

  // Accessors without a bufferView are all zeros and need no data.
  if (!accessor.contains("bufferView")) {
    return a;
  }
  INTR_ASSIGN_OR_RETURN(const uint64_t view_index,
                        GetUint(accessor, "bufferView", std::nullopt));
  if (view_index >= views.size()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "accessor references invalid bufferView %d", view_index));
  }
  a.view = view_index;
  const ViewInfo& view = views[view_index];
  INTR_ASSIGN_OR_RETURN(a.byte_offset, GetUint(accessor, "byteOffset", 0));
  const uint64_t element_size = a.components * component_size;
  a.stride = view.stride.value_or(element_size);
  if (a.stride < element_size) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "bufferView byteStride %d is smaller than the element size %d",
        a.stride, element_size));
  }
  if (!IsRangeInBounds(a.byte_offset, element_size, view.length) ||
      a.count - 1 > (view.length - a.byte_offset - element_size) / a.stride) {
    return absl::InvalidArgumentError(
        "accessor data extends beyond its bufferView");
  }
  return a;
}

// Writes the float data of `a` to `dst`, reading the quantized data from
// `view_data`, the start of its bufferView.
void Dequantize(const QuantizedAccessor& a, const char* view_data, char* dst) {
  const char* src = view_data + a.byte_offset;
  VisitComponentType(a.component_type, [&]<typename T>(std::type_identity<T>) {
    if (a.normalized) {
      DequantizeElements<T, true>(src, a.count, a.components, a.stride, dst);
    } else {
      DequantizeElements<T, false>(src, a.count, a.components, a.stride, dst);
    }
  });
}

// Updates the JSON of an accessor validated by `ParseQuantizedAccessor` to
// describe float data.
void ConvertAccessorToFloat(json& accessor, const QuantizedAccessor& a) {
  for (const char* key : {"min", "max"}) {
    const auto it = accessor.find(key);
    if (it == accessor.end()) {
      continue;
    }
    for (json& value : *it) {
      value = VisitComponentType(
          a.component_type, [&]<typename T>(std::type_identity<T>) {
            return ToFloat<T>(value.get<float>(), a.normalized);
          });
    }
  }
  accessor["componentType"] = kComponentTypeFloat;
  accessor.erase("normalized");
}

struct AttributeAccessors {
  // Accessors of quantizable attributes (see `IsQuantizableSemantic`) of mesh
  // primitives or morph targets that use a non-float component type.
  std::set<uint64_t> quantized;
  // Accessors used as primitive indices or JOINTS_n, which must not be float.
  std::set<uint64_t> integer_only;
};

absl::StatusOr<AttributeAccessors> CollectAttributeAccessors(const json& gltf) {
  AttributeAccessors result;
  const auto meshes = gltf.find("meshes");
  if (meshes == gltf.end()) {
    return result;
  }
  if (!meshes->is_array()) {
    return absl::InvalidArgumentError("'meshes' must be an array");
  }
  const auto accessors = gltf.find("accessors");
  const size_t accessor_count =
      (accessors != gltf.end() && accessors->is_array()) ? accessors->size()
                                                         : 0;

  const auto visit = [&](const json& attributes) -> absl::Status {
    if (!attributes.is_object()) {
      return absl::InvalidArgumentError("'attributes' must be an object");
    }
    for (const auto& [name, index] : attributes.items()) {
      if (absl::StartsWith(name, "JOINTS_") && index.is_number_unsigned()) {
        result.integer_only.insert(index.get<uint64_t>());
      }
      if (!IsQuantizableSemantic(name)) {
        continue;
      }
      if (!index.is_number_unsigned() ||
          index.get<uint64_t>() >= accessor_count) {
        return absl::InvalidArgumentError(
            absl::StrCat("Attribute ", name,
                         " references an invalid accessor: ", index.dump()));
      }
      const json& accessor = (*accessors)[index.get<uint64_t>()];
      if (!accessor.is_object()) {
        return absl::InvalidArgumentError("accessor must be an object");
      }
      INTR_ASSIGN_OR_RETURN(const uint64_t component_type,
                            GetUint(accessor, "componentType", std::nullopt));
      if (component_type != kComponentTypeFloat) {
        result.quantized.insert(index.get<uint64_t>());
      }
    }
    return absl::OkStatus();
  };

  for (const json& mesh : *meshes) {
    if (!mesh.is_object()) {
      return absl::InvalidArgumentError("mesh must be an object");
    }
    const auto primitives = mesh.find("primitives");
    if (primitives == mesh.end()) {
      continue;
    }
    if (!primitives->is_array()) {
      return absl::InvalidArgumentError("'primitives' must be an array");
    }
    for (const json& primitive : *primitives) {
      if (!primitive.is_object()) {
        return absl::InvalidArgumentError("primitive must be an object");
      }
      if (const auto indices = primitive.find("indices");
          indices != primitive.end() && indices->is_number_unsigned()) {
        result.integer_only.insert(indices->get<uint64_t>());
      }
      if (const auto attributes = primitive.find("attributes");
          attributes != primitive.end()) {
        INTR_RETURN_IF_ERROR(visit(*attributes));
      }
      if (const auto targets = primitive.find("targets");
          targets != primitive.end()) {
        if (!targets->is_array()) {
          return absl::InvalidArgumentError("'targets' must be an array");
        }
        for (const json& target : *targets) {
          INTR_RETURN_IF_ERROR(visit(target));
        }
      }
    }
  }
  return result;
}

// Collects bufferView indices referenced from within any `extensions` object,
// e.g. by KHR_draco_mesh_compression or EXT_texture_webp sources.
void CollectExtensionBufferViews(const json& node, bool in_extensions,
                                 std::set<uint64_t>& views) {
  if (node.is_object()) {
    for (const auto& [key, value] : node.items()) {
      if (in_extensions && key == "bufferView" && value.is_number_unsigned()) {
        views.insert(value.get<uint64_t>());
      }
      CollectExtensionBufferViews(value, in_extensions || key == "extensions",
                                  views);
    }
  } else if (node.is_array()) {
    for (const json& element : node) {
      CollectExtensionBufferViews(element, in_extensions, views);
    }
  }
}

// Describes how to convert the quantized attribute accessors to float.
struct DequantizationPlan {
  std::vector<QuantizedAccessor> accessors;
  // Per bufferView: positions in `accessors` of the converted accessors that
  // read the bufferView.
  std::vector<std::vector<size_t>> converted_users;
  // Per bufferView: whether anything other than converted accessors reads it.
  std::vector<bool> shared;
};

DequantizationPlan EmptyPlan(size_t view_count) {
  DequantizationPlan plan;
  plan.converted_users.resize(view_count);
  plan.shared.assign(view_count, false);
  return plan;
}

absl::StatusOr<DequantizationPlan> PlanDequantization(
    const json& gltf, const AttributeAccessors& uses,
    const std::vector<ViewInfo>& views) {
  DequantizationPlan plan = EmptyPlan(views.size());
  if (uses.quantized.empty()) {
    return plan;
  }

  // `CollectAttributeAccessors` verified that the quantized accessors exist.
  const json& accessors = gltf.at("accessors");
  for (const uint64_t index : uses.quantized) {
    if (uses.integer_only.contains(index)) {
      return absl::UnimplementedError(absl::StrFormat(
          "Accessor %d is used both by a quantized attribute and as indices "
          "or JOINTS_n, so it cannot be converted to float",
          index));
    }
    INTR_ASSIGN_OR_RETURN(
        QuantizedAccessor a,
        ParseQuantizedAccessor(accessors[index], index, views),
        _ << "while dequantizing accessor " << index);
    if (a.view.has_value()) {
      plan.converted_users[*a.view].push_back(plan.accessors.size());
    }
    plan.accessors.push_back(std::move(a));
  }

  const auto mark_shared = [&plan](const json& object, const char* key) {
    if (!object.is_object()) {
      return;
    }
    const auto it = object.find(key);
    if (it != object.end() && it->is_number_unsigned() &&
        it->get<uint64_t>() < plan.shared.size()) {
      plan.shared[it->get<uint64_t>()] = true;
    }
  };
  for (uint64_t i = 0; i < accessors.size(); ++i) {
    const json& accessor = accessors[i];
    if (!accessor.is_object()) {
      continue;
    }
    if (!uses.quantized.contains(i)) {
      mark_shared(accessor, "bufferView");
    }
    if (const auto sparse = accessor.find("sparse"); sparse != accessor.end()) {
      if (sparse->is_object()) {
        mark_shared(sparse->value("indices", json()), "bufferView");
        mark_shared(sparse->value("values", json()), "bufferView");
      }
    }
  }
  if (const auto images = gltf.find("images");
      images != gltf.end() && images->is_array()) {
    for (const json& image : *images) {
      mark_shared(image, "bufferView");
    }
  }
  std::set<uint64_t> extension_views;
  CollectExtensionBufferViews(gltf, /*in_extensions=*/false, extension_views);
  for (const uint64_t view : extension_views) {
    if (view < plan.shared.size()) {
      plan.shared[view] = true;
    }
  }
  return plan;
}

// Returns true if an EXT_mesh_gpu_instancing attribute uses a non-float
// accessor, which requires KHR_mesh_quantization to stay declared.
bool HasQuantizedInstancingAttributes(const json& gltf) {
  const auto nodes = gltf.find("nodes");
  const auto accessors = gltf.find("accessors");
  if (nodes == gltf.end() || !nodes->is_array() || accessors == gltf.end() ||
      !accessors->is_array()) {
    return false;
  }
  for (const json& node : *nodes) {
    if (!node.is_object()) {
      continue;
    }
    const auto exts = node.find("extensions");
    if (exts == node.end() || !exts->is_object()) {
      continue;
    }
    const auto instancing = exts->find(kExtMeshGpuInstancing);
    if (instancing == exts->end() || !instancing->is_object()) {
      continue;
    }
    const auto attributes = instancing->find("attributes");
    if (attributes == instancing->end() || !attributes->is_object()) {
      continue;
    }
    for (const json& index : *attributes) {
      if (!index.is_number_unsigned() ||
          index.get<uint64_t>() >= accessors->size()) {
        continue;
      }
      const json& accessor = (*accessors)[index.get<uint64_t>()];
      if (!accessor.is_object()) {
        continue;
      }
      const auto component_type = accessor.find("componentType");
      if (component_type != accessor.end() &&
          *component_type != kComponentTypeFloat) {
        return true;
      }
    }
  }
  return false;
}

// Rebuilds all bufferViews into a single new BIN chunk in one pass and returns
// it. On return, every bufferView references buffer 0 with an offset into the
// returned data.
//
// - KHR_meshopt_compression views are decoded; uncompressed views are copied.
//   Aliasing views share storage.
// - The accessors in `plan` are converted to float. bufferViews read only by
//   converted accessors are rewritten in place (tightly packed, with a
//   byteStride if several accessors share them). Converted accessors in
//   bufferViews shared with other users, or whose element size differs from
//   the other accessors of their bufferView, get new bufferViews appended at
//   the end, so that no existing bufferView index changes.
//
// Only the storage type changes; values stay in the coordinate frames chosen
// by gltfpack. The position dequantization transform remains in the node
// transforms (see `BakePositionDequantization`) and the texture coordinate
// dequantization transform remains in `KHR_texture_transform` (see
// `BakeTexcoordDequantization`).
absl::StatusOr<std::string> RebuildBinaryChunk(
    json& gltf, absl::string_view bin, const std::vector<ViewInfo>& views,
    const DequantizationPlan& plan) {
  std::string new_bin;
  if (views.empty()) {
    return new_bin;
  }

  // Reserves an upper bound of the output size to avoid reallocations.
  uint64_t capacity = 0;
  for (const ViewInfo& view : views) {
    capacity += view.length + 3;
  }
  for (const QuantizedAccessor& a : plan.accessors) {
    if (a.view.has_value()) {
      capacity += a.FloatSize() + 3;
    }
  }
  new_bin.reserve(std::min(capacity, kGlbMaxSize));

  json& view_array = gltf["bufferViews"];
  const auto accessor_json = [&gltf](uint64_t index) -> json& {
    return gltf["accessors"][index];
  };

  // Offsets in `new_bin` of already emitted source data, keyed by source.
  absl::flat_hash_map<std::pair<uint64_t, uint64_t>, uint64_t> copied_ranges;
  absl::flat_hash_map<decltype(MeshoptParams().Key()), uint64_t> decoded_views;
  // Decoded data of compressed views that are replaced by float data.
  std::string scratch;
  // Converted accessors that get a new bufferView: (position in
  // `plan.accessors`, offset of the float data in `new_bin`).
  std::vector<std::pair<size_t, uint64_t>> new_views;

  for (size_t v = 0; v < views.size(); ++v) {
    const ViewInfo& info = views[v];
    json& view = view_array[v];
    const std::vector<size_t>& users = plan.converted_users[v];

    if (!users.empty() && !plan.shared[v]) {
      // Only converted accessors read this bufferView, so its quantized data
      // is replaced by their float data.
      const char* source = bin.data() + info.source_offset;
      if (info.meshopt.has_value()) {
        const MeshoptParams& p = *info.meshopt;
        scratch.resize(info.length);
        INTR_RETURN_IF_ERROR(DecodeMeshopt(
            p, bin.substr(p.byte_offset, p.byte_length), scratch.data()));
        source = scratch.data();
      }
      const uint64_t element_size =
          plan.accessors[users.front()].FloatElementSize();
      INTR_ASSIGN_OR_RETURN(const uint64_t start, ReserveAligned(new_bin, 0));
      int accessors_in_view = 0;
      for (const size_t u : users) {
        const QuantizedAccessor& a = plan.accessors[u];
        if (a.FloatElementSize() != element_size) {
          continue;
        }
        INTR_ASSIGN_OR_RETURN(const uint64_t offset,
                              ReserveAligned(new_bin, a.FloatSize()));
        Dequantize(a, source, new_bin.data() + offset);
        accessor_json(a.index)["byteOffset"] = offset - start;
        ++accessors_in_view;
      }
      // glTF requires byteStride when several accessors share a bufferView.
      if (accessors_in_view > 1) {
        view["byteStride"] = element_size;
      } else {
        view.erase("byteStride");
      }
      view["byteOffset"] = start;
      view["byteLength"] = new_bin.size() - start;

      // A bufferView has a single byteStride, so accessors with another
      // element size are moved to their own bufferViews.
      for (const size_t u : users) {
        const QuantizedAccessor& a = plan.accessors[u];
        if (a.FloatElementSize() == element_size) {
          continue;
        }
        INTR_ASSIGN_OR_RETURN(const uint64_t offset,
                              ReserveAligned(new_bin, a.FloatSize()));
        Dequantize(a, source, new_bin.data() + offset);
        new_views.emplace_back(u, offset);
      }
    } else {
      uint64_t offset = 0;
      if (info.meshopt.has_value()) {
        const MeshoptParams& p = *info.meshopt;
        const auto [it, inserted] = decoded_views.try_emplace(p.Key(), 0);
        if (inserted) {
          INTR_ASSIGN_OR_RETURN(it->second,
                                ReserveAligned(new_bin, info.length));
          INTR_RETURN_IF_ERROR(
              DecodeMeshopt(p, bin.substr(p.byte_offset, p.byte_length),
                            new_bin.data() + it->second));
        }
        offset = it->second;
      } else {
        const auto [it, inserted] =
            copied_ranges.try_emplace({info.source_offset, info.length}, 0);
        if (inserted) {
          INTR_ASSIGN_OR_RETURN(
              it->second,
              ReserveAligned(new_bin, info.length, info.source_offset % 4));
          std::memcpy(new_bin.data() + it->second,
                      bin.data() + info.source_offset, info.length);
        }
        offset = it->second;
      }
      view["byteOffset"] = offset;

      // Converted accessors of shared bufferViews read the emitted data and
      // get new bufferViews.
      for (const size_t u : users) {
        const QuantizedAccessor& a = plan.accessors[u];
        INTR_ASSIGN_OR_RETURN(const uint64_t dst,
                              ReserveAligned(new_bin, a.FloatSize()));
        Dequantize(a, new_bin.data() + offset, new_bin.data() + dst);
        new_views.emplace_back(u, dst);
      }
    }

    view["buffer"] = 0;
    if (const auto exts = view.find("extensions"); exts != view.end()) {
      exts->erase(kKhrMeshopt);
      if (exts->empty()) {
        view.erase("extensions");
      }
    }
  }

  for (const auto& [u, offset] : new_views) {
    const QuantizedAccessor& a = plan.accessors[u];
    view_array.push_back({{"buffer", 0},
                          {"byteOffset", offset},
                          {"byteLength", a.FloatSize()},
                          {"target", kTargetArrayBuffer}});
    json& accessor = accessor_json(a.index);
    accessor["bufferView"] = view_array.size() - 1;
    accessor.erase("byteOffset");
  }
  return new_bin;
}

// ---------------------------------------------------------------------------
// Baking of the position dequantization transform.
// ---------------------------------------------------------------------------

// Translation and uniform scale of a mesh node. gltfpack stores the position
// dequantization in such a transform: position = offset + scale * quantized.
struct PositionTransform {
  std::array<double, 3> offset = {0, 0, 0};
  double scale = 1;

  bool operator==(const PositionTransform&) const = default;

  // Transforms component `c` of a position, or of a morph target position
  // displacement (without offset).
  float Apply(int c, float value, bool with_offset) const {
    return static_cast<float>((with_offset ? offset[c] : 0.0) + scale * value);
  }
};

bool IsNumberVec3(const json& value) {
  return value.is_array() && value.size() == 3 &&
         std::all_of(value.begin(), value.end(),
                     [](const json& c) { return c.is_number(); });
}

// Returns the transform of `node` if it can be moved into the vertices of its
// mesh without changing the scene, i.e. if `node` is a leaf mesh node without
// skin, camera, extensions, rotation or matrix, whose transform is a
// translation and/or a positive uniform scale. A negative scale is rejected
// since it also flips the triangle winding order.
std::optional<PositionTransform> GetBakeableTransform(const json& node) {
  if (!node.is_object() || !node.contains("mesh")) {
    return std::nullopt;
  }
  for (const char* key :
       {"children", "matrix", "rotation", "skin", "camera", "extensions"}) {
    if (node.contains(key)) {
      return std::nullopt;
    }
  }
  const auto translation = node.find("translation");
  const auto scale = node.find("scale");
  if (translation == node.end() && scale == node.end()) {
    return std::nullopt;
  }
  PositionTransform transform;
  if (translation != node.end()) {
    if (!IsNumberVec3(*translation)) {
      return std::nullopt;
    }
    for (int c = 0; c < 3; ++c) {
      transform.offset[c] = (*translation)[c].get<double>();
    }
  }
  if (scale != node.end()) {
    if (!IsNumberVec3(*scale)) {
      return std::nullopt;
    }
    transform.scale = (*scale)[0].get<double>();
    if (!(transform.scale > 0) ||
        (*scale)[1].get<double>() != transform.scale ||
        (*scale)[2].get<double>() != transform.scale) {
      return std::nullopt;
    }
  }
  return transform;
}

// Returns the member `key` of `object`, or null if `object` is not an object
// or has no such member. Unlike `json::value`, it neither copies nor throws.
const json& Member(const json& object, const char* key) {
  static const json* const kNull = new json();
  if (!object.is_object()) {
    return *kNull;
  }
  const auto it = object.find(key);
  return it == object.end() ? *kNull : *it;
}

// Returns the elements of `value` if it is an array, or an empty array.
const json& ArrayOrEmpty(const json& value) {
  static const json* const kEmpty = new json(json::array());
  return value.is_array() ? value : *kEmpty;
}

// Calls `visit(index, semantic, mesh)` for every accessor reference in
// `gltf`: for attributes of mesh primitives and morph targets with their
// semantic and mesh index, and for all other references (primitive indices,
// animation samplers, skins and EXT_mesh_gpu_instancing attributes) with an
// empty semantic and no mesh.
template <typename F>
void VisitAccessorReferences(const json& gltf, F&& visit) {
  const auto visit_index = [&visit](const json& index,
                                    absl::string_view semantic,
                                    std::optional<uint64_t> mesh) {
    if (index.is_number_unsigned()) {
      visit(index.get<uint64_t>(), semantic, mesh);
    }
  };
  const auto visit_attributes = [&visit_index](const json& attributes,
                                               std::optional<uint64_t> mesh) {
    if (!attributes.is_object()) {
      return;
    }
    for (const auto& [name, index] : attributes.items()) {
      visit_index(
          index,
          mesh.has_value() ? absl::string_view(name) : absl::string_view(),
          mesh);
    }
  };

  const json& meshes = ArrayOrEmpty(Member(gltf, "meshes"));
  for (uint64_t m = 0; m < meshes.size(); ++m) {
    for (const json& primitive :
         ArrayOrEmpty(Member(meshes[m], "primitives"))) {
      visit_index(Member(primitive, "indices"), "", std::nullopt);
      visit_attributes(Member(primitive, "attributes"), m);
      for (const json& target : ArrayOrEmpty(Member(primitive, "targets"))) {
        visit_attributes(target, m);
      }
    }
  }
  for (const json& animation : ArrayOrEmpty(Member(gltf, "animations"))) {
    for (const json& sampler : ArrayOrEmpty(Member(animation, "samplers"))) {
      visit_index(Member(sampler, "input"), "", std::nullopt);
      visit_index(Member(sampler, "output"), "", std::nullopt);
    }
  }
  for (const json& skin : ArrayOrEmpty(Member(gltf, "skins"))) {
    visit_index(Member(skin, "inverseBindMatrices"), "", std::nullopt);
  }
  for (const json& node : ArrayOrEmpty(Member(gltf, "nodes"))) {
    visit_attributes(
        Member(Member(Member(node, "extensions"), kExtMeshGpuInstancing),
               "attributes"),
        std::nullopt);
  }
}

// Returns, for every accessor referenced only as POSITION of primitives or
// morph targets of a single mesh, the index of that mesh.
absl::flat_hash_map<uint64_t, uint64_t> FindMeshOwnedPositionAccessors(
    const json& gltf) {
  // nullopt marks accessors with any other reference.
  absl::flat_hash_map<uint64_t, std::optional<uint64_t>> owners;
  VisitAccessorReferences(
      gltf, [&owners](uint64_t index, absl::string_view semantic,
                      std::optional<uint64_t> mesh) {
        const std::optional<uint64_t> owner =
            semantic == "POSITION" ? mesh : std::nullopt;
        const auto [it, inserted] = owners.try_emplace(index, owner);
        if (!inserted && it->second != owner) {
          it->second = std::nullopt;
        }
      });

  absl::flat_hash_map<uint64_t, uint64_t> result;
  for (const auto& [accessor, mesh] : owners) {
    if (mesh.has_value()) {
      result[accessor] = *mesh;
    }
  }
  return result;
}

// Returns the accessors referenced only as TEXCOORD_n of primitives or morph
// targets (of any meshes).
std::set<uint64_t> FindTexcoordOnlyAccessors(const json& gltf) {
  // Maps accessors to whether all their references are TEXCOORD_n.
  absl::flat_hash_map<uint64_t, bool> texcoord_only;
  VisitAccessorReferences(
      gltf, [&texcoord_only](uint64_t index, absl::string_view semantic,
                             std::optional<uint64_t> /*mesh*/) {
        const bool is_texcoord = absl::StartsWith(semantic, "TEXCOORD_");
        const auto [it, inserted] = texcoord_only.try_emplace(index, true);
        it->second = it->second && is_texcoord;
      });

  std::set<uint64_t> result;
  for (const auto& [accessor, only] : texcoord_only) {
    if (only) {
      result.insert(accessor);
    }
  }
  return result;
}

// Returns the indices of nodes whose transform is targeted by an animation.
std::set<uint64_t> CollectAnimatedNodes(const json& gltf) {
  std::set<uint64_t> nodes;
  for (const json& animation : ArrayOrEmpty(Member(gltf, "animations"))) {
    for (const json& channel : ArrayOrEmpty(Member(animation, "channels"))) {
      const json& node = Member(Member(channel, "target"), "node");
      if (node.is_number_unsigned()) {
        nodes.insert(node.get<uint64_t>());
      }
    }
  }
  return nodes;
}

// Applies `transform` in place to the float data and min/max of a POSITION
// accessor converted by `RebuildBinaryChunk` and `ConvertAccessorToFloat`.
void TransformPositions(json& accessor, const json& views, std::string& bin,
                        const QuantizedAccessor& a,
                        const PositionTransform& transform, bool with_offset) {
  if (a.view.has_value()) {
    // The bufferView may have changed during the conversion, so the location
    // is read from the updated JSON.
    const json& view = views[accessor["bufferView"].get<uint64_t>()];
    const uint64_t start = view.value("byteOffset", uint64_t{0}) +
                           accessor.value("byteOffset", uint64_t{0});
    const uint64_t stride = view.value("byteStride", a.FloatElementSize());
    for (uint64_t i = 0; i < a.count; ++i) {
      char* element = bin.data() + start + i * stride;
      for (int c = 0; c < 3; ++c) {
        float value;
        std::memcpy(&value, element + c * sizeof(float), sizeof(float));
        value = transform.Apply(c, value, with_offset);
        std::memcpy(element + c * sizeof(float), &value, sizeof(float));
      }
    }
  }
  // The transform is monotonic per component (the scale is positive), so the
  // transformed bounds are exactly the bounds of the transformed data.
  for (const char* key : {"min", "max"}) {
    const auto it = accessor.find(key);
    if (it == accessor.end()) {
      continue;
    }
    for (int c = 0; c < 3; ++c) {
      (*it)[c] = transform.Apply(c, (*it)[c].get<float>(), with_offset);
    }
  }
}

// Moves the position dequantization transform that gltfpack stores in mesh
// nodes into the POSITION data (and the morph target POSITION displacements)
// of their meshes, and removes it from the nodes. Afterwards, mesh-local
// positions no longer depend on the node transforms, so loaders that ignore
// node transforms, or flatten them with `aiProcess_PreTransformVertices`, are
// not needed for correct mesh geometry. The data size is unchanged since the
// converted positions are floats already.
//
// A mesh is baked only if all of the following hold, so that the scene is
// unchanged; otherwise its nodes keep their transforms:
// - All its POSITION accessors were converted by `plan` (they were quantized)
//   and are referenced only as POSITION of this mesh, so that no other user
//   sees the transformed data. Primitive POSITIONs have data (bufferViews).
// - All nodes using it have the same transform accepted by
//   `GetBakeableTransform` and are not animated.
void BakePositionDequantization(json& gltf, std::string& bin,
                                const DequantizationPlan& plan) {
  const auto meshes = gltf.find("meshes");
  const auto nodes = gltf.find("nodes");
  if (plan.accessors.empty() || meshes == gltf.end() || !meshes->is_array() ||
      nodes == gltf.end() || !nodes->is_array()) {
    return;
  }
  // Animation pointers can target node transforms without `target.node`.
  if (UsesExtension(gltf, kKhrAnimationPointer)) {
    return;
  }

  absl::flat_hash_map<uint64_t, const QuantizedAccessor*> converted;
  for (const QuantizedAccessor& a : plan.accessors) {
    converted[a.index] = &a;
  }
  const std::set<uint64_t> animated_nodes = CollectAnimatedNodes(gltf);

  // Per mesh: the transform shared by all nodes using it, and whether all of
  // them can be baked.
  std::vector<std::optional<PositionTransform>> transforms(meshes->size());
  std::vector<bool> bakeable(meshes->size(), true);
  for (uint64_t n = 0; n < nodes->size(); ++n) {
    const json& node = (*nodes)[n];
    if (!node.is_object()) {
      continue;
    }
    const auto mesh = node.find("mesh");
    if (mesh == node.end() || !mesh->is_number_unsigned() ||
        mesh->get<uint64_t>() >= meshes->size()) {
      continue;
    }
    const uint64_t m = mesh->get<uint64_t>();
    const std::optional<PositionTransform> transform =
        animated_nodes.contains(n) ? std::nullopt : GetBakeableTransform(node);
    if (!transform.has_value() ||
        (transforms[m].has_value() && *transforms[m] != *transform)) {
      bakeable[m] = false;
    } else {
      transforms[m] = transform;
    }
  }

  const absl::flat_hash_map<uint64_t, uint64_t> owners =
      FindMeshOwnedPositionAccessors(gltf);
  json& accessors = gltf["accessors"];
  const json& views = ArrayOrEmpty(Member(gltf, "bufferViews"));
  std::vector<bool> baked(meshes->size(), false);
  for (uint64_t m = 0; m < meshes->size(); ++m) {
    if (!bakeable[m] || !transforms[m].has_value()) {
      continue;
    }
    // POSITION accessors of the mesh, mapped to whether they hold positions
    // (true) or morph target displacements (false).
    std::map<uint64_t, bool> positions;
    bool valid = true;
    const auto add = [&](const json& attributes, bool with_offset) {
      const json& index = Member(attributes, "POSITION");
      if (index.is_null()) {
        return;
      }
      if (!index.is_number_unsigned()) {
        valid = false;
        return;
      }
      const uint64_t a = index.get<uint64_t>();
      const auto owner = owners.find(a);
      const auto quantized = converted.find(a);
      const auto [it, inserted] = positions.try_emplace(a, with_offset);
      valid = valid && owner != owners.end() && owner->second == m &&
              quantized != converted.end() &&
              quantized->second->components == 3 &&
              (!with_offset || quantized->second->view.has_value()) &&
              it->second == with_offset;
    };
    for (const json& primitive :
         ArrayOrEmpty(Member((*meshes)[m], "primitives"))) {
      add(Member(primitive, "attributes"), /*with_offset=*/true);
      for (const json& target : ArrayOrEmpty(Member(primitive, "targets"))) {
        add(target, /*with_offset=*/false);
      }
    }
    if (!valid || positions.empty()) {
      continue;
    }
    for (const auto& [a, with_offset] : positions) {
      TransformPositions(accessors[a], views, bin, *converted.at(a),
                         *transforms[m], with_offset);
    }
    baked[m] = true;
  }

  for (json& node : *nodes) {
    if (!node.is_object()) {
      continue;
    }
    const auto mesh = node.find("mesh");
    if (mesh != node.end() && mesh->is_number_unsigned() &&
        mesh->get<uint64_t>() < baked.size() && baked[mesh->get<uint64_t>()]) {
      node.erase("translation");
      node.erase("scale");
    }
  }
}

// ---------------------------------------------------------------------------
// Baking of the texture coordinate dequantization transform.
// ---------------------------------------------------------------------------

// KHR_texture_transform of a texture reference, which maps texture
// coordinates as uv' = offset + R(rotation) * diag(scale) * uv, see
// https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_transform
struct TextureTransform {
  std::array<double, 2> offset = {0, 0};
  std::array<double, 2> scale = {1, 1};
  double rotation = 0;

  bool operator==(const TextureTransform&) const = default;
};

// Returns the transform of a texture reference (the identity without
// KHR_texture_transform), or nullopt if it cannot be baked because it is
// malformed or overrides `texCoord`.
std::optional<TextureTransform> GetTextureTransform(const json& texture_info) {
  const json& ext =
      Member(Member(texture_info, "extensions"), kKhrTextureTransform);
  TextureTransform transform;
  if (ext.is_null()) {
    return transform;
  }
  if (!ext.is_object() || ext.contains("texCoord")) {
    return std::nullopt;
  }
  const auto read_vec2 = [&ext](const char* key, std::array<double, 2>& out) {
    const json& value = Member(ext, key);
    if (value.is_null()) {
      return true;
    }
    if (!value.is_array() || value.size() != 2 || !value[0].is_number() ||
        !value[1].is_number()) {
      return false;
    }
    out = {value[0].get<double>(), value[1].get<double>()};
    return true;
  };
  if (!read_vec2("offset", transform.offset) ||
      !read_vec2("scale", transform.scale)) {
    return std::nullopt;
  }
  if (const json& rotation = Member(ext, "rotation"); !rotation.is_null()) {
    if (!rotation.is_number()) {
      return std::nullopt;
    }
    transform.rotation = rotation.get<double>();
  }
  return transform;
}

// A texture reference (textureInfo) of a material.
struct TextureReference {
  json* info = nullptr;
  // The texture coordinate set (TEXCOORD_n) it reads.
  uint64_t tex_coord = 0;
  // Its transform, or nullopt if it cannot be baked.
  std::optional<TextureTransform> transform;
};

// Collects the texture references in `node`, a material or a part of it:
// objects with a numeric `index` under keys ending in "Texture", e.g.
// `baseColorTexture` or `clearcoatTexture` of KHR_materials_clearcoat.
void CollectTextureReferences(json& node, std::vector<TextureReference>& refs) {
  if (node.is_array()) {
    for (json& element : node) {
      CollectTextureReferences(element, refs);
    }
    return;
  }
  if (!node.is_object()) {
    return;
  }
  for (auto& [key, value] : node.items()) {
    if (!absl::EndsWith(key, "Texture") ||
        !Member(value, "index").is_number_unsigned()) {
      CollectTextureReferences(value, refs);
      continue;
    }
    TextureReference ref{.info = &value,
                         .transform = GetTextureTransform(value)};
    if (const json& tex_coord = Member(value, "texCoord");
        !tex_coord.is_null()) {
      if (tex_coord.is_number_unsigned()) {
        ref.tex_coord = tex_coord.get<uint64_t>();
      } else {
        ref.transform = std::nullopt;
      }
    }
    refs.push_back(ref);
  }
}

// Returns true if any `extensions` object within `node` contains `extension`.
bool HasExtensionObject(const json& node, const char* extension) {
  if (node.is_array()) {
    return std::any_of(node.begin(), node.end(), [extension](const json& e) {
      return HasExtensionObject(e, extension);
    });
  }
  if (!node.is_object()) {
    return false;
  }
  if (Member(node, "extensions").contains(extension)) {
    return true;
  }
  return std::any_of(node.begin(), node.end(), [extension](const json& e) {
    return HasExtensionObject(e, extension);
  });
}

// Applies `transform` in place to the float data of a VEC2 TEXCOORD accessor
// converted by `RebuildBinaryChunk` and `ConvertAccessorToFloat`, and updates
// its min/max.
void TransformTexcoords(json& accessor, const json& views, std::string& bin,
                        const QuantizedAccessor& a,
                        const TextureTransform& transform, bool with_offset) {
  // Accessors without data are zeros, which displacements map to zeros.
  if (!a.view.has_value()) {
    return;
  }
  const json& view = views[accessor["bufferView"].get<uint64_t>()];
  const uint64_t start = view.value("byteOffset", uint64_t{0}) +
                         accessor.value("byteOffset", uint64_t{0});
  const uint64_t stride = view.value("byteStride", a.FloatElementSize());
  const double cos_r = std::cos(transform.rotation);
  const double sin_r = std::sin(transform.rotation);
  const double offset_u = with_offset ? transform.offset[0] : 0.0;
  const double offset_v = with_offset ? transform.offset[1] : 0.0;
  std::array<float, 2> min = {std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::infinity()};
  std::array<float, 2> max = {-std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity()};
  for (uint64_t i = 0; i < a.count; ++i) {
    char* element = bin.data() + start + i * stride;
    std::array<float, 2> uv;
    std::memcpy(uv.data(), element, sizeof(uv));
    const double su = transform.scale[0] * uv[0];
    const double sv = transform.scale[1] * uv[1];
    uv = {static_cast<float>(offset_u + cos_r * su + sin_r * sv),
          static_cast<float>(offset_v - sin_r * su + cos_r * sv)};
    std::memcpy(element, uv.data(), sizeof(uv));
    for (int c = 0; c < 2; ++c) {
      min[c] = std::min(min[c], uv[c]);
      max[c] = std::max(max[c], uv[c]);
    }
  }
  // Texture transforms may rotate or mirror, so the bounds are recomputed.
  if (accessor.contains("min")) {
    accessor["min"] = json::array({min[0], min[1]});
  }
  if (accessor.contains("max")) {
    accessor["max"] = json::array({max[0], max[1]});
  }
}

// Moves the texture coordinate dequantization transform that gltfpack stores
// in KHR_texture_transform of all texture references of a material into the
// TEXCOORD_n data, and removes it from the texture references (and
// KHR_texture_transform from the extension lists once unused). Afterwards,
// texture coordinates no longer depend on texture transforms, so loaders that
// ignore them, or need `aiProcess_TransformUVCoords`, render correctly. The
// data size is unchanged since the converted texture coordinates are floats
// already.
//
// TEXCOORD accessors and the (material, texture coordinate set) pairs reading
// them form connected groups: a primitive connects its TEXCOORD_n accessors
// (and those of its morph targets) with set n of every material it can use,
// including KHR_materials_variants mappings. A group is baked only if all of
// the following hold, so that rendering is unchanged:
// - All texture references of its pairs have the same, non-identity
//   transform without `texCoord` override.
// - All its accessors were converted by `plan` (they were quantized), are
//   VEC2, are referenced only as TEXCOORD_n, and are used either as texture
//   coordinates (with data) or as morph target displacements, not both.
void BakeTexcoordDequantization(json& gltf, std::string& bin,
                                const DequantizationPlan& plan) {
  if (plan.accessors.empty() || !Member(gltf, "materials").is_array()) {
    return;
  }

  // Texture references per (material, texture coordinate set) pair.
  std::map<std::pair<uint64_t, uint64_t>, std::vector<TextureReference>> pairs;
  json& materials = gltf["materials"];
  for (uint64_t m = 0; m < materials.size(); ++m) {
    std::vector<TextureReference> refs;
    CollectTextureReferences(materials[m], refs);
    for (const TextureReference& ref : refs) {
      pairs[{m, ref.tex_coord}].push_back(ref);
    }
  }
  if (pairs.empty()) {
    return;
  }

  // Union-find over pairs and accessors.
  std::vector<size_t> parent;
  const auto find = [&parent](size_t id) {
    while (parent[id] != id) {
      id = parent[id] = parent[parent[id]];
    }
    return id;
  };
  std::map<std::pair<uint64_t, uint64_t>, size_t> pair_ids;
  for (const auto& [key, refs] : pairs) {
    pair_ids[key] = parent.size();
    parent.push_back(parent.size());
  }
  absl::flat_hash_map<uint64_t, size_t> accessor_ids;
  // Whether an accessor holds texture coordinates (true) or morph target
  // displacements (false).
  absl::flat_hash_map<uint64_t, bool> roles;
  std::set<uint64_t> conflicting_roles;

  for (const json& mesh : ArrayOrEmpty(Member(gltf, "meshes"))) {
    for (const json& primitive : ArrayOrEmpty(Member(mesh, "primitives"))) {
      std::vector<uint64_t> primitive_materials;
      if (const json& material = Member(primitive, "material");
          material.is_number_unsigned()) {
        primitive_materials.push_back(material.get<uint64_t>());
      }
      for (const json& mapping : ArrayOrEmpty(Member(
               Member(Member(primitive, "extensions"), kKhrMaterialsVariants),
               "mappings"))) {
        if (const json& material = Member(mapping, "material");
            material.is_number_unsigned()) {
          primitive_materials.push_back(material.get<uint64_t>());
        }
      }
      const auto connect = [&](const json& attributes, bool with_offset) {
        if (!attributes.is_object()) {
          return;
        }
        for (const auto& [name, index] : attributes.items()) {
          uint64_t set = 0;
          if (!absl::StartsWith(name, "TEXCOORD_") ||
              !absl::SimpleAtoi(absl::string_view(name).substr(9), &set) ||
              !index.is_number_unsigned()) {
            continue;
          }
          const uint64_t a = index.get<uint64_t>();
          const auto [role, inserted_role] = roles.try_emplace(a, with_offset);
          if (role->second != with_offset) {
            conflicting_roles.insert(a);
          }
          const auto [id, inserted_id] =
              accessor_ids.try_emplace(a, parent.size());
          if (inserted_id) {
            parent.push_back(parent.size());
          }
          for (const uint64_t material : primitive_materials) {
            if (const auto pair = pair_ids.find({material, set});
                pair != pair_ids.end()) {
              parent[find(pair->second)] = find(id->second);
            }
          }
        }
      };
      connect(Member(primitive, "attributes"), /*with_offset=*/true);
      for (const json& target : ArrayOrEmpty(Member(primitive, "targets"))) {
        connect(target, /*with_offset=*/false);
      }
    }
  }

  absl::flat_hash_map<uint64_t, const QuantizedAccessor*> converted;
  for (const QuantizedAccessor& a : plan.accessors) {
    converted[a.index] = &a;
  }
  const std::set<uint64_t> texcoord_only = FindTexcoordOnlyAccessors(gltf);

  struct Group {
    std::vector<std::pair<uint64_t, uint64_t>> pairs;
    std::vector<uint64_t> accessors;
    std::optional<TextureTransform> transform;
    bool valid = true;
  };
  std::map<size_t, Group> groups;
  for (const auto& [key, id] : pair_ids) {
    Group& group = groups[find(id)];
    group.pairs.push_back(key);
    for (const TextureReference& ref : pairs[key]) {
      if (!ref.transform.has_value() ||
          (group.transform.has_value() && *group.transform != *ref.transform)) {
        group.valid = false;
      } else {
        group.transform = ref.transform;
      }
    }
  }
  for (const auto& [a, id] : accessor_ids) {
    Group& group = groups[find(id)];
    group.accessors.push_back(a);
    const auto quantized = converted.find(a);
    group.valid = group.valid && quantized != converted.end() &&
                  quantized->second->components == 2 &&
                  texcoord_only.contains(a) && !conflicting_roles.contains(a) &&
                  (!roles.at(a) || quantized->second->view.has_value());
  }

  json& accessors = gltf["accessors"];
  const json& views = ArrayOrEmpty(Member(gltf, "bufferViews"));
  bool baked_any = false;
  for (const auto& [root, group] : groups) {
    if (!group.valid || group.pairs.empty() || group.accessors.empty() ||
        !group.transform.has_value() ||
        *group.transform == TextureTransform()) {
      continue;
    }
    for (const uint64_t a : group.accessors) {
      TransformTexcoords(accessors[a], views, bin, *converted.at(a),
                         *group.transform, roles.at(a));
    }
    for (const auto& key : group.pairs) {
      for (const TextureReference& ref : pairs[key]) {
        json& exts = (*ref.info)["extensions"];
        exts.erase(kKhrTextureTransform);
        if (exts.empty()) {
          ref.info->erase("extensions");
        }
      }
    }
    baked_any = true;
  }
  if (baked_any && !HasExtensionObject(gltf, kKhrTextureTransform)) {
    RemoveFromArray(gltf, "extensionsUsed", kKhrTextureTransform);
    RemoveFromArray(gltf, "extensionsRequired", kKhrTextureTransform);
  }
}

// Returns the unpacked GLB, or nullopt if `glb_bytes` needs no change.
absl::StatusOr<std::optional<std::string>> UnpackGlb(
    absl::string_view glb_bytes, const UnpackRenderableOptions& options) {
  INTR_ASSIGN_OR_RETURN(const GlbChunks chunks, SplitGlbChunks(glb_bytes));

  json gltf = json::parse(chunks.json.begin(), chunks.json.end(),
                          /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (gltf.is_discarded() || !gltf.is_object()) {
    return absl::InvalidArgumentError(
        "Failed to parse glTF JSON chunk as an object");
  }

  if (UsesExtension(gltf, kExtMeshopt)) {
    return absl::UnimplementedError(
        absl::StrCat(kExtMeshopt, " is not supported"));
  }
  const bool has_meshopt = UsesExtension(gltf, kKhrMeshopt);
  AttributeAccessors uses;
  if (options.dequantize_attributes) {
    INTR_ASSIGN_OR_RETURN(uses, CollectAttributeAccessors(gltf));
  }
  if (!has_meshopt && uses.quantized.empty()) {
    return std::nullopt;
  }

  INTR_ASSIGN_OR_RETURN(const std::vector<ViewInfo> views,
                        ParseBufferViews(gltf, chunks.bin));
  INTR_ASSIGN_OR_RETURN(const DequantizationPlan plan,
                        PlanDequantization(gltf, uses, views));
  INTR_ASSIGN_OR_RETURN(std::string new_bin,
                        RebuildBinaryChunk(gltf, chunks.bin, views, plan));
  for (const QuantizedAccessor& a : plan.accessors) {
    ConvertAccessorToFloat(gltf["accessors"][a.index], a);
  }
  if (options.bake_position_dequantization) {
    BakePositionDequantization(gltf, new_bin, plan);
  }
  if (options.bake_texcoord_dequantization) {
    BakeTexcoordDequantization(gltf, new_bin, plan);
  }

  RemoveFromArray(gltf, "extensionsUsed", kKhrMeshopt);
  RemoveFromArray(gltf, "extensionsRequired", kKhrMeshopt);
  if (!plan.accessors.empty() && !HasQuantizedInstancingAttributes(gltf)) {
    RemoveFromArray(gltf, "extensionsUsed", kKhrMeshQuantization);
    RemoveFromArray(gltf, "extensionsRequired", kKhrMeshQuantization);
  }
  // Top-level glTF arrays must not be empty.
  if (const auto it = gltf.find("bufferViews");
      it != gltf.end() && it->is_array() && it->empty()) {
    gltf.erase(it);
  }

  // Rebuilds the buffer table around the single GLB BIN chunk, keeping
  // metadata (name, extras) of the original GLB buffer. Fallback buffers are
  // dropped since no bufferView references them anymore.
  if (new_bin.empty()) {
    gltf.erase("buffers");
  } else {
    json buffer = IsGlbBinBuffer(gltf, 0) ? gltf["buffers"][0] : json::object();
    buffer["byteLength"] = new_bin.size();
    if (const auto exts = buffer.find("extensions"); exts != buffer.end()) {
      exts->erase(kKhrMeshopt);
      if (exts->empty()) {
        buffer.erase("extensions");
      }
    }
    gltf["buffers"] = json::array({std::move(buffer)});
  }

  INTR_ASSIGN_OR_RETURN(std::string glb, AssembleGlb(gltf.dump(), new_bin));
  return std::optional<std::string>(std::move(glb));
}

absl::StatusOr<std::optional<std::string>> UnpackGlbOrError(
    absl::string_view glb_bytes, const UnpackRenderableOptions& options) {
  try {
    return UnpackGlb(glb_bytes, options);
  } catch (const json::exception& e) {
    // Safety net for malformed JSON content, e.g. invalid UTF-8 on dump().
    return absl::InvalidArgumentError(
        absl::StrCat("Malformed glTF JSON: ", e.what()));
  }
}

}  // namespace

absl::StatusOr<std::string> UnpackRenderableGlbData(
    absl::string_view glb_bytes, const UnpackRenderableOptions& options) {
  INTR_ASSIGN_OR_RETURN(std::optional<std::string> unpacked,
                        UnpackGlbOrError(glb_bytes, options));
  if (!unpacked.has_value()) {
    return std::string(glb_bytes);
  }
  return *std::move(unpacked);
}

absl::StatusOr<RenderableUnpackingResult> UnpackRenderableFile(
    absl::string_view input_path, absl::string_view output_path,
    const UnpackRenderableOptions& options) {
  std::string input_bytes;
  if (absl::Status status = file::GetContents(std::string(input_path),
                                              &input_bytes, file::Defaults());
      !status.ok()) {
    // `GetContents` does not report missing files as NotFound. Checking only
    // after a failed read avoids a race with the file being created or removed.
    std::error_code ec;
    if (!std::filesystem::exists(input_path, ec) && !ec) {
      return absl::NotFoundError(
          absl::StrCat("Input renderable file not found: ", input_path));
    }
    return status;
  }

  INTR_ASSIGN_OR_RETURN(const std::optional<std::string> unpacked,
                        UnpackGlbOrError(input_bytes, options));
  // Inputs that need no change are written as-is, without an extra copy.
  const std::string& output_bytes =
      unpacked.has_value() ? *unpacked : input_bytes;

  INTR_RETURN_IF_ERROR(file::SetContents(std::string(output_path), output_bytes,
                                         file::Defaults()));

  RenderableUnpackingResult result;
  result.input_file_size = input_bytes.size();
  result.output_file_size = output_bytes.size();

  return result;
}

}  // namespace intrinsic::geo
