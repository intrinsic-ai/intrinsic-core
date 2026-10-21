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

#ifndef INTRINSIC_MOTION_PLANNING_SERVICE_CACHE_MOTION_PLANNER_CACHE_H_
#define INTRINSIC_MOTION_PLANNING_SERVICE_CACHE_MOTION_PLANNER_CACHE_H_

#include <cstddef>
#include <deque>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/robot_specification.pb.h"
#include "intrinsic/motion_planning/service/cache/cache_entry_features.h"
#include "intrinsic/motion_planning/service/cache/cache_key_normalization.h"
#include "intrinsic/motion_planning/service/cache/motion_planner_cache.pb.h"
#include "intrinsic/util/lru_cache.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/proto/collision_settings.pb.h"

namespace intrinsic {

static const auto* const distance_cache_format =
    new absl::ParsedFormat<'f', 'f', 'f', 'f', 'f', 'd', 'v', 'v', 'v', 'v',
                           'v'>(
        "MotionPlanningRequestCacheKeyDistance(diff_in_m_for_all_related_frame_"
        "poses = "
        "%.3f. diff_in_m_for_all_object_poses = %.3f."
        "max_diff_in_rad_for_starting_robot_configuration = %.3f. "
        "max_diff_in_rad_for_kinematic_objects = %.3f. "
        "max_diff_in_world_application_limits = %.3f. "
        "num_of_objects_new_in_one_key = %d. "
        "world_collision_settings_are_same = %v. "
        "motion_segment_collision_settings_are_same "
        "= "
        "%v. robot_links_are_same = %v. tool_links_are_same = %v. "
        "geometry_fingerprints_are_same = %v.)");

// The key type used for caching results from MotionPlanner::PlanTrajectory.
struct MotionPlanningRequestCacheKey {
  // Normalized input to the group ID hash computation, contains the normalized
  // motion specification and robot specification from the motion request.
  MotionPlanningCacheGroupSignature group_signature;
  // Precomputed group ID hash of `group_signature`. When `std::nullopt`
  // (e.g., in aggregate-initialized test keys), `GetGroupId()` falls back
  // to computing `group_signature.ComputeGroupId()`.
  std::optional<size_t> group_id;
  // The features of the key, describing the world and robot state at the time
  // of the request. Used to compute the similarity between this key
  // and the cache entries of the cache group matching the id of the key.
  MotionPlanningCacheEntryFeatures cache_entry_features;
  // UUID used for logging only.
  std::string uuid;

  // Create a `MotionPlanningRequestCacheKey` from the given set of input
  // arguments.
  static absl::StatusOr<MotionPlanningRequestCacheKey> Create(
      const object_world::ObjectWorld& object_world,
      const intrinsic_proto::motion_planning::v1::MotionPlanningRequest& request
  );

  intrinsic_proto::motion_planning::MotionPlanningRequestCacheKey ToProto()
      const;

  static absl::StatusOr<MotionPlanningRequestCacheKey> FromProto(
      const intrinsic_proto::motion_planning::MotionPlanningRequestCacheKey&
          key_proto);

  // Populates `key_proto` with serialized scene features from `features`.
  static void PopulateProtoFromCacheEntryFeatures(
      const MotionPlanningCacheEntryFeatures& features,
      intrinsic_proto::motion_planning::MotionPlanningRequestCacheKey*
          key_proto);

  // Deserializes `MotionPlanningCacheEntryFeatures` from `key_proto`, falling
  // back to deprecated map fields when the repeated feature fields are empty.
  static absl::StatusOr<MotionPlanningCacheEntryFeatures>
  ExtractCacheEntryFeaturesFromProto(
      const intrinsic_proto::motion_planning::MotionPlanningRequestCacheKey&
          key_proto);

