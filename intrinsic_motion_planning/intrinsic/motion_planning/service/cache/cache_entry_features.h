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

#ifndef INTRINSIC_MOTION_PLANNING_SERVICE_CACHE_CACHE_ENTRY_FEATURES_H_
#define INTRINSIC_MOTION_PLANNING_SERVICE_CACHE_CACHE_ENTRY_FEATURES_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/proto/collision_settings.pb.h"

namespace intrinsic {

// Entry describing the root-relative pose of a frame referenced in a motion
// specification segment.
struct CacheEntryFramePose {
  // Normalized hierarchical path or identifier of the referenced frame.
  std::string normalized_frame_name;
  // Pose of the frame relative to the world root.
  Pose3d pose;
};

// Returns true if `first` precedes `second` when ordered by
// `normalized_frame_name`.
inline bool IsFramePoseKeyLessThan(const CacheEntryFramePose& first,
                                   const CacheEntryFramePose& second) {
  return first.normalized_frame_name < second.normalized_frame_name;
}

// Entry describing the root-relative pose of a world object representing an
// instance of an object with the normalized path `normalized_object_name`.
struct CacheEntryObjectPose {
  // Normalized hierarchical path identifying the object class or collision
  // geometry within the scene hierarchy.
  std::string normalized_object_name;
  // Pose of the object relative to the world root.
  Pose3d pose;
};

// Result of comparing two sorted spans of `CacheEntryObjectPose`.
struct ObjectPosesDistanceResult {
  // Maximum directed Hausdorff pose distance (in meters, including weighted
  // angular distance) from `query_objects` to `cached_objects` across matched
  // object groups.
  double max_pose_diff_in_meters = 0.0;
  // Total symmetric difference in object instance counts across all object
  // groups between the two spans.
  int num_objects_new_in_one_key = 0;
};

// Entry describing an attachment relationship and its `parent_t_child` relative
// pose.
struct CacheEntryAttachmentPose {
  // Normalized identifier of the child entity or attached object link.
  std::string normalized_name;
  // Normalized identifier of the parent entity to which the link is attached.
  std::string normalized_parent_name;
  // Relative pose from the parent entity frame to the child entity frame.
  Pose3d parent_t_child;
};

// Returns true if `first` precedes `second` in the canonical `(normalized_name,
// normalized_parent_name)` ordering.
inline bool IsAttachmentLinkKeyLessThan(
    const CacheEntryAttachmentPose& first,
    const CacheEntryAttachmentPose& second) {
  if (first.normalized_name != second.normalized_name) {
    return first.normalized_name < second.normalized_name;
  }
  return first.normalized_parent_name < second.normalized_parent_name;
}

// Entry describing another kinematic actor in the scene (excluding the primary
// planning robot).
struct CacheEntryKinematicActor {
  // Normalized token identifying the kinematic actor.
  std::string normalized_name;
  // Joint configuration of the kinematic actor in radians or meters.
  eigenmath::VectorXd joint_positions;
};

// Holds separated attachment poses for the robot links and for its attached
// tools/workpieces.
struct AttachmentPoses {
  // Sorted attachment poses belonging to the primary planning robot.
  std::vector<CacheEntryAttachmentPose> robot_links;
  // Sorted attachment poses belonging to attached tools and workpieces.
  std::vector<CacheEntryAttachmentPose> tool_links;
};

// Result of comparing two sorted spans of `CacheEntryAttachmentPose`.
struct AttachmentPosesDistanceResult {
  // Maximum pose difference (in meters, including weighted angular distance)
  // across all matched attachment links.
  double max_pose_diff_in_meters = 0.0;
  // True if both spans contain the exact same sequence of `(normalized_name,
  // normalized_parent_name)` attachment pairs.
  bool attachment_links_are_same = true;
};

// Result of comparing two sorted spans of `CacheEntryKinematicActor`.
struct KinematicActorsDistanceResult {
  // Largest single-joint difference (L-inf) among matched actors (in radians
  // for revolute joints or in meters for prismatic joints), or
  // `std::numeric_limits<double>::max()` if a matched actor has a different
  // number of joints.
  double max_joint_diff = 0.0;
  // Total number of actors present in only one of the two spans.
  int num_actors_new_in_one_key = 0;
};

// Collects all continuous and discrete scene features of a motion planning
// cache entry used to compute distance and verify validity when queried against
// a new motion planning request.
struct MotionPlanningCacheEntryFeatures {
  // Sorted root-relative poses of frames referenced in the motion segments.
  std::vector<CacheEntryFramePose> poses_of_all_related_frames;
  // Sorted root-relative poses of scene objects (excluding the robot and its
  // attached descendants).
  std::vector<CacheEntryObjectPose> poses_of_all_objects;
  // Sorted relative attachment poses of the primary planning robot's links.
  std::vector<CacheEntryAttachmentPose> robot_links;
  // Sorted relative attachment poses of tools and workpieces attached to the
  // robot.
  std::vector<CacheEntryAttachmentPose> tool_links;
  // Sorted joint configurations of other kinematic actors in the scene.
  std::vector<CacheEntryKinematicActor> other_kinematic_actors;

  // Fingerprints of all collision geometry shapes in the scene.
  absl::flat_hash_set<std::string> geometry_fingerprints;
  // Serialized `ref_t_shape` transforms of all collision geometries in the
  // scene.
  absl::flat_hash_set<std::string> serialized_geometry_ref_t_shape_aff;

