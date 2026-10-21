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

#include "intrinsic/motion_planning/service/cache/motion_planner_cache.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "google/protobuf/map.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/logging/data_logger_client.h"
#include "intrinsic/logging/proto/log_item.pb.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/motion_planning/motion_planner/robot_specification.h"
#include "intrinsic/motion_planning/path_planning/planners/validation.h"
#include "intrinsic/motion_planning/proto/motion_planner_service_proto_utils.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/proto/v1/robot_specification.pb.h"
#include "intrinsic/motion_planning/service/cache/cache_entry_features.h"
#include "intrinsic/motion_planning/service/cache/cache_key_normalization.h"
#include "intrinsic/motion_planning/service/cache/motion_planner_cache.pb.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/proto/pb_hash.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/proto/collision_settings.pb.h"

namespace intrinsic {

static double kMaxDiffRobotAttachmentInM = 1e-6;

namespace {

// Compute the distance between two joint limit vectors. Returns max double if
// the limit vectors have different sizes or one component is unlimited and the
// other is limited. This is almost the same as
// `ComputeJointConfigurationDistance()`, except it handles the case of values =
// +/- inf. Note that this should never return inf, even if inputs have inf.
double ComputeDistanceForJointLimitVec(
    const eigenmath::VectorXd& first_limit_vec,
    const eigenmath::VectorXd& second_limit_vec) {
  double max_diff_for_limit_vec = 0.0;
  if (first_limit_vec.size() != second_limit_vec.size()) {
    return std::numeric_limits<double>::max();
  }
  if (first_limit_vec.size() == 0) {
    return 0.0;
  }
  for (int ii = 0; ii < first_limit_vec.size(); ++ii) {
    if (first_limit_vec(ii) == second_limit_vec(ii)) {
      continue;
    }
    if (std::isinf(first_limit_vec(ii)) || std::isinf(second_limit_vec(ii))) {
      return std::numeric_limits<double>::max();
    }
    max_diff_for_limit_vec =
        std::max(max_diff_for_limit_vec,
                 std::abs(first_limit_vec(ii) - second_limit_vec(ii)));
  }
  return max_diff_for_limit_vec;
}

// Compute the distance between two sets of joint limits.
double ComputeDistanceForJointLimits(const JointLimitsXd& first_joint_limits,
                                     const JointLimitsXd& second_joint_limits) {
  double max_diff_in_joint_limits = 0.0;
  if (first_joint_limits.size() != second_joint_limits.size()) {
    return std::numeric_limits<double>::max();
  }

  max_diff_in_joint_limits = std::max(
      max_diff_in_joint_limits,
      ComputeDistanceForJointLimitVec(first_joint_limits.min_position,
                                      second_joint_limits.min_position));

  max_diff_in_joint_limits = std::max(
      max_diff_in_joint_limits,
      ComputeDistanceForJointLimitVec(first_joint_limits.max_position,
                                      second_joint_limits.max_position));

  max_diff_in_joint_limits = std::max(
      max_diff_in_joint_limits,
      ComputeDistanceForJointLimitVec(first_joint_limits.max_velocity,
                                      second_joint_limits.max_velocity));

  max_diff_in_joint_limits = std::max(
      max_diff_in_joint_limits,
      ComputeDistanceForJointLimitVec(first_joint_limits.max_acceleration,
                                      second_joint_limits.max_acceleration));

  max_diff_in_joint_limits =
      std::max(max_diff_in_joint_limits,
               ComputeDistanceForJointLimitVec(first_joint_limits.max_jerk,
                                               second_joint_limits.max_jerk));

  max_diff_in_joint_limits =
      std::max(max_diff_in_joint_limits,
               ComputeDistanceForJointLimitVec(first_joint_limits.max_torque,
                                               second_joint_limits.max_torque));

  return max_diff_in_joint_limits;
}

// Computes a `MotionPlanningRequestCacheKeyDistance` comparing the query
// request features (`query_features`) against the cached entry features
// (`cached_features`), using `rotation_weight` to scale angular pose
// differences (in radians) into meters.
absl::StatusOr<MotionPlanningRequestCacheKeyDistance>
ComputeCacheEntryFeaturesDistance(
    const MotionPlanningCacheEntryFeatures& query_features,
    const MotionPlanningCacheEntryFeatures& cached_features,
    const double rotation_weight) {
  const intrinsic::pb_equals pb_equals{};

  INTR_ASSIGN_OR_RETURN(
      const double diff_in_m_for_all_related_frame_poses,
      ComputeRelatedFramePosesDistance(
          query_features.poses_of_all_related_frames,
          cached_features.poses_of_all_related_frames, rotation_weight));

  const ObjectPosesDistanceResult object_poses_distance =
      ComputeObjectPosesDistance(query_features.poses_of_all_objects,
                                 cached_features.poses_of_all_objects,
                                 rotation_weight);

  const AttachmentPosesDistanceResult robot_links_distance =
      ComputeAttachmentPosesDistance(query_features.robot_links,
                                     cached_features.robot_links,
                                     rotation_weight);

  const AttachmentPosesDistanceResult tool_links_distance =
      ComputeAttachmentPosesDistance(query_features.tool_links,
                                     cached_features.tool_links,
                                     rotation_weight);

  const KinematicActorsDistanceResult kinematic_actors_distance =
      ComputeKinematicActorsDistance(query_features.other_kinematic_actors,
                                     cached_features.other_kinematic_actors);

  const int total_num_objects_new_in_one_key =
      object_poses_distance.num_objects_new_in_one_key +
      kinematic_actors_distance.num_actors_new_in_one_key;

  const double max_diff_in_rad_for_starting_robot_configuration =
      ComputeJointConfigurationDistance(
          query_features.starting_robot_configuration,
          cached_features.starting_robot_configuration);

  const double max_diff_in_world_application_limits =
      ComputeDistanceForJointLimits(query_features.world_application_limits,
                                    cached_features.world_application_limits);

  const bool motion_segment_collision_settings_are_same = absl::c_equal(
      query_features.motion_segment_collision_settings,
      cached_features.motion_segment_collision_settings, pb_equals);

  return MotionPlanningRequestCacheKeyDistance{
      .diff_in_m_for_all_related_frame_poses =
          diff_in_m_for_all_related_frame_poses,
      .diff_in_m_for_all_object_poses =
          object_poses_distance.max_pose_diff_in_meters,
      .diff_in_m_for_robot_attachment_components =
          robot_links_distance.max_pose_diff_in_meters,
      .diff_in_m_for_robot_children_attachment_components =
          tool_links_distance.max_pose_diff_in_meters,
      .max_diff_in_rad_for_starting_robot_configuration =
          max_diff_in_rad_for_starting_robot_configuration,
      .max_diff_in_rad_for_kinematic_objects =
          kinematic_actors_distance.max_joint_diff,
      .max_diff_in_world_application_limits =
          max_diff_in_world_application_limits,
      .num_of_objects_new_in_one_key = total_num_objects_new_in_one_key,
      .world_collision_settings_are_same =
          pb_equals(query_features.world_collision_settings,
                    cached_features.world_collision_settings),
      .motion_segment_collision_settings_are_same =
          motion_segment_collision_settings_are_same,
      .robot_links_are_same = robot_links_distance.attachment_links_are_same,
      .tool_links_are_same = tool_links_distance.attachment_links_are_same,
      .geometry_fingerprints_are_same = query_features.geometry_fingerprints ==
                                        cached_features.geometry_fingerprints,
      .geometry_ref_t_shape_aff_are_same =
          query_features.serialized_geometry_ref_t_shape_aff ==
          cached_features.serialized_geometry_ref_t_shape_aff,

      .motion_segment_events_are_same = true,

  };
}

// Replaces `proto_links` with serialized `CacheEntryAttachmentPose` protos from
// `links`.
void AttachmentPosesToProto(
    absl::Span<const CacheEntryAttachmentPose> links,
    google::protobuf::RepeatedPtrField<
        intrinsic_proto::motion_planning::CacheEntryAttachmentPose>&
        proto_links) {
  proto_links.Clear();
  proto_links.Reserve(links.size());
  for (const CacheEntryAttachmentPose& link : links) {
    intrinsic_proto::motion_planning::CacheEntryAttachmentPose* const
        proto_link = proto_links.Add();
    proto_link->set_normalized_name(link.normalized_name);
    proto_link->set_normalized_parent_name(link.normalized_parent_name);
    *proto_link->mutable_parent_t_child() =
        intrinsic::ToProto(link.parent_t_child);
  }
}

// Deserializes sorted `CacheEntryFramePose` entries from `key_proto`, falling
// back to the deprecated `poses_of_all_related_frames` map when empty.
absl::StatusOr<std::vector<CacheEntryFramePose>> RelatedFramePosesFromProto(
    const intrinsic_proto::motion_planning::MotionPlanningRequestCacheKey&
        key_proto) {
  std::vector<CacheEntryFramePose> related_frame_poses;
  if (!key_proto.related_frame_poses().empty()) {
    related_frame_poses.reserve(key_proto.related_frame_poses_size());
    for (const intrinsic_proto::motion_planning::CacheEntryFramePose&
             proto_pose : key_proto.related_frame_poses()) {
      INTR_ASSIGN_OR_RETURN(const Pose3d pose,
                            intrinsic_proto::FromProto(proto_pose.pose()),
                            _ << "Failed to deserialize related frame pose.");
      related_frame_poses.push_back(CacheEntryFramePose{
          .normalized_frame_name = proto_pose.normalized_frame_name(),
          .pose = pose,
      });
    }
    return related_frame_poses;
  }

  // Fall back to deprecated map fields and sort deterministically.
  related_frame_poses.reserve(key_proto.poses_of_all_related_frames_size());
  for (const auto& [frame_name, pose_proto] :
       key_proto.poses_of_all_related_frames()) {
    INTR_ASSIGN_OR_RETURN(
        const Pose3d pose, intrinsic_proto::FromProto(pose_proto),
        _ << "Failed to deserialize legacy related frame pose.");
    related_frame_poses.push_back(CacheEntryFramePose{
        .normalized_frame_name = frame_name,
        .pose = pose,
    });
  }
  absl::c_sort(related_frame_poses, IsFramePoseKeyLessThan);
  return related_frame_poses;
}

// Deserializes sorted `CacheEntryObjectPose` entries from `key_proto`, falling
// back to the deprecated `poses_of_all_objects` map when empty.
absl::StatusOr<std::vector<CacheEntryObjectPose>> ObjectPosesFromProto(
    const intrinsic_proto::motion_planning::MotionPlanningRequestCacheKey&
        key_proto) {
  std::vector<CacheEntryObjectPose> object_poses;
  if (!key_proto.object_poses().empty()) {
    object_poses.reserve(key_proto.object_poses_size());
    for (const intrinsic_proto::motion_planning::CacheEntryObjectPose&
             proto_pose : key_proto.object_poses()) {
      INTR_ASSIGN_OR_RETURN(const Pose3d pose,
                            intrinsic_proto::FromProto(proto_pose.pose()),
                            _ << "Failed to deserialize object pose.");
      object_poses.push_back(CacheEntryObjectPose{
          .normalized_object_name = proto_pose.normalized_object_name(),
          .pose = pose,
      });
    }
    return object_poses;
  }

  // Fall back to deprecated map fields and sort deterministically.
  object_poses.reserve(key_proto.poses_of_all_objects_size());
  for (const auto& [object_name, pose_proto] :
       key_proto.poses_of_all_objects()) {
    INTR_ASSIGN_OR_RETURN(const Pose3d pose,
                          intrinsic_proto::FromProto(pose_proto),
                          _ << "Failed to deserialize legacy object pose.");
    object_poses.push_back(CacheEntryObjectPose{
        .normalized_object_name = object_name,
        .pose = pose,
    });
  }
  absl::c_sort(object_poses, [](const CacheEntryObjectPose& first_object,
                                const CacheEntryObjectPose& second_object) {
    return first_object.normalized_object_name <
           second_object.normalized_object_name;
  });
  return object_poses;
}

// Deserializes sorted `CacheEntryAttachmentPose` entries from `proto_links`,
// falling back to `legacy_poses_map` and `legacy_child_to_parent_map` when
// `proto_links` is empty.
absl::StatusOr<std::vector<CacheEntryAttachmentPose>> AttachmentPosesFromProto(
    const google::protobuf::RepeatedPtrField<
        intrinsic_proto::motion_planning::CacheEntryAttachmentPose>&
        proto_links,
    const google::protobuf::Map<uint32_t, intrinsic_proto::Pose>&
        legacy_poses_map,
    const google::protobuf::Map<uint32_t, uint32_t>&
        legacy_child_to_parent_map) {
  std::vector<CacheEntryAttachmentPose> attachment_links;
  if (!proto_links.empty()) {
    attachment_links.reserve(proto_links.size());
    for (const intrinsic_proto::motion_planning::CacheEntryAttachmentPose&
             proto_link : proto_links) {
      INTR_ASSIGN_OR_RETURN(
          const Pose3d pose,
          intrinsic_proto::FromProto(proto_link.parent_t_child()),
          _ << "Failed to deserialize attachment parent_t_child pose.");
      attachment_links.push_back(CacheEntryAttachmentPose{
          .normalized_name = proto_link.normalized_name(),
          .normalized_parent_name = proto_link.normalized_parent_name(),
          .parent_t_child = pose,
      });
    }
    return attachment_links;
  }

  // Fall back to deprecated map fields and sort deterministically.
  attachment_links.reserve(legacy_poses_map.size());
  for (const auto& [child_id, pose_proto] : legacy_poses_map) {
    INTR_ASSIGN_OR_RETURN(const Pose3d pose,
                          intrinsic_proto::FromProto(pose_proto),
                          _ << "Failed to deserialize legacy attachment pose.");
    const auto parent_it = legacy_child_to_parent_map.find(child_id);
    const bool has_parent = parent_it != legacy_child_to_parent_map.end();
    attachment_links.push_back(CacheEntryAttachmentPose{
        .normalized_name = absl::StrCat(child_id),
        .normalized_parent_name =
            has_parent ? absl::StrCat(parent_it->second) : "",
        .parent_t_child = pose,
    });
  }
  absl::c_sort(attachment_links, IsAttachmentLinkKeyLessThan);
  return attachment_links;
}

// Deserializes sorted `CacheEntryKinematicActor` entries from `key_proto`,
// falling back to the deprecated `other_kinematic_object_configs` map when
// empty.
std::vector<CacheEntryKinematicActor> KinematicActorsFromProto(
    const intrinsic_proto::motion_planning::MotionPlanningRequestCacheKey&
        key_proto) {
  std::vector<CacheEntryKinematicActor> other_kinematic_actors;
  if (!key_proto.other_kinematic_actors().empty()) {
    other_kinematic_actors.reserve(key_proto.other_kinematic_actors_size());
    for (const intrinsic_proto::motion_planning::CacheEntryKinematicActor&
             proto_actor : key_proto.other_kinematic_actors()) {
      other_kinematic_actors.push_back(CacheEntryKinematicActor{
          .normalized_name = proto_actor.normalized_name(),
          .joint_positions =
              RepeatedDoubleToVectorXd(proto_actor.joint_positions().joints()),
      });
    }
    return other_kinematic_actors;
  }

  // Fall back to deprecated map fields and sort deterministically.
  other_kinematic_actors.reserve(
      key_proto.other_kinematic_object_configs_size());
  for (const auto& [actor_name, config_proto] :
       key_proto.other_kinematic_object_configs()) {
    other_kinematic_actors.push_back(CacheEntryKinematicActor{
        .normalized_name = actor_name,
        .joint_positions = RepeatedDoubleToVectorXd(config_proto.joints()),
    });
  }
  absl::c_sort(
      other_kinematic_actors, [](const CacheEntryKinematicActor& first_actor,
                                 const CacheEntryKinematicActor& second_actor) {
        return first_actor.normalized_name < second_actor.normalized_name;
      });
  return other_kinematic_actors;
}

using CacheGroupEntries = PlanTrajectoryCache::CacheGroupEntries;

// Helper struct for searching for the matching cache entry.
struct CacheEntrySearchResult {
  CacheGroupEntries::iterator entry_it;
  MotionPlanningRequestCacheKeyDistance distance;
  bool is_exact_match = false;
};

// Finds the best matching entry in `entries` for `key` under `options`.
// Returns immediately with `is_exact_match = true` on the first exact hit, or
// returns the closest valid fuzzy hit (`is_exact_match = false`). Returns
// `absl::NotFoundError` if no entry in `entries` qualifies as an exact or
// fuzzy hit.
absl::StatusOr<CacheEntrySearchResult> FindClosestEntryInGroup(
    const MotionPlanningRequestCacheKey& key,
    const MotionPlanningRequestCacheKeyDistance::IsValidForCacheHitOptions&
        options,
    CacheGroupEntries& entries) {
  std::optional<MotionPlanningRequestCacheKeyDistance> min_distance;
  CacheGroupEntries::iterator closest_entry_it = entries.end();

  // Iterate through the cache entries and track the entry with the shortest
  // distance to the given `key`.
  for (CacheGroupEntries::iterator entry_it = entries.begin();
       entry_it != entries.end(); ++entry_it) {
    const PlanTrajectoryCache::CacheEntry& cached_entry = **entry_it;
    LOG(INFO) << "Checking distance between " << key.uuid << " and "
              << cached_entry.uuid;
    INTR_ASSIGN_OR_RETURN(
        const MotionPlanningRequestCacheKeyDistance entry_distance,
        ComputeCacheEntryFeaturesDistance(key.cache_entry_features,
                                          cached_entry.features,
                                          options.rotation_weight));

    // Check if within hit thresholds.
    if (entry_distance.IsValidForCacheHit(options)) {
      // This is an exact match. No need to look for more.
      return CacheEntrySearchResult{
          .entry_it = entry_it,
          .distance = entry_distance,
          .is_exact_match = true,
      };
    }
    if (!entry_distance.IsValidForFuzzyCacheHit(options)) {
      continue;
    }

    // This is a valid fuzzy hit, check if closer than the current closest hit.
    if (!min_distance.has_value() ||
        entry_distance.shorter_than(*min_distance)) {
      min_distance = entry_distance;
      closest_entry_it = entry_it;
    }
  }

  if (!min_distance.has_value()) {
    return absl::NotFoundError("No valid cache entry found.");
  }
  return CacheEntrySearchResult{
      .entry_it = closest_entry_it,
      .distance = *min_distance,
      .is_exact_match = false,
  };
}

}  // namespace

absl::StatusOr<MotionPlanningRequestCacheKeyDistance>
MotionPlanningRequestCacheKeyDistance::GetDistance(
    const MotionPlanningRequestCacheKey& first_key,
    const MotionPlanningRequestCacheKey& second_key,
    const double rotation_weight) {
  INTR_ASSIGN_OR_RETURN(MotionPlanningRequestCacheKeyDistance distance,
                        ComputeCacheEntryFeaturesDistance(
                            first_key.cache_entry_features,
                            second_key.cache_entry_features, rotation_weight));


  const intrinsic::pb_equals pb_equals{};
  // Motion events are expected to maintain their order.
  distance.motion_segment_events_are_same = absl::c_equal(
      first_key.group_signature.normalized_motion_specification
          .motion_segments(),
      second_key.group_signature.normalized_motion_specification
          .motion_segments(),
      [&pb_equals](const intrinsic_proto::motion_planning::v1::MotionSegment&
                       first_segment,
                   const intrinsic_proto::motion_planning::v1::MotionSegment&
                       second_segment) {
        return absl::c_equal(first_segment.motion_segment_events(),
                             second_segment.motion_segment_events(), pb_equals);
      });


  return distance;
}

bool MotionPlanningRequestCacheKeyDistance::shorter_than(
    const MotionPlanningRequestCacheKeyDistance& other) const {
  // First compare numerical values. Smaller values mean the distance is
  // shorter. The order of comparison is explicitly chosen to reduce the number
  // of comparison needed.
  // TODO(b/427746054): We should rank this according to the importance of the
  // attributes and potentially combine them into a single attribute.
  if (diff_in_m_for_robot_attachment_components !=
      other.diff_in_m_for_robot_attachment_components) {
    return diff_in_m_for_robot_attachment_components <
           other.diff_in_m_for_robot_attachment_components;
  }
  if (max_diff_in_rad_for_starting_robot_configuration !=
      other.max_diff_in_rad_for_starting_robot_configuration) {
    return max_diff_in_rad_for_starting_robot_configuration <
           other.max_diff_in_rad_for_starting_robot_configuration;
  }
  if (diff_in_m_for_robot_children_attachment_components !=
      other.diff_in_m_for_robot_children_attachment_components) {
    return diff_in_m_for_robot_children_attachment_components <
           other.diff_in_m_for_robot_children_attachment_components;
  }
  if (diff_in_m_for_all_related_frame_poses !=
      other.diff_in_m_for_all_related_frame_poses) {
    return diff_in_m_for_all_related_frame_poses <
           other.diff_in_m_for_all_related_frame_poses;
  }
  if (diff_in_m_for_all_object_poses != other.diff_in_m_for_all_object_poses) {
    return diff_in_m_for_all_object_poses <
           other.diff_in_m_for_all_object_poses;
  }
  if (num_of_objects_new_in_one_key != other.num_of_objects_new_in_one_key) {
    return num_of_objects_new_in_one_key < other.num_of_objects_new_in_one_key;
  }
  if (max_diff_in_rad_for_kinematic_objects !=
      other.max_diff_in_rad_for_kinematic_objects) {
    return max_diff_in_rad_for_kinematic_objects <
           other.max_diff_in_rad_for_kinematic_objects;
  }
  if (max_diff_in_world_application_limits !=
      other.max_diff_in_world_application_limits) {
    return max_diff_in_world_application_limits <
           other.max_diff_in_world_application_limits;
  }

  // Then compare boolean values. True means the distance is shorter.
  if (world_collision_settings_are_same !=
      other.world_collision_settings_are_same) {
    return world_collision_settings_are_same;
  }
  if (motion_segment_collision_settings_are_same !=
      other.motion_segment_collision_settings_are_same) {
    return motion_segment_collision_settings_are_same;
  }
  if (robot_links_are_same != other.robot_links_are_same) {
    return robot_links_are_same;
  }
  if (tool_links_are_same != other.tool_links_are_same) {
    return tool_links_are_same;
  }
  if (geometry_fingerprints_are_same != other.geometry_fingerprints_are_same) {
    return geometry_fingerprints_are_same;
  }
  if (geometry_ref_t_shape_aff_are_same !=
      other.geometry_ref_t_shape_aff_are_same) {
    return geometry_ref_t_shape_aff_are_same;
  }

  if (motion_segment_events_are_same != other.motion_segment_events_are_same) {
    return motion_segment_events_are_same;
  }

  // If all values are identical, return False.
  return false;
}

absl::Status
MotionPlanningRequestCacheKeyDistance::IsValidForCacheHitOptions::Validate()
    const {
  if (diff_in_m_for_all_related_frame_poses_threshold <= 0.0) {
    return absl::InvalidArgumentError(
        "diff_in_m_for_all_related_frame_poses_threshold must be positive.");
  }
  if (diff_in_m_for_all_object_poses_threshold <= 0.0) {
    return absl::InvalidArgumentError(
        "diff_in_m_for_all_object_poses_threshold must be positive.");
  }
  if (max_diff_in_rad_for_starting_robot_configuration_threshold <= 0.0) {
    return absl::InvalidArgumentError(
        "max_diff_in_rad_for_starting_robot_configuration_threshold must be "
        "positive.");
  }
  if (max_diff_in_rad_for_kinematic_objects_threshold <= 0.0) {
    return absl::InvalidArgumentError(
        "max_diff_in_rad_for_kinematic_objects_threshold must be positive.");
  }
  if (max_diff_in_world_application_limits_threshold < 0.0) {
    return absl::InvalidArgumentError(
        "max_diff_in_world_application_limits_threshold must be non-negative.");
  }
  return absl::OkStatus();
}

bool MotionPlanningRequestCacheKeyDistance::IsValidForFuzzyCacheHit(
    const IsValidForCacheHitOptions& options) const {
  if (!motion_segment_collision_settings_are_same) {
    LOG(INFO) << "motion_segment_collision_settings_are_same is false";
    return false;
  }
  if (diff_in_m_for_robot_attachment_components >= kMaxDiffRobotAttachmentInM) {
    LOG(INFO) << "diff_in_m_for_robot_attachment_components is "
              << diff_in_m_for_robot_attachment_components;
    return false;
  }
  if (!robot_links_are_same) {
    LOG(INFO) << "robot_links_are_same is false";
    return false;
  }
  if (max_diff_in_world_application_limits >
      options.max_diff_in_world_application_limits_threshold) {
    LOG(INFO) << "max_diff_in_world_application_limits is "
              << max_diff_in_world_application_limits;
    return false;
  }
  if (diff_in_m_for_all_related_frame_poses >=
      options.diff_in_m_for_all_related_frame_poses_threshold) {
    LOG(INFO) << "diff_in_m_for_all_related_frame_poses is "
              << diff_in_m_for_all_related_frame_poses;
    return false;
  }
  return true;
}

bool MotionPlanningRequestCacheKeyDistance::IsValidForCacheHit(
    const IsValidForCacheHitOptions& options) const {
  if (!world_collision_settings_are_same) {
    LOG(INFO) << "world_collision_settings_are_same is false";
    return false;
  }
  if (!motion_segment_collision_settings_are_same) {
    LOG(INFO) << "motion_segment_collision_settings_are_same is false";
    return false;
  }

  if (!motion_segment_events_are_same) {
    LOG(INFO) << "motion_segment_events_are_same is false";
    return false;
  }

  if (!tool_links_are_same) {
    LOG(INFO) << "tool_links_are_same is false";
    return false;
  }
  if (!robot_links_are_same) {
    LOG(INFO) << "robot_links_are_same is false";
    return false;
  }
  if (!geometry_fingerprints_are_same) {
    LOG(INFO) << "geometry_fingerprints_are_same is false";
    return false;
  }
  if (!geometry_ref_t_shape_aff_are_same) {
    LOG(INFO) << "geometry_ref_t_shape_aff_are_same is false";
    return false;
  }
  if (num_of_objects_new_in_one_key != 0) {
    LOG(INFO) << "num_of_objects_new_in_one_key is "
              << num_of_objects_new_in_one_key;
    return false;
  }
  if (diff_in_m_for_all_related_frame_poses >=
      options.diff_in_m_for_all_related_frame_poses_threshold) {
    LOG(INFO) << "diff_in_m_for_all_related_frame_poses is "
              << diff_in_m_for_all_related_frame_poses;
    return false;
  }
  if (diff_in_m_for_all_object_poses >=
      options.diff_in_m_for_all_object_poses_threshold) {
    LOG(INFO) << "diff_in_m_for_all_object_poses is "
              << diff_in_m_for_all_object_poses;
    return false;
  }
  if (diff_in_m_for_robot_attachment_components >= kMaxDiffRobotAttachmentInM) {
    LOG(INFO) << "diff_in_m_for_all_related_attachment_components is "
              << diff_in_m_for_robot_attachment_components;
    return false;
  }
  if (diff_in_m_for_robot_children_attachment_components >=
      options.diff_in_m_for_all_related_frame_poses_threshold) {
    LOG(INFO) << "diff_in_m_for_robot_children_attachment_components is "
              << diff_in_m_for_robot_children_attachment_components;
    return false;
  }
  if (max_diff_in_rad_for_starting_robot_configuration >=
      options.max_diff_in_rad_for_starting_robot_configuration_threshold) {
    LOG(INFO) << "max_diff_in_rad_for_starting_robot_configuration is "
              << max_diff_in_rad_for_starting_robot_configuration;
    return false;
  }
  if (max_diff_in_rad_for_kinematic_objects >=
      options.max_diff_in_rad_for_kinematic_objects_threshold) {
    LOG(INFO) << "max_diff_in_rad_for_kinematic_objects is "
              << max_diff_in_rad_for_kinematic_objects;
    return false;
  }
  if (max_diff_in_world_application_limits >
      options.max_diff_in_world_application_limits_threshold) {
    LOG(INFO) << "max_diff_in_world_application_limits is "
              << max_diff_in_world_application_limits;
    return false;
  }
  return true;
}

absl::StatusOr<MotionPlanningRequestCacheKey>
MotionPlanningRequestCacheKey::Create(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::MotionPlanningRequest& request
) {
  INTR_ASSIGN_OR_RETURN(
      MotionPlanningCacheGroupSignature group_signature,
      CreateMotionPlanningCacheGroupSignature(object_world, request),
      _ << "Failed to create motion planning cache group signature.");
  INTR_ASSIGN_OR_RETURN(
      MotionPlanningCacheEntryFeatures features,
      ExtractCacheEntryFeatures(object_world, request),
      _ << "Failed to extract motion planning cache entry features.");

  constexpr absl::string_view kDefaultCallerId = "Anonymous";
  const absl::string_view caller_id =
      request.has_caller_id() ? absl::string_view(request.caller_id())
                              : kDefaultCallerId;

  const size_t group_id = group_signature.ComputeGroupId();

  return MotionPlanningRequestCacheKey{
      .group_signature = std::move(group_signature),
      .group_id = group_id,
      .cache_entry_features = std::move(features),
      // clang-format off
       .uuid = std::string(caller_id),
      // clang-format on
  };
}

void MotionPlanningRequestCacheKey::PopulateProtoFromCacheEntryFeatures(
    const MotionPlanningCacheEntryFeatures& features,
    intrinsic_proto::motion_planning::MotionPlanningRequestCacheKey* const
        key_proto) {
  key_proto->clear_related_frame_poses();
  key_proto->mutable_related_frame_poses()->Reserve(
      features.poses_of_all_related_frames.size());
  for (const CacheEntryFramePose& frame_pose :
       features.poses_of_all_related_frames) {
    intrinsic_proto::motion_planning::CacheEntryFramePose* const proto_pose =
        key_proto->add_related_frame_poses();
    proto_pose->set_normalized_frame_name(frame_pose.normalized_frame_name);
    *proto_pose->mutable_pose() = intrinsic::ToProto(frame_pose.pose);
  }

  key_proto->clear_object_poses();
  key_proto->mutable_object_poses()->Reserve(
      features.poses_of_all_objects.size());
  for (const CacheEntryObjectPose& object_pose :
       features.poses_of_all_objects) {
    intrinsic_proto::motion_planning::CacheEntryObjectPose* const proto_pose =
        key_proto->add_object_poses();
    proto_pose->set_normalized_object_name(object_pose.normalized_object_name);
    *proto_pose->mutable_pose() = intrinsic::ToProto(object_pose.pose);
  }

  AttachmentPosesToProto(features.robot_links,
                         *key_proto->mutable_robot_links());
  AttachmentPosesToProto(features.tool_links, *key_proto->mutable_tool_links());

  key_proto->clear_other_kinematic_actors();
  key_proto->mutable_other_kinematic_actors()->Reserve(
      features.other_kinematic_actors.size());
  for (const CacheEntryKinematicActor& actor :
       features.other_kinematic_actors) {
    intrinsic_proto::motion_planning::CacheEntryKinematicActor* const
        proto_actor = key_proto->add_other_kinematic_actors();
    proto_actor->set_normalized_name(actor.normalized_name);
    VectorXdToRepeatedDouble(
        actor.joint_positions,
        proto_actor->mutable_joint_positions()->mutable_joints());
  }

  VectorXdToRepeatedDouble(
      features.starting_robot_configuration,
      key_proto->mutable_starting_robot_configuration()->mutable_joints());
  *key_proto->mutable_world_application_limits() =
      intrinsic::ToProto(features.world_application_limits);
  *key_proto->mutable_world_collision_settings() =
      features.world_collision_settings;
  *key_proto->mutable_motion_segment_collision_settings() = {
      features.motion_segment_collision_settings.begin(),
      features.motion_segment_collision_settings.end()};
  *key_proto->mutable_geometry_fingerprints() = {
      features.geometry_fingerprints.begin(),
      features.geometry_fingerprints.end()};
  *key_proto->mutable_serialized_geometry_ref_t_shape_aff() = {
      features.serialized_geometry_ref_t_shape_aff.begin(),
      features.serialized_geometry_ref_t_shape_aff.end()};
}

absl::StatusOr<MotionPlanningCacheEntryFeatures>
MotionPlanningRequestCacheKey::ExtractCacheEntryFeaturesFromProto(
    const intrinsic_proto::motion_planning::MotionPlanningRequestCacheKey&
        key_proto) {
  INTR_ASSIGN_OR_RETURN(std::vector<CacheEntryFramePose> related_frame_poses,
                        RelatedFramePosesFromProto(key_proto));
  INTR_ASSIGN_OR_RETURN(std::vector<CacheEntryObjectPose> object_poses,
                        ObjectPosesFromProto(key_proto));
  INTR_ASSIGN_OR_RETURN(std::vector<CacheEntryAttachmentPose> robot_links,
                        AttachmentPosesFromProto(
                            key_proto.robot_links(),
                            key_proto.poses_of_attachment_components_robot(),
                            key_proto.attachment_child_to_parent_ids_robot()));
  INTR_ASSIGN_OR_RETURN(
      std::vector<CacheEntryAttachmentPose> tool_links,
      AttachmentPosesFromProto(
          key_proto.tool_links(),
          key_proto.poses_of_attachment_components_robot_children_objects(),
          key_proto.attachment_child_to_parent_ids_robot_children_objects()));
  std::vector<CacheEntryKinematicActor> other_kinematic_actors =
      KinematicActorsFromProto(key_proto);

  INTR_ASSIGN_OR_RETURN(JointLimitsXd world_application_limits,
                        ToJointLimitsXd(key_proto.world_application_limits()),
                        _ << "Failed to deserialize world application limits.");

  return MotionPlanningCacheEntryFeatures{
      .poses_of_all_related_frames = std::move(related_frame_poses),
      .poses_of_all_objects = std::move(object_poses),
      .robot_links = std::move(robot_links),
      .tool_links = std::move(tool_links),
      .other_kinematic_actors = std::move(other_kinematic_actors),
      .geometry_fingerprints = {key_proto.geometry_fingerprints().begin(),
                                key_proto.geometry_fingerprints().end()},
      .serialized_geometry_ref_t_shape_aff =
          {key_proto.serialized_geometry_ref_t_shape_aff().begin(),
           key_proto.serialized_geometry_ref_t_shape_aff().end()},
      .starting_robot_configuration = RepeatedDoubleToVectorXd(
          key_proto.starting_robot_configuration().joints()),
      .world_application_limits = std::move(world_application_limits),
      .world_collision_settings = key_proto.world_collision_settings(),
      .motion_segment_collision_settings =
          {key_proto.motion_segment_collision_settings().begin(),
           key_proto.motion_segment_collision_settings().end()},
  };
}

intrinsic_proto::motion_planning::MotionPlanningRequestCacheKey
MotionPlanningRequestCacheKey::ToProto() const {
  intrinsic_proto::motion_planning::MotionPlanningRequestCacheKey key_proto;
  *key_proto.mutable_motion_specification() =
      group_signature.normalized_motion_specification;
  *key_proto.mutable_robot_specification() =
      group_signature.normalized_robot_specification;
  PopulateProtoFromCacheEntryFeatures(cache_entry_features, &key_proto);
  key_proto.set_uuid(uuid);
  return key_proto;
}

absl::StatusOr<MotionPlanningRequestCacheKey>
MotionPlanningRequestCacheKey::FromProto(
    const intrinsic_proto::motion_planning::MotionPlanningRequestCacheKey&
        key_proto) {
  INTR_ASSIGN_OR_RETURN(MotionPlanningCacheEntryFeatures cache_entry_features,
                        ExtractCacheEntryFeaturesFromProto(key_proto));
  MotionPlanningCacheGroupSignature group_signature{
      .normalized_motion_specification = key_proto.motion_specification(),
      .normalized_robot_specification = key_proto.robot_specification(),
  };
  const size_t group_id = group_signature.ComputeGroupId();
  return MotionPlanningRequestCacheKey{
      .group_signature = std::move(group_signature),
      .group_id = group_id,
      .cache_entry_features = std::move(cache_entry_features),
      .uuid = key_proto.uuid(),
  };
}

PlanTrajectoryCache::PlanTrajectoryCache(
    int max_num_of_groups, int max_num_of_entries_per_group,
    const MotionPlanningRequestCacheKeyDistance::IsValidForCacheHitOptions&
        distance_options)
    : max_num_of_entries_per_group_(max_num_of_entries_per_group),
      group_id_to_entries_(/*total_units=*/max_num_of_groups),
      distance_options_(distance_options) {}

absl::StatusOr<bool> PlanTrajectoryCache::LookupResult::HasValidTrajectory(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::RobotSpecification&
        robot_specification_proto,
    const intrinsic_proto::world::CollisionCheckerConfig&
        collision_checker_config,
    double max_diff_in_rad_for_starting_robot_configuration_threshold,
    bool allow_fuzzy_check, const double collision_check_spacing) {
  // If this lookup result is an exact match, we already have a valid
  // trajectory.
  if (exact_match) {
    LOG(INFO) << "Trajectory of an exact match is valid.";
    return true;
  }

  if (!allow_fuzzy_check) {
    LOG(INFO) << "Cannot find a valid trajectory with exact match.";
    return false;
  }

  if (distance.max_diff_in_rad_for_starting_robot_configuration >
      max_diff_in_rad_for_starting_robot_configuration_threshold) {
    LOG(INFO) << "Not a valid trajectory due to larger than expected "
                 "max_diff_in_rad_for_starting_robot_configuration. Actual: "
              << distance.max_diff_in_rad_for_starting_robot_configuration
              << " Expected: "
              << max_diff_in_rad_for_starting_robot_configuration_threshold;
    return false;
  }

  if (!distance.IsValidForFuzzyCacheHit({})) {
    return false;
  }

  // Unpack the robot information.
  INTR_ASSIGN_OR_RETURN(
      const RobotSpecification robot_specification,
      RobotSpecification::Create(object_world, robot_specification_proto));

  INTR_ASSIGN_OR_RETURN(
      const bool valid,
      CheckLimitsAndCollisionsForPathSegments(
          object_world, *robot_specification.robot, collision_checker_config,
          cached_entry.result.path_segments, collision_check_spacing));
  if (valid) {
    LOG(INFO) << "Found a valid trajectory through fuzzy match.";
  } else {
    LOG(INFO) << "Cannot find a valid trajectory with fuzzy match.";
  }
  return valid;
}

absl::StatusOr<std::unique_ptr<PlanTrajectoryCache>>
PlanTrajectoryCache::Create(
    int max_num_of_groups, int max_num_of_entries_per_group,
    const MotionPlanningRequestCacheKeyDistance::IsValidForCacheHitOptions&
        distance_options) {
  if (max_num_of_groups <= 0) {
    return absl::InvalidArgumentError("max_num_of_groups must be positive.");
  }
  if (max_num_of_entries_per_group <= 0) {
    return absl::InvalidArgumentError(
        "max_num_of_entries_per_group must be positive.");
  }
  INTR_RETURN_IF_ERROR(distance_options.Validate());
  return absl::WrapUnique(new PlanTrajectoryCache(
      max_num_of_groups, max_num_of_entries_per_group, distance_options));
}

void PlanTrajectoryCache::ClearCache() {
  absl::MutexLock lock(mutex_);
  group_id_to_entries_.clear();
}

size_t PlanTrajectoryCache::GetNumOfEntries() const {
  absl::MutexLock lock(mutex_);
  size_t num_entries = 0;
  for (const auto& [group_id, entries] : group_id_to_entries_) {
    num_entries += entries->size();
  }
  return num_entries;
}

std::vector<std::string> PlanTrajectoryCache::GetUUIDsOfGroup(size_t group_id) {
  std::vector<std::string> uuids;

  absl::MutexLock lock(mutex_);

  GroupCache::ScopedLookup lookup(&group_id_to_entries_, group_id);
  if (lookup.found()) {
    uuids.reserve(lookup.value()->size());
    for (const auto& entry : *lookup.value()) {
      uuids.push_back(entry->uuid);
    }
  }

  return uuids;
}

absl::Status PlanTrajectoryCache::Insert(std::unique_ptr<CacheEntry> entry) {
  const size_t group_id = entry->group_id;

  absl::MutexLock lock(mutex_);

  // Making this variable `optional` allows us to re-assign `lookup` if it's not
  // found at first. As long as `lookup` is in-scope and `lookup->found() ==
  // true`, it is guaranteed that the pointed-to memory will not be evicted from
  // the `group_id_to_entries_` cache.
  std::optional<GroupCache::ScopedLookup> lookup;
  lookup.emplace(&group_id_to_entries_, group_id);
  if (!lookup->found()) {
    group_id_to_entries_.insert(group_id, new CacheGroupEntries(),
                                /*units=*/1);
    lookup.emplace(&group_id_to_entries_, group_id);
    if (!lookup->found()) {
      return absl::InternalError("Failed to find newly created group");
    }
    LOG(INFO) << "Successfully created a new group " << group_id;
  }

  INTR_RET_CHECK(lookup.has_value());
  CacheGroupEntries& entries = *lookup->value();

  if (entries.size() >= max_num_of_entries_per_group_) {
    LOG(INFO) << "Group " << group_id << " is full. Removing the oldest entry.";
    entries.pop_back();
  }

  entries.push_front(std::move(entry));
  LOG(INFO) << "Successfully inserted a new entry to group " << group_id
            << ". Number of entries in the group: " << entries.size();

  return absl::OkStatus();
}

absl::StatusOr<PlanTrajectoryCache::LookupResult> PlanTrajectoryCache::Lookup(
    const MotionPlanningRequestCacheKey& key) {
  const size_t group_id = key.GetGroupId();

  absl::MutexLock lock(mutex_);

  GroupCache::ScopedLookup lookup(&group_id_to_entries_, group_id);
  if (!lookup.found()) {
    return absl::NotFoundError(
        absl::StrFormat("Cannot find group %u of the given key.", group_id));
  }
  CacheGroupEntries& group_entries = *lookup.value();

  INTR_ASSIGN_OR_RETURN(
      const CacheEntrySearchResult search_result,
      FindClosestEntryInGroup(key, distance_options_, group_entries));

  // Put the matched entry at the front of the queue to make this cache LRU
  // without reallocating or copying `CacheEntry`.
  const bool need_to_put_entry_at_front =
      group_entries.size() > 1 &&
      search_result.entry_it != group_entries.begin();
  if (need_to_put_entry_at_front) {
    std::unique_ptr<CacheEntry> entry = std::move(*search_result.entry_it);
    group_entries.erase(search_result.entry_it);
    group_entries.push_front(std::move(entry));
  }
  const CacheEntry& matched_entry = *group_entries.front();

  if (search_result.is_exact_match) {
    LOG(INFO) << "Return the exact cache entry " << matched_entry.uuid;
  } else {
    LOG(INFO) << "Return the closest cache entry " << matched_entry.uuid;
  }

  return PlanTrajectoryCache::LookupResult{
      .exact_match = search_result.is_exact_match,
      .given_cache_key_uuid = key.uuid,
      .cached_entry = matched_entry,
      .distance = search_result.distance};
}

}  // namespace intrinsic
