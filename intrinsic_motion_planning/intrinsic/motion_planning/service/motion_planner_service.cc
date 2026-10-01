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

#include "intrinsic/motion_planning/service/motion_planner_service.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/assets/id_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/affine_transform_of_geometry.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/geometry/proto/v1/transformed_geometry.pb.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/icon/proto/joint_trajectory_conversion.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/kinematics/state.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/kinematics/utils/compute_ik_util.h"
#include "intrinsic/logging/data_logger_client.h"
#include "intrinsic/logging/log_item_builder.h"
#include "intrinsic/logging/proto/log_dispatcher_service.pb.h"
#include "intrinsic/logging/proto/log_item.pb.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/pose.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/motion_planning/conversions.h"
#include "intrinsic/motion_planning/metrics/metrics.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_flags.h"
#include "intrinsic/motion_planning/motion_planner/motion_planning_error_utils.h"
#include "intrinsic/motion_planning/motion_planner/motion_planning_utils.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy_util.h"
#include "intrinsic/motion_planning/path_planning/path_planner_definitions.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"
#include "intrinsic/motion_planning/path_planning/planners/validation.h"
#include "intrinsic/motion_planning/proto/motion_planner_service_proto_utils.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_config.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning_error.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/proto/v1/robot_specification.pb.h"
#include "intrinsic/motion_planning/service/debug/timing_debug_logger.h"
#include "intrinsic/motion_planning/service/motion_planner_cache.h"
#include "intrinsic/motion_planning/service/motion_planner_service_asset_utils.h"
#include "intrinsic/motion_planning/service/motion_planner_service_utils.h"
#include "intrinsic/motion_planning/service/nonvolatile_cache/motion_planner_nonvolatile_cache.h"
#include "intrinsic/motion_planning/service/nonvolatile_cache/motion_planner_nonvolatile_cache_utils.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/skills/internal/world_service_utils.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/proto/type_url.h"
#include "intrinsic/util/status/log_if_error.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_builder_grpc.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/collision/util/collision_world_util.h"
#include "intrinsic/world/collision/util/make_collision_settings.h"
#include "intrinsic/world/collision/util/make_rule_set.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_proto_utils.h"
#include "intrinsic/world/objects/transform_node_internal.h"
#include "intrinsic/world/proto/collision_checker_config.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"
#include "intrinsic/world/service/objects/object_world_converter.h"
#include "intrinsic/world/world.h"
#include "opencensus/stats/stats.h"

