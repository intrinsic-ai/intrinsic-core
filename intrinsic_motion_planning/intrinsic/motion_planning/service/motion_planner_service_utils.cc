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

#include "intrinsic/motion_planning/service/motion_planner_service_utils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "grpcpp/channel.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/logging/structured_logging_client.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_flags.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/skills/internal/world_service_utils.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/util/collision_world_util.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/service/world_service.grpc.pb.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

absl::StatusOr<MotionPlanner::PlanTrajectoryResult>
PlanTrajectoryImplMotionPlannerResponse(
    const World& initial_world,
    const intrinsic_proto::motion_planning::v1::MotionPlanningRequest& request,
    const MotionPlannerFlags& flags) {
  INTR_ASSIGN_OR_RETURN(auto motion_planner, MotionPlanner::Create(flags));
  INTR_ASSIGN_OR_RETURN(auto object_world,
                        object_world::ObjectWorld::CreateView(initial_world));
  INTR_ASSIGN_OR_RETURN(
      const MotionPlanner::PlanTrajectoryResult planning_result,
      motion_planner->PlanTrajectory(
          *object_world, request.robot_specification(),
          request.motion_specification(), request.motion_planner_config(),
          /*run_time_flags=*/std::nullopt));

  return planning_result;
}

absl::StatusOr<JointTrajectoryPVA> PlanTrajectoryImpl(
    const World& initial_world,
    const intrinsic_proto::motion_planning::v1::MotionPlanningRequest& request,
    const MotionPlannerFlags& flags) {
  INTR_ASSIGN_OR_RETURN(
      const MotionPlanner::PlanTrajectoryResult planning_result,
      PlanTrajectoryImplMotionPlannerResponse(initial_world, request, flags));
  return planning_result.trajectory;
}

absl::StatusOr<intrinsic_proto::motion_planning::v1::MotionPlanningRequest>
CreateMotionPlanningRequestFromPointPath(
    absl::Span<const eigenmath::VectorXd> point_path,
    absl::Span<
        const intrinsic_proto::motion_planning::v1::MotionSegment::MotionType>
        motion_types,
    absl::string_view robot_name) {
  if (point_path.size() != motion_types.size()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The size of `point_path` and `motion_types` must be the "
        "same, but got ",
        point_path.size(), " and ", motion_types.size(), ", respectively."));
  }
  if (point_path.empty()) {
    return absl::InvalidArgumentError(
        "The size of `point_path` must be greater than 0.");
  }

  intrinsic_proto::motion_planning::v1::MotionPlanningRequest request;
  request.mutable_robot_specification()
      ->mutable_robot_reference()
      ->mutable_object_id()
      ->mutable_by_name()
      ->set_object_name(robot_name);
  for (size_t i = 0; i < point_path.size(); ++i) {
    intrinsic_proto::motion_planning::v1::MotionSegment* motion_segment =
        request.mutable_motion_specification()->add_motion_segments();
    motion_segment->set_motion_type(motion_types[i]);
    VectorXdToRepeatedDouble(point_path[i], motion_segment->mutable_target()
                                                ->mutable_joint_position()
                                                ->mutable_joints());
  }
  return request;
}

absl::StatusOr<
    std::unique_ptr<intrinsic_proto::world::ObjectWorldService::Stub>>
CreateObjectWorldServiceStub(const std::string& world_service_address,
                             const absl::Duration& grpc_connect_timeout) {
  INTR_ASSIGN_OR_RETURN(
      const std::shared_ptr<grpc::Channel> channel,
      connect::CreateClientChannel(world_service_address,
                                   absl::Now() + grpc_connect_timeout));
  return intrinsic_proto::world::ObjectWorldService::NewStub(channel);
}

absl::StatusOr<
    std::unique_ptr<intrinsic_proto::world::internal::WorldService::Stub>>
CreateWorldServiceStub(const std::string& world_service_address,
                       const absl::Duration& grpc_connect_timeout) {
  INTR_ASSIGN_OR_RETURN(
      const std::shared_ptr<grpc::Channel> world_service_channel,
      connect::CreateClientChannel(world_service_address,
                                   absl::Now() + grpc_connect_timeout));
  return intrinsic_proto::world::internal::WorldService::NewStub(
      world_service_channel);
}

absl::StatusOr<
    std::unique_ptr<intrinsic_proto::geometry::GeometryService::Stub>>
CreateGeometryServiceStub(const std::string& geometry_service_address,
                          const absl::Duration& grpc_connect_timeout) {
  INTR_ASSIGN_OR_RETURN(
      const std::shared_ptr<grpc::Channel> channel,
      connect::CreateClientChannel(
          geometry_service_address, absl::Now() + grpc_connect_timeout,
          connect::UnlimitedMessageSizeGrpcChannelArgs()));
  return intrinsic_proto::geometry::GeometryService::NewStub(channel);
}

StructuredLoggingClientAndStub CreateStructuredLoggingClient(
    const std::string& data_logger_service_address,
    const absl::Duration& grpc_connect_timeout) {
  StructuredLoggingClientAndStub result{nullptr, nullptr};
  if (data_logger_service_address.empty()) {
    return result;
  }
  auto channel_or = connect::CreateClientChannel(
      data_logger_service_address, absl::Now() + grpc_connect_timeout,
      connect::UnlimitedMessageSizeGrpcChannelArgs());
  if (!channel_or.ok()) {
    LOG(WARNING) << "Failed to connect to data logger: " << channel_or.status();
    return result;
  }
  result.client = std::make_unique<StructuredLoggingClient>(*channel_or);
  result.stub = intrinsic_proto::data_logger::DataLogger::NewStub(*channel_or);
  return result;
}

