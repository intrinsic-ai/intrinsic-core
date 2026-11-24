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

#ifndef INTRINSIC_GEOMETRY_GLTF_UNPACK_RENDERABLE_H_
#define INTRINSIC_GEOMETRY_GLTF_UNPACK_RENDERABLE_H_

#include <cstddef>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace intrinsic::geo {

// Options for unpacking renderable.
struct UnpackRenderableOptions {
  // Converts vertex attributes quantized with `KHR_mesh_quantization`
  // (POSITION, NORMAL, TANGENT and TEXCOORD_n of primitives and morph targets
  // stored as (normalized) 8/16-bit integers) to 32-bit floats, and removes
  // `KHR_mesh_quantization` from the extension lists. Applies to inputs with or
  // without `KHR_meshopt_compression`. Increases the output size.
  //
  // Enabled by default so that the output is loadable by the widest range of
  // glTF loaders. Set to false to keep the smaller quantized storage.
  //
  // Needed by loaders that ignore the accessor component type and read these
  // attributes as raw floats, e.g. Assimp (its glTF2 importer copies the raw
  // bytes, producing garbage vertices for quantized data).
  //
  // Can be disabled for loaders that honor the accessor `componentType` and
  // `normalized` flags or implement `KHR_mesh_quantization`, e.g. tinygltf and
  // cgltf (expose typed accessor data) and renderers such as three.js,
  // Babylon.js or Filament.
  //
  // The conversion is lossless and only changes the storage type:
  // - gltfpack stores the position dequantization transform in the mesh
  //   nodes, so POSITION values remain in the quantized coordinate frame
  //   unless `bake_position_dequantization` moves the transform into them.
  // - gltfpack stores the texture coordinate dequantization in
  //   `KHR_texture_transform` of every texture reference of a material, so
  //   TEXCOORD_n values remain in the quantized frame unless
  //   `bake_texcoord_dequantization` moves the transform into them.
  bool dequantize_attributes = true;

  // Moves the position dequantization transform that gltfpack stores in mesh
  // nodes (a translation and a uniform scale) into the POSITION data of their
  // meshes, and removes it from the nodes. Mesh-local positions are then
  // correct without applying node transforms, so Assimp users do not need
  // `aiProcess_PreTransformVertices`, which would also flatten the node
  // hierarchy that the packer keeps. Does not change the output size, since
  // the dequantized positions are floats already.
  //
  // Only applies with `dequantize_attributes`, to meshes whose POSITION
  // attributes were quantized. The scene is unchanged: a mesh keeps the
  // transform in its nodes unless all nodes using it are non-animated leaf
  // nodes with the same translation and positive uniform scale and without
  // rotation, matrix, skin, camera or extensions, and its POSITION accessors
  // are used by no other mesh or property.
  //
  // The baked positions are in the frame of gltfpack's output, which is not
  // necessarily the frame of the original meshes.
  bool bake_position_dequantization = true;

  // Moves the texture coordinate dequantization transform that gltfpack
  // stores in `KHR_texture_transform` of the texture references of a material
  // into the TEXCOORD_n data, and removes it from the texture references (and
  // `KHR_texture_transform` from the extension lists once unused). Texture
  // coordinates are then correct without applying texture transforms, so
  // Assimp users do not need `aiProcess_TransformUVCoords`. Does not change
  // the output size, since the dequantized texture coordinates are floats
  // already.
  //
  // Only applies with `dequantize_attributes`, to TEXCOORD_n attributes that
  // were quantized. Rendering is unchanged: texture coordinates are only baked
  // if all texture references reading them, across all materials of the
  // primitives using them (including `KHR_materials_variants`), have the same
  // transform without `texCoord` override, and if they are used by nothing but
  // TEXCOORD_n attributes.
  bool bake_texcoord_dequantization = true;
};

struct RenderableUnpackingResult {
  // Size of the input file in bytes.
  size_t input_file_size = 0;
  // Size of the output file in bytes.
  size_t output_file_size = 0;
};

// Unpacks a renderable (.glb) produced by `PackRenderableFile` and writes a
// renderable (.glb) that loaders without `KHR_meshopt_compression` or
// `KHR_mesh_quantization` support (e.g., Assimp) can read.
//
// Reverses exactly these encodings:
// - `KHR_meshopt_compression`: decodes compressed bufferViews (ATTRIBUTES,
//   TRIANGLES and INDICES modes; OCTAHEDRAL, QUATERNION, EXPONENTIAL and COLOR
//   filters) into a single uncompressed GLB BIN chunk, and removes the
//   extension and its fallback buffer.
// - `KHR_mesh_quantization`: converts quantized attributes to floats when
//   `UnpackRenderableOptions::dequantize_attributes` is set (the default), and
//   moves the dequantization transforms of positions (from mesh nodes) and
//   texture coordinates (from `KHR_texture_transform`) into the data when
//   `UnpackRenderableOptions::bake_position_dequantization` and
//   `UnpackRenderableOptions::bake_texcoord_dequantization` are set (the
//   default).
//
// All other content, including other extensions, is preserved as-is. Inputs
// that need no change (no `KHR_meshopt_compression` and no quantized
// attributes to convert) are returned unchanged. `KHR_mesh_quantization` stays
// declared if `EXT_mesh_gpu_instancing` attributes still use quantized types.
//
// Must be kept in sync with `PackRenderableFile`: it covers the output of
// `gltfpack -cz`, the only encoding option used by the packer. Other gltfpack
// encodings are not decoded: `EXT_meshopt_compression` (`-c`) is rejected, and
// others, e.g. `KHR_texture_basisu` (`-tc`), are passed through.
//
// Note to Assimp users (inherent to gltfpack output):
// tl;dr: set both `bake_position_dequantization` and
// `bake_texcoord_dequantization` options during unpacking.
// - gltfpack stores the position dequantization scale and offset in the mesh
//   node transforms. `UnpackRenderableOptions::bake_position_dequantization`
//   (the default) moves them into the vertices where possible; otherwise,
//   mesh-local POSITION values are only correct after applying node
//   transforms (traverse `aiNode::mTransformation` or use
//   `aiProcess_PreTransformVertices`).
// - gltfpack stores the texture coordinate dequantization in
//   `KHR_texture_transform`. `UnpackRenderableOptions::
//   bake_texcoord_dequantization` (the default) moves it into the texture
//   coordinates where possible; otherwise, Assimp exposes it as
//   `AI_MATKEY_UVTRANSFORM` (apply with `aiProcess_TransformUVCoords`).
//
// Returns (uncommon errors):
// - UnimplementedError for `EXT_meshopt_compression`, data stored outside the
//   GLB BIN chunk (external or data URI buffers), dequantizing sparse
//   accessors, or dequantizing accessors also used as indices or JOINTS_n.
// - OutOfRangeError if the unpacked GLB exceeds the 4 GiB GLB size limit.
//
// References:
// - KHR_meshopt_compression Specification:
//   https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_meshopt_compression
// - Canonical decompression flow::
//   https://github.com/jkuhlmann/cgltf/pull/129
absl::StatusOr<RenderableUnpackingResult> UnpackRenderableFile(
    absl::string_view input_path, absl::string_view output_path,
    const UnpackRenderableOptions& options = {});

// Unpacks in-memory GLB buffer and returns the uncompressed GLB buffer.
// Same as `UnpackRenderableFile` except that unpacked data is returned
// in-memory.
absl::StatusOr<std::string> UnpackRenderableGlbData(
    absl::string_view glb_bytes, const UnpackRenderableOptions& options = {});

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_GLTF_UNPACK_RENDERABLE_H_
