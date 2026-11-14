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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_IO_SAVE_MESH_TO_STL_FILE_H_
#define INTRINSIC_GEOMETRY_INTERNAL_IO_SAVE_MESH_TO_STL_FILE_H_

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"

namespace intrinsic::geo {

absl::Status SaveMeshToBinaryStlFile(absl::string_view filename,
                                     const Mesh& mesh);

}  // namespace intrinsic::geo
#endif  // INTRINSIC_GEOMETRY_INTERNAL_IO_SAVE_MESH_TO_STL_FILE_H_