namespace intrinsic {
namespace {
static const auto* const kConfigFormatString =
    new absl::ParsedFormat<'s', 's', 'f'>(
        "Cache <%s> and <%s> has a max starting robot configuration "
        "difference of %.3f.");
static const auto* const kPoseFormatString = new absl::ParsedFormat<
    's', 's', 's', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f', 'f',
    'f', 'f', 'f', 'f'>(
    "Cache <%s> and <%s> has a mismatch pose for object/frame: %s. "
    "Trans Diff: %.3f (m). Rot Diff: %.3f (rad). Pose1: %.3f, %.3f, %.3f, "
    "%.3f, %.3f, %.3f, %.3f. Pose2: %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f.");
static const auto* const new_cache_format_string =
    new absl::ParsedFormat<'s', 'u'>(
        "Inserted a new cache uuid <%s>. Current cache size: %u.");
static const auto* const return_cache_format_string =
    new absl::ParsedFormat<'s', 's', 'u'>(
        "Return a cache uuid <%s> for given cache uuid <%s>. Current cache "
        "size: %u.");
static const auto* const failed_plan_cache_format_string =
    new absl::ParsedFormat<'s', 'u'>(
        "Cache uuid <%s> is not inserted due to failed planning result. "
        "Current cache size: %u.");
static constexpr char kPubSubName[] = "motion_planning";
static constexpr char kPlannedTrajectoryTopicName[] = "planned_trajectory";

using intrinsic_proto::motion_planning::v1::MotionPlanningRequest;
using intrinsic_proto::world::CreateWorldRequest;
using intrinsic_proto::world::Objects;
using intrinsic_proto::world::ObjectView;
using intrinsic_proto::world::WorldMetadata;

// Return the `MotionPlannerService` Asset Version if provided in the
// `server_context`, otherwise return an `std::nullopt`. It is expected that
// `server_context` either has no or 1 information about the MPS Asset Version,
// and if not --i.e. that `server_context` has more than 1 information about the
// MPS Asset Version--, then a `kInternalError` will be returned.
absl::StatusOr<std::optional<std::string>>
GetMotionPlannerServiceAssetVersionFromServerContextIfProvided(
    const ::grpc::ServerContext& server_context) {
  const std::multimap<grpc::string_ref, grpc::string_ref>& client_metadata =
      server_context.client_metadata();
  const size_t num_info_motion_planner_service_asset_version =
      client_metadata.count(std::string(kMotionPlannerServiceAssetVersionKey));
  if (num_info_motion_planner_service_asset_version == 0) {
    return std::nullopt;
  } else if (num_info_motion_planner_service_asset_version == 1) {
    auto [metadata_begin, metadata_end] = client_metadata.equal_range(
        std::string(kMotionPlannerServiceAssetVersionKey));
    return /*mps_asset_version=*/std::string(metadata_begin->second.data(),
                                             metadata_begin->second.size());
  } else {
    return absl::InternalError(
        absl::StrCat("There must only be 1 definition of the "
                     "`MotionPlannerService` Asset Version, but found ",
                     num_info_motion_planner_service_asset_version,
                     " definitions instead."));
  }
}

// Get the `MotionPlannerService` (MPS) Asset Major Version from MPS Asset
// Version (`mps_asset_version`).
absl::StatusOr<std::string> GetMotionPlannerServiceMajorAssetVersion(
    std::string_view mps_asset_id_name, std::string_view mps_asset_version) {
  INTR_ASSIGN_OR_RETURN(assets::IdVersionParts id_version_parts,
                        assets::IdVersionParts::Create(
                            /*id_version=*/absl::StrCat(
                                kMotionPlannerServiceAssetIdPackage, ".",
                                mps_asset_id_name, ".", mps_asset_version)));
  return std::string(id_version_parts.VersionMajor());
}

// Return the `MotionPlannerService` Asset Major Version if provided in the
// `server_context`, otherwise return an `std::nullopt`. It is expected that
// `server_context` either has no or 1 information about the MPS Asset Version,
// and if not --i.e. that `server_context` has more than 1 information about the
// MPS Asset Version--, then a `kInternalError` will be returned.
absl::StatusOr<std::optional<std::string>>
GetMotionPlannerServiceMajorAssetVersionFromServerContextIfProvided(
    const ::grpc::ServerContext& server_context,
    std::string_view mps_asset_id_name) {
  INTR_ASSIGN_OR_RETURN(
      std::optional<std::string> optional_mps_asset_version,
      GetMotionPlannerServiceAssetVersionFromServerContextIfProvided(
          server_context));
  if (!optional_mps_asset_version.has_value()) {
    return std::nullopt;
  }
  LOG(INFO) << "Installed MPS Asset Version: "
            << optional_mps_asset_version.value();
  return GetMotionPlannerServiceMajorAssetVersion(
      mps_asset_id_name, optional_mps_asset_version.value());
}

// Update type url to contain correct information
absl::Status AddTypeUrlPrefixToPayload(const absl::Status& status) {
  absl::Status result = status;
  status.ForEachPayload([&](absl::string_view type_url,
                            const absl::Cord& payload) {
    if (absl::StrContains(type_url, "MotionPlanningError") &&
        !(absl::StrContains(type_url, kTypeUrlPrefix))) {
      const std::string new_type_url = absl::StrCat(kTypeUrlPrefix, type_url);
      result.SetPayload(new_type_url, payload);
    }
  });
  return result;
}

absl::StatusOr<std::unique_ptr<object_world::ObjectWorld>> GetObjectWorldView(
    World& world) {
  INTR_ASSIGN_OR_RETURN(auto object_world,
                        object_world::ObjectWorld::CreateView(world));
  return object_world;
}

absl::StatusOr<std::vector<intrinsic_proto::geometry::v1::TransformedGeometry>>
ComputeSweptVolume(const World& world, const RobotCollectionsEntityId& robot_id,
                   GeometrySerializer& geolib, const PointPath& path) {
  INTR_ASSIGN_OR_RETURN(const auto shape_data,
                        ComputeSweptVolumeFromPath(world, robot_id, path));

  std::vector<intrinsic_proto::geometry::v1::TransformedGeometry> result;
  result.reserve(shape_data.size());
  for (const TransformedGeometry& shape : shape_data) {
    INTR_ASSIGN_OR_RETURN(
        intrinsic_proto::geometry::v1::TransformedGeometry shape_proto,
        intrinsic::geo::ToProto(shape, &geolib));
    result.push_back(std::move(shape_proto));
  }
  return result;
}

PointPath PathSegmentsToPath(const std::vector<PathSegment>& path_segments) {
  PointPath path;
  for (const auto& path_segment : path_segments) {
    for (const auto& joint_configuration : path_segment.joint_configurations) {
      path.push_back(joint_configuration);
    }
    path.pop_back();
  }
  if (!path.empty()) {
    path.push_back(path_segments.back().joint_configurations.back());
  }
  return path;
}

absl::StatusOr<std::vector<intrinsic_proto::geometry::v1::TransformedGeometry>>
ComputeSweptVolume(const World& world, const RobotCollectionsEntityId& robot_id,
                   GeometrySerializer& geolib,
                   const JointTrajectoryPVA& trajectory) {
  PointPath path;
  path.reserve(trajectory.size());
  for (const JointStatePVA& pva : trajectory.data()) {
    path.push_back(pva.position);
  }

  return ComputeSweptVolume(world, robot_id, geolib, path);
}

absl::Status ComputeSweptVolumeForPathAndAssignToResponse(
    const World& world, const RobotCollectionsEntityId& robot_id,
    GeometrySerializer& geolib, const PointPath& path,
    intrinsic_proto::motion_planning::v1::PathPlanningResponse* response) {
  std::vector<intrinsic_proto::geometry::v1::TransformedGeometry> swept_volumes;
  {
    INTR_ASSIGN_OR_RETURN(swept_volumes,
                          ComputeSweptVolume(world, robot_id, geolib, path));
  }

  for (auto& swept_volume : swept_volumes) {
    *response->add_swept_volumes() = std::move(swept_volume);
  }
  return absl::OkStatus();
}

absl::Status ComputeSweptVolumeAndAssignToResponse(
    const World& world, const RobotCollectionsEntityId& robot_id,
    GeometrySerializer& geolib, const JointTrajectoryPVA& trajectory,
    intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse*
        response) {
  std::vector<intrinsic_proto::geometry::v1::TransformedGeometry> swept_volumes;
  {
    INTR_ASSIGN_OR_RETURN(
        swept_volumes, ComputeSweptVolume(world, robot_id, geolib, trajectory));
  }

  for (auto& swept_volume : swept_volumes) {
    *response->add_swept_volumes() = std::move(swept_volume);
  }
  return absl::OkStatus();
}

// Inserts the given result into the cache if the cache is not null and the
// result is ok.
absl::Status MaybeInsertMotionPlanningRequestCacheHit(
    const MotionPlanningRequestCacheKey& cache_key,
    const absl::StatusOr<MotionPlanner::PlanTrajectoryResult>& result,
    PlanTrajectoryCache* plan_trajectory_cache) {
  if (plan_trajectory_cache == nullptr) {
    return absl::OkStatus();
  }

  if (result.ok()) {
    // The pointer will be destroyed when the entry is removed from the cache.
    INTR_RETURN_IF_ERROR_GRPC(plan_trajectory_cache->Insert(
        std::make_unique<PlanTrajectoryCache::CacheEntry>(cache_key,
                                                          result.value())));
    LOG(INFO) << absl::StrFormat(*new_cache_format_string, cache_key.uuid,
                                 plan_trajectory_cache->GetNumOfEntries());

  } else {
    LOG(INFO) << absl::StrFormat(*failed_plan_cache_format_string,
                                 cache_key.uuid,
                                 plan_trajectory_cache->GetNumOfEntries());
  }
  return absl::OkStatus();
}

}  // namespace

void FromProto(
    const intrinsic_proto::motion_planning::v1::MotionPlannerServiceConfig&
        proto_config,
    MotionPlannerServiceConfig& config) {
  if (proto_config.has_flags()) {
    FromProto(proto_config.flags(), config.flags);
  }
  config.enable_motion_planner_service_caching_fuzzy_check =
      proto_config.enable_motion_planner_service_caching_fuzzy_check();
  config.max_diff_in_rad_for_starting_robot_configuration_threshold =
      proto_config.joint_tolerance_in_rad();
}

absl::StatusOr<WorldAndProto>
MotionPlannerService::GetInitialWorldFromWorldService(
    absl::string_view world_id) {
  if (object_world_service_ == nullptr) {
    return absl::FailedPreconditionError(
        "ObjectWorldService stub is not configured.");
  }
  INTR_ASSIGN_OR_RETURN(
      World initial_world,
      skills::DownloadWorldFromObjectWorldService(
          world_id, *object_world_service_, /*geolib=*/nullptr));
  LOG(INFO) << "Serializing initial world proto...";
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::world::internal::World initial_world_proto,
      initial_world.Serialize());
  if (geometry_library_ != nullptr) {
    INTR_RETURN_IF_ERROR(skills::DeserializeWorldGeometries(
        initial_world, geometry_library_->Deserializer()));
  }
  return WorldAndProto{.world = std::move(initial_world),
                       .world_proto = std::move(initial_world_proto)};
}

