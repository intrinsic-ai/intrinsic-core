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

#ifndef INTRINSIC_MOTION_PLANNING_SERVICE_CACHE_CACHE_UTILS_H_
#define INTRINSIC_MOTION_PLANNING_SERVICE_CACHE_CACHE_UTILS_H_

#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/logging/proto/log_item.pb.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/proto/attachment_component.pb.h"
#include "intrinsic/world/proto/collision_settings.pb.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/world.pb.h"

namespace intrinsic {

// Collects all non-owning pointers to `TransformNodeReference`s present in the
// target and path constraints of `motion_segment`.
absl::StatusOr<
    std::vector<const intrinsic_proto::world::TransformNodeReference*>>
CollectTransformNodeReferencesFromMotionSegment(
    const intrinsic_proto::motion_planning::v1::MotionSegment& motion_segment);

// Resolves all transform nodes referenced in `top_level_constraint` in
// `object_world` and inserts their resource IDs and root-relative poses into
// `transform_nodes_with_poses`.
absl::Status ExtractIdWithPoseFromGeometricConstraint(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::GeometricConstraint&
        top_level_constraint,
    absl::flat_hash_map<ObjectWorldResourceId, Pose3d>&
        transform_nodes_with_poses);

// Resolves all transform nodes referenced in `top_level_constraint` in
// `object_world` and inserts their resource IDs and root-relative poses into
// `transform_nodes_with_poses`.
absl::Status ExtractIdWithPoseFromUniformGeometricConstraint(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::UniformGeometricConstraint&
        top_level_constraint,
    absl::flat_hash_map<ObjectWorldResourceId, Pose3d>&
        transform_nodes_with_poses);

// Extract the IDs and poses of all transform nodes referred in the given
// motion specification and saves them in transform_nodes_with_poses.
absl::Status ExtractIdWithPoseFromMotionSpecification(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::MotionSpecification&
        motion_specification,
    absl::flat_hash_map<ObjectWorldResourceId, Pose3d>&
        transform_nodes_with_poses);

// Resolves all transform nodes referenced in the target and path constraints of
// `motion_segment` in `object_world` and inserts their resource IDs and
// root-relative poses into `transform_nodes_with_poses`.
absl::Status ExtractIdWithPoseFromMotionSegment(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::MotionSegment& motion_segment,
    absl::flat_hash_map<ObjectWorldResourceId, Pose3d>&
        transform_nodes_with_poses);

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_SERVICE_CACHE_CACHE_UTILS_H_
