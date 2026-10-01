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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_MESH_IO_RESTRICT_IMPORTER_H_
#define INTRINSIC_GEOMETRY_INTERNAL_MESH_IO_RESTRICT_IMPORTER_H_

#include <assimp/Importer.hpp>

#include "absl/strings/string_view.h"

namespace intrinsic::geo {

// Restricts the registered importers in Assimp to only those supporting
// `extension`. This prevents Assimp from falling back to arbitrary format
// auto-detection (such as LWSImporter or other scene formats) when the file
// content does not match the requested extension.
void RestrictImporterToExtension(Assimp::Importer& importer,
                                 absl::string_view extension);

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_INTERNAL_MESH_IO_RESTRICT_IMPORTER_H_
