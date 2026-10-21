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

#include "intrinsic/motion_planning/service/motion_planner_service_base_test_fixture.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/support/channel_arguments.h"
#include "internal/testing.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/geometry/service/in_memory_geometry_service.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_service_storage.h"
#include "intrinsic/icon/proto/joint_trajectory_conversion.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/logging/structured_logging_client.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_flags.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy_util.h"
#include "intrinsic/motion_planning/proto/motion_planner_service_proto_utils.h"
#include "intrinsic/motion_planning/proto/v1/compute_ik.pb.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.grpc.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/service/cache/motion_planner_cache.h"
#include "intrinsic/motion_planning/service/motion_planner_service.h"
#include "intrinsic/motion_planning/service/motion_planner_service_asset_utils.h"
#include "intrinsic/motion_planning/service/motion_planner_service_proxy.h"
#include "intrinsic/motion_planning/service/motion_planner_service_utils.h"
#include "intrinsic/motion_planning/service/nonvolatile_cache/motion_planner_nonvolatile_cache.h"
#include "intrinsic/motion_planning/skills/motion_planning_util.h"
#include "intrinsic/skills/internal/world_service_utils.h"
#include "intrinsic/storage/content_addressable_storage/testing/in_memory_cas_service.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/robot_component.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/service/test/world_service_fake.h"
#include "intrinsic/world/world.h"
#include "intrinsic/world/world.pb.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic {

using ::absl_testing::StatusIs;
using ::protobuf_matchers::EqualsProto;
using ::protobuf_matchers::proto::IgnoringFieldPaths;
using ::protobuf_matchers::proto::IgnoringRepeatedFieldOrdering;
using ::testing::HasSubstr;

using ::google::protobuf::Empty;
using ::intrinsic_proto::motion_planning::v1::CheckCollisionsRequest;
using ::intrinsic_proto::motion_planning::v1::CheckCollisionsResponse;
using ::intrinsic_proto::motion_planning::v1::FkRequest;
using ::intrinsic_proto::motion_planning::v1::FkResponse;
using ::intrinsic_proto::motion_planning::v1::IkRequest;
using ::intrinsic_proto::motion_planning::v1::IkResponse;
using ::intrinsic_proto::motion_planning::v1::MotionPlanningRequest;
using ::intrinsic_proto::motion_planning::v1::PathPlanningResponse;
using ::intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse;
intrinsic_proto::motion_planning::v1::MotionPlanningRequest UpdateJerkLimits(
    const intrinsic_proto::motion_planning::v1::MotionPlanningRequest&
        motion_planning_request,
    bool set_infinite_jerk_limits) {
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest request =
      motion_planning_request;
  const double kInfiniteJerk = std::numeric_limits<double>::infinity();
  const int ndofs = request.motion_specification()
                        .motion_segments(0)
                        .target()
                        .joint_position()
                        .joints_size();
  for (int seg_id = 0;
       seg_id < request.motion_specification().motion_segments().size();
       ++seg_id) {
    for (int dof_id = 0; dof_id < ndofs; ++dof_id) {
      request.mutable_motion_specification()
          ->mutable_motion_segments()
          ->at(seg_id)
          .mutable_joint_limits()
          ->mutable_max_jerk()
          ->set_values(dof_id, kInfiniteJerk);
    }
  }

  return request;
}

absl::Status SetCartesianLimitsInWorld(World& world,
                                       std::string_view robot_name,
                                       const CartesianLimits& cart_limits) {
  for (const auto& robot_id :
       world.GetTypedEntityIds<RobotCollectionsEntityId>()) {
    INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                          world.GetEntityById(robot_id));
    if (entity->GetLocalName() == robot_name ||
        entity->GetAlias() == robot_name) {
      INTR_ASSIGN_OR_RETURN(
          RobotComponent * component,
          world.GetComponentByEntityId<RobotComponent>(robot_id));
      INTR_RETURN_IF_ERROR(component->SetCartesianLimits(cart_limits));

      return absl::OkStatus();
    }
  }
  return absl::NotFoundError(
      absl::StrCat("Robot ", robot_name, " not found in world."));
}

