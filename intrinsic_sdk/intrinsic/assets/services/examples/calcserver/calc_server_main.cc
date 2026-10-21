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

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "grpc/grpc.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "intrinsic/assets/dependencies/utils.h"
#include "intrinsic/assets/services/examples/calcserver/calc_server.h"
#include "intrinsic/assets/services/examples/calcserver/calc_server.pb.h"
#include "intrinsic/icon/release/file_helpers.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/resources/proto/runtime_context.pb.h"
#include "intrinsic/util/proto/any.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::services {

absl::Status MainImpl() {
  constexpr absl::string_view kContextFilePath =
      "/etc/intrinsic/runtime_config.pb";
  INTR_ASSIGN_OR_RETURN(
      const auto context,
      GetBinaryProto<intrinsic_proto::config::RuntimeContext>(kContextFilePath),
      _ << "Reading runtime context");

  auto config = std::make_unique<intrinsic_proto::services::CalculatorConfig>();
  INTR_RETURN_IF_ERROR(UnpackAny(context.config(), *config));

  CalculatorServiceImpl calculator_service(
      *config, [](const auto& dep, absl::string_view interface) {
        return assets::dependencies::GetDataPayload(dep, interface);
      });

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
  builder.RegisterService(&calculator_service);

  // Start the gRPC server.
  std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    LOG(FATAL) << "Cannot create Calculator Service " << server_address;
  }
  LOG(INFO) << "--------------------------------";
  LOG(INFO) << "-- Calculator Service listening on " << server_address;
  LOG(INFO) << "--------------------------------";

  // Block gRPC server until it shuts down.
  server->Wait();
  return absl::OkStatus();
}

}  // namespace intrinsic::services

int main(int argc, char** argv) {
  InitIntrinsic(argv[0], argc, argv);
  QCHECK_OK(intrinsic::services::MainImpl());
  return 0;
}
