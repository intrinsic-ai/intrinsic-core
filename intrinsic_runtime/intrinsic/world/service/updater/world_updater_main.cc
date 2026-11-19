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

// Startup code for the world updater.
//
// The world updater listens to Pub/Sub messages about the world and fuses them
// into updates that are passed along to the world service.
#include <atomic>
#include <csignal>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/flags.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.grpc.pb.h"
#include "google/longrunning/operations.pb.h"
#include "grpc/grpc.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/support/channel_arguments.h"
#include "intrinsic/assets/proto/asset_deployment.grpc.pb.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/resources/client/resource_registry_client.h"
#include "intrinsic/stats/opencensus.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/service/robot_calibration/robot_kinematics_updater.h"
#include "intrinsic/world/service/updater/world_updater.h"
#include "intrinsic/world/service/updater/world_updater_config.pb.h"
#include "intrinsic/world/service/world_service.grpc.pb.h"

ABSL_FLAG(int32_t, port, 10002, "port to listen on");
ABSL_FLAG(std::string, world_address, "", "address for the world server");
ABSL_FLAG(std::string, asset_deployment_address, "",
          "address for the asset deployment service");
ABSL_FLAG(absl::Duration, grpc_client_connect_timeout,
          intrinsic::connect::kGrpcClientConnectDefaultTimeout,
          "Time to wait for the grpc server to become available.");
ABSL_FLAG(std::string, config_filename, "",
          "Filename to read the configurations from.");
ABSL_FLAG(std::string, resource_registry_address, "",
          "address for the resource registry service");
ABSL_FLAG(absl::Duration, shutdown_grace_period, absl::Seconds(10),
          "Time to wait for graceful cancellation on shutdown");

