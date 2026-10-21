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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpc/grpc.h"
#include "grpcpp/channel.h"
#include "grpcpp/client_context.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server_builder.h"
#include "intrinsic/assets/dependencies/utils.h"
#include "intrinsic/assets/proto/installed_assets.grpc.pb.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/geometry/proto/geometry_service.pb.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_service_storage.h"
#include "intrinsic/icon/release/file_helpers.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/logging/data_logger_client.h"
#include "intrinsic/logging/structured_logging_client.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_flags.h"
#include "intrinsic/motion_planning/service/cache/motion_planner_cache.h"
#include "intrinsic/motion_planning/service/motion_planner_service.h"
#include "intrinsic/motion_planning/service/motion_planner_service_asset_utils.h"
#include "intrinsic/motion_planning/service/motion_planner_service_utils.h"
#include "intrinsic/motion_planning/service/nonvolatile_cache/motion_planner_nonvolatile_cache.h"
#include "intrinsic/resources/proto/runtime_context.pb.h"
#include "intrinsic/skills/internal/world_service_utils.h"
#include "intrinsic/stats/opencensus.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/proto/any.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/world.h"

ABSL_FLAG(std::string, world_service_address,
          "world.app-intrinsic-base.svc.cluster.local:8080",
          "gRpc target for the World service");
ABSL_FLAG(std::string, geometry_service_address,
          "geomservice.app-intrinsic-base.svc.cluster.local:8080",
          "gRpc target for the geometry service");
ABSL_FLAG(absl::Duration, grpc_connect_timeout,
          intrinsic::connect::kGrpcClientConnectDefaultTimeout,
          "Time to wait for other grpc services to become available.");
ABSL_FLAG(std::string, cas_grpc_service_address, "",
          "(optional) Address of the Intrinsic ContentAddressableStorage gRPC "
          "service.");
ABSL_FLAG(std::string, workcell_name, "",
          "Name of the workcell/cluster the motion planner service is running "
          "in.");
ABSL_FLAG(std::string, organization_id, "",
          "ID of the organization the motion planner service is running in.");
ABSL_FLAG(std::string, runtime_context_file, "/etc/intrinsic/runtime_config.pb",
          "The path to the runtime context file containing "
          "intrinsic_proto.config.RuntimeContext binary proto.");