  // Group ID of this `MotionPlanningRequestCacheKey`.
  size_t GetGroupId() const {
    return group_id.has_value() ? *group_id : group_signature.ComputeGroupId();
  }
};

// Distance between two `MotionPlanningRequestCacheKey` instances.
// Since a `MotionPlanningRequestCacheKey` is created from a world and a
// planning request. This distance class is used to represent how different the
// world+request used to create two `MotionPlanningRequestCacheKey` instances
// are. Each member attribute represents one aspect of planning related
// information.
struct MotionPlanningRequestCacheKeyDistance {
  // `poses_of_all_related_frames` have all references and poses of
  // FRAMES/OBJECTS REFERRED IN THE MOTION SPECIFICATION. Any change in their
  // poses may result in different planning results. So we want to keep track
  // how different these poses are in two `MotionPlanningRequestCacheKey`
  // instances. This attribute represents the sum of differences in
  // `poses_of_all_related_frames` between two `MotionPlanningRequestCacheKey`s.
  // To compare two `MotionPlanningRequestCacheKey` instances, they must already
  // have the same motion specification. Therefore, they will have the same map
  // keys in `poses_of_all_related_frames`. The difference between two poses are
  // calculated by `(pose1 * kOffsetPose - pose2 * kOffsetPose).norm()`, where
  // `kOffsetPose` is a fix vector.
  double diff_in_m_for_all_related_frame_poses;
  // `poses_of_all_objects` has all references and poses of all objects in the
  // world with the exception of those attached to the robot. Note this is
  // different from `poses_of_all_related_frames`. We only care about objects
  // here because frames do not affect planning result unless they are referred
  // in the motion specification. This attribute represents the sum of
  // differences in `poses_of_all_objects` between two
  // `MotionPlanningRequestCacheKey`s. Different from
  // `poses_of_all_related_frames`, the two keys may not have the same ids
  // in `poses_of_all_objects` since the two worlds might be different. So we
  // only compare objects exist in both worlds. The difference between two poses
  // are calculated by `(pose1 * kOffsetPose - pose2 * kOffsetPose).norm()`,
  // where `kOffsetPose` is a fix vector.
  double diff_in_m_for_all_object_poses;
  // The pose difference between the attachment components of the robot in the
  // two keys. This captures changes in the kinematic chain of the robot between
  // two executions. Note that this is different from
  // `diff_in_m_for_robot_children_attachment_components` which captures changes
  // in the kinematic chain of the children objects of the robot. Those two get
  // captured separately, as the robot and its children objects are treated
  // differently in the cache key. Changes in the robot kinematic always need
  // replanning, while changes in the children objects only require
  // collision checking of the existing trajectory.
  double diff_in_m_for_robot_attachment_components;
  // The pose difference between the attachment components of the children
  // objects of the robot. This captures changes in the kinematic chain of the
  // children objects of the robot between two executions.
  double diff_in_m_for_robot_children_attachment_components;
  // The max absolute joint difference in `starting_robot_configuration` of the
  // two keys.
  double max_diff_in_rad_for_starting_robot_configuration;
  // the max absolute joint difference in `other_kinematic_object_ids` of the
  // two keys.
  double max_diff_in_rad_for_kinematic_objects;
  // The max absolute difference in any of the joint limit values of the two
  // keys.
  double max_diff_in_world_application_limits;
  // The number of objects exists in only one key but not the other.
  int num_of_objects_new_in_one_key;
  // True if both keys have the same `world_collision_settings`.
  bool world_collision_settings_are_same;
  // True if both keys have the same `motion_segment_collision_settings`.
  bool motion_segment_collision_settings_are_same;
  // True if both feature sets have identical robot link `(normalized_name,
  // normalized_parent_name)` attachment pairs.
  bool robot_links_are_same;
  // True if both feature sets have identical attached tool/workpiece link
  // `(normalized_name, normalized_parent_name)` attachment pairs.
  bool tool_links_are_same;
  // True if both keys have the same `geometry_fingerprints`.
  bool geometry_fingerprints_are_same;
  // True if both keys have the same `geometry_ref_t_shape_aff`.
  bool geometry_ref_t_shape_aff_are_same;

  // True if all motion segment events are identical.
  bool motion_segment_events_are_same;


  // Return True if `this` distance is considered shorter than the `other`
  // distance. Two caches with a shorter distance is more likely to match each
  // other. Instead of overriding the < operator, we use a custom function. The
  // logic implemented in this function might be against intuition of a typical
  // < operator.
  bool shorter_than(const MotionPlanningRequestCacheKeyDistance& other) const;

  // Calculate a `MotionPlanningRequestCacheKeyDistance` between two keys.
  // The order of keys does not matter.
  // Return the absl::NotFoundError if there exists a frame in one key but not
  // the other.
  static absl::StatusOr<MotionPlanningRequestCacheKeyDistance> GetDistance(
      const MotionPlanningRequestCacheKey& first_key,
      const MotionPlanningRequestCacheKey& second_key,
      double rotation_weight = 1.0);

  struct IsValidForCacheHitOptions {
    const double diff_in_m_for_all_related_frame_poses_threshold = 0.001;
    const double diff_in_m_for_all_object_poses_threshold = 0.001;
    const double max_diff_in_rad_for_starting_robot_configuration_threshold =
        0.001;
    const double max_diff_in_rad_for_kinematic_objects_threshold = 0.001;
    const double max_diff_in_world_application_limits_threshold = 0.0;
    const double rotation_weight = 1.0;
    absl::Status Validate() const;
  };

