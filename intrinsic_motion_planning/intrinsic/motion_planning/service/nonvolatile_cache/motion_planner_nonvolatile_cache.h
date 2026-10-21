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

#ifndef INTRINSIC_MOTION_PLANNING_SERVICE_NONVOLATILE_CACHE_MOTION_PLANNER_NONVOLATILE_CACHE_H_
#define INTRINSIC_MOTION_PLANNING_SERVICE_NONVOLATILE_CACHE_MOTION_PLANNER_NONVOLATILE_CACHE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_config.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/proto/v1/robot_specification.pb.h"
#include "intrinsic/motion_planning/service/nonvolatile_cache/motion_planner_nonvolatile_cache.pb.h"
#include "intrinsic/storage/content_addressable_storage/proto/cas_service.grpc.pb.h"
#include "intrinsic/util/lru_cache.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/proto/collision_settings.pb.h"
#include "intrinsic/world/world.pb.h"

namespace intrinsic {

constexpr double kMaxDiffInRadForStartingRobotConfiguration = 1e-3;
constexpr double kMaxPositionDiffForPoses = 1e-4;
constexpr double kMaxRotationDiffForPoses = 1e-5;

using ::intrinsic_proto::content_addressable_storage::v1::
    ContentAddressableStorageService;

// The key type used for caching results from
// MotionPlannerService::PlanTrajectory to a nonvolatile storage.
struct MotionPlannerNonvolatileCacheKey {
  // The MotionSpecification from the planning request.
  const intrinsic_proto::motion_planning::v1::MotionSpecification
      motion_specification;

  // The RobotSpecification from the planning request but without the
  // `start_configuration`.
  const intrinsic_proto::motion_planning::v1::RobotSpecification
      robot_specification;

  // References and poses in root frame of all transform nodes referred in
  // each motion segment.
  const std::vector<absl::flat_hash_map<ObjectWorldResourceId, Pose3d>>
      poses_of_all_related_transform_nodes;

  // The starting robot configuration in the request.
  const eigenmath::VectorXd starting_robot_configuration;

  // Motion ID used to load the cache entry. Not used for saving.
  const std::string uuid;

  // `MotionPlannerService` (MPS) Asset major version. Variations of MPS Asset
  // minor version may still result in cache hits as long as both the major
  // version (`mps_asset_major_version`) and the `motion_planning_proto_version`
  // are the same.
  const std::string mps_asset_major_version;

  // Motion Planning proto version.
  const std::string motion_planning_proto_version;

  // Create a `MotionPlannerNonvolatileCacheKey` from the given set of input
  // arguments.
  static absl::StatusOr<MotionPlannerNonvolatileCacheKey> Create(
      const object_world::ObjectWorld& object_world,
      const intrinsic_proto::motion_planning::v1::MotionSpecification&
          motion_specification,
      const intrinsic_proto::motion_planning::v1::RobotSpecification&
          robot_specification,
      absl::string_view uuid = "",
      absl::string_view mps_asset_major_version = "");

  bool IsApproximate(
      const MotionPlannerNonvolatileCacheKey& other_key,
      const absl::flat_hash_set<uint32_t>& ignore_motion_segment_ids,
      double max_diff_in_rad_for_starting_robot_configuration =
          kMaxDiffInRadForStartingRobotConfiguration,
      double max_position_diff_for_poses = kMaxPositionDiffForPoses,
      double max_rotation_diff_for_poses = kMaxRotationDiffForPoses) const;
};

intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheKey ToProto(
    const MotionPlannerNonvolatileCacheKey& key);

absl::StatusOr<MotionPlannerNonvolatileCacheKey> FromProto(
    const intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheKey&
        key_proto);

// The value type used for caching results from
// MotionPlannerService::PlanTrajectory to a nonvolatile storage.
struct MotionPlannerNonvolatileCacheValue {
  // The planned trajectory.
  const JointTrajectoryPVA trajectory;
  // The path segments used to generate the planned trajectory.
  const std::vector<PathSegment> path_segments;