absl::Status
MotionPlannerService::SetRobotStartConfigurationIfSetInRobotSpecification(
    const intrinsic_proto::motion_planning::v1::RobotSpecification&
        robot_specification,
    object_world::KinematicObject& robot) {
  if (robot_specification.has_start_configuration() &&
      (robot_specification.start_configuration().joints_size() > 0)) {
    const eigenmath::VectorXd start_configuration = RepeatedDoubleToVectorXd(
        robot_specification.start_configuration().joints());
    return robot.SetJointPositions(start_configuration);
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<KinematicsSystemProxy>> GetProxyForPathPlanning(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    const intrinsic_proto::world::CollisionCheckerConfig&
        collision_checker_config,
    const intrinsic_proto::RuleSet& rule_set, bool require_ik_and_fk,
    bool disable_collision_checking) {
  const World& world = object_world.GetEntityWorld();

  INTR_ASSIGN_OR_RETURN(const bool is_kinematic_chain,
                        world.IsChainKinematicChain(robot.GetRobotEntityId()));

  if (!is_kinematic_chain && require_ik_and_fk) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "We currently do not support resolving Cartesian motion targets "
              "for branching kinematic structures.";
  }
  return CreateKinematicsProxyWithConfig(object_world, robot,
                                         collision_checker_config, std::nullopt,
                                         rule_set, disable_collision_checking);
}

/*static*/
absl::StatusOr<std::unique_ptr<MotionPlannerService>>
MotionPlannerService::Create(
    intrinsic_proto::world::ObjectWorldService::StubInterface*
        object_world_service,
    GeometryLibrary* geometry_library, std::string_view mps_asset_id_name,
    std::unique_ptr<PlanTrajectoryCache> plan_trajectory_cache,
    std::unique_ptr<MotionPlannerNonvolatileCache>
        plan_trajectory_nonvolatile_cache,
    const MotionPlannerServiceConfig& mps_config,
    std::optional<std::string> mps_asset_version,
    const StructuredLoggingClient* logger_client) {
  INTR_ASSIGN_OR_RETURN(auto motion_planner,
                        MotionPlanner::Create(mps_config.flags));

  MotionPlannerServiceConfig actual_config = mps_config;
  // Tracks if the logger context was successfully retrieved synchronously.
  // If false and a logger client is provided, we will start a background
  // thread in the constructor to retry context resolution asynchronously.
  bool context_resolved = false;
  if (logger_client != nullptr) {
    auto context_or = logger_client->GetLoggerContext();
    if (context_or.ok()) {
      actual_config.organization_id = context_or->organization_id();
      actual_config.workcell_name = context_or->workcell_name();
      LOG(INFO) << "Using organization ID " << actual_config.organization_id
                << " and workcell name " << actual_config.workcell_name
                << " from logger context"
                << (mps_asset_version.has_value()
                        ? absl::StrCat(" and asset version ",
                                       *mps_asset_version)
                        : "")
                << ".";
      context_resolved = true;
    } else {
      // TODO (b/533110833) Remove the static flags for organization ID and
      // workcell in MPS config Fallback to static config values if dynamic
      // query fails.
      actual_config.organization_id = mps_config.organization_id;
      actual_config.workcell_name = mps_config.workcell_name;
      LOG(WARNING)
          << "Failed to retrieve organization ID and workcell name from logger "
          << "context. Using static values from MPS config"
          << (mps_asset_version.has_value()
                  ? absl::StrCat(" (asset version: ", *mps_asset_version, ")")
                  : "")
          << ".";
    }
  }

  if (actual_config.organization_id.empty()) {
    LOG(WARNING) << "Organization ID is empty; logging will be disabled.";
  }
  if (actual_config.workcell_name.empty()) {
    LOG(WARNING) << "Workcell Name is empty; logging will be disabled.";
  }

  return absl::WrapUnique(new MotionPlannerService(
      object_world_service, geometry_library, std::move(motion_planner),
      mps_asset_id_name, std::move(plan_trajectory_cache),
      std::move(plan_trajectory_nonvolatile_cache), actual_config,
      mps_asset_version, logger_client, context_resolved));
}

MotionPlannerService::MotionPlannerService(
    ::intrinsic_proto::world::ObjectWorldService::StubInterface*
        object_world_service,
    GeometryLibrary* geometry_library,
    std::unique_ptr<MotionPlanner> motion_planner,
    std::string_view mps_asset_id_name,
    std::unique_ptr<PlanTrajectoryCache> plan_trajectory_cache,
    std::unique_ptr<MotionPlannerNonvolatileCache>
        plan_trajectory_nonvolatile_cache,
    const MotionPlannerServiceConfig& mps_config,
    std::optional<std::string> mps_asset_version,
    const StructuredLoggingClient* logger_client, bool context_resolved)
    : object_world_service_(object_world_service),
      geometry_library_(geometry_library),
      mps_config_(mps_config),
      motion_planner_(std::move(motion_planner)),
      plan_trajectory_cache_(std::move(plan_trajectory_cache)),
      plan_trajectory_nonvolatile_cache_(
          std::move(plan_trajectory_nonvolatile_cache)),
      pubsub_(kPubSubName),
      planned_trajectory_publisher_(
          pubsub_.CreatePublisher(kPlannedTrajectoryTopicName, TopicConfig())
              .value()),
      mps_asset_version_(mps_asset_version),
      mps_asset_id_name_(std::string(mps_asset_id_name)),
      organization_id_(mps_config.organization_id),
      workcell_name_(mps_config.workcell_name) {
  LOG(INFO) << "enable_motion_planner_service_caching_fuzzy_check: "
            << mps_config.enable_motion_planner_service_caching_fuzzy_check;
  LOG(INFO) << "publishing trajectories on topic: "
            << kPlannedTrajectoryTopicName;

  if (logger_client != nullptr && !context_resolved) {
    context_fetcher_thread_ =
        std::make_unique<Thread>([this, logger_client](StopToken stop_token) {
          while (!stop_token.stop_requested()) {
            auto context_or = logger_client->GetLoggerContext();
            if (context_or.ok()) {
              absl::WriterMutexLock lock(&context_mutex_);
              organization_id_ = context_or->organization_id();
              workcell_name_ = context_or->workcell_name();
              LOG(INFO) << "Dynamically resolved logger context: org_id="
                        << organization_id_ << ", workcell=" << workcell_name_;
              break;
            }
            // Instead of a single 5-second sleep, check stop_requested() in
            // 100ms increments to ensure the thread exits immediately during
            // service shutdown.
            for (int i = 0; i < 50 && !stop_token.stop_requested(); ++i) {
              absl::SleepFor(absl::Milliseconds(100));
            }
          }
        });
  }
}
absl::Status
MotionPlannerService::ConvertPlanTrajectoryResultToTrajectoryPlanningResponse(
    const MotionPlanner::PlanTrajectoryResult& plan_trajectory_result,
    const MotionPlanningRequest& request, const World& initial_world,
    absl::string_view cache_hit, bool enable_collision_stats,
    const object_world::KinematicObject& robot,
    intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse& response
) {
  // Fill-out the response path. We explicitly allow empty trajectories,
  // for cases in which we already are at the desired location.
  INTR_ASSIGN_OR_RETURN(
      const auto trajectory_proto,
      ToProto(plan_trajectory_result.trajectory)
  );
  *response.mutable_discretized() = trajectory_proto;
  INTR_LOG_IF_ERROR(absl::LogSeverity::kError,
                    planned_trajectory_publisher_.Publish(trajectory_proto));

  // Calculate and fill-in the swept volume
  if (request.compute_swept_volume()) {
    INTR_RETURN_IF_ERROR(ComputeSweptVolumeAndAssignToResponse(
        initial_world, robot.GetRobotEntityId(),
        geometry_library_->Serializer(), plan_trajectory_result.trajectory,
        &response));
  }
  return absl::OkStatus();
}

::grpc::Status MotionPlannerService::PlanTrajectoryInternal(
    grpc::ServerContext* context, const MotionPlanningRequest* request,
    intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse* response,
    std::optional<RunTimeMotionPlannerFlags> run_time_flags) {
  TimingDebugLogger logger("PlanTrajectoryInternal");
  std::string cache_hit = "no";
  std::optional<std::string> optional_mps_asset_version = std::nullopt;
  std::optional<std::string> optional_mps_asset_major_version = std::nullopt;
  if (mps_asset_version_.has_value()) {
    optional_mps_asset_version = mps_asset_version_.value();
    LOG(INFO) << "Initialized with Installed MPS Asset Version: "
              << optional_mps_asset_version.value();
    INTR_ASSIGN_OR_RETURN_GRPC(
        optional_mps_asset_major_version,
        GetMotionPlannerServiceMajorAssetVersion(mps_asset_id_name_,
                                                 mps_asset_version_.value()));
  } else {
    INTR_ASSIGN_OR_RETURN_GRPC(
        optional_mps_asset_version,
        GetMotionPlannerServiceAssetVersionFromServerContextIfProvided(
            *context));
    INTR_ASSIGN_OR_RETURN_GRPC(
        optional_mps_asset_major_version,
        GetMotionPlannerServiceMajorAssetVersionFromServerContextIfProvided(
            *context, mps_asset_id_name_));
  }
  // Download the world from the world service.
  logger.Start("downloading world");
  INTR_ASSIGN_OR_RETURN_GRPC(
      WorldAndProto initial_world_and_proto,
      GetInitialWorldFromWorldService(request->world_id()), _.LogError());

  LOG(INFO) << "World download complete!";
  // Create the object world view and robot.
  INTR_ASSIGN_OR_RETURN_GRPC(
      std::unique_ptr<object_world::ObjectWorld> object_world,
      GetObjectWorldView(initial_world_and_proto.world), _.LogError());
  INTR_ASSIGN_OR_RETURN_GRPC(
      object_world::KinematicObject * robot,
      GetRobot(request->robot_specification().robot_reference(),
               object_world.get()),
      _.LogError());

  // Extract the starting joint positions of the robot.
  INTR_RETURN_IF_ERROR_GRPC(SetRobotStartConfigurationIfSetInRobotSpecification(
      request->robot_specification(), *robot));

  logger.Start("checking cache");
  logger.Attach("cache_hit_type", "no");
  INTR_ASSIGN_OR_RETURN_GRPC(
      MotionPlanningRequestCacheKey cache_key,
      MotionPlanningRequestCacheKey::Create(
          initial_world_and_proto.world_proto, *object_world,
          *request
          )
  );
  logger.Attach("cache_key_id", cache_key.uuid);
  bool runtime_enabled_distance_check_statistics =
      run_time_flags.has_value()
          ? run_time_flags->enable_distance_check_statistics
          : false;
  bool enable_collision_stats =
      mps_config_.flags.enable_distance_check_statistics ||
      runtime_enabled_distance_check_statistics;

  INTR_ASSIGN_OR_RETURN_GRPC(
      const double collision_check_spacing,
      GetCollisionCheckSpacing(mps_config_.flags,
                               request->motion_planner_config()));

  INTR_ASSIGN_OR_RETURN_GRPC(
      std::optional<MotionPlanner::PlanTrajectoryResult> plan_trajectory_result,
      LoadMotionFromNonvolatileCacheIfRequested(
          *motion_planner_, plan_trajectory_nonvolatile_cache_.get(),
          *object_world, *request, collision_check_spacing,
          optional_mps_asset_major_version)
  );

  if (plan_trajectory_result.has_value()) {
    INTR_RETURN_IF_ERROR_GRPC(
        ConvertPlanTrajectoryResultToTrajectoryPlanningResponse(
            plan_trajectory_result.value(), *request,
            initial_world_and_proto.world, cache_hit, enable_collision_stats,
            *robot,
            *response
            ));
    return grpc::Status::OK;
  }

  if (plan_trajectory_cache_ != nullptr) {
    absl::StatusOr<PlanTrajectoryCache::LookupResult> status_or_result =
        plan_trajectory_cache_->Lookup(cache_key);

    if (status_or_result.ok()) {
      // Initialize the variable
      PlanTrajectoryCache::LookupResult lookup_result =
          status_or_result.value();

      INTR_ASSIGN_OR_RETURN_GRPC(
          const bool valid_trajectory,
          lookup_result.HasValidTrajectory(
              *object_world, request->robot_specification(),
              request->motion_planner_config().collision_checker_config(),
              mps_config_
                  .max_diff_in_rad_for_starting_robot_configuration_threshold,
              /*allow_fuzzy_check=*/
              mps_config_.enable_motion_planner_service_caching_fuzzy_check &&
                  !request->motion_planner_config().skip_fuzzy_cache_check(),
              collision_check_spacing)
      );

      if (valid_trajectory) {
        // Log the cache hit and increase the cache hit count.
        LOG(INFO) << absl::StrFormat(
            *return_cache_format_string, lookup_result.cached_entry.key.uuid,
            cache_key.uuid, plan_trajectory_cache_->GetNumOfEntries());
        cache_hit = lookup_result.exact_match ? "exact" : "fuzzy";
        logger.Attach("cache_hit_type", cache_hit);
        logger.Attach("matched_cache_key_id",
                      lookup_result.cached_entry.key.uuid);

        INTR_RETURN_IF_ERROR_GRPC(
            ConvertPlanTrajectoryResultToTrajectoryPlanningResponse(
                lookup_result.cached_entry.result, *request,
                initial_world_and_proto.world, cache_hit,
                enable_collision_stats, *robot,
                *response
                ));

        INTR_ASSIGN_OR_RETURN_GRPC(
            std::optional<std::string> lock_motion_id,
            SaveMotionToNonvolatileCacheIfRequested(
                plan_trajectory_nonvolatile_cache_.get(), *object_world,
                *request, lookup_result.cached_entry.result,
                optional_mps_asset_major_version)
        );
        if (lock_motion_id.has_value()) {
          response->set_lock_motion_id(lock_motion_id.value());
        }
        // We only cache successful planning results.
        return grpc::Status::OK;
      } else {
        LOG(INFO)
            << "Found a cached entry but it does not have a valid trajectory.";
      }
    } else {
      LOG(INFO) << "Failed to find a cache match: "
                << status_or_result.status();
    }
  }

  logger.Start("planning");
  absl::StatusOr<MotionPlanner::PlanTrajectoryResult> result =
      motion_planner_->PlanTrajectory(
          *object_world, request->robot_specification(),
          request->motion_specification(), request->motion_planner_config(),
          run_time_flags);

  logger.Start("finalize");
  INTR_RETURN_IF_ERROR_GRPC(MaybeInsertMotionPlanningRequestCacheHit(
      cache_key, result, plan_trajectory_cache_.get()));

  if (result.ok()) {
    absl::Status status =
        ConvertPlanTrajectoryResultToTrajectoryPlanningResponse(
            result.value(), *request, initial_world_and_proto.world, cache_hit,
            enable_collision_stats, *robot,
            *response
        );
    if (!status.ok()) {
      return ToGrpcStatus(
          status
      );
    };

    INTR_ASSIGN_OR_RETURN_GRPC(
        std::optional<std::string> lock_motion_id,
        SaveMotionToNonvolatileCacheIfRequested(
            plan_trajectory_nonvolatile_cache_.get(), *object_world, *request,
            result.value(), optional_mps_asset_major_version)
    );
    if (lock_motion_id.has_value()) {
      response->set_lock_motion_id(lock_motion_id.value());
    }
  }

  return ToGrpcStatus(
      result.status()
  );
}

::grpc::Status MotionPlannerService::PlanTrajectory(
    ::grpc::ServerContext* context, const MotionPlanningRequest* request,
    intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse*
        response) {
  return PlanTrajectoryInternal(
      context, request, response,
      /*run_time_flags=*/std::nullopt);
}

::grpc::Status MotionPlannerService::PlanPath(
    ::grpc::ServerContext* context, const MotionPlanningRequest* request,
    intrinsic_proto::motion_planning::v1::PathPlanningResponse* response) {
  TimingDebugLogger logger("MotionPlannerService::PlanPath");
  std::string cache_hit = "no";

  std::optional<std::string> optional_mps_asset_version = std::nullopt;
  if (mps_asset_version_.has_value()) {
    optional_mps_asset_version = mps_asset_version_.value();
  } else {
    INTR_ASSIGN_OR_RETURN_GRPC(
        optional_mps_asset_version,
        GetMotionPlannerServiceAssetVersionFromServerContextIfProvided(
            *context));
  }
  // Download the world from the world service.
  logger.Start("downloading world");
  INTR_ASSIGN_OR_RETURN_GRPC(
      WorldAndProto initial_world_and_proto,
      GetInitialWorldFromWorldService(request->world_id()), _.LogError());
  // Create the object world view and robot.
  INTR_ASSIGN_OR_RETURN_GRPC(
      std::unique_ptr<object_world::ObjectWorld> object_world,
      GetObjectWorldView(initial_world_and_proto.world), _.LogError());
  INTR_ASSIGN_OR_RETURN_GRPC(
      object_world::KinematicObject * robot,
      GetRobot(request->robot_specification().robot_reference(),
               object_world.get()),
      _.LogError());

  // Extract the starting joint positions of the robot.
  INTR_RETURN_IF_ERROR_GRPC(SetRobotStartConfigurationIfSetInRobotSpecification(
      request->robot_specification(), *robot));

  if (request->has_motion_planner_config() &&
      request->motion_planner_config().has_lock_motion_configuration()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "Lock motion is not supported for path planning. Use PlanTrajectory "
        "instead."));
  }

  if (request->has_motion_planner_config() &&
      request->motion_planner_config().has_skip_fuzzy_cache_check()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "Skip fuzzy cache check is not supported for path planning, because "
        "there is no cache for path planning. Use PlanTrajectory instead if "
        "you require caching."));
  }

  logger.Start("planning");
  absl::StatusOr<MotionPlanner::PlanPathResult> result =
      motion_planner_->PlanPath(*object_world, request->robot_specification(),
                                request->motion_specification(),
                                request->motion_planner_config(),
                                /*run_time_flags=*/std::nullopt);

  logger.Start("finalize");
  if (result.ok()) {
    // Fill out the response path. We explicitly allow empty paths, for
    // cases in which we already are at the desired location.
    std::vector<eigenmath::VectorXd> path =
        PathSegmentsToPath(result.value().path_segments);
    intrinsic_proto::motion_planning::v1::Path path_proto;
    ToJointVecs(path, path_proto.mutable_waypoints());
    *response->mutable_path() = path_proto;
    // Calculate and fill in the swept volume
    if (request->compute_swept_volume()) {
      INTR_RETURN_IF_ERROR_GRPC(ComputeSweptVolumeForPathAndAssignToResponse(
          initial_world_and_proto.world, robot->GetRobotEntityId(),
          geometry_library_->Serializer(), path, response));
    }
  }

  return ToGrpcStatus(
      result.status()
  );
}

