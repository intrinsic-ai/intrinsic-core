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

#include "intrinsic/motion_planning/service/cache/cache_entry_features.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/affine_transform_of_geometry.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/motion_planning/proto/motion_planner_service_proto_utils.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/service/cache/cache_key_normalization.h"
#include "intrinsic/motion_planning/service/cache/cache_utils.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/util/make_collision_settings.h"
#include "intrinsic/world/component/attachment_component.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/component/kinematics_component.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/objects/frame.h"
#include "intrinsic/world/objects/frame_internal.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/object_world_proto_utils.h"
#include "intrinsic/world/objects/transform_node_internal.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"

namespace intrinsic {
namespace {

using ::intrinsic_proto::motion_planning::v1::MotionSegment;
using ::intrinsic_proto::motion_planning::v1::MotionSpecification;
using ::intrinsic_proto::world::TransformNodeReference;

// Consumes and returns the leading contiguous slice of `remaining_objects` that
// share the same `normalized_object_name`, advancing `remaining_objects` past
// that slice.
absl::Span<const CacheEntryObjectPose> ConsumeNextObjectGroup(
    absl::Span<const CacheEntryObjectPose>& remaining_objects) {
  if (remaining_objects.empty()) {
    return {};
  }
  const absl::string_view group_name =
      remaining_objects.front().normalized_object_name;
  size_t group_size = 1;
  while (group_size < remaining_objects.size() &&
         remaining_objects[group_size].normalized_object_name == group_name) {
    ++group_size;
  }
  const absl::Span<const CacheEntryObjectPose> group_slice =
      remaining_objects.subspan(0, group_size);
  remaining_objects.remove_prefix(group_size);
  return group_slice;
}

// Computes the weighted pose distance between `first_pose` and `second_pose`
// using `rotation_weight` to scale the angular distance and add it to the norm
// of translational distance.
double ComputePoseDistance(const Pose3d& first_pose, const Pose3d& second_pose,
                           const double rotation_weight) {
  const double translation_distance =
      (first_pose.translation() - second_pose.translation()).norm();
  const double angular_distance =
      first_pose.quaternion().angularDistance(second_pose.quaternion());
  return translation_distance + rotation_weight * angular_distance;
}

// Computes the directed Hausdorff pose distance from `source_objects` to
// `target_objects`: for each object in `source_objects`, finds its closest
// match in `target_objects` and returns the maximum of those minimum distances.
// The pose-pose distances are computed as the sum of the translational distance
// and the angular distance weighted by `rotation_weight`. The functionr returns
// 0.0 if `source_objects` is empty and `std::numeric_limits<double>::max()` if
// `target_objects` is empty when `source_objects` is non-empty.
double ComputeDirectedHausdorffDistance(
    absl::Span<const CacheEntryObjectPose> source_objects,
    absl::Span<const CacheEntryObjectPose> target_objects,
    const double rotation_weight) {
  double max_of_min_distances = 0.0;
  for (const CacheEntryObjectPose& source_object : source_objects) {
    double min_distance_to_target = std::numeric_limits<double>::max();
    for (const CacheEntryObjectPose& target_object : target_objects) {
      const double pose_distance = ComputePoseDistance(
          source_object.pose, target_object.pose, rotation_weight);
      min_distance_to_target = std::min(min_distance_to_target, pose_distance);
    }
    max_of_min_distances =
        std::max(max_of_min_distances, min_distance_to_target);
  }
  return max_of_min_distances;
}

// Returns the antipodally canonicalized quaternion coefficients `(w, x, y, z)`
// with `w >= 0.0` so that `quaternion` and `-quaternion` compare identically.
eigenmath::Vector4d GetCanonicalQuaternionCoefficients(
    const eigenmath::Quaterniond& quaternion) {
  const eigenmath::Vector4d coefficients(quaternion.w(), quaternion.x(),
                                         quaternion.y(), quaternion.z());
  return (quaternion.w() < 0.0) ? -coefficients : coefficients;
}

// Returns true if `first_pose` precedes `second_pose` in a deterministic
// lexicographical ordering over translation and antipodal-normalized
// quaternion components.
bool IsPoseDeterministicallyLessThan(const Pose3d& first_pose,
                                     const Pose3d& second_pose) {
  const eigenmath::Vector3d& first_translation = first_pose.translation();
  const eigenmath::Vector3d& second_translation = second_pose.translation();
  for (int index = 0; index < first_translation.size(); ++index) {
    if (first_translation[index] != second_translation[index]) {
      return first_translation[index] < second_translation[index];
    }
  }
  const eigenmath::Vector4d first_quaternion_coefficients =
      GetCanonicalQuaternionCoefficients(first_pose.quaternion());
  const eigenmath::Vector4d second_quaternion_coefficients =
      GetCanonicalQuaternionCoefficients(second_pose.quaternion());
  for (int index = 0; index < first_quaternion_coefficients.size(); ++index) {
    if (first_quaternion_coefficients[index] !=
        second_quaternion_coefficients[index]) {
      return first_quaternion_coefficients[index] <
             second_quaternion_coefficients[index];
    }
  }
  return false;
}

// Returns true if `first` and `second` share the same `(normalized_name,
// normalized_parent_name)` attachment key.
bool AreAttachmentLinkKeysEqual(const CacheEntryAttachmentPose& first,
                                const CacheEntryAttachmentPose& second) {
  return first.normalized_name == second.normalized_name &&
         first.normalized_parent_name == second.normalized_parent_name;
}

constexpr absl::string_view kPathDelimiter = "/";

// Formats a normalized name for `entity` given its `owning_object`. For
// entities belonging to a `KinematicObject` (such as a robot) or without an
// owner, returns `entity.GetLocalName()`. For entities belonging to non-robot
// `WorldObject`s (such as attached workpieces or tools), uses the
// geometry-based `GetNormalizedObjectToken()` so different instances of
// identical geometry produce identical attachment link names.
// Propagates any unexpected error from `GetNormalizedObjectToken()`.
absl::StatusOr<std::string> FormatNormalizedEntityName(
    const object_world::WorldObject* const owning_object,
    const WorldEntity& entity) {
  const bool is_unowned_or_kinematic_object =
      owning_object == nullptr || IsKinematicObject(*owning_object);
  if (is_unowned_or_kinematic_object) {
    return entity.GetLocalName();
  }
  INTR_ASSIGN_OR_RETURN(const std::string object_token,
                        GetNormalizedObjectToken(*owning_object));
  if (entity.GetLocalName() == owning_object->GetName().value()) {
    return object_token;
  }
  return absl::StrCat(object_token, kPathDelimiter, entity.GetLocalName());
}

// Extracts all attachment poses for the entities belonging to `owning_object`
// in `object_world` using `entity_to_owning_object` and appends them to
// `output_links`.
absl::Status ExtractObjectAttachmentPoses(
    const object_world::ObjectWorld& object_world,
    const object_world::WorldObject& owning_object,
    const absl::flat_hash_map<EntityId, const object_world::WorldObject*>&
        entity_to_owning_object,
    std::vector<CacheEntryAttachmentPose>& output_links) {
  for (const EntityId entity_id : owning_object.GetEntityIds()) {
    INTR_ASSIGN_OR_RETURN(
        const WorldEntity* const entity,
        object_world.GetEntityWorld().GetEntityById(entity_id));
    const absl::StatusOr<const AttachmentComponent*> status_or_attachment =
        entity->GetComponent<AttachmentComponent>();
    if (!status_or_attachment.ok() || status_or_attachment.value() == nullptr) {
      continue;
    }

    // Articulated links store their fixed parent-to-inboard transform on
    // `KinematicsComponent`, whereas static attachments store their relative
    // transform on `AttachmentComponent`.
    const absl::StatusOr<const KinematicsComponent*> status_or_kinematics =
        entity->GetComponent<KinematicsComponent>();
    const bool has_kinematics_component =
        status_or_kinematics.ok() && status_or_kinematics.value() != nullptr;
    const Pose3d parent_t_child =
        has_kinematics_component
            ? status_or_kinematics.value()->GetParentTInboard()
            : status_or_attachment.value()->GetParentTThis();

    const EntityId parent_id = status_or_attachment.value()->GetParentId();
    const absl::StatusOr<const WorldEntity*> parent_entity_status =
        object_world.GetEntityWorld().GetEntityById(parent_id);
    INTR_ASSIGN_OR_RETURN(
        std::string normalized_parent_name,
        [&]() -> absl::StatusOr<std::string> {
          const bool has_valid_parent_entity =
              parent_entity_status.ok() &&
              parent_entity_status.value() != nullptr;
          if (!has_valid_parent_entity) {
            return std::string();
          }
          const auto parent_owner_iterator =
              entity_to_owning_object.find(parent_id);
          const object_world::WorldObject* const parent_owner =
              (parent_owner_iterator != entity_to_owning_object.end())
                  ? parent_owner_iterator->second
                  : nullptr;
          return FormatNormalizedEntityName(parent_owner,
                                            *parent_entity_status.value());
        }());
    INTR_ASSIGN_OR_RETURN(std::string normalized_name,
                          FormatNormalizedEntityName(&owning_object, *entity));

    output_links.push_back(CacheEntryAttachmentPose{
        .normalized_name = std::move(normalized_name),
        .normalized_parent_name = std::move(normalized_parent_name),
        .parent_t_child = parent_t_child,
    });
  }
  return absl::OkStatus();
}

// Returns the set of all descendant object IDs in the kinematic subtree rooted
// at `parent_object` (excluding `parent_object` itself).
absl::flat_hash_set<ObjectWorldResourceId> GetAllOffspringObjectIDs(
    const object_world::WorldObject& parent_object) {
  absl::flat_hash_set<ObjectWorldResourceId> offspring_ids;
  std::deque<const object_world::WorldObject*> pending_parents{&parent_object};
  while (!pending_parents.empty()) {
    const object_world::WorldObject* const current_object =
        pending_parents.front();
    pending_parents.pop_front();
    for (const object_world::WorldObject* const child :
         current_object->GetChildren()) {
      if (child != nullptr && offspring_ids.insert(child->GetId()).second) {
        pending_parents.push_back(child);
      }
    }
  }
  return offspring_ids;
}

// Returns the explicit `start_configuration` from `robot_specification` if
// present, or queries `robot`'s current joint positions in the world.
absl::StatusOr<eigenmath::VectorXd> ExtractStartingRobotConfiguration(
    const intrinsic_proto::motion_planning::v1::RobotSpecification&
        robot_specification,
    const object_world::KinematicObject& robot) {
  if (robot_specification.has_start_configuration()) {
    return RepeatedDoubleToVectorXd(
        robot_specification.start_configuration().joints());
  }
  return robot.GetJointPositions();
}

// Struct storing the collision settings of a cache entry, contains both the
// world collision settings and the motion segment collision settings.
struct ExtractedCollisionSettings {
  intrinsic_proto::world::CollisionSettings world_collision_settings;
  std::vector<intrinsic_proto::world::CollisionSettings>
      motion_segment_collision_settings;
};

// Extracts and normalizes the world collision settings and per-segment
// collision settings for `request` in `object_world`.
absl::StatusOr<ExtractedCollisionSettings> ExtractCollisionSettings(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::MotionPlanningRequest&
        request) {
  ExtractedCollisionSettings result;
  const auto& segments = request.motion_specification().motion_segments();
  result.motion_segment_collision_settings.reserve(segments.size());
  const intrinsic_proto::world::CollisionSettings default_collision_settings;
  bool all_segments_disable_collision_checking = !segments.empty();
  for (const MotionSegment& segment : segments) {
    intrinsic_proto::world::CollisionSettings segment_settings =
        segment.has_collision_settings() ? segment.collision_settings()
                                         : default_collision_settings;
    INTR_RETURN_IF_ERROR(
        NormalizeCollisionSettings(object_world, segment_settings));
    result.motion_segment_collision_settings.push_back(
        std::move(segment_settings));
    all_segments_disable_collision_checking =
        all_segments_disable_collision_checking &&
        result.motion_segment_collision_settings.back()
            .disable_collision_checking();
  }

  INTR_ASSIGN_OR_RETURN(
      const intrinsic_proto::RuleSet rule_set,
      object_world.GetDefaultCollisionSettings(),
      _ << "Failed to get default collision settings from ObjectWorld.");
  result.world_collision_settings = MakeCollisionSettings(rule_set);
  if (all_segments_disable_collision_checking) {
    result.world_collision_settings.set_disable_collision_checking(true);
  }
  INTR_RETURN_IF_ERROR(NormalizeCollisionSettings(
      object_world, result.world_collision_settings));
  return result;
}

// A struct storing the geometry features of a cache entry, i.e. the
// fingerprints and local transforms applied to the geometry for all geometries
// present in the world.
struct ExtractedGeometryFeatures {
  absl::flat_hash_set<std::string> geometry_fingerprints;
  absl::flat_hash_set<std::string> serialized_geometry_ref_t_shape_aff;
};

// Extracts all collision geometry shape fingerprints and serialized
// `ref_t_shape` transforms across all entities in `object_world`.
absl::StatusOr<ExtractedGeometryFeatures> ExtractCollisionGeometryFeatures(
    const object_world::ObjectWorld& object_world) {
  ExtractedGeometryFeatures result;
  const World& world = object_world.GetEntityWorld();

  for (const EntityId entity_id :
       world.GetTypedEntityIds<GeometryComponentType>()) {
    INTR_ASSIGN_OR_RETURN(
        std::vector<EntityCollisionGeometryFeature> entity_features,
        ExtractEntityCollisionGeometryFeatures(world, entity_id),
        _ << "Failed to extract collision geometry features for entity: "
          << entity_id.value());
    for (EntityCollisionGeometryFeature& feature : entity_features) {
      result.geometry_fingerprints.insert(
          std::move(feature.geometry_fingerprint));
      const intrinsic_proto::Matrixd ref_t_shape_proto =
          ::intrinsic::ToProto(feature.ref_t_shape);
      result.serialized_geometry_ref_t_shape_aff.insert(
          ref_t_shape_proto.SerializeAsString());
    }
  }
  return result;
}

}  // namespace

absl::StatusOr<std::vector<CacheEntryFramePose>> ExtractRelatedFramePoses(
    const object_world::ObjectWorld& object_world,
    const MotionSpecification& motion_specification) {
  INTR_ASSIGN_OR_RETURN(const object_world::TransformNode* const root_node,
                        object_world.GetObject(RootObjectId()),
                        _ << "Failed to get root node from ObjectWorld.");
  std::vector<CacheEntryFramePose> related_frame_poses;

  for (const MotionSegment& segment : motion_specification.motion_segments()) {
    INTR_ASSIGN_OR_RETURN(
        const std::vector<const TransformNodeReference*> references,
        CollectTransformNodeReferencesFromMotionSegment(segment));

    for (const TransformNodeReference* const reference : references) {
      if (reference == nullptr ||
          reference->transform_node_reference_case() ==
              TransformNodeReference::TRANSFORM_NODE_REFERENCE_NOT_SET) {
        continue;
      }
      INTR_ASSIGN_OR_RETURN(
          const object_world::TransformNode* const node,
          object_world::GetTransformNodeByReference(object_world, *reference),
          _ << "Failed to resolve transform node reference: "
            << reference->ShortDebugString());
      if (node == nullptr) {
        return absl::NotFoundError(
            absl::StrCat("Cannot find transform node for reference: ",
                         reference->ShortDebugString()));
      }
      INTR_ASSIGN_OR_RETURN(const Pose3d root_to_node,
                            root_node->GetTransform(node),
                            _ << "Failed to get transform from root to node: "
                              << node->GetId().value());

      INTR_ASSIGN_OR_RETURN(
          std::string normalized_frame_name,
          GetNormalizedTransformNodeName(object_world, *node),
          _ << "Failed to normalize transform node name for node: "
            << node->GetId().value());
      related_frame_poses.push_back(CacheEntryFramePose{
          .normalized_frame_name = std::move(normalized_frame_name),
          .pose = root_to_node,
      });
    }
  }

  // Deterministically sort and deduplicate by `normalized_frame_name` as the
  // same frame can be referenced in multiple constraints or in multiple
  // segments.
  std::sort(related_frame_poses.begin(), related_frame_poses.end(),
            IsFramePoseKeyLessThan);
  related_frame_poses.erase(
      std::unique(related_frame_poses.begin(), related_frame_poses.end(),
                  [](const CacheEntryFramePose& first,
                     const CacheEntryFramePose& second) {
                    return first.normalized_frame_name ==
                           second.normalized_frame_name;
                  }),
      related_frame_poses.end());

  return related_frame_poses;
}

absl::StatusOr<std::vector<CacheEntryObjectPose>> ExtractObjectPoses(
    const object_world::ObjectWorld& object_world,
    const absl::flat_hash_set<ObjectWorldResourceId>& ignore_list) {
  INTR_ASSIGN_OR_RETURN(const object_world::TransformNode* const root_node,
                        object_world.GetObject(RootObjectId()),
                        _ << "Failed to get root node from ObjectWorld.");
  std::vector<CacheEntryObjectPose> object_poses;
  for (const object_world::WorldObject* const object :
       object_world.GetObjects()) {
    if (object == nullptr || ignore_list.contains(object->GetId()) ||
        object->GetParent() == nullptr || object->GetId() == RootObjectId()) {
      continue;
    }
    INTR_ASSIGN_OR_RETURN(const Pose3d root_to_object,
                          root_node->GetTransform(object),
                          _ << "Failed to get transform from root to object: "
                            << object->GetId().value());
    // Use `GetNormalizedFullPath()` as identifiers for the objects to only
    // group objects with the same transform parents for smaller groups and
    // ability to distinguish when an object has moved between frames.
    INTR_ASSIGN_OR_RETURN(std::string normalized_object_name,
                          GetNormalizedFullPath(*object));
    object_poses.push_back(CacheEntryObjectPose{
        .normalized_object_name = std::move(normalized_object_name),
        .pose = root_to_object,
    });
  }

  // Sort the objects by name first to group them. Contiguous ranges of equally
  // named objects are treated as groups in `ComputeRelatedFramePosesDistance()`
  // for which the directed Hausdorff distance is computed. The pose comparison
  // `IsPoseDeterministicallyLessThan()` in the sorting lambda is only used for
  // tie-breaking to ensure deterministic sorting.
  std::sort(
      object_poses.begin(), object_poses.end(),
      [](const CacheEntryObjectPose& first,
         const CacheEntryObjectPose& second) {
        if (first.normalized_object_name != second.normalized_object_name) {
          return first.normalized_object_name < second.normalized_object_name;
        }
        return IsPoseDeterministicallyLessThan(first.pose, second.pose);
      });
  return object_poses;
}

absl::StatusOr<double> ComputeRelatedFramePosesDistance(
    absl::Span<const CacheEntryFramePose> first_frames,
    absl::Span<const CacheEntryFramePose> second_frames,
    const double rotation_weight) {
  double max_pose_difference_in_meters = 0.0;
  auto first_iterator = first_frames.begin();
  auto second_iterator = second_frames.begin();

  // Traverse `first_frames` and `second_frames` in lockstep, returning an error
  // immediately if any `normalized_frame_name` is missing
  // from either sorted span.
  while (first_iterator != first_frames.end() ||
         second_iterator != second_frames.end()) {
    const bool first_has_unmatched_frame =
        second_iterator == second_frames.end() ||
        (first_iterator != first_frames.end() &&
         IsFramePoseKeyLessThan(*first_iterator, *second_iterator));
    if (first_has_unmatched_frame) {
      return absl::NotFoundError(
          absl::StrCat("First frame span contains frame '",
                       first_iterator->normalized_frame_name,
                       " not found in second frame span."));
    }

    const bool second_has_unmatched_frame =
        first_iterator == first_frames.end() ||
        IsFramePoseKeyLessThan(*second_iterator, *first_iterator);
    if (second_has_unmatched_frame) {
      return absl::NotFoundError(
          absl::StrCat("Second frame span contains frame '",
                       second_iterator->normalized_frame_name,
                       " not found in first frame span."));
    }

    // The frame is listed in both spans, compute the distance between them.
    const double frame_pose_difference = ComputePoseDistance(
        first_iterator->pose, second_iterator->pose, rotation_weight);
    max_pose_difference_in_meters =
        std::max(max_pose_difference_in_meters, frame_pose_difference);
    ++first_iterator;
    ++second_iterator;
  }

  return max_pose_difference_in_meters;
}

ObjectPosesDistanceResult ComputeObjectPosesDistance(
    absl::Span<const CacheEntryObjectPose> query_objects,
    absl::Span<const CacheEntryObjectPose> cached_objects,
    const double rotation_weight) {
  ObjectPosesDistanceResult result;
  absl::Span<const CacheEntryObjectPose> remaining_query = query_objects;
  absl::Span<const CacheEntryObjectPose> remaining_cached = cached_objects;

  // Traverse `remaining_query` and `remaining_cached` concurrently by object
  // group, counting unmatched object instances when a group name appears in
  // only one span and computing `ComputeDirectedHausdorffDistance()` from
  // `query_group` to `cached_group` when a common group slice is present in
  // both.
  while (!remaining_query.empty() || !remaining_cached.empty()) {
    const bool query_is_empty_or_greater =
        remaining_query.empty() ||
        (!remaining_cached.empty() &&
         remaining_cached.front().normalized_object_name <
             remaining_query.front().normalized_object_name);
    if (query_is_empty_or_greater) {
      const absl::Span<const CacheEntryObjectPose> unmatched_group =
          ConsumeNextObjectGroup(remaining_cached);
      result.num_objects_new_in_one_key +=
          static_cast<int>(unmatched_group.size());
      continue;
    }

    const bool cached_is_empty_or_greater =
        remaining_cached.empty() ||
        remaining_query.front().normalized_object_name <
            remaining_cached.front().normalized_object_name;
    if (cached_is_empty_or_greater) {
      const absl::Span<const CacheEntryObjectPose> unmatched_group =
          ConsumeNextObjectGroup(remaining_query);
      result.num_objects_new_in_one_key +=
          static_cast<int>(unmatched_group.size());
      continue;
    }

    // Get the spans of the matching groups from both queries.
    const absl::Span<const CacheEntryObjectPose> query_group =
        ConsumeNextObjectGroup(remaining_query);
    const absl::Span<const CacheEntryObjectPose> cached_group =
        ConsumeNextObjectGroup(remaining_cached);

    const int count_difference =
        std::abs(static_cast<int>(query_group.size()) -
                 static_cast<int>(cached_group.size()));
    result.num_objects_new_in_one_key += count_difference;

    // Compute the directed Hausdorff distance from `query_group` to
    // `cached_group`. Using the directed distance avoids penalizing
    // `query_group` when an object was removed relative to `cached_group`
    // (resulting in a distance of 0.0 if the remaining objects did not move),
    // while penalizing any extra object added in `query_group` by its distance
    // to the nearest object in `cached_group` (or infinite distance if no such
    // object exists).
    const double group_directed_hausdorff_distance =
        ComputeDirectedHausdorffDistance(query_group, cached_group,
                                         rotation_weight);
    result.max_pose_diff_in_meters = std::max(
        result.max_pose_diff_in_meters, group_directed_hausdorff_distance);
  }

  return result;
}

absl::StatusOr<AttachmentPoses> ExtractAttachmentPoses(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    const absl::flat_hash_set<ObjectWorldResourceId>& robot_offspring) {
  // Map every entity ID of `robot` and `robot_offspring` to its owning
  // `WorldObject` so parent links can be normalized consistently.
  absl::flat_hash_map<EntityId, const object_world::WorldObject*>
      entity_to_owning_object;
  for (const EntityId entity_id : robot.GetEntityIds()) {
    entity_to_owning_object.emplace(entity_id, &robot);
  }
  std::vector<const object_world::WorldObject*> offspring_objects;
  offspring_objects.reserve(robot_offspring.size());
  for (const ObjectWorldResourceId& object_id : robot_offspring) {
    INTR_ASSIGN_OR_RETURN(const object_world::WorldObject* const object,
                          object_world.GetObject(object_id));
    if (object == nullptr) {
      continue;
    }
    offspring_objects.push_back(object);
    for (const EntityId entity_id : object->GetEntityIds()) {
      entity_to_owning_object.emplace(entity_id, object);
    }
  }

  // Extract attachment poses separately for `robot` links and attached
  // tool/workpiece links.
  AttachmentPoses result;
  INTR_RETURN_IF_ERROR(ExtractObjectAttachmentPoses(
      object_world, robot, entity_to_owning_object, result.robot_links));
  for (const object_world::WorldObject* const object : offspring_objects) {
    INTR_RETURN_IF_ERROR(ExtractObjectAttachmentPoses(
        object_world, *object, entity_to_owning_object, result.tool_links));
  }

  // Deterministically sort both link lists by `(normalized_name,
  // normalized_parent_name, parent_t_child)`.
  const auto sort_predicate = [](const CacheEntryAttachmentPose& first,
                                 const CacheEntryAttachmentPose& second) {
    if (!AreAttachmentLinkKeysEqual(first, second)) {
      return IsAttachmentLinkKeyLessThan(first, second);
    }
    return IsPoseDeterministicallyLessThan(first.parent_t_child,
                                           second.parent_t_child);
  };
  std::sort(result.robot_links.begin(), result.robot_links.end(),
            sort_predicate);
  std::sort(result.tool_links.begin(), result.tool_links.end(), sort_predicate);

  return result;
}

absl::StatusOr<std::vector<CacheEntryKinematicActor>> ExtractKinematicActors(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot) {
  std::vector<CacheEntryKinematicActor> kinematic_actors;
  for (const object_world::WorldObject* const object :
       object_world.GetObjects()) {
    if (object == nullptr) {
      continue;
    }

    const bool object_is_kinematic = IsKinematicObject(*object);
    const bool object_is_robot = object->GetId() == robot.GetId();
    if (!object_is_kinematic || object_is_robot) {
      continue;
    }
    const object_world::KinematicObject& kinematic_object =
        *static_cast<const object_world::KinematicObject*>(object);

    INTR_ASSIGN_OR_RETURN(const eigenmath::VectorXd joint_positions,
                          kinematic_object.GetJointPositions());
    INTR_ASSIGN_OR_RETURN(std::string normalized_name,
                          GetNormalizedObjectToken(kinematic_object));
    kinematic_actors.push_back(CacheEntryKinematicActor{
        .normalized_name = std::move(normalized_name),
        .joint_positions = joint_positions,
    });
  }

  std::sort(kinematic_actors.begin(), kinematic_actors.end(),
            [](const CacheEntryKinematicActor& first,
               const CacheEntryKinematicActor& second) {
              if (first.normalized_name != second.normalized_name) {
                return first.normalized_name < second.normalized_name;
              }
              return std::lexicographical_compare(
                  first.joint_positions.begin(), first.joint_positions.end(),
                  second.joint_positions.begin(), second.joint_positions.end());
            });
  return kinematic_actors;
}

AttachmentPosesDistanceResult ComputeAttachmentPosesDistance(
    absl::Span<const CacheEntryAttachmentPose> first_links,
    absl::Span<const CacheEntryAttachmentPose> second_links,
    const double rotation_weight) {
  AttachmentPosesDistanceResult result;
  auto first_iterator = first_links.begin();
  auto second_iterator = second_links.begin();

  // Traverse `first_links` and `second_links` concurrently, flagging
  // `attachment_links_are_same = false` and advancing the smaller link key on
  // mismatches, or accumulating the maximum relative pose difference when link
  // keys match.
  while (first_iterator != first_links.end() &&
         second_iterator != second_links.end()) {
    if (!AreAttachmentLinkKeysEqual(*first_iterator, *second_iterator)) {
      result.attachment_links_are_same = false;
      if (IsAttachmentLinkKeyLessThan(*first_iterator, *second_iterator)) {
        ++first_iterator;
      } else {
        ++second_iterator;
      }
      continue;
    }

    const double link_pose_difference =
        ComputePoseDistance(first_iterator->parent_t_child,
                            second_iterator->parent_t_child, rotation_weight);
    result.max_pose_diff_in_meters =
        std::max(result.max_pose_diff_in_meters, link_pose_difference);
    ++first_iterator;
    ++second_iterator;
  }

  if (first_iterator != first_links.end() ||
      second_iterator != second_links.end()) {
    result.attachment_links_are_same = false;
  }

  return result;
}

double ComputeJointConfigurationDistance(
    const eigenmath::VectorXd& first_configuration,
    const eigenmath::VectorXd& second_configuration) {
  if (first_configuration.size() != second_configuration.size()) {
    return std::numeric_limits<double>::max();
  }
  if (first_configuration.size() == 0) {
    return 0.0;
  }
  return (first_configuration - second_configuration).cwiseAbs().maxCoeff();
}

KinematicActorsDistanceResult ComputeKinematicActorsDistance(
    absl::Span<const CacheEntryKinematicActor> first_actors,
    absl::Span<const CacheEntryKinematicActor> second_actors) {
  KinematicActorsDistanceResult result;
  auto first_iterator = first_actors.begin();
  auto second_iterator = second_actors.begin();

  // Traverse `first_actors` and `second_actors` concurrently by
  // `normalized_name`, counting actors present in only one span and computing
  // the maximum joint difference along any dimension when actors match.
  while (first_iterator != first_actors.end() &&
         second_iterator != second_actors.end()) {
    if (first_iterator->normalized_name < second_iterator->normalized_name) {
      ++result.num_actors_new_in_one_key;
      ++first_iterator;
      continue;
    }
    if (second_iterator->normalized_name < first_iterator->normalized_name) {
      ++result.num_actors_new_in_one_key;
      ++second_iterator;
      continue;
    }

    const double actor_joint_difference = ComputeJointConfigurationDistance(
        first_iterator->joint_positions, second_iterator->joint_positions);
    result.max_joint_diff =
        std::max(result.max_joint_diff, actor_joint_difference);
    ++first_iterator;
    ++second_iterator;
  }

  result.num_actors_new_in_one_key +=
      static_cast<int>(std::distance(first_iterator, first_actors.end()) +
                       std::distance(second_iterator, second_actors.end()));

  return result;
}

absl::StatusOr<MotionPlanningCacheEntryFeatures> ExtractCacheEntryFeatures(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::MotionPlanningRequest&
        request) {
  INTR_ASSIGN_OR_RETURN(ExtractedCollisionSettings collision_settings,
                        ExtractCollisionSettings(object_world, request));

  INTR_ASSIGN_OR_RETURN(
      const object_world::KinematicObject* const robot,
      GetRobot(request.robot_specification().robot_reference(), &object_world),
      _.LogError());
  INTR_ASSIGN_OR_RETURN(
      eigenmath::VectorXd starting_robot_configuration,
      ExtractStartingRobotConfiguration(request.robot_specification(), *robot),
      _ << "Failed to extract starting robot configuration.");

  INTR_ASSIGN_OR_RETURN(
      JointLimitsXd world_application_limits,
      robot->GetJointApplicationLimits(),
      _ << "Failed to get joint application limits for robot.");

  INTR_ASSIGN_OR_RETURN(
      std::vector<CacheEntryKinematicActor> other_kinematic_actors,
      ExtractKinematicActors(object_world, *robot));

  const absl::flat_hash_set<ObjectWorldResourceId> robot_offspring =
      GetAllOffspringObjectIDs(*robot);
  absl::flat_hash_set<ObjectWorldResourceId> robot_and_offspring =
      robot_offspring;
  robot_and_offspring.insert(robot->GetId());

  INTR_ASSIGN_OR_RETURN(
      std::vector<CacheEntryFramePose> poses_of_all_related_frames,
      ExtractRelatedFramePoses(object_world, request.motion_specification()));

  INTR_ASSIGN_OR_RETURN(std::vector<CacheEntryObjectPose> poses_of_all_objects,
                        ExtractObjectPoses(object_world, robot_and_offspring));

  INTR_ASSIGN_OR_RETURN(
      AttachmentPoses attachment_poses,
      ExtractAttachmentPoses(object_world, *robot, robot_offspring));

  INTR_ASSIGN_OR_RETURN(ExtractedGeometryFeatures geometry_features,
                        ExtractCollisionGeometryFeatures(object_world));

  return MotionPlanningCacheEntryFeatures{
      .poses_of_all_related_frames = std::move(poses_of_all_related_frames),
      .poses_of_all_objects = std::move(poses_of_all_objects),
      .robot_links = std::move(attachment_poses.robot_links),
      .tool_links = std::move(attachment_poses.tool_links),
      .other_kinematic_actors = std::move(other_kinematic_actors),
      .geometry_fingerprints =
          std::move(geometry_features.geometry_fingerprints),
      .serialized_geometry_ref_t_shape_aff =
          std::move(geometry_features.serialized_geometry_ref_t_shape_aff),
      .starting_robot_configuration = std::move(starting_robot_configuration),
      .world_application_limits = std::move(world_application_limits),
      .world_collision_settings =
          std::move(collision_settings.world_collision_settings),
      .motion_segment_collision_settings =
          std::move(collision_settings.motion_segment_collision_settings),
  };
}

}  // namespace intrinsic
