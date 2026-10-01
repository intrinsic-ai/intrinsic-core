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

#ifndef INTRINSIC_SCENE_SDF_SCENE_OBJECT_TO_ZIPPED_SDF_H_
#define INTRINSIC_SCENE_SDF_SCENE_OBJECT_TO_ZIPPED_SDF_H_

#include <string>

#include "absl/status/statusor.h"
#include "intrinsic/scene/sdf/scene_object_to_sdf.h"

namespace intrinsic {
namespace scene_object {

// Converts a SceneObject proto to a zipped SDF archive buffer containing
// model.sdf and all referenced geometry meshes.
//
// Meshes are saved into a relative "meshes" directory in the zip archive, and
// the corresponding mesh URIs inside model.sdf are made relative to the SDF.
absl::StatusOr<std::string> SceneObjectToZippedSdf(
    const intrinsic_proto::scene_object::v1::SceneObject& scene_object,
    const sdf::SceneObjectToSdfOptions& options = {});

}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SDF_SCENE_OBJECT_TO_ZIPPED_SDF_H_