absl::StatusOr<WorldHashMap<std::string, World>> CreateDefault4RobotsCubeWorld(
    bool set_infinite_jerk_limits) {
  WorldHashMap<std::string, World> worlds;
  const std::string world_gzf_filename =
      PathResolver::ResolveRunfilesPathForTest(
          "intrinsic/world/test_data/4_robots_cube_object_world.gzf");
  INTR_ASSIGN_OR_RETURN(auto gz_file, GZFile::Open(world_gzf_filename));
  INTR_ASSIGN_OR_RETURN(World world, World::FromFile(*gz_file));
  INTR_RETURN_IF_ERROR(SetCartesianLimitsInWorld(
      world, "agilus_04",
      ::intrinsic::motion_planning::CreateDefaultCartLimits(
          set_infinite_jerk_limits)));
  INTR_RETURN_IF_ERROR(SetCartesianLimitsInWorld(
      world, "iris622",
      ::intrinsic::motion_planning::CreateDefaultCartLimits(
          set_infinite_jerk_limits)));
  worlds.emplace("world", std::move(world));
  return worlds;
}

void MotionPlannerServiceBaseTest::Init(
    bool use_nonvolatile_cache, MotionPlannerFlags extra_flags,
    WorldHashMap<std::string, World> worlds_by_name,
    const StructuredLoggingClient* logger_client) {
  geometry_service_ = std::make_unique<InMemoryGeometryService>();
  cas_service_ = std::make_unique<InMemoryCasService>();

  int port = 0;
  grpc::ServerBuilder builder;
  builder.AddListeningPort("dns:///localhost:0",
                           grpc::experimental::LocalServerCredentials(
                               grpc_local_connect_type::LOCAL_TCP),
                           &port);
  builder.RegisterService(geometry_service_.get());
  builder.RegisterService(cas_service_.get());
  builder.SetMaxSendMessageSize(-1);
  builder.SetMaxReceiveMessageSize(-1);
  geometry_and_cas_server_ = builder.BuildAndStart();
  const std::string cas_address = absl::StrCat("dns:///localhost:", port);

  geometry_service_stub_ = intrinsic_proto::geometry::GeometryService::NewStub(
      geometry_and_cas_server_->InProcessChannel(
          connect::UnlimitedMessageSizeGrpcChannelArgs()));

  auto geo_lib =
      GetGeometryServiceGeometryLibrary(geometry_service_stub_.get());

  ASSERT_OK_AND_ASSIGN(
      fake_world_service_,
      FakeWorldService::Create(std::move(worlds_by_name), std::move(geo_lib),
                               FakeWorldService::CreateOptions{}));

  object_world_service_stub_ = fake_world_service_->NewObjectStub();
  fake_world_service_geo_library_ = fake_world_service_->GetGeometryLibrary();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<PlanTrajectoryCache> cache,
                       PlanTrajectoryCache::Create(kDefaultCacheSize, 5));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<PlanTrajectoryCache> cache_with_asset_version,
      PlanTrajectoryCache::Create(kDefaultCacheSize, 5));
  // The nonvolatile cache is null by default.
  std::unique_ptr<MotionPlannerNonvolatileCache>
      plan_trajectory_nonvolatile_cache = nullptr;
  std::unique_ptr<MotionPlannerNonvolatileCache>
      plan_trajectory_with_asset_version_nonvolatile_cache = nullptr;
  if (use_nonvolatile_cache) {
    cas_service_stub_ = intrinsic_proto::content_addressable_storage::v1::
        ContentAddressableStorageService::NewStub(
            geometry_and_cas_server_->InProcessChannel(
                connect::UnlimitedMessageSizeGrpcChannelArgs()));
    ASSERT_OK_AND_ASSIGN(
        plan_trajectory_nonvolatile_cache,
        MotionPlannerNonvolatileCache::CreateWithContentAddressableStorage(
            kDefaultCacheSize, cas_address));
    ASSERT_OK_AND_ASSIGN(
        plan_trajectory_with_asset_version_nonvolatile_cache,
        MotionPlannerNonvolatileCache::CreateWithContentAddressableStorage(
            kDefaultCacheSize, cas_address));
  }
  ASSERT_OK_AND_ASSIGN(
      motion_planner_service_,
      MotionPlannerService::Create(
          object_world_service_stub_.get(),
          fake_world_service_geo_library_.get(),
          /*mps_asset_id_name=*/kMotionPlannerServiceName,
          /*plan_trajectory_cache=*/std::move(cache),
          /*plan_trajectory_nonvolatile_cache=*/
          std::move(plan_trajectory_nonvolatile_cache),
          /*mps_config=*/
          {
              .enable_motion_planner_service_caching_fuzzy_check = true,
              .workcell_name = std::string(kTestWorkcellName),
              .organization_id = std::string(kTestOrgId),
              .flags = extra_flags,
          },
          /*mps_asset_version=*/std::nullopt, logger_client));

  server_ = grpc::ServerBuilder()
                .RegisterService(motion_planner_service_.get())
                .BuildAndStart();

  std::unique_ptr<
      intrinsic_proto::motion_planning::v1::MotionPlannerService::StubInterface>
      mps_stub_for_mps_proxy =
          intrinsic_proto::motion_planning::v1::MotionPlannerService::NewStub(
              server_->InProcessChannel(grpc::ChannelArguments()));

  ASSERT_OK_AND_ASSIGN(
      motion_planner_service_proxy_,
      MotionPlannerServiceProxy::Create(std::move(mps_stub_for_mps_proxy),
                                        kTestMotionPlannerServiceAssetVersion));

  mps_proxy_server_ = grpc::ServerBuilder()
                          .RegisterService(motion_planner_service_proxy_.get())
                          .BuildAndStart();

  ASSERT_OK_AND_ASSIGN(
      motion_planner_service_with_asset_version_,
      MotionPlannerService::Create(
          object_world_service_stub_.get(),
          fake_world_service_geo_library_.get(),
          /*mps_asset_id_name=*/kMotionPlannerServiceName,
          /*plan_trajectory_cache=*/std::move(cache_with_asset_version),
          /*plan_trajectory_nonvolatile_cache=*/
          std::move(plan_trajectory_with_asset_version_nonvolatile_cache),
          /*mps_config=*/
          {
              .enable_motion_planner_service_caching_fuzzy_check = true,
              .workcell_name = std::string(kTestWorkcellName),
              .organization_id = std::string(kTestOrgId),
              .flags = extra_flags,
          },
          /*mps_asset_version=*/
          std::string(kOtherMotionPlannerServiceAssetVersion), logger_client));

  mps_with_asset_version_server_ =
      grpc::ServerBuilder()
          .RegisterService(motion_planner_service_with_asset_version_.get())
          .BuildAndStart();

  motion_planner_service_stub_ =
      intrinsic_proto::motion_planning::v1::MotionPlannerService::NewStub(
          server_->InProcessChannel(grpc::ChannelArguments()));

  motion_planner_service_proxy_stub_ =
      intrinsic_proto::motion_planning::v1::MotionPlannerService::NewStub(
          mps_proxy_server_->InProcessChannel(grpc::ChannelArguments()));

  motion_planner_service_with_asset_version_stub_ =
      intrinsic_proto::motion_planning::v1::MotionPlannerService::NewStub(
          mps_with_asset_version_server_->InProcessChannel(
              grpc::ChannelArguments()));

  // Clear the cache before each test.
  const Empty clear_cache_request;
  ASSERT_OK(ClearCache(clear_cache_request));
}

