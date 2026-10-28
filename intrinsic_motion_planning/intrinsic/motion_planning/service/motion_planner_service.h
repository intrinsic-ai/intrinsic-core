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

#ifndef INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_H_
#define INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/logging/structured_logging_client.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_flags.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_config.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.grpc.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/proto/v1/robot_specification.pb.h"
#include "intrinsic/motion_planning/service/cache/motion_planner_cache.h"
#include "intrinsic/motion_planning/service/motion_planner_service_asset_utils.h"
#include "intrinsic/motion_planning/service/nonvolatile_cache/motion_planner_nonvolatile_cache.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

// Chooses which version of the proxy to use.
//
// Note: The dof view proxy is used because it does not cause issues for
// branching kinematic structures, at the expense of not supporting FK/IK.
// Ideally, we will get to a state where we have one proxy that supports a
// "control point" more explicitly.
absl::StatusOr<std::unique_ptr<KinematicsSystemProxy>> GetProxyForPathPlanning(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    const intrinsic_proto::world::CollisionCheckerConfig&
        collision_checker_config,
    const intrinsic_proto::RuleSet& rule_set, bool require_ik_and_fk,
    bool disable_collision_checking);

// Options for debugging to be set on startup.
struct MotionPlannerServiceDebugConfig {
  // If true, logs the MotionPlanningDebugData that can be used to reproduce the
  // motion planning call offline.
  bool save_debug_data = true;
};

// Configuration for creating the motion planner service.
struct MotionPlannerServiceConfig {
  MotionPlannerServiceDebugConfig debug_config = {};

  // Enable fuzzy cache checking for `PlanTrajectory` results.
  bool enable_motion_planner_service_caching_fuzzy_check = false;

  // Whether MotionPlannerService accepts requests with `compute_swept_volume`
  // set. If false, such `PlanTrajectory`/`PlanPath` requests fail with
  // FAILED_PRECONDITION.
  bool enable_motion_planner_service_swept_volumes = false;

  // Threshold of max_diff_in_rad_for_starting_robot_configuration used to check
  // if a cached PlanTrajectory entry is valid.
  double max_diff_in_rad_for_starting_robot_configuration_threshold = 0.002;

  // Name of the workcell/cluster the motion planner service is running in.
  std::string workcell_name = "";

  // ID of the organization the motion planner service is running in.
  std::string organization_id = "";

  // Please see the definition of `MotionPlannerFlags` to see the default values
  // of the flags therein contained.
  MotionPlannerFlags flags = {};

  // If true, creates a recording for all motion planning calls, including
  // successful calls and routine failures (e.g. unreachable or in-collision IK
  // targets), instead of only unexpected failures.
  bool log_all_recordings = false;
};

// Assigns the configuration fields of C++ `MotionPlannerServiceConfig` from
// proto `MotionPlannerServiceConfig`.
void FromProto(
    const intrinsic_proto::motion_planning::v1::MotionPlannerServiceConfig&
        proto_config,
    MotionPlannerServiceConfig& config);