  // Return true if this distance is considered valid for cache hit.
  // The distance is valid if all following are true:
  // * world_collision_settings_are_same
  // * motion_segment_collision_settings_are_same
  // * robot_links_are_same
  // * tool_links_are_same
  // * geometry_fingerprints_are_same
  // * geometry_ref_t_shape_aff_are_same

  // * motion_segment_events_are_same

  // * num_of_objects_new_in_one_key == 0
  // * diff_in_m_for_all_related_frame_poses <
  //    diff_in_m_for_all_related_frame_poses_threshold
  // * diff_in_m_for_all_object_poses < diff_in_m_for_all_object_poses_threshold
  // * max_diff_in_rad_for_starting_robot_configuration <
  //    max_diff_in_rad_for_starting_robot_configuration_threshold
  // * max_diff_in_world_application_limits <=
  //    max_diff_in_world_application_limits_threshold
  bool IsValidForCacheHit(const IsValidForCacheHitOptions& options) const;

  // Returns true if this distance qualifies as a fuzzy cache hit candidate
  // under `options`. Requires `motion_segment_collision_settings_are_same` and
  // `robot_links_are_same` to be true,
  // `diff_in_m_for_robot_attachment_components` to be within tolerance, and
  // `max_diff_in_world_application_limits` to be within
  // `options.max_diff_in_world_application_limits_threshold`.
  bool IsValidForFuzzyCacheHit(const IsValidForCacheHitOptions& options) const;
};

inline std::ostream& operator<<(
    std::ostream& strm, const MotionPlanningRequestCacheKeyDistance& distance) {
  return strm << absl::StrFormat(
             *distance_cache_format,
             distance.diff_in_m_for_all_related_frame_poses,
             distance.diff_in_m_for_all_object_poses,
             distance.max_diff_in_rad_for_starting_robot_configuration,
             distance.max_diff_in_rad_for_kinematic_objects,
             distance.max_diff_in_world_application_limits,
             distance.num_of_objects_new_in_one_key,
             distance.world_collision_settings_are_same,
             distance.motion_segment_collision_settings_are_same,
             distance.robot_links_are_same, distance.tool_links_are_same,
             distance.geometry_fingerprints_are_same);
}

// A cache for storing input and output of MotionPlannerService::PlanTrajectory.
// All public functions of this class are thread-safe.
// The core of the cache is implemented by a mutex-guarded `LruCache`.
// The overall structure of the cache is:
//
// |       |          Element_1          |  Element_2 | Element_3          |
// |-------|:---------------------------:|:----------:|--------------------|
// | Key   | group_id_1                  | group_id_2 | group_id_3         |
// | Value | [entry_a, entry_b, entry_c] | [entry_d]  | [entry_e, entry_f] |
//
// A cache entry holds the input and output of
// MotionPlannerService::PlanTrajectory.
// An element of the cache holds a group_id (Key) and an ordered
// list of cache entries (Value). All cache entries of an element have the same
// group_id. The "front" of the list is the newest entry based on insertion.
// See `Insert` and `Lookup` for more details.
class PlanTrajectoryCache {
 public:
  // An entry in the cache holding the group identifier (`group_id`), logging
  // identifier (`uuid`), extracted scene features (`features`), and planned
  // trajectory (`result`) of `MotionPlannerService::PlanTrajectory()`.
  struct CacheEntry {
    std::string uuid;
    size_t group_id = 0;
    MotionPlanningCacheEntryFeatures features;
    MotionPlanner::PlanTrajectoryResult result;

    CacheEntry() = default;
    // Constructs a `CacheEntry` by extracting `uuid`, `group_id`, and
    // `features` from `cache_key` along with `plan_result`.
    explicit CacheEntry(const MotionPlanningRequestCacheKey& cache_key,
                        MotionPlanner::PlanTrajectoryResult plan_result = {})
        : uuid(cache_key.uuid),
          group_id(cache_key.GetGroupId()),
          features(cache_key.cache_entry_features),
          result(std::move(plan_result)) {}
  };
  using CacheGroupEntries = std::deque<std::unique_ptr<CacheEntry>>;

  // Perform input validation and create a cache.
  static absl::StatusOr<std::unique_ptr<PlanTrajectoryCache>> Create(
      int max_num_of_groups, int max_num_of_entries_per_group,
      const MotionPlanningRequestCacheKeyDistance::IsValidForCacheHitOptions&
          distance_options = MotionPlanningRequestCacheKeyDistance::
              IsValidForCacheHitOptions{});