// Applies to
// `PlanTrajectory` and `PlanPath`.
#define EXECUTE_COMPARE_AND_RETURN_RPC_RESPONSE_SPECIFIED_FIELD_ORDER_IGNORE(  \
    MethodName, RequestType, ResponseType)                                     \
  absl::StatusOr<ResponseType> MotionPlannerServiceBaseTest::MethodName(       \
      const RequestType& request,                                              \
      const std::initializer_list<std::string>& field_ignore_list,             \
      const std::initializer_list<std::string>&                                \
          repeated_field_ordering_ignore_list) {                               \
    /* Execute RPC on a `MotionPlannerService` instance. */                    \
    grpc::ClientContext mps_context;                                           \
    ResponseType mps_response;                                                 \
    auto mps_status = ToAbslStatus(motion_planner_service_stub_->MethodName(   \
        &mps_context, request, &mps_response));                                \
                                                                               \
    /* Execute RPC on a `MotionPlannerServiceProxy` instance. */               \
    grpc::ClientContext mps_proxy_context;                                     \
    ResponseType mps_proxy_response;                                           \
    auto mps_proxy_status =                                                    \
        ToAbslStatus(motion_planner_service_proxy_stub_->MethodName(           \
            &mps_proxy_context, request, &mps_proxy_response));                \
                                                                               \
    /* Testing to make sure that `MotionPlannerService` and                    \
       `MotionPlannerServiceProxy` behaves the same when given the same        \
       `request`. */                                                           \
    if (!mps_status.ok()) {                                                    \
      EXPECT_THAT(                                                             \
          mps_proxy_status,                                                    \
          StatusIs(mps_status.code(),                                          \
                   HasSubstr(mps_status.message().substr(                      \
                       0, kNumNFirstCharactersThatMustMatchInErrorMessage)))); \
    } else {                                                                   \
      /* `TreatingFieldsAsUnordered` matcher is not supported, so we have to   \
         split into two matcher calls below. */                                \
      EXPECT_THAT(                                                             \
          mps_proxy_response,                                                  \
          IgnoringFieldPaths(field_ignore_list, EqualsProto(mps_response)));   \
      EXPECT_THAT(mps_proxy_response,                                          \
                  IgnoringRepeatedFieldOrdering(                               \
                      IgnoringFieldPaths(repeated_field_ordering_ignore_list,  \
                                         EqualsProto(mps_response))));         \
    }                                                                          \
    INTR_RETURN_IF_ERROR(mps_status);                                          \
    return mps_response;                                                       \
  }

