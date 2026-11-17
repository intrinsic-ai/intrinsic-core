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

#ifndef INTRINSIC_SCENE_PROCESSING_UTILS_TRANSFORM_UTILS_H_
#define INTRINSIC_SCENE_PROCESSING_UTILS_TRANSFORM_UTILS_H_

#include "absl/status/status.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"

namespace intrinsic {
namespace scene_object {

// Applies a Pose3d transformation to an entity's parent_t_this pose.
absl::Status TransformEntityByPose(
    ::intrinsic_proto::scene_object::v1::Entity& entity,
    const Pose3d& new_parent_t_old_parent);

// Applies a Pose3d transformation to all geometries and named_geometries
// attached to an entity's link.
absl::Status TransformLinkGeometry(
    ::intrinsic_proto::scene_object::v1::Entity& entity,
    const Pose3d& new_parent_t_old_parent);

}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_PROCESSING_UTILS_TRANSFORM_UTILS_H_
