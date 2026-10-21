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

#include "intrinsic/motion_planning/service/cache/cache_utils.h"

#include <queue>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/logging/proto/log_item.pb.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/object_world_proto_utils.h"
#include "intrinsic/world/objects/transform_node_internal.h"
#include "intrinsic/world/proto/attachment_component.pb.h"
#include "intrinsic/world/proto/collision_settings.pb.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/world.pb.h"

namespace intrinsic {

namespace {

using GeometricConstraintCase =
    ::intrinsic_proto::motion_planning::v1::GeometricConstraint::ConstraintCase;
using UniformGeometricConstraintCase = ::intrinsic_proto::motion_planning::v1::
    UniformGeometricConstraint::ConstraintCase;
using ::intrinsic_proto::motion_planning::v1::GeometricConstraint;
using ::intrinsic_proto::motion_planning::v1::MotionSegment;
using ::intrinsic_proto::motion_planning::v1::MotionSpecification;
using ::intrinsic_proto::motion_planning::v1::UniformGeometricConstraint;
using ::intrinsic_proto::world::TransformNodeReference;

// Resolves `reference` in `object_world` and inserts the target node's resource
// ID and root-relative pose into `transform_nodes_with_poses`.
absl::Status ExtractIdWithPoseFromTransformNodeReference(
    const object_world::ObjectWorld& object_world,
    const TransformNodeReference& reference,
    absl::flat_hash_map<ObjectWorldResourceId, Pose3d>&
        transform_nodes_with_poses) {
  INTR_ASSIGN_OR_RETURN(
      const object_world::TransformNode* const transform_node,
      object_world::GetTransformNodeByReference(object_world, reference),
      _ << "Failed to resolve transform node reference: "
        << reference.ShortDebugString());

  // If this node was already resolved and inserted, avoid redundant transform
  // computation.
  if (transform_nodes_with_poses.contains(transform_node->GetId())) {
    return absl::OkStatus();
  }

  if (transform_node->GetParent() == nullptr) {
    transform_nodes_with_poses.insert(
        {transform_node->GetId(), Pose3d::Identity()});
    return absl::OkStatus();
  }

  INTR_ASSIGN_OR_RETURN(const object_world::TransformNode* const root_node,
                        object_world.GetObject(RootObjectId()),
                        _ << "Failed to get root node from ObjectWorld.");
  INTR_ASSIGN_OR_RETURN(const Pose3d root_to_node_pose,
                        root_node->GetTransform(transform_node),
                        _ << "Failed to get transform to root for node: "
                          << transform_node->GetId().value());
  transform_nodes_with_poses.insert(
      {transform_node->GetId(), root_to_node_pose});
  return absl::OkStatus();
}

// Appends const pointers to the `moving_frame()` and `target_frame()` transform
// nodes of `constraint` to `references`, where `SubConstraint` is any
// constraint type containing `moving_frame()` and `target_frame()` members.
template <typename SubConstraint>
void AddMovingAndTargetFrames(
    const SubConstraint& constraint,
    std::vector<const TransformNodeReference*>& references) {
  references.push_back(&constraint.moving_frame());
  references.push_back(&constraint.target_frame());
}

// Appends const pointers to the `moving_frame()` and optional
// `reference_frame()` transform nodes of `constraint` to `references`, where
// `RelativeSubConstraint` is any constraint type containing a `moving_frame()`
// member and an optional `reference_frame()` member.
template <typename RelativeSubConstraint>
void AddRelativeFrames(const RelativeSubConstraint& constraint,
                       std::vector<const TransformNodeReference*>& references) {
  references.push_back(&constraint.moving_frame());
  // In relative constraints (`RelativePositionEquality`,
  // `RelativeRotationEquality`, and `RelativeCartesianPose`), `reference_frame`
  // is optional and defaults to the `moving_frame` pose at the start of the
  // motion segment when unset. Only collect `reference_frame` when a target
  // node reference is actually specified.
  if (constraint.has_reference_frame() &&
      constraint.reference_frame().transform_node_reference_case() !=
          TransformNodeReference::TRANSFORM_NODE_REFERENCE_NOT_SET) {
    references.push_back(&constraint.reference_frame());
  }
}

// Appends `TransformNodeReference` pointers from `constraint` to `references`,
// or enqueues nested sub-constraints into `constraints` when `constraint` is a
// `ConstraintIntersection`. Returns `absl::InvalidArgumentError` if
// `constraint` has an unrecognized `constraint_case()`.
absl::Status CollectSingleConstraintReferences(
    const GeometricConstraint& constraint,
    std::queue<const GeometricConstraint*>& constraints,
    std::vector<const TransformNodeReference*>& references) {
  switch (constraint.constraint_case()) {
    case GeometricConstraintCase::kJointPosition:
    case GeometricConstraintCase::kJointPositionLimits:
    case GeometricConstraintCase::kJointPositionSumLimit:
      // Joint position constraints only contain `ObjectReference` fields and
      // have no `TransformNodeReference` fields.
      return absl::OkStatus();
    case GeometricConstraintCase::kPositionEquality:
      AddMovingAndTargetFrames(constraint.position_equality(), references);
      return absl::OkStatus();
    case GeometricConstraintCase::kRotationEquality:
      AddMovingAndTargetFrames(constraint.rotation_equality(), references);
      return absl::OkStatus();
    case GeometricConstraintCase::kRotationCone:
      AddMovingAndTargetFrames(constraint.rotation_cone(), references);
      return absl::OkStatus();
    case GeometricConstraintCase::kCartesianPose:
      AddMovingAndTargetFrames(constraint.cartesian_pose(), references);
      return absl::OkStatus();
    case GeometricConstraintCase::kPositionBoundingBox:
      AddMovingAndTargetFrames(constraint.position_bounding_box(), references);
      return absl::OkStatus();
    case GeometricConstraintCase::kPointAt:
      AddMovingAndTargetFrames(constraint.point_at(), references);
      return absl::OkStatus();
    case GeometricConstraintCase::kRelativePositionEquality:
      AddRelativeFrames(constraint.relative_position_equality(), references);
      return absl::OkStatus();
    case GeometricConstraintCase::kRelativeRotationEquality:
      AddRelativeFrames(constraint.relative_rotation_equality(), references);
      return absl::OkStatus();
    case GeometricConstraintCase::kRelativeCartesianPose:
      AddRelativeFrames(constraint.relative_cartesian_pose(), references);
      return absl::OkStatus();
    case GeometricConstraintCase::kConstraintIntersection:
      for (const GeometricConstraint& sub_constraint :
           constraint.constraint_intersection().constraints()) {
        constraints.push(&sub_constraint);
      }
      return absl::OkStatus();
    case GeometricConstraintCase::CONSTRAINT_NOT_SET:
      return absl::OkStatus();
  }
  return absl::InvalidArgumentError(absl::StrCat(
      "Unhandled GeometricConstraint case: ", constraint.constraint_case()));
}

// Appends `TransformNodeReference` pointers from `constraint` to `references`,
// or enqueues nested sub-constraints into `constraints` when `constraint` is a
// `UniformGeometricConstraintIntersection`. Returns
// `absl::InvalidArgumentError` if `constraint` has an unrecognized
// `constraint_case()`.
absl::Status CollectSingleConstraintReferences(
    const UniformGeometricConstraint& constraint,
    std::queue<const UniformGeometricConstraint*>& constraints,
    std::vector<const TransformNodeReference*>& references) {
  switch (constraint.constraint_case()) {
    case UniformGeometricConstraintCase::kJointPositionSumLimit:
      // Joint position constraints only contain `ObjectReference` fields and
      // have no `TransformNodeReference` fields.
      return absl::OkStatus();
    case UniformGeometricConstraintCase::kRotationCone:
      AddMovingAndTargetFrames(constraint.rotation_cone(), references);
      return absl::OkStatus();
    case UniformGeometricConstraintCase::kPositionBoundingBox:
      AddMovingAndTargetFrames(constraint.position_bounding_box(), references);
      return absl::OkStatus();
    case UniformGeometricConstraintCase::kPointAt:
      AddMovingAndTargetFrames(constraint.point_at(), references);
      return absl::OkStatus();
    case UniformGeometricConstraintCase::
        kUniformGeometricConstraintIntersection:
      for (const UniformGeometricConstraint& sub_constraint :
           constraint.uniform_geometric_constraint_intersection()
               .constraints()) {
        constraints.push(&sub_constraint);
      }
      return absl::OkStatus();
    case UniformGeometricConstraintCase::CONSTRAINT_NOT_SET:
      return absl::OkStatus();
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Unhandled UniformGeometricConstraint case: ",
                   constraint.constraint_case()));
}

// Recursively traverses `top_level_constraint` and returns const pointers to
// all contained `TransformNodeReference` fields.
absl::StatusOr<std::vector<const TransformNodeReference*>>
CollectTransformNodeReferences(
    const GeometricConstraint& top_level_constraint) {
  std::vector<const TransformNodeReference*> references;
  std::queue<const GeometricConstraint*> constraints;
  constraints.push(&top_level_constraint);
  while (!constraints.empty()) {
    const GeometricConstraint* const constraint = constraints.front();
    constraints.pop();
    INTR_RETURN_IF_ERROR(CollectSingleConstraintReferences(
        *constraint, constraints, references));
  }
  return references;
}

// Recursively traverses `top_level_constraint` and returns const pointers to
// all contained `TransformNodeReference` fields.
absl::StatusOr<std::vector<const TransformNodeReference*>>
CollectTransformNodeReferences(
    const UniformGeometricConstraint& top_level_constraint) {
  std::vector<const TransformNodeReference*> references;
  std::queue<const UniformGeometricConstraint*> constraints;
  constraints.push(&top_level_constraint);
  while (!constraints.empty()) {
    const UniformGeometricConstraint* const constraint = constraints.front();
    constraints.pop();
    INTR_RETURN_IF_ERROR(CollectSingleConstraintReferences(
        *constraint, constraints, references));
  }
  return references;
}

// Collects all transform node references in `top_level_constraint` (of type
// `ConstraintT`, such as `GeometricConstraint` or
// `UniformGeometricConstraint`), resolves them against `object_world`, and
// inserts their resource IDs and root-relative poses into
// `transform_nodes_with_poses`.
template <typename ConstraintT>
absl::Status ExtractIdWithPoseFromConstraint(
    const object_world::ObjectWorld& object_world,
    const ConstraintT& top_level_constraint,
    absl::flat_hash_map<ObjectWorldResourceId, Pose3d>&
        transform_nodes_with_poses) {
  INTR_ASSIGN_OR_RETURN(
      const std::vector<const TransformNodeReference*> references,
      CollectTransformNodeReferences(top_level_constraint),
      _ << "Failed to collect transform node references.");

  for (const TransformNodeReference* const reference : references) {
    INTR_RETURN_IF_ERROR(ExtractIdWithPoseFromTransformNodeReference(
        object_world, *reference, transform_nodes_with_poses))
        << "Failed to extract node ID and pose for reference: "
        << reference->ShortDebugString();
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::vector<const TransformNodeReference*>>
CollectTransformNodeReferencesFromMotionSegment(
    const MotionSegment& motion_segment) {
  std::vector<const TransformNodeReference*> references;
  if (motion_segment.has_target()) {
    INTR_ASSIGN_OR_RETURN(
        const std::vector<const TransformNodeReference*> target_references,
        CollectTransformNodeReferences(motion_segment.target()));
    references.insert(references.end(), target_references.begin(),
                      target_references.end());
  }
  if (motion_segment.has_path_constraints()) {
    INTR_ASSIGN_OR_RETURN(
        const std::vector<const TransformNodeReference*> path_references,
        CollectTransformNodeReferences(motion_segment.path_constraints()));
    references.insert(references.end(), path_references.begin(),
                      path_references.end());
  }
  return references;
}

absl::Status ExtractIdWithPoseFromGeometricConstraint(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::GeometricConstraint&
        top_level_constraint,
    absl::flat_hash_map<ObjectWorldResourceId, Pose3d>&
        transform_nodes_with_poses) {
  return ExtractIdWithPoseFromConstraint(object_world, top_level_constraint,
                                         transform_nodes_with_poses);
}

absl::Status ExtractIdWithPoseFromUniformGeometricConstraint(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::UniformGeometricConstraint&
        top_level_constraint,
    absl::flat_hash_map<ObjectWorldResourceId, Pose3d>&
        transform_nodes_with_poses) {
  return ExtractIdWithPoseFromConstraint(object_world, top_level_constraint,
                                         transform_nodes_with_poses);
}

absl::Status ExtractIdWithPoseFromMotionSpecification(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::MotionSpecification&
        motion_specification,
    absl::flat_hash_map<ObjectWorldResourceId, Pose3d>&
        transform_nodes_with_poses) {
  for (const auto& segment : motion_specification.motion_segments()) {
    INTR_RETURN_IF_ERROR(ExtractIdWithPoseFromMotionSegment(
        object_world, segment, transform_nodes_with_poses));
  }
  return absl::OkStatus();
}

absl::Status ExtractIdWithPoseFromMotionSegment(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::MotionSegment& motion_segment,
    absl::flat_hash_map<ObjectWorldResourceId, Pose3d>&
        transform_nodes_with_poses) {
  if (motion_segment.has_target()) {
    INTR_RETURN_IF_ERROR(ExtractIdWithPoseFromGeometricConstraint(
        object_world, motion_segment.target(), transform_nodes_with_poses))
        << "Failed to extract IDs and poses from motion segment target.";
  }
  if (motion_segment.has_path_constraints()) {
    INTR_RETURN_IF_ERROR(ExtractIdWithPoseFromUniformGeometricConstraint(
        object_world, motion_segment.path_constraints(),
        transform_nodes_with_poses))
        << "Failed to extract IDs and poses from motion segment path "
           "constraints.";
  }
  return absl::OkStatus();
}

}  // namespace intrinsic