EXECUTE_COMPARE_AND_RETURN_RPC_RESPONSE_SPECIFIED_FIELD_ORDER_IGNORE(
    PlanTrajectory, MotionPlanningRequest, TrajectoryPlanningResponse);

EXECUTE_COMPARE_AND_RETURN_RPC_RESPONSE_SPECIFIED_FIELD_ORDER_IGNORE(
    PlanPath, MotionPlanningRequest, PathPlanningResponse);
#undef EXECUTE_COMPARE_AND_RETURN_RPC_RESPONSE_SPECIFIED_FIELD_ORDER_IGNORE

#define EXECUTE_COMPARE_AND_RETURN_RPC_RESPONSE_SPECIFIED_FIELD_IGNORE(        \
    MethodName, RequestType, ResponseType, FieldIgnoreList)                    \
  absl::StatusOr<ResponseType> MotionPlannerServiceBaseTest::MethodName(       \
      const RequestType& request, const bool compare_against_mps_proxy) {      \
    /* Execute RPC on a `MotionPlannerService` instance. */                    \
    grpc::ClientContext mps_context;                                           \
    ResponseType mps_response;                                                 \
    auto mps_status = ToAbslStatus(motion_planner_service_stub_->MethodName(   \
        &mps_context, request, &mps_response));                                \
                                                                               \
    if (compare_against_mps_proxy) {                                           \
      /* Execute RPC on a `MotionPlannerServiceProxy` instance. */             \
      grpc::ClientContext mps_proxy_context;                                   \
      ResponseType mps_proxy_response;                                         \
      auto mps_proxy_status =                                                  \
          ToAbslStatus(motion_planner_service_proxy_stub_->MethodName(         \
              &mps_proxy_context, request, &mps_proxy_response));              \
                                                                               \
      /* Testing to make sure that `MotionPlannerService` and                  \
         `MotionPlannerServiceProxy` behaves the same when given the same      \
         `request`. */                                                         \
      if (!mps_status.ok()) {                                                  \
        EXPECT_THAT(                                                           \
            mps_proxy_status,                                                  \
            StatusIs(mps_status.code(), HasSubstr(mps_status.message())));     \
      } else {                                                                 \
        EXPECT_THAT(                                                           \
            mps_proxy_response,                                                \
            IgnoringFieldPaths((FieldIgnoreList), EqualsProto(mps_response))); \
      }                                                                        \
    }                                                                          \
    INTR_RETURN_IF_ERROR(mps_status);                                          \
    return mps_response;                                                       \
  }