struct WorldAndProto {
  World world;
  intrinsic_proto::world::internal::World world_proto;
};
// A gRPC server that handles motion planning requests.
class MotionPlannerService : public intrinsic_proto::motion_planning::v1::
                                 MotionPlannerService::Service {
 public:
  // Set `plan_trajectory_cache` to nullptr to disable caching for
  // `PlanTrajectory` results. Once enabled, PlanTrajectory may return a
  // previously cached plan if the planning arguments are considered close
  // enough to a cached one. See go/intrinsic-cache-motion-plan-design for
  // details.
  static absl::StatusOr<std::unique_ptr<MotionPlannerService>> Create(
      intrinsic_proto::world::ObjectWorldService::StubInterface*
          object_world_service,
      GeometryLibrary* geometry_library,
      std::string_view mps_asset_id_name = kMotionPlannerServiceName,
      // TODO(b/317516475) Create the cache in MPS::Create instead of passing it
      // as a pointer.
      std::unique_ptr<PlanTrajectoryCache> plan_trajectory_cache = nullptr,
      std::unique_ptr<MotionPlannerNonvolatileCache>
          plan_trajectory_nonvolatile_cache = nullptr,
      const MotionPlannerServiceConfig& mps_config = {},
      std::optional<std::string> mps_asset_version = std::nullopt,
      const StructuredLoggingClient* logger_client = nullptr);

  ~MotionPlannerService() override {
    if (plan_trajectory_cache_ != nullptr) plan_trajectory_cache_->ClearCache();
  }

  ::grpc::Status PlanTrajectory(
      ::grpc::ServerContext* context,
      const intrinsic_proto::motion_planning::v1::MotionPlanningRequest*
          request,
      intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse*
          response) override;

  ::grpc::Status PlanPath(
      ::grpc::ServerContext* context,
      const intrinsic_proto::motion_planning::v1::MotionPlanningRequest*
          request,
      intrinsic_proto::motion_planning::v1::PathPlanningResponse* response)
      override;

  ::grpc::Status ComputeIk(
      ::grpc::ServerContext* context,
      const intrinsic_proto::motion_planning::v1::IkRequest* request,
      intrinsic_proto::motion_planning::v1::IkResponse* response) override;

  ::grpc::Status ComputeFk(
      ::grpc::ServerContext* context,
      const intrinsic_proto::motion_planning::v1::FkRequest* request,
      intrinsic_proto::motion_planning::v1::FkResponse* response) override;

  ::grpc::Status CheckCollisions(
      ::grpc::ServerContext* context,
      const intrinsic_proto::motion_planning::v1::CheckCollisionsRequest*
          request,
      intrinsic_proto::motion_planning::v1::CheckCollisionsResponse* response)
      override;

  // Computes a batch of IK queries and/or collision check queries against a
  // single world and robot. The world, object world view and kinematics proxy
  // are built once and reused for all queries. Per-query failures are reported
  // in the individual query results and do not fail the whole RPC.
  ::grpc::Status BatchCheck(
      ::grpc::ServerContext* context,
      const intrinsic_proto::motion_planning::v1::BatchCheckRequest* request,
      intrinsic_proto::motion_planning::v1::BatchCheckResponse* response)
      override;
  ::grpc::Status ClearCache(::grpc::ServerContext* context,
                            const google::protobuf::Empty* request,
                            google::protobuf::Empty* response) override;
 private:
  explicit MotionPlannerService(
      intrinsic_proto::world::ObjectWorldService::StubInterface*
          object_world_service,
      GeometryLibrary* geometry_library,
      std::unique_ptr<MotionPlanner> motion_planner,
      std::string_view mps_asset_id_name,
      // TODO(b/317516475) Create the cache in MPS::Create instead of passing it
      // as a pointer.
      std::unique_ptr<PlanTrajectoryCache> plan_trajectory_cache = nullptr,
      std::unique_ptr<MotionPlannerNonvolatileCache>
          plan_trajectory_nonvolatile_cache = nullptr,
      const MotionPlannerServiceConfig& mps_config = {},
      std::optional<std::string> mps_asset_version = std::nullopt,
      const StructuredLoggingClient* logger_client = nullptr,
      bool context_resolved = false);

  // Returns both the initial World and its proto from the World Service with
  // ID `world_id`.
  absl::StatusOr<WorldAndProto> GetInitialWorldFromWorldService(
      absl::string_view world_id);

  // Serializes `world` into its proto and, if a geometry library is
  // configured, deserializes the world geometries in-place. Shared tail of all
  // paths that construct the initial world of a request.
  absl::StatusOr<WorldAndProto> MakeWorldAndProto(World world);

  // Returns both the initial World and its proto for a BatchCheck request,
  // built from the request's `world_snapshot` or fetched from the World
  // Service using `world_id`, depending on which of the two is set. Returns
  // INVALID_ARGUMENT if neither is set or `world_id` is empty.
  absl::StatusOr<WorldAndProto> GetWorldAndProtoForBatchCheck(
      const intrinsic_proto::motion_planning::v1::BatchCheckRequest& request);

  // If the `robot_specification` specifies a start joint configuration, then
  // set this joint configuration on the `robot`.
  absl::Status SetRobotStartConfigurationIfSetInRobotSpecification(
      const intrinsic_proto::motion_planning::v1::RobotSpecification&
          robot_specification,
      object_world::KinematicObject& robot);

  ::grpc::Status PlanTrajectoryInternal(
      grpc::ServerContext* context,
      const intrinsic_proto::motion_planning::v1::MotionPlanningRequest*
          request,
      intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse*
          response,
      std::optional<RunTimeMotionPlannerFlags> run_time_flags);

  // Given `request`, `initial_world`, `cache_hit`, `enable_collision_stats`,
  // and `robot`, this function converts `plan_trajectory_result` into
  // `response`.
  absl::Status ConvertPlanTrajectoryResultToTrajectoryPlanningResponse(
      const MotionPlanner::PlanTrajectoryResult& plan_trajectory_result,
      const intrinsic_proto::motion_planning::v1::MotionPlanningRequest&
          request,
      const World& initial_world, absl::string_view cache_hit,
      bool enable_collision_stats, const object_world::KinematicObject& robot,
      intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse& response
  );

  // Computes the swept volume of `robot_id` moving along `path` in `world`,
  // stores the resulting meshes in the geometry library, and appends them to
  // `swept_volumes_out`.
  // Returns `kUnimplemented` in builds without swept volume support.
  absl::Status ComputeSweptVolumeAndAssignToResponse(
      const World& world, const RobotCollectionsEntityId& robot_id,
      const std::vector<eigenmath::VectorXd>& path,
      google::protobuf::RepeatedPtrField<
          intrinsic_proto::geometry::v1::TransformedGeometry>*
          swept_volumes_out);
  intrinsic_proto::world::ObjectWorldService::StubInterface*
      object_world_service_;           // Not owned.
  GeometryLibrary* geometry_library_;  // Not owned.
  const MotionPlannerServiceConfig mps_config_;

  // This is used to generate the motion planning results.
  std::unique_ptr<MotionPlanner> motion_planner_ = nullptr;

  // This is used to cache the planning result from PlanTrajectory.
  std::unique_ptr<PlanTrajectoryCache> plan_trajectory_cache_;
  // This is used to lock the planning result from PlanTrajectory.
  std::unique_ptr<MotionPlannerNonvolatileCache>
      plan_trajectory_nonvolatile_cache_;
  // PubSub and Publisher used to publish trajectories
  PubSub pubsub_;
  Publisher planned_trajectory_publisher_;

  std::optional<std::string> mps_asset_version_;
  std::string mps_asset_id_name_;

  absl::BitGen rng_;
  mutable absl::Mutex context_mutex_;
  std::string organization_id_ ABSL_GUARDED_BY(context_mutex_);
  std::string workcell_name_ ABSL_GUARDED_BY(context_mutex_);
  std::unique_ptr<Thread> context_fetcher_thread_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_H_
