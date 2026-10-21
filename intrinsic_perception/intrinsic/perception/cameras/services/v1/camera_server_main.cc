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

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "intrinsic/assets/dependencies/utils.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/icon/release/file_helpers.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/logging/data_logger_client.h"
#include "intrinsic/logging/proto/logger_service.grpc.pb.h"
#include "intrinsic/perception/cameras/camera.h"
#include "intrinsic/perception/cameras/services/v1/camera_config_service.h"
#include "intrinsic/perception/cameras/services/v1/camera_dynamic_reconfiguration_service.h"
#include "intrinsic/perception/cameras/services/v1/camera_health_service.h"
#include "intrinsic/perception/cameras/services/v1/camera_service_impl.h"
#include "intrinsic/perception/proto/v1/camera_config.pb.h"
#include "intrinsic/resources/proto/runtime_context.pb.h"
#include "intrinsic/stats/opencensus.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"

constexpr char kRuntimeContextFile[] = "/etc/intrinsic/runtime_config.pb";
ABSL_FLAG(std::string, runtime_context_file, kRuntimeContextFile,
          "The path to the runtime context file containing "
          "intrinsic_proto.config.RuntimeContext binary proto.");

namespace intrinsic {
namespace perception {
namespace {

absl::Status StartServerAndListen() {
  INTR_ASSIGN_OR_RETURN(const intrinsic_proto::config::RuntimeContext context,
                        GetBinaryProto<intrinsic_proto::config::RuntimeContext>(
                            absl::GetFlag(FLAGS_runtime_context_file)),
                        _ << "Reading runtime context");

  INTR_ASSIGN_OR_RETURN(
      const intrinsic_proto::perception::v1::CameraConfig camera_config_proto,
      UnpackCameraConfig(context.config()));
  INTR_ASSIGN_OR_RETURN(const ActiveCameraConfig active_camera,
                        ActiveCameraConfig::FromProto(camera_config_proto));

  if (context.name().empty()) {
    return intrinsic::InvalidArgumentErrorBuilder()
           << "Context name must not be empty.";
  }

  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<grpc::Channel> data_logger_channel,
      intrinsic::assets::dependencies::
          ConnectWithRuntimeAssetFallbackForAssetMigrationOnly(
              camera_config_proto.intrinsic_runtime(),
              "grpc://intrinsic_proto.data_logger.DataLogger",
              connect::UnlimitedMessageSizeGrpcChannelArgs()));
  if (const absl::Status status = intrinsic::connect::WaitForChannelReady(
          data_logger_channel,
          intrinsic::connect::kGrpcClientConnectDefaultTimeout);
      !status.ok()) {
    LOG(ERROR) << "Failed to connect to data logger: " << status;
  } else {
    auto data_logger_stub =
        intrinsic_proto::data_logger::DataLogger::NewStub(data_logger_channel);
    if (const absl::Status init_status =
            intrinsic::data_logger::StartUpIntrinsicLoggerViaStub(
                std::move(data_logger_stub));
        !init_status.ok()) {
      LOG(ERROR) << "Failed to initialize logging singleton: " << init_status;
    }
  }

  std::string streaming_topic_name =
      absl::StrCat("/assets/", context.name(), "/capture_result");

  auto camera_manager = std::make_shared<CameraManager>(active_camera);
  auto camera_service = std::make_unique<CameraServiceImpl>(
      camera_manager, std::move(streaming_topic_name));
  auto camera_health_service =
      std::make_unique<CameraHealthService>(camera_manager);
  auto dynamic_reconfiguration_service =
      std::make_unique<CameraDynamicReconfigurationService>(camera_manager);
  auto camera_config_service =
      std::make_unique<CameraConfigService>(camera_manager);

  LOG(INFO) << "Starting camera service for instance " << context.name()
            << " with camera config: "
            << camera_config_proto.ShortDebugString();
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<grpc::Server> server,
      intrinsic::CreateServer(
          context.port(), {camera_service.get(), camera_health_service.get(),
                           dynamic_reconfiguration_service.get(),
                           camera_config_service.get()}));
  if (server == nullptr) {
    return intrinsic::InternalErrorBuilder()
           << "Failed to build camera service on port " << context.port();
  }
  LOG(INFO) << "Camera service listening on: " << context.port();

  absl::Notification registered;
  return intrinsic::RegisterSignalHandlerAndWait(
      server.get(), intrinsic::ShutdownParams::Aggressive(), registered);
}

}  // namespace
}  // namespace perception
}  // namespace intrinsic

int main(int argc, char** argv) {
  InitIntrinsic(argv[0], argc, argv);
  intrinsic::OpenCensusPlugin open_census(/*service_name=*/"camera",
                                          /*use_otel_metrics=*/true);
  intrinsic::perception::RegisterCameraServiceMetrics();

  if (auto status = intrinsic::perception::StartServerAndListen();
      !status.ok()) {
    LOG(ERROR) << status;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