namespace intrinsic {

int MainImpl() {
  OpenCensusPlugin open_census(/*service_name=*/kMotionPlannerServiceName,
                               /*use_otel_metrics=*/true);

  // Initialize sub-services.
  ASSIGN_OR_DIE(
      std::unique_ptr<intrinsic_proto::world::ObjectWorldService::Stub>
          object_world_service,
      CreateObjectWorldServiceStub(absl::GetFlag(FLAGS_world_service_address),
                                   absl::GetFlag(FLAGS_grpc_connect_timeout)));

  ASSIGN_OR_DIE(
      std::unique_ptr<intrinsic_proto::geometry::GeometryService::StubInterface>
          geometry_service,
      CreateGeometryServiceStub(absl::GetFlag(FLAGS_geometry_service_address),
                                absl::GetFlag(FLAGS_grpc_connect_timeout)));
  ASSIGN_OR_DIE(const auto context,
                GetBinaryProto<intrinsic_proto::config::RuntimeContext>(
                    absl::GetFlag(FLAGS_runtime_context_file)));

  ASSIGN_OR_DIE(
      const auto proto_config,
      UnpackAny<
          intrinsic_proto::motion_planning::v1::MotionPlannerServiceConfig>(
          context.config()));

  auto geo_lib(GetGeometryServiceGeometryLibrary(
      geometry_service.get(), proto_config.geometry_service_cache_size()));

  // Launch a background thread to fetch the default 'world' from the world
  // service and deserialize it. This will effectively cache the geometry from
  // the world. Once we have done this, we can go through each piece of geometry
  // and attempt to prepare it for collision checking.
  intrinsic::Thread fetch_world_thread([&object_world_service, &geo_lib] {
    auto status =
        AttemptToPreCacheTheWorldData(*object_world_service, *geo_lib);
    if (!status.ok()) {
      LOG(WARNING) << "Unable to pre cache world data: " << status;
    }
  });

  // Start the structured logging client and data logger.
  // Do not fail if the logger is unavailable.
  std::shared_ptr<grpc::Channel> data_logger_channel = nullptr;
  if (auto channel_or = intrinsic::assets::dependencies::
          ConnectWithRuntimeAssetFallbackForAssetMigrationOnly(
              proto_config.intrinsic_runtime(),
              "grpc://intrinsic_proto.data_logger.DataLogger",
              connect::UnlimitedMessageSizeGrpcChannelArgs());
      channel_or.ok()) {
    data_logger_channel = *std::move(channel_or);
    if (auto s = intrinsic::connect::WaitForChannelReady(
            data_logger_channel, absl::GetFlag(FLAGS_grpc_connect_timeout));
        !s.ok()) {
      // TODO(b/540431451): Reconnect periodically in a background thread so
      // transient startup failures do not permanently degrade structured
      // logging functionality.
      LOG(WARNING) << "Failed to connect to data logger: " << s;
      data_logger_channel = nullptr;
    }
  } else {
    // TODO(b/540431451): Reconnect periodically in a background thread so
    // transient startup failures do not permanently degrade structured
    // logging functionality.
    LOG(WARNING) << "Failed to create data logger channel: "
                 << channel_or.status();
  }

  auto [logger_stub, logger_client] =
      CreateStructuredLoggingClient(data_logger_channel);

  if (!absl::GetFlag(FLAGS_organization_id).empty() ||
      !absl::GetFlag(FLAGS_workcell_name).empty()) {
    LOG(WARNING)
        << "Command-line flags --organization_id and --workcell_name "
        << "are deprecated and ignored while using motion planner service in "
        << "assetized mode. Values are retrieved dynamically "
        << "from the logger context.";
  }

  MotionPlannerServiceConfig mps_config{
      .workcell_name = absl::GetFlag(FLAGS_workcell_name),
      .organization_id = absl::GetFlag(FLAGS_organization_id),
  };

  FromProto(proto_config, mps_config);

  if (proto_config.has_log_all_recordings()) {
    mps_config.log_all_recordings = proto_config.log_all_recordings();
  }

  std::unique_ptr<PlanTrajectoryCache> cache = nullptr;
  if (proto_config.enable_motion_planner_service_caching()) {
    MotionPlanningRequestCacheKeyDistance::IsValidForCacheHitOptions
        distance_options{
            .diff_in_m_for_all_related_frame_poses_threshold =
                proto_config.pose_linear_tolerance_in_m(),
            .diff_in_m_for_all_object_poses_threshold =
                proto_config.pose_linear_tolerance_in_m(),
            .max_diff_in_rad_for_starting_robot_configuration_threshold =
                proto_config.joint_tolerance_in_rad(),
        };
    // Initialize the cache. Set `max_num_of_entries_per_group` to 5 for
    // now. We can expose it through ABSL_FLAG later if needed.
    ASSIGN_OR_DIE(cache, PlanTrajectoryCache::Create(
                             proto_config.cache_size(),
                             /*max_num_of_entries_per_group=*/10,
                             /*distance_options=*/distance_options));
  }

  std::unique_ptr<MotionPlannerNonvolatileCache>
      plan_trajectory_nonvolatile_cache = nullptr;
  if (proto_config.enable_motion_planner_service_nonvolatile_cache()) {
    ASSIGN_OR_DIE(
        plan_trajectory_nonvolatile_cache,
        MotionPlannerNonvolatileCache::CreateWithContentAddressableStorage(
            proto_config.cache_size(),
            absl::GetFlag(FLAGS_cas_grpc_service_address)));
  }

  ASSIGN_OR_DIE(
      std::shared_ptr<grpc::Channel> installed_assets_reader_channel,
      intrinsic::assets::dependencies::
          ConnectWithRuntimeAssetFallbackForAssetMigrationOnly(
              proto_config.intrinsic_runtime(),
              "grpc://intrinsic_proto.assets.v1.InstalledAssetsReader",
              connect::UnlimitedMessageSizeGrpcChannelArgs()));

  QCHECK_OK(
      intrinsic::connect::WaitForChannelReady(installed_assets_reader_channel));

  auto installed_assets_reader_stub =
      intrinsic_proto::assets::v1::InstalledAssetsReader::NewStub(
          installed_assets_reader_channel);

  ASSIGN_OR_DIE(const std::string installed_mps_asset_version,
                GetInstalledMotionPlannerServiceAssetVersion(
                    installed_assets_reader_stub.get()));

  ASSIGN_OR_DIE(
      std::unique_ptr<MotionPlannerService> service,
      MotionPlannerService::Create(
          object_world_service.get(), geo_lib.get(),
          /*mps_asset_id_name=*/kMotionPlannerServiceName, std::move(cache),
          std::move(plan_trajectory_nonvolatile_cache), mps_config,
          installed_mps_asset_version, logger_client.get()));

  if (logger_stub != nullptr) {
    if (auto s = intrinsic::data_logger::StartUpIntrinsicLoggerViaStub(
            std::move(logger_stub));
        !s.ok()) {
      LOG(ERROR) << "Failed to initialize logging singleton: " << s;
    }
  }

  const std::string server_address = absl::StrFormat("[::]:%d", context.port());
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address,
                           grpc::InsecureServerCredentials());  // NOLINT
  // "0" means no port reuse. Allowing other servers on the same port could
  // introduce hard-to-debug behavior or flaky tests.
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.RegisterService(service.get());
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  LOG_IF(QFATAL, server == nullptr)
      << "Cannot create motion planner server " << server_address;
  LOG(INFO) << "Motion planner server listening on " << server_address;
  server->Wait();
  fetch_world_thread.join();
  return 0;
}

}  // namespace intrinsic

int main(int argc, char** argv) {
  InitIntrinsic(argv[0], argc, argv);
  return intrinsic::MainImpl();
}
