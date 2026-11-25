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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_MESH_IO_CHECK_GLTF_BINARY_HEADER_H_
#define INTRINSIC_GEOMETRY_INTERNAL_MESH_IO_CHECK_GLTF_BINARY_HEADER_H_

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace intrinsic::geo {

// Returns InvalidArgument if `extension` is gltf, glb, or vrm and `content` is
// a binary glTF file (glTF 1.0 or 2.0) whose header declares a JSON length
// larger than the rest of `content`. Returns OK otherwise.
//
// Assimp's binary glTF readers allocate a buffer of the declared JSON length
// before checking it against the file size, so a small malformed file can make
// them allocate up to 4 GB (b/568616441). Call this before passing a buffer to
// Assimp.
// TODO(b/590557129): Remove once insrc uses an Assimp release that includes
// https://github.com/assimp/assimp/pull/7080.
absl::Status CheckGltfBinaryJsonLength(absl::string_view content,
                                       absl::string_view extension);

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_INTERNAL_MESH_IO_CHECK_GLTF_BINARY_HEADER_H_