namespace intrinsic {

namespace {
// std::atomic is safe, as long as it is lock-free
std::atomic<bool> g_shutdown_requested = false;
static_assert(std::atomic<bool>::is_always_lock_free);
}  // namespace

absl::StatusOr<std::unique_ptr<
    intrinsic_proto::world::internal::WorldService::StubInterface>>
GetWorldStub(const std::string& grpc_target, absl::Duration timeout) {
  grpc::ChannelArguments channel_args;
  channel_args.SetMaxReceiveMessageSize(-1);

  INTR_ASSIGN_OR_RETURN(auto channel,
                        connect::CreateClientChannel(
                            grpc_target, absl::Now() + timeout, channel_args));

  return intrinsic_proto::world::internal::WorldService::NewStub(
      std::move(channel));
}

absl::StatusOr<std::unique_ptr<
    intrinsic_proto::assets::AssetDeploymentService::StubInterface>>
GetAssetDeploymentStub(const std::string& grpc_target, absl::Duration timeout) {
  grpc::ChannelArguments channel_args;
  channel_args.SetMaxReceiveMessageSize(-1);

  INTR_ASSIGN_OR_RETURN(auto channel,
                        connect::CreateClientChannel(
                            grpc_target, absl::Now() + timeout, channel_args));

  return intrinsic_proto::assets::AssetDeploymentService::NewStub(
      std::move(channel));
}

absl::StatusOr<std::unique_ptr<google::longrunning::Operations::StubInterface>>
GetOperationsStub(const std::string& grpc_target, absl::Duration timeout) {
  grpc::ChannelArguments channel_args;
  channel_args.SetMaxReceiveMessageSize(-1);

  INTR_ASSIGN_OR_RETURN(auto channel,
                        connect::CreateClientChannel(
                            grpc_target, absl::Now() + timeout, channel_args));

  return google::longrunning::Operations::NewStub(std::move(channel));
}

absl::StatusOr<
    std::unique_ptr<intrinsic_proto::world::ObjectWorldService::Stub>>
GetObjectWorldStub(absl::string_view object_world_service_address,
                   absl::Duration timeout) {
  INTR_ASSIGN_OR_RETURN(auto channel, connect::CreateClientChannel(
                                          object_world_service_address,
                                          /*deadline=*/absl::Now() + timeout));
  return intrinsic_proto::world::ObjectWorldService::NewStub(channel);
}

absl::Status MainImpl() {
  const std::string config_filename = absl::GetFlag(FLAGS_config_filename);
  if (!config_filename.empty()) {
    LOG(WARNING) << "The value of --config_filename is ignored, and this flag "
                    "will be deprecated.";
  }

  const std::string world_grpc_target = absl::GetFlag(FLAGS_world_address);
  if (world_grpc_target.empty()) {
    return absl::InvalidArgumentError("Missing --world_address");
  }

  const absl::Duration timeout =
      absl::GetFlag(FLAGS_grpc_client_connect_timeout);

  INTR_ASSIGN_OR_RETURN(auto world_stub,
                        GetWorldStub(world_grpc_target, timeout));

  INTR_ASSIGN_OR_RETURN(
      auto resource_registry_client,
      intrinsic::resources::CreateResourceRegistryClient(
          absl::GetFlag(FLAGS_resource_registry_address), absl::Seconds(60)));

  INTR_ASSIGN_OR_RETURN(auto object_world_stub,
                        GetObjectWorldStub(world_grpc_target, timeout));

  const std::string asset_deployment_grpc_target =
      absl::GetFlag(FLAGS_asset_deployment_address);
  if (asset_deployment_grpc_target.empty()) {
    return absl::InvalidArgumentError("Missing --asset_deployment_address");
  }

  INTR_ASSIGN_OR_RETURN(
      auto asset_deployment_stub,
      GetAssetDeploymentStub(asset_deployment_grpc_target, timeout));
  INTR_ASSIGN_OR_RETURN(
      auto asset_deployment_operations_stub,
      GetOperationsStub(asset_deployment_grpc_target, timeout));

  icon::DefaultChannelFactory icon_channel_factory;

  // TODO(b/435458124): Rename this binary or move RobotKinematicsUpdater to a
  // more appropriate existing binary.
  auto kinematics_updater = std::make_unique<icon::RobotKinematicsUpdater>(
      object_world_stub.get(), asset_deployment_stub.get(),
      asset_deployment_operations_stub.get(), resource_registry_client.get(),
      &icon_channel_factory);

  const std::string server_address =
      absl::StrCat("[::]:", absl::GetFlag(FLAGS_port));

  std::shared_ptr<grpc::ServerCredentials> creds =
      grpc::InsecureServerCredentials();  // NOLINT
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, creds);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.RegisterService(kinematics_updater.get());
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    LOG(QFATAL) << "Cannot create World Updater " << server_address;
  }

  LOG(INFO) << "World Updater listening on " << server_address;

  std::signal(SIGTERM, [](int) {
    // async-signal-safe implementation
    // for details, see here:
    // https://man7.org/linux/man-pages/man7/signal-safety.7.html
    // Note: To prevent undefined behavior, do not do any logging (unless using
    //   async-safe write) or else here without making sure that the function is
    //   async-signal-safe.
    g_shutdown_requested = true;
    g_shutdown_requested.notify_all();
  });

  intrinsic::Thread shutdown([&]() {
    g_shutdown_requested.wait(/*old=*/false);
    if (server->GetHealthCheckService()) {
      server->GetHealthCheckService()->SetServingStatus(false);
    }
    // Give grace period for clean shutdown
    absl::SleepFor(absl::GetFlag(FLAGS_shutdown_grace_period));
    server->Shutdown();
  });

  // Keep the program running until the server shuts down.
  server->Wait();
  shutdown.join();

  return absl::OkStatus();
}

}  // namespace intrinsic

int main(int argc, char** argv) {
  InitIntrinsic(argv[0], argc, argv);
  intrinsic::OpenCensusPlugin open_census(/*service_name=*/"world_updater",
                                          /*use_otel_metrics=*/true);

  QCHECK_OK(intrinsic::MainImpl());

  return 0;
}