EXECUTE_COMPARE_AND_RETURN_RPC_RESPONSE_SPECIFIED_FIELD_IGNORE(
    ComputeIk, IkRequest, IkResponse,
    std::initializer_list<std::string>({
    }));

EXECUTE_COMPARE_AND_RETURN_RPC_RESPONSE_SPECIFIED_FIELD_IGNORE(
    ComputeFk, FkRequest, FkResponse, std::initializer_list<std::string>({}));

EXECUTE_COMPARE_AND_RETURN_RPC_RESPONSE_SPECIFIED_FIELD_IGNORE(
    CheckCollisions, CheckCollisionsRequest, CheckCollisionsResponse,
    std::initializer_list<std::string>({}));
EXECUTE_COMPARE_AND_RETURN_RPC_RESPONSE_SPECIFIED_FIELD_IGNORE(
    ClearCache, Empty, Empty, std::initializer_list<std::string>({}));

#undef EXECUTE_COMPARE_AND_RETURN_RPC_RESPONSE_SPECIFIED_FIELD_IGNORE

absl::StatusOr<TrajectoryPlanningResponse>
MotionPlannerServiceBaseTest::PlanTrajectoryWithMPSProxy(
    const MotionPlanningRequest& request) {
  grpc::ClientContext context;
  TrajectoryPlanningResponse response;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(motion_planner_service_proxy_stub_->PlanTrajectory(
          &context, request, &response)));
  return response;
}
absl::StatusOr<TrajectoryPlanningResponse>
MotionPlannerServiceBaseTest::PlanTrajectoryWithMPSWithAssetVersion(
    const MotionPlanningRequest& request) {
  grpc::ClientContext context;
  TrajectoryPlanningResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      motion_planner_service_with_asset_version_stub_->PlanTrajectory(
          &context, request, &response)));
  return response;
}
absl::StatusOr<World> MotionPlannerServiceBaseTest::GetWorld(
    absl::string_view world_id) {
  return skills::DownloadWorldFromObjectWorldService(
      world_id, *object_world_service_stub_,
      fake_world_service_geo_library_.get());
}

