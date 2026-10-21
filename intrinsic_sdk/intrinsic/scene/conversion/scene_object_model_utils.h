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

#ifndef INTRINSIC_SCENE_CONVERSION_SCENE_OBJECT_MODEL_UTILS_H_
#define INTRINSIC_SCENE_CONVERSION_SCENE_OBJECT_MODEL_UTILS_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"

namespace intrinsic {
namespace scene_object {

// Returns the names of the non-fixed joints in the given entities.
absl::flat_hash_set<std::string> GetNonFixedJointNames(
    const std::vector<intrinsic_proto::scene_object::v1::Entity>& entities);

// Computes a joint entity's local parent_t_this transform from its
// KinematicsComponent:
//   parent_t_this = parent_t_inboard * inboard_t_outboard * outboard_t_child
//
// Note: Any `parent_t_this` field set on `entity` is ignored; the pose is
// computed solely from `entity.joint().kinematics_component()`.
//
// The inboard_t_outboard transform is determined by the joint's motion_type,
// axis, raw_value, and position limits:
// - Fixed joints: inboard_t_outboard is identity; raw_value must be 0.0 if set.
// - Revolute / Prismatic joints: the joint position is taken from raw_value
//   when set, or defaults based on the joint's fixed position limits (from
//   application_limits if set, otherwise system_limits): 0.0 if within [lower,
//   upper], or the midpoint lower + (upper - lower) * 0.5 when both bounds are
//   finite, or the finite bound (lower or upper). If no fixed limits are
//   present, defaults to 0.0. The axis defaults to (0, 0, 1) and is normalized.
absl::StatusOr<Pose3d> ResolveJointEntityPose(
    const intrinsic_proto::scene_object::v1::Entity& entity);

}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_CONVERSION_SCENE_OBJECT_MODEL_UTILS_H_