::grpc::Status MotionPlannerService::ComputeIk(
    ::grpc::ServerContext* context,
    const intrinsic_proto::motion_planning::v1::IkRequest* request,
    intrinsic_proto::motion_planning::v1::IkResponse* response) {
  // Return error early for invalid max_num_solutions before doing any further
  // computations.
  if (request->max_num_solutions() < 0) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        absl::StrCat("max_num_solutions must be non-negative. Got ",
                     request->max_num_solutions())));
  }
  // Get maximum number of IK solutions that should be sampled for each pose
  // from the request. Defaults to 8 if not provided.
  const int max_num_solutions =
      request->max_num_solutions() == 0 ? 8 : request->max_num_solutions();

  // Download the world from the world service.
  INTR_ASSIGN_OR_RETURN_GRPC(
      WorldAndProto initial_world_and_proto,
      GetInitialWorldFromWorldService(request->world_id()));

  INTR_ASSIGN_OR_RETURN_GRPC(
      std::unique_ptr<object_world::ObjectWorld> object_world,
      GetObjectWorldView(initial_world_and_proto.world));

  // Unpack the robot information from the request.
  INTR_ASSIGN_OR_RETURN_GRPC(
      object_world::KinematicObject * robot,
      GetRobot(request->robot_reference(), object_world.get()));

  // Set dof values for the robot if provided.
  if (request->has_starting_joints()) {
    eigenmath::VectorXd starting_joints =
        RepeatedDoubleToVectorXd(request->starting_joints().joints());
    INTR_RETURN_IF_ERROR_GRPC(robot->SetJointPositions(starting_joints));
  }

  // Keep proxy construction and usage within this block so that we don't
  // incur the cost of setting up the proxy if collision checking is not going
  // to be used.
  const bool collision_checking_enabled =
      request->has_collision_settings()
          ? !request->collision_settings().disable_collision_checking()
          : false;

  intrinsic_proto::RuleSet rule_set;
  if (collision_checking_enabled) {
    INTR_ASSIGN_OR_RETURN_GRPC(
        rule_set, MakeRuleSet(request->collision_settings(), *object_world));
  }
  // TODO(b/338108549): We only use the proxy for validation, not FK/IK. It
  // should not be necessary to require non branching structures.
  INTR_ASSIGN_OR_RETURN_GRPC(
      std::unique_ptr<KinematicsSystemProxy> proxy,
      GetProxyForPathPlanning(
          *object_world, *robot, request->collision_checker_config(), rule_set,
          /*require_ik_and_fk=*/true, !collision_checking_enabled));
  if (!proxy) {
    return ToGrpcStatus(absl::InternalError(
        "An unexpected error occurred during ComputeIk. No "
        "validation proxy available for inverse kinematic solution "
        "validation."));
  }
  ComputeIkDebugInformation ik_debug_info;
  auto request_result = GetCollisionFreeIkSolutions(
      *object_world, *proxy, *robot, request->target(),
      {
          .max_num_solutions = max_num_solutions,
          .ensure_same_branch = request->ensure_same_branch(),
          .prefer_same_branch = request->prefer_same_branch(),
          .disable_error_on_collisions = request->disable_error_on_collisions(),
      },
      &ik_debug_info);

  if (!request_result.ok()) {
    return ToGrpcStatus(AddTypeUrlPrefixToPayload(request_result.status()));
  }
  std::vector<eigenmath::VectorXd> ik_solutions = request_result.value();

  for (const auto& solution : ik_solutions) {
    auto* solution_proto = response->add_solutions();
    VectorXdToRepeatedDouble(solution, solution_proto->mutable_joints());
  }
  return grpc::Status::OK;
}

