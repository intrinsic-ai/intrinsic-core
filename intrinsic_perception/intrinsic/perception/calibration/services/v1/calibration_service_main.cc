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

#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.grpc.pb.h"
#include "grpc/grpc.h"
#include "grpcpp/client_context.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "intrinsic/assets/dependencies/utils.h"
#include "intrinsic/assets/proto/v1/asset_instances.grpc.pb.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/icon/release/file_helpers.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/logging/data_logger_client.h"
#include "intrinsic/logging/proto/logger_service.grpc.pb.h"
#include "intrinsic/perception/calibration/services/v1/calibration_service_impl.h"
#include "intrinsic/perception/proto/v1/calibration_service.pb.h"
#include "intrinsic/resources/proto/runtime_context.pb.h"
#include "intrinsic/util/proto/any.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"

ABSL_FLAG(std::string, runtime_context_file, "/etc/intrinsic/runtime_config.pb",
          "The path to the runtime context file containing "
          "intrinsic_proto.config.RuntimeContext binary proto.");

ABSL_FLAG(absl::Duration, grpc_client_connection_timeout,
          intrinsic::connect::kGrpcClientConnectDefaultTimeout,
          "Time to wait for the grpc server to become available.");

namespace intrinsic::perception {

absl::Status MainImpl() {
  LOG(INFO) << "Reading runtime context from "
            << absl::GetFlag(FLAGS_runtime_context_file);
  INTR_ASSIGN_OR_RETURN(const auto context,
                        GetBinaryProto<intrinsic_proto::config::RuntimeContext>(
                            absl::GetFlag(FLAGS_runtime_context_file)),
                        _ << "Reading runtime context");

  INTR_ASSIGN_OR_RETURN(
      const auto proto_config,
      UnpackAny<intrinsic_proto::perception::v1::CalibrationServiceConfig>(
          context.config()),
      _ << "Unpacking CalibrationServiceConfig");

  INTR_ASSIGN_OR_RETURN(
      (auto [operations_channel, asset_instances_channel, data_logger_channel,
             object_world_channel]),
      intrinsic::assets::dependencies::ConnectMany(
          proto_config.intrinsic_runtime(),
          {
              "grpc://google.longrunning.Operations",
              "grpc://intrinsic_proto.assets.v1.AssetInstances",
              "grpc://intrinsic_proto.data_logger.DataLogger",
              "grpc://intrinsic_proto.world.ObjectWorldService",
          },
          connect::UnlimitedMessageSizeGrpcChannelArgs()));

  INTR_RETURN_IF_ERROR(intrinsic::connect::WaitForChannelReady(
      operations_channel, absl::GetFlag(FLAGS_grpc_client_connection_timeout)));
  auto operations_stub =
      google::longrunning::Operations::NewStub(operations_channel);

  INTR_RETURN_IF_ERROR(intrinsic::connect::WaitForChannelReady(
      asset_instances_channel,
      absl::GetFlag(FLAGS_grpc_client_connection_timeout)));
  auto asset_instances_stub =
      intrinsic_proto::assets::v1::AssetInstances::NewStub(
          asset_instances_channel);

  INTR_RETURN_IF_ERROR(intrinsic::connect::WaitForChannelReady(
      object_world_channel,
      absl::GetFlag(FLAGS_grpc_client_connection_timeout)));
  auto object_world_service_stub =
      intrinsic_proto::world::ObjectWorldService::NewStub(object_world_channel);

  if (auto status = intrinsic::connect::WaitForChannelReady(
          data_logger_channel,
          absl::GetFlag(FLAGS_grpc_client_connection_timeout));
      !status.ok()) {
    LOG(ERROR) << "Failed to connect to data logger: " << status;
  } else {
    auto data_logger_stub =
        intrinsic_proto::data_logger::DataLogger::NewStub(data_logger_channel);
    if (auto s = intrinsic::data_logger::StartUpIntrinsicLoggerViaStub(
            std::move(data_logger_stub));
        !s.ok()) {
      LOG(ERROR) << "Failed to initialize logging singleton: " << s;
    }
  }

  auto calibration_service_impl = std::make_unique<CalibrationServiceImpl>(
      std::move(operations_stub), std::move(asset_instances_stub), nullptr,
      std::move(object_world_service_stub));

  // Use the port passed in the runtime context, and use a localhost address.
  std::string server_address = absl::StrCat("0.0.0.0:", context.port());

  // In this example we use insecure credentials because the server is to be run
  // inside of a Kubernetes Pod. Security, authentication, etc. will be managed
  // by the cluster's configuration.
  std::shared_ptr<grpc::ServerCredentials> creds =
      grpc::InsecureServerCredentials();  // NOLINT (insecure)

  // Register the service with a gRPC server.
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, creds);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.RegisterService(calibration_service_impl.get());

  // Start the gRPC server.
  std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    LOG(FATAL) << "Cannot create calibration service " << server_address;
  }
  LOG(INFO) << "--------------------------------";
  LOG(INFO) << "-- Calibration service listening on " << server_address;
  LOG(INFO) << "--------------------------------";

  // Block gRPC server until it shuts down.
  server->Wait();
  return absl::OkStatus();
}

}  // namespace intrinsic::perception

int main(int argc, char** argv) {
  InitIntrinsic(argv[0], argc, argv);
  QCHECK_OK(intrinsic::perception::MainImpl());
  return 0;
}