  // The return type of `Lookup()`.
  // If `exact_match` is true, `cached_entry` is an exact cache hit for the
  // given `key`. If `exact_match` is false, `cached_entry` is the closest
  // fuzzy cache hit candidate in the group. See `Lookup()` for more details.
  struct LookupResult {
    // True if the returned cached entry is an exact match to the given cache
    // key.
    const bool exact_match;
    // The uuid of the given cache key for look up.
    const std::string given_cache_key_uuid;
    // The returned cached entry.
    const PlanTrajectoryCache::CacheEntry cached_entry;
    // The difference between the given cache key to `Lookup()` and the returned
    // cached entry.
    const MotionPlanningRequestCacheKeyDistance distance;

    // Return true if this LookupResult has a valid trajectory.
    // If this LookupResult is an exact match, then it has a valid trajectory.
    // Otherwise, we check the following criteria if allow_fuzzy_check is true.
    // * max_diff_in_rad_for_starting_robot_configuration is under the given
    // threshold.
    // * diff_in_m_for_all_related_frame_poses is under a default threshold.
    // * The cached path segments are within limit and collision free.
    absl::StatusOr<bool> HasValidTrajectory(
        const object_world::ObjectWorld& object_world,
        const intrinsic_proto::motion_planning::v1::RobotSpecification&
            robot_specification_proto,
        const intrinsic_proto::world::CollisionCheckerConfig&
            collision_checker_config,
        double max_diff_in_rad_for_starting_robot_configuration_threshold,
        bool allow_fuzzy_check, double collision_check_spacing);
  };

  // Remove all entries from the cache.
  void ClearCache();
  ~PlanTrajectoryCache() { ClearCache(); }

  PlanTrajectoryCache() = delete;
  PlanTrajectoryCache(const PlanTrajectoryCache&) = delete;
  PlanTrajectoryCache& operator=(const PlanTrajectoryCache&) = delete;
  PlanTrajectoryCache(PlanTrajectoryCache&&) = delete;
  PlanTrajectoryCache& operator=(PlanTrajectoryCache&&) = delete;

  size_t GetNumOfGroups() const {
    absl::MutexLock lock(mutex_);
    return group_id_to_entries_.entries();
  }
  // Sum up the numbers of entries in all groups. Cannot be marked as const
  // since it requires reservation.
  size_t GetNumOfEntries() const;
  // Get ordered uuids of all entries in a group.
  std::vector<std::string> GetUUIDsOfGroup(size_t group_id);

  // Insert a new entry. The ownership of the entry will be transferred to the
  // cache.
  // If the new entry requires a new group and `max_num_of_groups` is reached.
  // The least used group will be removed. (Not implemented yet.)
  // If the group of the new entry is full (`max_num_of_entries_per_group` is
  // reached). The oldest entry of that group will be removed.
  absl::Status Insert(std::unique_ptr<CacheEntry> entry);

  // Looks up `key` in the cache. Cannot be marked `const` since `LruCache`
  // lookup updates LRU ordering.
  // First looks up the group matching `key.GetGroupId()`, returning
  // `absl::NotFoundError` if the group does not exist.
  // Then searches the group for the first exact match (`IsValidForCacheHit()`),
  // or the closest fuzzy match candidate (`IsValidForFuzzyCacheHit()`).
  // Returns `absl::NotFoundError` if no entry in the group is a valid exact or
  // fuzzy hit.
  absl::StatusOr<LookupResult> Lookup(const MotionPlanningRequestCacheKey& key);

 private:
  // Constructor.
  // `max_num_of_groups`: The maximum number of groups to cache.
  // `max_num_of_entries_per_group`: The maximum number of entries per group to
  // cache.
  // `distance_options`: Options to use for calculating distance between
  // entries.
  explicit PlanTrajectoryCache(
      int max_num_of_groups, int max_num_of_entries_per_group,
      const MotionPlanningRequestCacheKeyDistance::IsValidForCacheHitOptions&
          distance_options = MotionPlanningRequestCacheKeyDistance::
              IsValidForCacheHitOptions{});

  const int max_num_of_entries_per_group_;
  using GroupCache = LruCache<size_t, CacheGroupEntries>;
  GroupCache group_id_to_entries_ ABSL_GUARDED_BY(mutex_);
  mutable absl::Mutex mutex_;
  const MotionPlanningRequestCacheKeyDistance::IsValidForCacheHitOptions
      distance_options_;
};

}  // namespace intrinsic
#endif  // INTRINSIC_MOTION_PLANNING_SERVICE_CACHE_MOTION_PLANNER_CACHE_H_