::grpc::Status MotionPlannerService::ComputeFk(
    ::grpc::ServerContext* context,
    const intrinsic_proto::motion_planning::v1::FkRequest* request,
    intrinsic_proto::motion_planning::v1::FkResponse* response) {
  // Download the world from the world service.
  INTR_ASSIGN_OR_RETURN_GRPC(
      WorldAndProto initial_world_and_proto,
      GetInitialWorldFromWorldService(request->world_id()));
  INTR_ASSIGN_OR_RETURN_GRPC(
      std::unique_ptr<object_world::ObjectWorld> object_world,
      GetObjectWorldView(initial_world_and_proto.world));

  // Unpack the robot information from the request.
  INTR_ASSIGN_OR_RETURN_GRPC(
      object_world::KinematicObject * robot,
      GetRobot(request->robot_reference(), object_world.get()));

  // Set dof values for the robot if provided.
  if (request->has_joints()) {
    eigenmath::VectorXd starting_joints =
        RepeatedDoubleToVectorXd(request->joints().joints());
    INTR_RETURN_IF_ERROR_GRPC(robot->SetJointPositions(starting_joints));
  }

  // Unpack the target data.
  INTR_ASSIGN_OR_RETURN_GRPC(
      const object_world::TransformNode* reference,
      GetTransformNodeByReference(*object_world, request->reference()));
  INTR_ASSIGN_OR_RETURN_GRPC(
      const object_world::TransformNode* target,
      GetTransformNodeByReference(*object_world, request->target()));

  // Compute the transform.
  INTR_ASSIGN_OR_RETURN_GRPC(auto reference_t_target,
                             reference->GetTransform(target));

  // Populate the result.
  *response->mutable_reference_t_target() = ToProto(reference_t_target);

  return grpc::Status::OK;
}

