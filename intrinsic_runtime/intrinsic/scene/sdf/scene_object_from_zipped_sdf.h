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

#ifndef INTRINSIC_SCENE_SDF_SCENE_OBJECT_FROM_ZIPPED_SDF_H_
#define INTRINSIC_SCENE_SDF_SCENE_OBJECT_FROM_ZIPPED_SDF_H_

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/sdf/scene_object_from_sdf.h"

namespace intrinsic {
namespace scene_object {

// Creates a SceneObject from zipped buffer `zipped_data` that is expected to
// unzip to a directory tree containing an entry point SDF file in the root
// directory and all its referenced uri in the directory tree.
absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromZippedSdf(absl::string_view zipped_data,
                         GeometrySerializer& geometry_serializer,
                         const SceneObjectFromSdfOptions& options = {});

// Creates a SceneObject from `sdf_root_directory` that is expected to contain
// an entry point SDF file in `sdf_root_directory` (or its subdirectory).
// Paths referenced from the entry point SDF are resolved relative to the parent
// directory of the SDF.
//
// Returns NotFoundError when a main SDF file cannot be
// found. Returns InvalidArgumentError when a main SDF file is found but
// conversion to SceneObject fails.
absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromSdfInDirectory(absl::string_view sdf_root_directory,
                              GeometrySerializer& geometry_serializer,
                              const SceneObjectFromSdfOptions& options = {});

}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SDF_SCENE_OBJECT_FROM_ZIPPED_SDF_H_