StructuredLoggingClientAndStub CreateStructuredLoggingClient(
    std::shared_ptr<grpc::Channel> channel) {
  StructuredLoggingClientAndStub result{nullptr, nullptr};
  if (channel == nullptr) {
    return result;
  }
  result.client = std::make_unique<StructuredLoggingClient>(channel);
  result.stub = intrinsic_proto::data_logger::DataLogger::NewStub(channel);
  return result;
}

absl::Status AttemptToPreCacheTheWorldData(
    intrinsic_proto::world::ObjectWorldService::StubInterface&
        object_world_service,
    GeometryLibrary& geometry_library) {
  // Download the world proto and seed the GeometryLibrary with the geometry in
  // it
  constexpr const absl::string_view kDefaultWorldId = "world";
  INTR_ASSIGN_OR_RETURN(
      World default_world,
      skills::DownloadWorldFromObjectWorldService(
          kDefaultWorldId, object_world_service, &geometry_library));

  // Go through each entity with a geometry component and attempt to pre-cache
  // the computation for preparing distance checks.
  //
  // TODO(b/491556123): If the CoalCollisionChecker is active, we don't need to
  // do this. This can save us some time and RAM.
  LOG(INFO) << "Starting to pre-cache the geometry data from the default world";
  absl::Time pre_cache_start = absl::Now();
  const auto entities = default_world.GetTypedEntityIds<GeometryEntityId>();
  for (const auto& entity_id : entities) {
    auto unmerged_local_set = default_world.GetLocallyTransformedShapes(
        PhysicalEntityId(entity_id.value()), /*merge=*/false);
    auto merged_local_set = default_world.GetLocallyTransformedShapes(
        PhysicalEntityId(entity_id.value()), /*merge=*/true);
    auto spacial_tree = default_world.GetSpatialTreeInModelSpace(
        PhysicalEntityId(entity_id.value()));

    // We don't actually need to use the results, the act of getting them will
    // cache the computation of the prepared geometry.
    (void)unmerged_local_set;
    (void)merged_local_set;
    (void)spacial_tree;
  }
  absl::Time pre_cache_end = absl::Now();
  LOG(INFO) << "Finished pre-caching " << entities.size()
            << " geometry data from the default world in "
            << absl::ToDoubleSeconds(pre_cache_end - pre_cache_start)
            << " seconds.";

  return absl::OkStatus();
}

void FromProto(
    const intrinsic_proto::motion_planning::v1::MotionPlannerFlags& proto_flags,
    MotionPlannerFlags& flags) {
  if (proto_flags.has_enable_path_refinement_validation_step()) {
    flags.enable_path_refinement_validation_step =
        proto_flags.enable_path_refinement_validation_step();
  }
  if (proto_flags.has_path_refinement_validation_margin_relative_factor()) {
    flags.path_refinement_validation_margin_relative_factor =
        proto_flags.path_refinement_validation_margin_relative_factor();
  }
  if (proto_flags.has_path_refinement_validation_margin_absolute_factor()) {
    flags.path_refinement_validation_margin_absolute_factor =
        proto_flags.path_refinement_validation_margin_absolute_factor();
  }
  if (proto_flags.has_enable_distance_check_statistics()) {
    flags.enable_distance_check_statistics =
        proto_flags.enable_distance_check_statistics();
  }
  if (proto_flags.has_enable_concurrent_collision_checking()) {
    flags.enable_concurrent_collision_checking =
        proto_flags.enable_concurrent_collision_checking();
  }
  if (proto_flags.has_concurrent_collision_checking_thread_count()) {
    flags.concurrent_collision_checking_thread_count =
        proto_flags.concurrent_collision_checking_thread_count();
  }
  if (proto_flags.has_enable_fallback_trajectory()) {
    flags.enable_fallback_trajectory = proto_flags.enable_fallback_trajectory();
  }
  if (proto_flags.has_enable_fallback_trajectory_local_adjust()) {
    flags.enable_fallback_trajectory_local_adjust =
        proto_flags.enable_fallback_trajectory_local_adjust();
  }
  if (proto_flags.has_default_collision_check_spacing()) {
    flags.default_collision_check_spacing =
        proto_flags.default_collision_check_spacing();
  }
}

intrinsic_proto::motion_planning::v1::MotionPlannerFlags ToProto(
    const MotionPlannerFlags& flags) {
  intrinsic_proto::motion_planning::v1::MotionPlannerFlags proto_flags;
  proto_flags.set_enable_path_refinement_validation_step(
      flags.enable_path_refinement_validation_step);
  proto_flags.set_path_refinement_validation_margin_relative_factor(
      flags.path_refinement_validation_margin_relative_factor);
  proto_flags.set_path_refinement_validation_margin_absolute_factor(
      flags.path_refinement_validation_margin_absolute_factor);
  proto_flags.set_enable_distance_check_statistics(
      flags.enable_distance_check_statistics);
  proto_flags.set_enable_concurrent_collision_checking(
      flags.enable_concurrent_collision_checking);
  proto_flags.set_concurrent_collision_checking_thread_count(
      flags.concurrent_collision_checking_thread_count);
  proto_flags.set_enable_fallback_trajectory(flags.enable_fallback_trajectory);
  proto_flags.set_enable_fallback_trajectory_local_adjust(
      flags.enable_fallback_trajectory_local_adjust);
  proto_flags.set_default_collision_check_spacing(
      flags.default_collision_check_spacing);
  return proto_flags;
}

}  // namespace intrinsic