::grpc::Status MotionPlannerService::CheckCollisions(
    ::grpc::ServerContext* context,
    const intrinsic_proto::motion_planning::v1::CheckCollisionsRequest* request,
    intrinsic_proto::motion_planning::v1::CheckCollisionsResponse* response) {
  // Download the world from the world service.
  INTR_ASSIGN_OR_RETURN_GRPC(
      WorldAndProto initial_world_and_proto,
      GetInitialWorldFromWorldService(request->world_id()));
  INTR_ASSIGN_OR_RETURN_GRPC(auto object_world,
                             GetObjectWorldView(initial_world_and_proto.world));

  intrinsic_proto::RuleSet rule_set;
  if (!request->collision_settings().disable_collision_checking()) {
    INTR_ASSIGN_OR_RETURN_GRPC(
        rule_set, MakeRuleSet(request->collision_settings(), *object_world));
  }

  // Unpack the robot information from the request.
  INTR_ASSIGN_OR_RETURN_GRPC(
      const object_world::KinematicObject* robot,
      GetRobot(request->robot_reference(), object_world.get()));
  INTR_ASSIGN_OR_RETURN_GRPC(
      std::unique_ptr<KinematicsSystemProxy> proxy,
      CreateKinematicsProxyWithConfig(
          initial_world_and_proto.world, robot->GetRobotEntityId(),
          request->collision_checker_config(),
          /*constraints=*/{}, rule_set,
          /*disable_collision_checking=*/
          request->collision_settings().disable_collision_checking()));

  if (request->waypoint_size() == 0) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "The input path must contain at least one joint configuration."));
  }

  if (request->waypoint_size() == 1) {
    // Single point validation
    INTR_ASSIGN_OR_RETURN_GRPC(
        auto result, CheckLimitsAndCollisionsForConfiguration(
                         *proxy, ToVectorXds(request->waypoint()).front()));
    response->set_has_collision(!result.is_valid);
    response->set_collision_debug_msg(result.debug_message);
  } else {
    // Path validation
    INTR_ASSIGN_OR_RETURN_GRPC(
        auto result, CheckLimitsAndCollisionsForPointPath(
                         *proxy, ToVectorXds(request->waypoint()),
                         mps_config_.flags.default_collision_check_spacing));
    response->set_has_collision(!result.is_valid);
    response->set_collision_debug_msg(result.debug_message);
  }

  return grpc::Status::OK;
}
::grpc::Status MotionPlannerService::ClearCache(
    ::grpc::ServerContext* context, const google::protobuf::Empty* request,
    google::protobuf::Empty* response) {
  if (plan_trajectory_cache_ != nullptr) {
    LOG(INFO) << "Clearing PlanTrajectory caches.";
    plan_trajectory_cache_->ClearCache();
  } else {
    LOG(INFO) << "PlanTrajectory cache is null. Skip ClearCache.";
  }
  return grpc::Status::OK;
}
}  // namespace intrinsic
