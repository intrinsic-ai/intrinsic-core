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

#include "intrinsic/motion_planning/service/cache/cache_key_normalization.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_fingerprint.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/shapes/point_cloud.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/proto/v1/robot_specification.pb.h"
#include "intrinsic/util/hash.h"
#include "intrinsic/util/object_store/memoize_fingerprint.h"
#include "intrinsic/util/object_store/object_ref.h"
#include "intrinsic/util/proto/pb_hash.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/util/make_collision_settings.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/objects/defaulting_world_object_visitor.h"
#include "intrinsic/world/objects/frame_internal.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_creation_utils.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/object_world_proto_utils.h"
#include "intrinsic/world/objects/transform_node_internal.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/proto/collision_settings.pb.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

namespace {

using GeometricConstraintCase =
    ::intrinsic_proto::motion_planning::v1::GeometricConstraint::ConstraintCase;
using UniformGeometricConstraintCase = ::intrinsic_proto::motion_planning::v1::
    UniformGeometricConstraint::ConstraintCase;
using ::intrinsic_proto::motion_planning::v1::GeometricConstraint;
using ::intrinsic_proto::motion_planning::v1::MotionSegment;
using ::intrinsic_proto::motion_planning::v1::MotionSpecification;
using ::intrinsic_proto::motion_planning::v1::RobotSpecification;
using ::intrinsic_proto::motion_planning::v1::UniformGeometricConstraint;
using ::intrinsic_proto::world::CollisionSettings;
using ::intrinsic_proto::world::EntityReference;
using ::intrinsic_proto::world::FrameReferenceByName;
using ::intrinsic_proto::world::ObjectEntityFilter;
using ::intrinsic_proto::world::ObjectOrEntityReference;
using ::intrinsic_proto::world::ObjectReference;
using ::intrinsic_proto::world::TransformNodeReference;

constexpr absl::string_view kPathDelimiter = "/";
constexpr absl::string_view kRootObjectName = "root";
constexpr absl::string_view kGeometryFingerprintPrefix = "geo_";

// Appends collision shape fingerprints for `entity_id` in `entity_world` to
// `collision_fingerprints`. Leaves `collision_fingerprints` unchanged and
// returns `absl::OkStatus()` if `entity_id` has no collision geometry.
// Propagates any unexpected error from
// `ExtractEntityCollisionGeometryFeatures()` such as `entity_id` not existing
// in `entity_world` or fingerprint generation failing.
absl::Status AppendEntityCollisionFingerprints(
    const World& entity_world, const EntityId entity_id,
    std::vector<std::string>& collision_fingerprints) {
  INTR_ASSIGN_OR_RETURN(
      const std::vector<EntityCollisionGeometryFeature> entity_features,
      ExtractEntityCollisionGeometryFeatures(entity_world, entity_id),
      _ << "Failed to extract collision geometry features for entity ID: "
        << entity_id.value());
  for (const EntityCollisionGeometryFeature& feature : entity_features) {
    collision_fingerprints.push_back(feature.geometry_fingerprint);
  }
  return absl::OkStatus();
}

// Formats `collision_fingerprints` into a canonical `geo_...` token, sorting
// and hashing deterministically when multiple geometries are present so
// multi-geometry tokens are map-order-independent. Returns `std::nullopt` if
// `collision_fingerprints` is empty.
std::optional<std::string> FormatCollisionGeometryToken(
    std::vector<std::string>& collision_fingerprints) {
  if (collision_fingerprints.empty()) {
    return std::nullopt;
  }

  if (collision_fingerprints.size() == 1) {
    return absl::StrCat(kGeometryFingerprintPrefix,
                        collision_fingerprints.front());
  }

  // Sort fingerprints deterministically before hashing so multi-geometry
  // objects produce a map-order-independent token.
  absl::c_sort(collision_fingerprints);
  const uint64_t combined_fingerprint =
      Fingerprint(absl::StrJoin(collision_fingerprints, ":"));
  return absl::StrCat(kGeometryFingerprintPrefix,
                      absl::Hex(combined_fingerprint));
}

// Gathers all collision geometry fingerprints across all entities belonging to
// `object`, sorts them deterministically, and combines them into a
// normalized `geo_...` token. Returns `std::nullopt` (`absl::OkStatus()`) if
// `object` has no collision geometry across its entities, or propagates any
// unexpected error encountered while extracting entity collision fingerprints.
absl::StatusOr<std::optional<std::string>> ComputeCollisionGeometryToken(
    const object_world::WorldObject& object) {
  const World& entity_world = object.GetEntityWorld();
  std::vector<std::string> collision_fingerprints;
  for (const EntityId entity_id : object.GetEntityIds()) {
    INTR_RETURN_IF_ERROR(AppendEntityCollisionFingerprints(
        entity_world, entity_id, collision_fingerprints));
  }
  return FormatCollisionGeometryToken(collision_fingerprints);
}

// Returns the full normalized hierarchical path for `parent` via
// `GetNormalizedFullPath()`, or `"root"` (`absl::OkStatus()`) when `parent` is
// `nullptr`. Propagates any unexpected error from `GetNormalizedFullPath()`.
absl::StatusOr<std::string> GetNormalizedParentPath(
    const object_world::WorldObject* const parent) {
  if (parent == nullptr) {
    return std::string(kRootObjectName);
  }
  return GetNormalizedFullPath(*parent);
}

// Populates the `by_name` subfield of `reference` with the canonical
// hierarchical path of `node` in `object_world`. Returns `OkStatus` if `node`
// was resolved as the root object, a `Frame`, or a `WorldObject` in
// `object_world`, or `NotFoundError` otherwise.
absl::Status SetCanonicalByNameForTransformNode(
    const object_world::ObjectWorld& object_world,
    const object_world::TransformNode& node,
    TransformNodeReference& reference) {
  if (node.GetId() == RootObjectId()) {
    reference.mutable_by_name()->mutable_object()->set_object_name(
        std::string(kRootObjectName));
    return absl::OkStatus();
  }

  const absl::StatusOr<const object_world::Frame*> status_or_frame =
      object_world.GetFrame(node.GetId());
  const bool is_frame_node =
      status_or_frame.ok() && status_or_frame.value() != nullptr;
  if (is_frame_node) {
    const object_world::Frame* const frame = status_or_frame.value();
    INTR_ASSIGN_OR_RETURN(
        std::string normalized_parent_path,
        GetNormalizedParentPath(frame->GetParent()),
        _ << "Failed to compute normalized parent path for frame: "
          << frame->GetName().value());
    FrameReferenceByName& frame_reference =
        *reference.mutable_by_name()->mutable_frame();
    frame_reference.set_object_name(std::move(normalized_parent_path));
    frame_reference.set_frame_name(frame->GetName().value());
    return absl::OkStatus();
  }

  const absl::StatusOr<const object_world::WorldObject*>
      status_or_world_object = object_world.GetObject(node.GetId());
  const bool is_world_object_node =
      status_or_world_object.ok() && status_or_world_object.value() != nullptr;
  if (is_world_object_node) {
    INTR_ASSIGN_OR_RETURN(
        std::string normalized_object_path,
        GetNormalizedFullPath(*status_or_world_object.value()),
        _ << "Failed to compute normalized path for WorldObject: "
          << status_or_world_object.value()->GetName().value());
    reference.mutable_by_name()->mutable_object()->set_object_name(
        std::move(normalized_object_path));
    return absl::OkStatus();
  }

  // Every `TransformNode` belonging to `object_world` is either a `Frame`
  // or a `WorldObject`. This fallback is only reached if `node` is not
  // registered in `object_world` (e.g., it belongs to a different
  // `ObjectWorld` instance).
  return absl::NotFoundError(absl::StrCat(
      "TransformNode with id \"", node.GetId().value(),
      "\" could not be resolved as a Frame or WorldObject in ObjectWorld."));
}

// Helper struct implementing a visitor for `WorldObject::accept` that
// determines if the passed object is a kinematic object or not.
struct IsKinematicObjectVisitor
    : public object_world::DefaultingWorldObjectConstVisitor {
  // The default visit function used by any type that does not have an
  // override for `Visit()` defined in this type, sets `is_kinematic` to false.
  absl::Status DefaultVisit(const object_world::WorldObject&) override {
    is_kinematic = false;
    return absl::OkStatus();
  }

  // The `Visit()` override for `KinematicObject`, sets `is_kinematic` to true.
  absl::Status Visit(const object_world::KinematicObject&) override {
    is_kinematic = true;
    return absl::OkStatus();
  }

  // Member tracking if the last passed object to `Visit()` was a
  // `KinematicObject`. Defaults to false.
  bool is_kinematic = false;
};

// Appends mutable pointers to the `mutable_moving_frame()` and
// `mutable_target_frame()` transform nodes of `constraint` to `references`,
// where `SubConstraint` is any constraint type containing
// `mutable_moving_frame()` and `mutable_target_frame()` members.
template <typename SubConstraint>
void AddMutableMovingAndTargetFrames(
    SubConstraint& constraint,
    std::vector<TransformNodeReference*>& references) {
  references.push_back(constraint.mutable_moving_frame());
  references.push_back(constraint.mutable_target_frame());
}

// Appends mutable pointers to the `mutable_moving_frame()` and optional
// `mutable_reference_frame()` transform nodes of `constraint` to `references`,
// where `RelativeSubConstraint` is any constraint type containing a
// `mutable_moving_frame()` member and an optional `mutable_reference_frame()`
// member.
template <typename RelativeSubConstraint>
void AddMutableRelativeFrames(
    RelativeSubConstraint& constraint,
    std::vector<TransformNodeReference*>& references) {
  references.push_back(constraint.mutable_moving_frame());
  // In relative constraints, `reference_frame` is optional. If an empty
  // `reference_frame` submessage is present without a target node reference,
  // clear it so that unset and empty `reference_frame` fields normalize
  // identically.
  if (!constraint.has_reference_frame()) {
    return;
  }
  if (constraint.reference_frame().transform_node_reference_case() ==
      TransformNodeReference::TRANSFORM_NODE_REFERENCE_NOT_SET) {
    constraint.clear_reference_frame();
    return;
  }
  references.push_back(constraint.mutable_reference_frame());
}

// Appends a mutable pointer to the `mutable_object_id()` field of `constraint`
// to `object_references` when `has_object_id()` is true.
template <typename JointSubConstraint>
void AddMutableObjectReferenceIfPresent(
    JointSubConstraint& constraint,
    std::vector<ObjectReference*>& object_references) {
  if (constraint.has_object_id()) {
    object_references.push_back(constraint.mutable_object_id());
  }
}

// Appends mutable `TransformNodeReference` and `ObjectReference` pointers from
// `constraint` to `transform_references` and `object_references`, or enqueues
// nested sub-constraints into `constraints` when `constraint` is a
// `ConstraintIntersection`. Returns `absl::InvalidArgumentError` if
// `constraint` has an unrecognized `constraint_case()`.
absl::Status CollectSingleConstraintMutableReferences(
    GeometricConstraint& constraint,
    std::queue<GeometricConstraint*>& constraints,
    std::vector<TransformNodeReference*>& transform_references,
    std::vector<ObjectReference*>& object_references) {
  switch (constraint.constraint_case()) {
    case GeometricConstraintCase::kJointPosition:
      // `JointPosition` only contains joint values and has no object or
      // transform node references.
      return absl::OkStatus();
    case GeometricConstraintCase::kJointPositionLimits:
      AddMutableObjectReferenceIfPresent(
          *constraint.mutable_joint_position_limits(), object_references);
      return absl::OkStatus();
    case GeometricConstraintCase::kJointPositionSumLimit:
      AddMutableObjectReferenceIfPresent(
          *constraint.mutable_joint_position_sum_limit(), object_references);
      return absl::OkStatus();
    case GeometricConstraintCase::kPositionEquality:
      AddMutableMovingAndTargetFrames(*constraint.mutable_position_equality(),
                                      transform_references);
      return absl::OkStatus();
    case GeometricConstraintCase::kRotationEquality:
      AddMutableMovingAndTargetFrames(*constraint.mutable_rotation_equality(),
                                      transform_references);
      return absl::OkStatus();
    case GeometricConstraintCase::kRotationCone:
      AddMutableMovingAndTargetFrames(*constraint.mutable_rotation_cone(),
                                      transform_references);
      return absl::OkStatus();
    case GeometricConstraintCase::kCartesianPose:
      AddMutableMovingAndTargetFrames(*constraint.mutable_cartesian_pose(),
                                      transform_references);
      return absl::OkStatus();
    case GeometricConstraintCase::kPositionBoundingBox:
      AddMutableMovingAndTargetFrames(
          *constraint.mutable_position_bounding_box(), transform_references);
      return absl::OkStatus();
    case GeometricConstraintCase::kPointAt:
      AddMutableMovingAndTargetFrames(*constraint.mutable_point_at(),
                                      transform_references);
      return absl::OkStatus();
    case GeometricConstraintCase::kRelativePositionEquality:
      AddMutableRelativeFrames(*constraint.mutable_relative_position_equality(),
                               transform_references);
      return absl::OkStatus();
    case GeometricConstraintCase::kRelativeRotationEquality:
      AddMutableRelativeFrames(*constraint.mutable_relative_rotation_equality(),
                               transform_references);
      return absl::OkStatus();
    case GeometricConstraintCase::kRelativeCartesianPose:
      AddMutableRelativeFrames(*constraint.mutable_relative_cartesian_pose(),
                               transform_references);
      return absl::OkStatus();
    case GeometricConstraintCase::kConstraintIntersection:
      for (GeometricConstraint& sub_constraint :
           *constraint.mutable_constraint_intersection()
                ->mutable_constraints()) {
        constraints.push(&sub_constraint);
      }
      return absl::OkStatus();
    case GeometricConstraintCase::CONSTRAINT_NOT_SET:
      return absl::OkStatus();
  }
  return absl::InvalidArgumentError(absl::StrCat(
      "Unhandled GeometricConstraint case: ", constraint.constraint_case()));
}

// Appends mutable `TransformNodeReference` and `ObjectReference` pointers from
// `constraint` to `transform_references` and `object_references`, or enqueues
// nested sub-constraints into `constraints` when `constraint` is a
// `UniformGeometricConstraintIntersection`. Returns
// `absl::InvalidArgumentError` if `constraint` has an unrecognized
// `constraint_case()`.
absl::Status CollectSingleConstraintMutableReferences(
    UniformGeometricConstraint& constraint,
    std::queue<UniformGeometricConstraint*>& constraints,
    std::vector<TransformNodeReference*>& transform_references,
    std::vector<ObjectReference*>& object_references) {
  switch (constraint.constraint_case()) {
    case UniformGeometricConstraintCase::kJointPositionSumLimit:
      AddMutableObjectReferenceIfPresent(
          *constraint.mutable_joint_position_sum_limit(), object_references);
      return absl::OkStatus();
    case UniformGeometricConstraintCase::kRotationCone:
      AddMutableMovingAndTargetFrames(*constraint.mutable_rotation_cone(),
                                      transform_references);
      return absl::OkStatus();
    case UniformGeometricConstraintCase::kPositionBoundingBox:
      AddMutableMovingAndTargetFrames(
          *constraint.mutable_position_bounding_box(), transform_references);
      return absl::OkStatus();
    case UniformGeometricConstraintCase::kPointAt:
      AddMutableMovingAndTargetFrames(*constraint.mutable_point_at(),
                                      transform_references);
      return absl::OkStatus();
    case UniformGeometricConstraintCase::
        kUniformGeometricConstraintIntersection:
      for (UniformGeometricConstraint& sub_constraint :
           *constraint.mutable_uniform_geometric_constraint_intersection()
                ->mutable_constraints()) {
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

// Recursively traverses `top_level_constraint` (of type `ConstraintT`, such as
// `GeometricConstraint` or `UniformGeometricConstraint`) and populates mutable
// pointers to all contained `TransformNodeReference` and `ObjectReference`
// fields.
template <typename ConstraintT>
absl::Status CollectMutableReferences(
    ConstraintT& top_level_constraint,
    std::vector<TransformNodeReference*>& transform_references,
    std::vector<ObjectReference*>& object_references) {
  std::queue<ConstraintT*> constraints;
  constraints.push(&top_level_constraint);
  while (!constraints.empty()) {
    ConstraintT* const constraint = constraints.front();
    constraints.pop();
    INTR_RETURN_IF_ERROR(CollectSingleConstraintMutableReferences(
        *constraint, constraints, transform_references, object_references));
  }
  return absl::OkStatus();
}

// Normalizes `object_reference` in-place against `object_world` by clearing
// debug hints and ephemeral runtime IDs and populating `by_name`.
absl::Status NormalizeObjectReference(
    const object_world::ObjectWorld& object_world,
    ObjectReference& object_reference) {
  if (object_reference.object_reference_case() ==
      ObjectReference::OBJECT_REFERENCE_NOT_SET) {
    object_reference.clear_debug_hint();
    return absl::OkStatus();
  }
  INTR_ASSIGN_OR_RETURN(
      const object_world::WorldObject* const world_object,
      object_world::GetObjectByReference(object_world, object_reference),
      _ << "Failed to resolve object reference for normalization: "
        << object_reference.ShortDebugString());
  INTR_ASSIGN_OR_RETURN(
      std::string normalized_object_path, GetNormalizedFullPath(*world_object),
      _ << "Failed to compute normalized path for object reference: "
        << object_reference.ShortDebugString());
  object_reference.clear_debug_hint();
  object_reference.clear_id();
  object_reference.mutable_by_name()->set_object_name(
      std::move(normalized_object_path));
  return absl::OkStatus();
}

// Normalizes `reference` in-place by resolving its target node in
// `object_world`, clearing ephemeral runtime IDs and debug hints, and
// populating its canonical hierarchical `by_name` path.
absl::Status NormalizeTransformNodeReference(
    const object_world::ObjectWorld& object_world,
    TransformNodeReference& reference) {
  if (reference.transform_node_reference_case() ==
      TransformNodeReference::TRANSFORM_NODE_REFERENCE_NOT_SET) {
    reference.clear_debug_hint();
    return absl::OkStatus();
  }

  INTR_ASSIGN_OR_RETURN(
      const object_world::TransformNode* const transform_node,
      object_world::GetTransformNodeByReference(object_world, reference),
      _ << "Failed to resolve transform node reference for normalization: "
        << reference.ShortDebugString());
  reference.clear_debug_hint();
  reference.clear_id();
  return SetCanonicalByNameForTransformNode(object_world, *transform_node,
                                            reference);
}

// Collects all mutable transform node and object references in
// `top_level_constraint` (of type `ConstraintT`, such as `GeometricConstraint`
// or `UniformGeometricConstraint`) and normalizes them in-place against
// `object_world`.
template <typename ConstraintT>
absl::Status NormalizeGeometricConstraint(
    const object_world::ObjectWorld& object_world,
    ConstraintT& top_level_constraint) {
  std::vector<TransformNodeReference*> transform_references;
  std::vector<ObjectReference*> object_references;
  INTR_RETURN_IF_ERROR(CollectMutableReferences(
      top_level_constraint, transform_references, object_references))
      << "Failed to collect mutable references for normalization.";
  for (TransformNodeReference* const reference : transform_references) {
    INTR_RETURN_IF_ERROR(
        NormalizeTransformNodeReference(object_world, *reference));
  }
  for (ObjectReference* const object_reference : object_references) {
    INTR_RETURN_IF_ERROR(
        NormalizeObjectReference(object_world, *object_reference));
  }
  return absl::OkStatus();
}

// Normalizes the target and path constraints of `motion_segment` in-place
// against `object_world` to canonical `by_name` references, and clears segment
// collision settings and empty path constraints.
absl::Status NormalizeMotionSegment(
    const object_world::ObjectWorld& object_world,
    MotionSegment& motion_segment) {
  if (motion_segment.has_target()) {
    INTR_RETURN_IF_ERROR(NormalizeGeometricConstraint(
        object_world, *motion_segment.mutable_target()))
        << "Failed to normalize motion segment target constraint.";
  }
  if (motion_segment.has_path_constraints()) {
    INTR_RETURN_IF_ERROR(NormalizeGeometricConstraint(
        object_world, *motion_segment.mutable_path_constraints()))
        << "Failed to normalize motion segment path constraints.";
  }
  // Per-segment `collision_settings` are cleared from the group signature
  // because they are extracted and compared separately on the cache entry.
  motion_segment.clear_collision_settings();
  // In proto2/proto3 submessages, an explicitly initialized empty submessage
  // (`has_path_constraints() == true`, `ByteSizeLong() == 0`) serializes
  // differently from an unset submessage (`has_path_constraints() == false`),
  // so clear empty `path_constraints` to ensure identical `pb_hash` values.
  if (motion_segment.has_path_constraints() &&
      motion_segment.path_constraints().ByteSizeLong() == 0) {
    motion_segment.clear_path_constraints();
  }
  return absl::OkStatus();
}

// Normalizes all motion segments of `motion_specification` in-place against
// `object_world`.
absl::Status NormalizeMotionSpecification(
    const object_world::ObjectWorld& object_world,
    MotionSpecification& motion_specification) {
  for (MotionSegment& segment :
       *motion_specification.mutable_motion_segments()) {
    INTR_RETURN_IF_ERROR(NormalizeMotionSegment(object_world, segment));
  }
  return absl::OkStatus();
}

// Normalizes `robot_specification` in-place against `object_world` by clearing
// ephemeral or continuous fields (such as `start_configuration`) and converting
// `robot_reference` to a canonical `by_name` object reference.
absl::Status NormalizeRobotSpecification(
    const object_world::ObjectWorld& object_world,
    RobotSpecification& robot_specification) {
  robot_specification.clear_start_configuration();
  const bool has_robot_object_id =
      robot_specification.has_robot_reference() &&
      robot_specification.robot_reference().has_object_id();
  if (!has_robot_object_id) {
    return absl::OkStatus();
  }
  return NormalizeObjectReference(
      object_world,
      *robot_specification.mutable_robot_reference()->mutable_object_id());
}

// Returns a normalized name for `entity_id` in `entity_world`: uses the
// entity's local name if set, or falls back to its collision geometry
// fingerprint. Returns `NotFoundError` if the entity does not exist or has
// neither.
absl::StatusOr<std::string> GetNormalizedEntityToken(const World& entity_world,
                                                     const EntityId entity_id) {
  INTR_ASSIGN_OR_RETURN(
      const WorldEntity* const entity, entity_world.GetEntityById(entity_id),
      _ << "Failed to look up entity in World: " << entity_id.value());
  if (entity == nullptr) {
    return absl::NotFoundError(absl::StrCat(
        "Entity lookup returned nullptr for entity ID: ", entity_id.value()));
  }
  const std::string local_name = entity->GetLocalName();
  if (!local_name.empty()) {
    return local_name;
  }

  std::vector<std::string> collision_fingerprints;
  INTR_RETURN_IF_ERROR(AppendEntityCollisionFingerprints(
      entity_world, entity_id, collision_fingerprints));
  const std::optional<std::string> geometry_token =
      FormatCollisionGeometryToken(collision_fingerprints);
  if (geometry_token.has_value()) {
    return *geometry_token;
  }
  return absl::NotFoundError(absl::StrCat(
      "Entity with ID ", entity_id.value(),
      " has neither a local name nor collision geometry for normalization."));
}

// Removes adjacent duplicate messages from an already-sorted `repeated_field`.
template <typename MessageT>
void DeduplicateSortedRepeatedField(
    google::protobuf::RepeatedPtrField<MessageT>& repeated_field) {
  const intrinsic::pb_equals pb_equals{};
  repeated_field.erase(
      std::unique(repeated_field.begin(), repeated_field.end(), pb_equals),
      repeated_field.end());
}

// Normalizes `entity_filter` in-place by converting `entity_references()` into
// sorted, deduplicated `entity_names()`. If `include_all_entities()` is true,
// clears all other filter fields since they have no effect.
absl::Status NormalizeEntityFilter(
    const object_world::ObjectWorld& object_world,
    ObjectEntityFilter& entity_filter) {
  // When `include_all_entities()` is enabled, all narrower selection fields are
  // redundant.
  if (entity_filter.include_all_entities()) {
    entity_filter.clear_include_base_entity();
    entity_filter.clear_include_final_entity();
    entity_filter.clear_entity_references();
    entity_filter.clear_entity_names();
    return absl::OkStatus();
  }

  const World& entity_world = object_world.GetEntityWorld();
  std::vector<std::string> normalized_names(
      entity_filter.entity_names().begin(), entity_filter.entity_names().end());

  for (const EntityReference& entity_reference :
       entity_filter.entity_references()) {
    INTR_ASSIGN_OR_RETURN(
        const EntityId entity_id,
        object_world::ObjectWorldResourceIdToEntityId(
            ObjectWorldResourceId(entity_reference.id())),
        _ << "Failed to parse entity reference ID in ObjectEntityFilter: "
          << entity_reference.id());
    INTR_ASSIGN_OR_RETURN(
        std::string resolved_token,
        GetNormalizedEntityToken(entity_world, entity_id),
        _ << "Failed to resolve entity reference in ObjectEntityFilter: "
          << entity_reference.id());
    normalized_names.push_back(std::move(resolved_token));
  }

  entity_filter.clear_entity_references();
  absl::c_sort(normalized_names);
  normalized_names.erase(
      std::unique(normalized_names.begin(), normalized_names.end()),
      normalized_names.end());
  entity_filter.clear_entity_names();
  for (const std::string& name : normalized_names) {
    entity_filter.add_entity_names(name);
  }
  return absl::OkStatus();
}

// Converts an entity reference (`ObjectOrEntityReference::kEntity`) into an
// object reference on the `WorldObject` that owns the entity: uses `kObject`
// if the object has only one entity, or `kObjectWithFilter` (filtering for
// that specific link/entity) if the object has multiple entities.
absl::Status NormalizeEntityReferenceInCollisionRule(
    const object_world::ObjectWorld& object_world,
    ObjectOrEntityReference& reference) {
  INTR_ASSIGN_OR_RETURN(
      const EntityId entity_id,
      object_world::ObjectWorldResourceIdToEntityId(
          ObjectWorldResourceId(reference.entity().id())),
      _ << "Failed to parse entity reference ID in collision rule: "
        << reference.entity().id());
  const AttachmentEntityId attachment_entity_id(entity_id.value());
  INTR_ASSIGN_OR_RETURN(
      const object_world::WorldObject* const world_object,
      object_world.GetObjectByMemberEntityId(attachment_entity_id),
      _ << "Failed to find owning WorldObject in ObjectWorld for entity "
           "reference: "
        << reference.entity().id());
  if (world_object == nullptr) {
    return absl::NotFoundError(absl::StrCat(
        "Owning WorldObject lookup returned nullptr for entity reference: ",
        reference.entity().id()));
  }
  INTR_ASSIGN_OR_RETURN(
      std::string normalized_object_path, GetNormalizedFullPath(*world_object),
      _ << "Failed to compute normalized path for owning WorldObject of entity "
           "reference: "
        << reference.entity().id());
  // If the object only has one entity, refer to the whole object (`kObject`)
  // so entity-based and object-based rules normalize to the same form.
  if (world_object->GetEntityIds().size() == 1) {
    reference.mutable_object()->mutable_by_name()->set_object_name(
        std::move(normalized_object_path));
    return absl::OkStatus();
  }
  // If the object has multiple entities (such as robot links), use
  // `kObjectWithFilter` to select only this entity by name.
  INTR_ASSIGN_OR_RETURN(
      const std::string entity_token,
      GetNormalizedEntityToken(object_world.GetEntityWorld(), entity_id),
      _ << "Failed to resolve entity token for multi-entity object in "
           "collision rule: "
        << reference.entity().id());
  reference.mutable_object_with_filter()
      ->mutable_reference()
      ->mutable_by_name()
      ->set_object_name(std::move(normalized_object_path));
  reference.mutable_object_with_filter()
      ->mutable_entity_filter()
      ->add_entity_names(entity_token);
  return absl::OkStatus();
}

// Normalizes `reference` in-place by replacing object and entity IDs with
// normalized `by_name` paths.
absl::Status NormalizeObjectOrEntityReference(
    const object_world::ObjectWorld& object_world,
    ObjectOrEntityReference& reference) {
  switch (reference.type_case()) {
    case ObjectOrEntityReference::kObject: {
      return NormalizeObjectReference(object_world,
                                      *reference.mutable_object());
    }
    case ObjectOrEntityReference::kObjectWithFilter: {
      INTR_RETURN_IF_ERROR(NormalizeObjectReference(
          object_world,
          *reference.mutable_object_with_filter()->mutable_reference()));
      return NormalizeEntityFilter(
          object_world,
          *reference.mutable_object_with_filter()->mutable_entity_filter());
    }
    case ObjectOrEntityReference::kEntity: {
      return NormalizeEntityReferenceInCollisionRule(object_world, reference);
    }
    case ObjectOrEntityReference::TYPE_NOT_SET:
      return absl::OkStatus();
  }
  return absl::InvalidArgumentError(absl::StrCat(
      "Unhandled ObjectOrEntityReference case: ", reference.type_case()));
}

// Normalizes, sorts, and deduplicates `references` against `object_world`.
absl::Status NormalizeAndSortReferenceList(
    const object_world::ObjectWorld& object_world,
    google::protobuf::RepeatedPtrField<ObjectOrEntityReference>& references) {
  for (ObjectOrEntityReference& reference : references) {
    INTR_RETURN_IF_ERROR(
        NormalizeObjectOrEntityReference(object_world, reference));
  }
  absl::c_sort(references, CompareReferences);
  DeduplicateSortedRepeatedField(references);
  return absl::OkStatus();
}

}  // namespace

bool IsKinematicObject(const object_world::WorldObject& object) {
  IsKinematicObjectVisitor visitor;
  // The error can be ignored as `Visit` and `DefaultVisit` functions of
  // `IsKinematicObjectVisitor` always returns `absl::OkStatus()`.
  object.Accept(visitor).IgnoreError();
  return visitor.is_kinematic;
}

absl::StatusOr<std::string> ComputeCollisionGeometryFingerprint(
    const Geometry& geometry) {
  // Guard against silent O(N) fallback on future shape additions.
  // Any new alternative added to `ComputedShape` must be handled explicitly
  // here.
  static_assert(std::variant_size_v<geo::ExactGeometry::ComputedShape> == 2,
                "Handle the new ExactGeometry::ComputedShape alternative in "
                "ComputeCollisionGeometryFingerprint().");

  const geo::ExactGeometry& exact_geometry = geometry.GetExactGeometry();

  // Primitives must be checked prior to meshes because `geo::ExactGeometry`
  // constructors for primitive shapes eagerly instantiate an internal
  // `ObjectRef<Mesh>`. Checking `HasPrimitiveShapes()` first ensures that
  // the analytical dimensions of the primitive shape are hashed rather
  // than its tessellation.
  if (exact_geometry.HasPrimitiveShapes()) {
    // Use the default geometry options to make the fingerprint only depend on
    // the exact geometry, avoiding to create a new ExactGeometry object if
    // already using the default options.
    if (exact_geometry.options() == geo::GeometryOptions::Default()) {
      return geo::GenerateFingerprint(exact_geometry);
    }
    return geo::GenerateFingerprint(
        geo::ExactGeometry(exact_geometry, geo::GeometryOptions::Default()));
  }

  // For meshes, use the precomputed `ObjectStore` content hash in
  // `ObjectRef<Mesh>`. `MemoizeFingerprint` provides lock-free O(1) retrieval.
  if (exact_geometry.HasMesh()) {
    INTR_ASSIGN_OR_RETURN(const ObjectRef<geo::Mesh> mesh_ref,
                          exact_geometry.GetMesh(),
                          _ << "Failed to get mesh from exact geometry.");
    const uint64_t mesh_fingerprint =
        memoize::MemoizeFingerprint<ObjectRef<geo::Mesh>>::Fingerprint(
            mesh_ref);
    return absl::StrCat(absl::Hex(mesh_fingerprint));
  }

  // For point clouds, use the precomputed `ObjectStore` content hash in
  // `ObjectRef<PointCloud>`. `MemoizeFingerprint` provides lock-free O(1)
  // retrieval.
  if (exact_geometry.HasPointCloud()) {
    INTR_ASSIGN_OR_RETURN(
        const ObjectRef<geo::PointCloud> point_cloud_ref,
        exact_geometry.GetPointCloud(),
        _ << "Failed to get point cloud from exact geometry.");
    const uint64_t point_cloud_fingerprint =
        memoize::MemoizeFingerprint<ObjectRef<geo::PointCloud>>::Fingerprint(
            point_cloud_ref);
    return absl::StrCat(absl::Hex(point_cloud_fingerprint));
  }

  // This condition should be unreachable as ExactGeometry can only hold `Mesh`,
  // `PointCloud` or primitve shapes.
  return absl::InternalError(
      "ExactGeometry has an unsupported shape representation.");
}

absl::StatusOr<std::vector<EntityCollisionGeometryFeature>>
ExtractEntityCollisionGeometryFeatures(const World& entity_world,
                                       const EntityId entity_id) {
  if (!entity_world.HasEntity(entity_id)) {
    return absl::NotFoundError(absl::StrCat(
        "Entity with ID ", entity_id.value(), " does not exist in World."));
  }
  const absl::StatusOr<NamedGeometrySet> collision_geometries =
      entity_world.GetGeometryForEntity(entity_id, kKindCollisionGeometry);
  // Since `entity_id` exists in `entity_world`, `NotFound` indicates that the
  // entity either has no `GeometryComponent` or has no collision geometry
  // (`kKindCollisionGeometry`), both of which are expected for non-collidable
  // entities.
  if (absl::IsNotFound(collision_geometries.status())) {
    return std::vector<EntityCollisionGeometryFeature>{};
  }
  INTR_RETURN_IF_ERROR(collision_geometries.status())
      << "Failed to get collision geometry for entity ID: "
      << entity_id.value();

  std::vector<EntityCollisionGeometryFeature> features;
  features.reserve(collision_geometries->size());
  for (const auto& [geometry_name, transformed_geometry] :
       *collision_geometries) {
    INTR_ASSIGN_OR_RETURN(
        std::string fingerprint,
        ComputeCollisionGeometryFingerprint(transformed_geometry.shape()),
        _ << "Failed to generate fingerprint for collision geometry: "
          << geometry_name);
    features.push_back(EntityCollisionGeometryFeature{
        .geometry_fingerprint = std::move(fingerprint),
        .ref_t_shape = transformed_geometry.ref_t_shape(),
    });
  }
  return features;
}

absl::StatusOr<std::string> GetNormalizedObjectToken(
    const object_world::WorldObject& object) {
  // `KinematicObject` instances retain their object names
  // (e.g. "iris622" vs "iris692") since they are never ephemeral objects.
  if (IsKinematicObject(object)) {
    return object.GetName().value();
  }

  INTR_ASSIGN_OR_RETURN(
      const std::optional<std::string> geometry_token,
      ComputeCollisionGeometryToken(object),
      _ << "Failed to compute collision geometry token for object: "
        << object.GetName().value());
  if (geometry_token.has_value()) {
    return *geometry_token;
  }

  // Final fallback: use the object name.
  return object.GetName().value();
}

absl::StatusOr<std::string> GetNormalizedFullPath(
    const object_world::WorldObject& object) {
  if (object.GetId() == RootObjectId()) {
    return std::string(kRootObjectName);
  }

  std::vector<std::string> path_tokens;
  const object_world::WorldObject* current_object = &object;

  // Walk the parent chain up to the root and collect each ancestor's token.
  while (current_object != nullptr &&
         current_object->GetId() != RootObjectId()) {
    INTR_ASSIGN_OR_RETURN(std::string object_token,
                          GetNormalizedObjectToken(*current_object));
    path_tokens.push_back(std::move(object_token));
    current_object = current_object->GetParent();
  }

  std::reverse(path_tokens.begin(), path_tokens.end());
  return absl::StrJoin(path_tokens, kPathDelimiter);
}

absl::StatusOr<std::string> GetNormalizedTransformNodeName(
    const object_world::ObjectWorld& object_world,
    const object_world::TransformNode& node) {
  TransformNodeReference reference;
  INTR_RETURN_IF_ERROR(
      SetCanonicalByNameForTransformNode(object_world, node, reference));
  if (reference.by_name().has_frame()) {
    return absl::StrCat(reference.by_name().frame().object_name(),
                        kPathDelimiter,
                        reference.by_name().frame().frame_name());
  }
  return reference.by_name().object().object_name();
}

absl::Status NormalizeCollisionSettings(
    const object_world::ObjectWorld& object_world,
    CollisionSettings& collision_settings) {
  for (CollisionSettings::CollisionRule& rule :
       *collision_settings.mutable_collision_rules()) {
    INTR_RETURN_IF_ERROR(
        NormalizeAndSortReferenceList(object_world, *rule.mutable_left()));
    INTR_RETURN_IF_ERROR(
        NormalizeAndSortReferenceList(object_world, *rule.mutable_right()));

    // Since collision rules are symmetric between `left` and `right`, put any
    // empty side (which acts as a wildcard matching all objects) on `right`,
    // and otherwise order `left` and `right` lexicographically.
    const bool wildcard_on_left = rule.left().empty() && !rule.right().empty();
    const bool both_non_empty = !rule.left().empty() && !rule.right().empty();
    const bool right_precedes_left =
        both_non_empty && absl::c_lexicographical_compare(
                              rule.right(), rule.left(), CompareReferences);
    if (wildcard_on_left || right_precedes_left) {
      rule.mutable_left()->Swap(rule.mutable_right());
    }
  }

  SortCollisionRules(collision_settings);
  DeduplicateSortedRepeatedField(*collision_settings.mutable_collision_rules());
  return absl::OkStatus();
}

absl::StatusOr<MotionPlanningCacheGroupSignature>
CreateMotionPlanningCacheGroupSignature(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::MotionPlanningRequest&
        request) {
  MotionPlanningCacheGroupSignature signature{
      .normalized_motion_specification = request.motion_specification(),
      .normalized_robot_specification = request.robot_specification(),
  };
  INTR_RETURN_IF_ERROR(NormalizeMotionSpecification(
      object_world, signature.normalized_motion_specification));
  INTR_RETURN_IF_ERROR(NormalizeRobotSpecification(
      object_world, signature.normalized_robot_specification));
  return signature;
}

}  // namespace intrinsic