  // The motion events.
  const std::vector<intrinsic_proto::motion_planning::v1::MotionEvent>
      motion_events = {};

};

absl::StatusOr<
    intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheValue>
ToProto(const MotionPlannerNonvolatileCacheValue& value);

absl::StatusOr<MotionPlannerNonvolatileCacheValue> FromProto(
    const intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheValue&
        value_proto);

// The entry (key+value) type used for caching results from
// MotionPlannerService::PlanTrajectory to a nonvolatile storage.
struct MotionPlannerNonvolatileCacheEntry {
  // The key used to save the planning result.
  const MotionPlannerNonvolatileCacheKey key;
  // The planning result to save.
  const MotionPlannerNonvolatileCacheValue value;
};

absl::StatusOr<
    intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheEntry>
ToProto(const MotionPlannerNonvolatileCacheEntry& entry);

absl::StatusOr<MotionPlannerNonvolatileCacheEntry> FromProto(
    const intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheEntry&
        entry_proto);

// A cache for storing input and output of MotionPlannerService::PlanTrajectory
// in a nonvolatile cache and a volatile cache. All public functions
// of this class are thread-safe. The core of the nonvolatile cache is
// implemented by CAS. The core of the volatile cache
// is implemented by a mutex-guarded `LruCache`.
//
// In Insert, the entry is inserted to both caches.
// In Lookup, the entry is first looked up from the volatile cache. If it does
// not exist in the volatile cache, it is looked up from the nonvolatile cache.
// If it is in the nonvolatile cache, we will insert it to volatile cache for
// faster lookup next time.

class MotionPlannerNonvolatileCache {
 public:
  using VolatileCacheType =
      LruCache<std::string, MotionPlannerNonvolatileCacheEntry>;

  // Perform input validation and create a cache backed by CAS.
  static absl::StatusOr<std::unique_ptr<MotionPlannerNonvolatileCache>>
  CreateWithContentAddressableStorage(int max_num_of_volatile_cache_entries,
                                      absl::string_view cas_service_address);

  void ClearVolatileCache();
  ~MotionPlannerNonvolatileCache() { ClearVolatileCache(); }

  MotionPlannerNonvolatileCache() = delete;
  MotionPlannerNonvolatileCache(const MotionPlannerNonvolatileCache&) = delete;
  MotionPlannerNonvolatileCache& operator=(
      const MotionPlannerNonvolatileCache&) = delete;
  MotionPlannerNonvolatileCache(MotionPlannerNonvolatileCache&&) = delete;
  MotionPlannerNonvolatileCache& operator=(MotionPlannerNonvolatileCache&&) =
      delete;

  // Insert a new entry to both nonvolatile and volatile cache and return
  // the uuid.
  absl::StatusOr<std::string> Insert(
      const MotionPlannerNonvolatileCacheEntry& entry);

  // Lookup a key in both caches. This cannot be marked const since it requires
  // reservations. Only the uuid of the key is used for lookup. It is possible
  // that the returned entry has different key field values form the given one.
  // The entry is first looked up from the volatile cache. If it does not exist
  // in the volatile cache, it is looked up from the nonvolatile cache. If it is
  // in the nonvolatile cache, we will insert it to volatile cache for faster
  // lookup next time. If no matching entry is found, return a std:nullopt.
  absl::StatusOr<std::optional<MotionPlannerNonvolatileCacheEntry>> Lookup(
      const MotionPlannerNonvolatileCacheKey& key);

  // Return the number of entries currently in the volatile cache.
  size_t GetVolatileCacheSize() const {
    absl::MutexLock lock(&mutex_);
    return volatile_cache_.entries();
  }

 private:
  // Constructor.
  // `max_num_of_volatile_cache_entries`: The maximum number of entries in the
  // volatile cache.
  explicit MotionPlannerNonvolatileCache(
      int max_num_of_volatile_cache_entries,
      std::unique_ptr<ContentAddressableStorageService::Stub> cas_service_stub_)
      : volatile_cache_(max_num_of_volatile_cache_entries),
        cas_service_stub_(std::move(cas_service_stub_)) {}

  absl::StatusOr<std::string> SaveToNonvolatileCache(
      const MotionPlannerNonvolatileCacheEntry& entry);

  absl::StatusOr<std::optional<MotionPlannerNonvolatileCacheEntry>>
  LoadFromNonvolatileCache(const MotionPlannerNonvolatileCacheKey& key);

  VolatileCacheType volatile_cache_ ABSL_GUARDED_BY(mutex_);
  mutable absl::Mutex mutex_;
  std::unique_ptr<ContentAddressableStorageService::Stub> cas_service_stub_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_SERVICE_NONVOLATILE_CACHE_MOTION_PLANNER_NONVOLATILE_CACHE_H_
