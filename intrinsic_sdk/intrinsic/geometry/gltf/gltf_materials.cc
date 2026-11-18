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

#include "intrinsic/geometry/gltf/gltf_materials.h"

#include <algorithm>
#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {
namespace {

// Property key of the transmission factor in KHR_materials_transmission.
constexpr absl::string_view kTransmissionFactorKey = "transmissionFactor";

// Returns the numeric value of `value` if it holds a JSON number (integer or
// real), or std::nullopt otherwise. JSON does not distinguish integers from
// reals, and tinygltf serializes integral doubles such as 1.0 as "1", which
// re-parse as an integer Value.
std::optional<double> GetGltfNumber(const tinygltf::Value& value) {
  if (!value.IsNumber()) {
    return std::nullopt;
  }
  return value.GetNumberAsDouble();
}

// Returns the extension named `name` in `ext`, or nullptr if absent.
const tinygltf::Value* FindGltfExtension(const tinygltf::ExtensionMap& ext,
                                         absl::string_view name) {
  const auto it = ext.find(std::string(name));
  return it != ext.end() ? &it->second : nullptr;
}

// Returns the value for `key` if `object` is a JSON object containing it, or
// nullptr otherwise.
const tinygltf::Value* FindGltfObjectValue(const tinygltf::Value& object,
                                           absl::string_view key) {
  if (!object.IsObject()) {
    return nullptr;
  }
  const auto& map = object.Get<tinygltf::Value::Object>();
  const auto it = map.find(std::string(key));
  return it != map.end() ? &it->second : nullptr;
}

}  // namespace

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

void SetGltfTransmissionFactor(tinygltf::Material& mat, double factor) {
  mat.extensions[std::string(kKhrMaterialsTransmission)] =
      tinygltf::Value(tinygltf::Value::Object{
          {std::string(kTransmissionFactorKey), tinygltf::Value(factor)}});
}

void AddGltfExtensionUsed(tinygltf::Model& model, absl::string_view extension) {
  if (std::ranges::find(model.extensionsUsed, extension) ==
      model.extensionsUsed.end()) {
    model.extensionsUsed.emplace_back(extension);
  }
}

}  // namespace intrinsic::geo