intrinsic_proto::motion_planning::v1::JointPositionSumLimit
MotionPlannerServiceBaseTest::CreateJointPositionSumLimitConstraintFor6DofRobot(
    double joint_sum_limit, absl::string_view robot_name) {
  intrinsic_proto::motion_planning::v1::JointPositionSumLimit
      joint_position_sum_limit_proto;

  *joint_position_sum_limit_proto.mutable_object_id()
       ->mutable_by_name()
       ->mutable_object_name() = robot_name;

  joint_position_sum_limit_proto.add_joint_signs(
      ::intrinsic_proto::motion_planning::v1::JointPositionSumLimit::
          UNSPECIFIED);  // First joint sign.
  joint_position_sum_limit_proto.add_joint_signs(
      ::intrinsic_proto::motion_planning::v1::JointPositionSumLimit::
          POSITIVE);  // Second joint sign.
  joint_position_sum_limit_proto.add_joint_signs(
      ::intrinsic_proto::motion_planning::v1::JointPositionSumLimit::
          UNSPECIFIED);  // Third joint sign.
  joint_position_sum_limit_proto.add_joint_signs(
      ::intrinsic_proto::motion_planning::v1::JointPositionSumLimit::
          UNSPECIFIED);  // Fourth joint sign.
  joint_position_sum_limit_proto.add_joint_signs(
      ::intrinsic_proto::motion_planning::v1::JointPositionSumLimit::
          POSITIVE);  // Fifth joint sign.
  joint_position_sum_limit_proto.add_joint_signs(
      ::intrinsic_proto::motion_planning::v1::JointPositionSumLimit::
          UNSPECIFIED);  // Sixth joint sign.

  joint_position_sum_limit_proto.set_joint_sum_limit(joint_sum_limit);

  return joint_position_sum_limit_proto;
}

absl::StatusOr<bool> MotionPlannerServiceBaseTest::
    PlanSingleSegmentConstrainedTrajectoryAndCheckPathConstraintsSatisfaction(
        const intrinsic_proto::motion_planning::v1::MotionPlanningRequest&
            request) {
  // This method assumes that the request has a single motion segment with
  // a path constraint.
  CHECK_EQ(request.motion_specification().motion_segments_size(), 1);
  CHECK(
      request.motion_specification().motion_segments(0).has_path_constraints());

  // Get the trajectory.
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse
          trajectory_planning_response,
      PlanTrajectory(request));
  CHECK(trajectory_planning_response.has_discretized());
  INTR_ASSIGN_OR_RETURN(JointTrajectoryPVA trajectory,
                        FromProto(trajectory_planning_response.discretized()));

  // Construct the `KinematicsSystemProxy`.
  INTR_ASSIGN_OR_RETURN(World world, GetWorld(request.world_id()));
  INTR_ASSIGN_OR_RETURN(
      const std::unique_ptr<object_world::ObjectWorld> object_world,
      object_world::ObjectWorld::CreateView(world));
  INTR_ASSIGN_OR_RETURN(
      const object_world::KinematicObject* robot,
      GetRobot(request.robot_specification().robot_reference(),
               object_world.get()));
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<KinematicsSystemProxy> proxy,
      CreateKinematicsProxy(
          *object_world, *robot,
          KinematicsProxyOptions{
              .constraints_proto = request.motion_specification()
                                       .motion_segments(0)
                                       .path_constraints(),
          }));

  CollisionCheckingDebug collision_checking_debug;
  // Check that all states in the trajectory satisfies the path constraints.
  for (size_t i = 0; i < trajectory.size(); ++i) {
    INTR_ASSIGN_OR_RETURN(JointStatePVA state, trajectory.DataAt(i));
    INTR_ASSIGN_OR_RETURN(
        auto validation_result,
        proxy->IsValid(state.position, &collision_checking_debug));
    if (!validation_result) {
      return false;
    }
  }
  return true;
}

void MotionPlannerServiceWithoutLoggerTest::SetUpWithTestParams(
    const MotionPlannerServiceTestParams& test_params,
    WorldHashMap<std::string, World> extra_worlds) {
  ASSERT_OK_AND_ASSIGN(auto worlds, CreateDefault4RobotsCubeWorld(
                                        test_params.set_infinite_jerk_limits));
  for (auto& [name, world] : extra_worlds) {
    worlds.emplace(std::move(name), std::move(world));
  }

  MotionPlannerFlags motion_planner_flags;
  ASSERT_OK(CheckJerkLimitsSupported(test_params.set_infinite_jerk_limits));
  Init(/*use_nonvolatile_cache=*/false, /*extra_flags=*/motion_planner_flags,
       /*worlds_by_name=*/std::move(worlds));
}

}  // namespace intrinsic