  // Starting joint configuration of the primary planning robot.
  eigenmath::VectorXd starting_robot_configuration;
  // Joint application limits of the primary planning robot in the world.
  JointLimitsXd world_application_limits;
  // Effective world collision settings (with `disable_collision_checking` set
  // if all segments disable collision checking).
  intrinsic_proto::world::CollisionSettings world_collision_settings;
  // Per-segment collision settings in motion segment order.
  std::vector<intrinsic_proto::world::CollisionSettings>
      motion_segment_collision_settings;
};

// Extracts the normalized frame names and root-relative poses of all transform
// nodes referenced across all segments of `motion_specification` in
// `object_world`. Returns the entries deterministically sorted and deduplicated
// by `normalized_frame_name`.
absl::StatusOr<std::vector<CacheEntryFramePose>> ExtractRelatedFramePoses(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::MotionSpecification&
        motion_specification);

// Extracts the normalized hierarchical object paths and root-relative poses of
// all objects in `object_world` (excluding the world root and any object IDs in
// `ignore_list`, such as the planning robot and its descendants). Returns the
// entries deterministically sorted by `normalized_object_name` and pose
// translation coordinates.
absl::StatusOr<std::vector<CacheEntryObjectPose>> ExtractObjectPoses(
    const object_world::ObjectWorld& object_world,
    const absl::flat_hash_set<ObjectWorldResourceId>& ignore_list);

// Compares two sorted spans of `CacheEntryFramePose` (`first_frames` and
// `second_frames`) using `rotation_weight` and returns the maximum pose
// distance across all matched frames. Returns `absl::NotFoundError` if any
// frame or segment index is present in one span but missing in the other as
// cache entries coming from matching cache groups should contain the same
// frames.
absl::StatusOr<double> ComputeRelatedFramePosesDistance(
    absl::Span<const CacheEntryFramePose> first_frames,
    absl::Span<const CacheEntryFramePose> second_frames,
    double rotation_weight);

// Compares two sorted spans of `CacheEntryObjectPose` (`query_objects` and
// `cached_objects`) grouped by `normalized_object_name`, computing the directed
// Hausdorff pose distance for each group and returing the maximum distance for
// every group. The directed Hausdorff distance is the maximum taken over all
// objects in a group of objects in `query_objects` of the minimum pose distance
// from any fixed object of the group to any object in the matching group within
// `cached_objects`, i.e.
//
// Hausdorff(O_q, O_c) =   max   (  min   (dist(obj_i, obj_j)) )
//                       i in q    j in c
//
// where `O_q` is the set of objects in a group in `query_objects` and `O_c` is
// the matching group of objects in `cached_objects`.
// The function also counts added or removed object instances between the
// groups.
ObjectPosesDistanceResult ComputeObjectPosesDistance(
    absl::Span<const CacheEntryObjectPose> query_objects,
    absl::Span<const CacheEntryObjectPose> cached_objects,
    double rotation_weight);

// Extracts the normalized attachment relationships and `parent_t_child`
// relative poses for the links of `robot` and for all attached tools/workpieces
// in `robot_offspring` from `object_world`. The returned vectors in
// `AttachmentPoses` are deterministically sorted by `(normalized_name,
// normalized_parent_name, parent_t_child)`.
absl::StatusOr<AttachmentPoses> ExtractAttachmentPoses(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    const absl::flat_hash_set<ObjectWorldResourceId>& robot_offspring);

// Extracts the normalized names and joint positions of all other kinematic
// actors in `object_world` (excluding `robot`). Returns the entries
// deterministically sorted by `(normalized_name, joint_positions)`.
absl::StatusOr<std::vector<CacheEntryKinematicActor>> ExtractKinematicActors(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot);

// Compares two sorted spans of `CacheEntryAttachmentPose` (`first_links` and
// `second_links`) using `rotation_weight` and returns the maximum pose
// difference across all matching links, setting
// `attachment_links_are_same = false` if any `(normalized_name,
// normalized_parent_name)` pair differs.
AttachmentPosesDistanceResult ComputeAttachmentPosesDistance(
    absl::Span<const CacheEntryAttachmentPose> first_links,
    absl::Span<const CacheEntryAttachmentPose> second_links,
    double rotation_weight);

// Returns the largest absolute joint difference  between `first_configuration`
// and `second_configuration` along any dimension, returning `0.0` if both
// configurations are empty or `std::numeric_limits<double>::max()` if their
// dimensions differ.
double ComputeJointConfigurationDistance(
    const eigenmath::VectorXd& first_configuration,
    const eigenmath::VectorXd& second_configuration);

// Compares two sorted spans of `CacheEntryKinematicActor` (`first_actors` and
// `second_actors`), returning the largest joint delta across matching actors
// and the count of unmatched actors.
KinematicActorsDistanceResult ComputeKinematicActorsDistance(
    absl::Span<const CacheEntryKinematicActor> first_actors,
    absl::Span<const CacheEntryKinematicActor> second_actors);

// Extracts all continuous and discrete scene features into a
// `MotionPlanningCacheEntryFeatures` struct from `object_world` for `request`.
absl::StatusOr<MotionPlanningCacheEntryFeatures> ExtractCacheEntryFeatures(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::MotionPlanningRequest& request);

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_SERVICE_CACHE_CACHE_ENTRY_FEATURES_H_
