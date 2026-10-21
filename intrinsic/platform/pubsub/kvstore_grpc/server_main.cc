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

// Starts a GRPC server for the KVStore service. This is a simple wrapper around
// the KVStore class.

#include <cstdlib>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "grpc/grpc.h"
#include "grpcpp/resource_quota.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server_builder.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/platform/pubsub/kvstore_grpc/metrics.h"
#include "intrinsic/platform/pubsub/kvstore_grpc/server_impl.h"
#include "intrinsic/stats/opencensus.h"

ABSL_FLAG(int, port, 8080, "Port to listen on.");

int main(int argc, char* argv[]) {
  InitIntrinsic(argv[0], argc, argv);
  intrinsic::OpenCensusPlugin opencensus(/*service_name=*/"kvstore_service",
                                         /*use_otel_metrics=*/true);
  intrinsic::kvstore::RegisterKvstoreGrpcMetrics();

  intrinsic::kvstore::KVStoreServerImpl kvstore;
  CHECK_OK(kvstore.Init());

  grpc::ServerBuilder builder;
  const std::string server_address =
      absl::StrFormat("0.0.0.0:%d", absl::GetFlag(FLAGS_port));
  // Set the authentication mechanism.
  std::shared_ptr<grpc::ServerCredentials> credentials =
      grpc::InsecureServerCredentials();  // NOLINT
  builder.AddListeningPort(server_address, credentials);
  builder.SetResourceQuota(grpc::ResourceQuota().SetMaxThreads(200));
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.RegisterService(&kvstore);

  builder.SetSyncServerOption(
      grpc::ServerBuilder::SyncServerOption::MAX_POLLERS, 25);
  builder.SetSyncServerOption(
      grpc::ServerBuilder::SyncServerOption::CQ_TIMEOUT_MSEC, 60000);
  // By default gRPC restricts messages to 4MB. Perception payloads can be
  // larger than that.
  builder.SetMaxReceiveMessageSize(-1);

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    LOG(ERROR) << "Error building server.";
    return EXIT_FAILURE;
  }
  grpc::Server* kvstore_server = server.get();

  LOG(INFO) << "KV store server listening on " << server_address;

  kvstore_server->Wait();

  return 0;
}
