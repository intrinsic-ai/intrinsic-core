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

#include "intrinsic/geometry/internal/mesh/io/check_gltf_binary_header.h"

#include <cstddef>
#include <cstdint>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

namespace intrinsic::geo {

absl::Status CheckGltfBinaryJsonLength(absl::string_view content,
                                       absl::string_view extension) {
  extension = absl::StripPrefix(extension, ".");
  if (!absl::EqualsIgnoreCase(extension, "gltf") &&
      !absl::EqualsIgnoreCase(extension, "glb") &&
      !absl::EqualsIgnoreCase(extension, "vrm")) {
    return absl::OkStatus();
  }
  // A binary glTF file starts with the magic "glTF", a version and the total
  // length (12 bytes). The little-endian uint32 JSON length follows at byte 12
  // (contentLength in glTF 1.0, called sceneLength in Assimp; chunkLength of
  // the JSON chunk in glTF 2.0), and the JSON starts at byte 20.
  // Specs:
  // - glTF 2.0:
  //   https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#glb-file-format-specification
  // - glTF 1.0 (KHR_binary_glTF):
  //   https://github.com/KhronosGroup/glTF/blob/main/extensions/1.0/Khronos/KHR_binary_glTF/README.md
  constexpr size_t kJsonLengthOffset = 12;
  constexpr size_t kJsonOffset = 20;
  if (!absl::StartsWith(content, "glTF") || content.size() < kJsonOffset) {
    return absl::OkStatus();
  }
  uint32_t json_length = 0;
  for (int i = 3; i >= 0; --i) {
    json_length = (json_length << 8) |
                  static_cast<uint8_t>(content[kJsonLengthOffset + i]);
  }
  if (json_length > content.size() - kJsonOffset) {
    return absl::InvalidArgumentError(
        absl::StrCat("Binary glTF JSON length ", json_length, " exceeds the ",
                     content.size() - kJsonOffset, " bytes after the header"));
  }
  return absl::OkStatus();
}

}  // namespace intrinsic::geo
