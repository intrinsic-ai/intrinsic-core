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

#include "intrinsic/geometry/internal/util/gltf_helpers.h"

#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {
namespace {

void ConfigurePassthroughImageLoader(tinygltf::TinyGLTF& loader) {
  loader.SetImageLoader(
      [](tinygltf::Image*, const int, std::string*, std::string*, int, int,
         const unsigned char*, int, void*) { return true; },
      nullptr);
}

bool HasGlbMagic(absl::string_view bytes) {
  return bytes.size() >= 4 && bytes.substr(0, 4) == "glTF";
}

// Loads `bytes` as binary GLB if `binary` is true, or as glTF JSON otherwise.
// Stores tinygltf messages in `diagnostics` if non-null, or logs warnings
// otherwise. Prefixes parser errors with `error_prefix`.
absl::StatusOr<tinygltf::Model> Load(absl::string_view bytes, bool binary,
                                     absl::string_view error_prefix,
                                     GltfParseDiagnostics* diagnostics) {
  tinygltf::TinyGLTF loader;
  ConfigurePassthroughImageLoader(loader);
  tinygltf::Model model;
  std::string err;
  std::string warn;
  const bool ok =
      binary ? loader.LoadBinaryFromMemory(
                   &model, &err, &warn,
                   reinterpret_cast<const unsigned char*>(bytes.data()),
                   bytes.size())
             : loader.LoadASCIIFromString(&model, &err, &warn, bytes.data(),
                                          bytes.size(), /*base_dir=*/"");
  absl::Status status =
      ok ? absl::OkStatus()
         : absl::InvalidArgumentError(absl::StrCat(error_prefix, err));
  if (diagnostics != nullptr) {
    diagnostics->errors = std::move(err);
    diagnostics->warnings = std::move(warn);
  } else if (!warn.empty()) {
    LOG(WARNING) << "tinygltf warnings while parsing glTF: " << warn;
  }
  if (!status.ok()) {
    return status;
  }
  return model;
}

}  // namespace

std::optional<double> GetGltfNumber(const tinygltf::Value& value) {
  if (!value.IsNumber()) {
    return std::nullopt;
  }
  return value.GetNumberAsDouble();
}

const tinygltf::Value* FindGltfExtension(const tinygltf::ExtensionMap& ext,
                                         absl::string_view name) {
  const auto it = ext.find(std::string(name));
  return it != ext.end() ? &it->second : nullptr;
}

const tinygltf::Value* FindGltfObjectValue(const tinygltf::Value& object,
                                           absl::string_view key) {
  if (!object.IsObject()) {
    return nullptr;
  }
  const auto& map = object.Get<tinygltf::Value::Object>();
  const auto it = map.find(std::string(key));
  return it != map.end() ? &it->second : nullptr;
}

absl::StatusOr<tinygltf::Model> ParseGltf(absl::string_view bytes,
                                          GltfParseDiagnostics* diagnostics) {
  if (diagnostics != nullptr) {
    *diagnostics = {};
  }
  if (bytes.empty()) {
    return absl::InvalidArgumentError(
        "Failed to parse GLTF model: input is empty");
  }
  return Load(bytes, /*binary=*/HasGlbMagic(bytes),
              /*error_prefix=*/"Failed to parse GLTF model: ", diagnostics);
}

absl::StatusOr<tinygltf::Model> ParseGlb(absl::string_view glb_bytes,
                                         GltfParseDiagnostics* diagnostics) {
  if (diagnostics != nullptr) {
    *diagnostics = {};
  }
  if (!HasGlbMagic(glb_bytes)) {
    return absl::InvalidArgumentError(
        "Failed to parse GLB: Invalid GLB magic bytes");
  }
  return Load(glb_bytes, /*binary=*/true,
              /*error_prefix=*/"Failed to parse GLB: ", diagnostics);
}

absl::StatusOr<std::string> SerializeGltfToGlb(const tinygltf::Model& model) {
  std::ostringstream ss;
  tinygltf::TinyGLTF writer;
  if (!writer.WriteGltfSceneToStream(&model, ss,
                                     /*prettyPrint=*/false,
                                     /*writeBinary=*/true)) {
    return absl::InternalError("Failed to write glTF binary");
  }
  return ss.str();
}

double GetGltfTransmissionFactor(const tinygltf::Material& mat) {
  const tinygltf::Value* ext =
      FindGltfExtension(mat.extensions, kKhrMaterialsTransmission);
  if (ext == nullptr) {
    return 0.0;
  }
  const tinygltf::Value* factor =
      FindGltfObjectValue(*ext, kTransmissionFactorKey);
  if (factor == nullptr) {
    return 0.0;
  }
  return GetGltfNumber(*factor).value_or(0.0);
}

}  // namespace intrinsic::geo
