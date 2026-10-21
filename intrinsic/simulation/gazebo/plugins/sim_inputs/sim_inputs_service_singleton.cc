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

#include "intrinsic/simulation/gazebo/plugins/sim_inputs/sim_inputs_service_singleton.h"

#include <cstdlib>
#include <memory>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "grpc/grpc.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/time.h"
#include "intrinsic/simulation/gazebo/plugins/sim_inputs/v1/set_simulated_inputs.grpc.pb.h"
#include "intrinsic/util/status/status_conversion_grpc.h"

namespace intrinsic::simulation {

SimInputsServiceSingleton& SimInputsServiceSingleton::Get() {
  static SimInputsServiceSingleton* kSimInputsService = []() {
    auto* sim_inputs_service = new SimInputsServiceSingleton;
    return sim_inputs_service;
  }();
  return *kSimInputsService;
}

SimInputsServiceSingleton::SimInputsServiceSingleton() {
  const char* server_address = getenv(kServiceAddressEnvironmentVariable);
  if (server_address == nullptr) {
    server_address = "0.0.0.0:12476";
  }

  ::grpc::ServerBuilder builder;
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.AddListeningPort(
      server_address,
      ::grpc::InsecureServerCredentials());  // NOLINT (insecure)
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  CHECK(server_) << "Can't set up SetSimulatedInputsService";
  LOG(INFO) << "Started SetSimulatedInputsService at: " << server_address;
}

SimInputsServiceSingleton::~SimInputsServiceSingleton() {
  // Stop gRPC server, if any
  if (server_) {
    LOG(INFO) << "Shutting down SetSimulatedInputs gRPC server";
    server_->Shutdown();
  }
}

// gRPC service methods. These delegate to the actual plugin if there is one,
// and return UnimplementedError otherwise.
::grpc::Status SimInputsServiceSingleton::SetSimulatedInputs(
    ::grpc::ServerContext* context,
    const intrinsic_proto::simulation::v1::SetSimulatedInputsRequest* request,
    intrinsic_proto::simulation::v1::SetSimulatedInputsResponse* response) {
  absl::MutexLock l(implementation_mutex_);
  auto impl = implementation_.lock();
  if (!impl) {
    return ToGrpcStatus(absl::UnimplementedError(
        "There is no plugin for SetSimInputsService. Check the SDFormat "
        "definition of your Gazebo World"));
  }
  return impl->SetSimulatedInputs(context, request, response);
}
::grpc::Status SimInputsServiceSingleton::ListSimulatedInputs(
    ::grpc::ServerContext* context,
    const intrinsic_proto::simulation::v1::ListSimulatedInputsRequest* request,
    intrinsic_proto::simulation::v1::ListSimulatedInputsResponse* response) {
  absl::MutexLock l(implementation_mutex_);
  auto impl = implementation_.lock();

  if (!impl) {
    return ToGrpcStatus(absl::UnimplementedError(
        "There is no plugin for SetSimInputsService. Check the SDFormat "
        "definition of your Gazebo World"));
  }
  return impl->ListSimulatedInputs(context, request, response);
}

absl::Status SimInputsServiceSingleton::RegisterServiceImplementation(
    std::weak_ptr<
        intrinsic_proto::simulation::v1::SetSimulatedInputsService::Service>
        implementation) {
  absl::MutexLock l(implementation_mutex_);
  if (!implementation_.expired()) {
    return absl::AlreadyExistsError(
        "Cannot register more than one SetSimulatedInputs plugin. Check the "
        "SDFormat definition of your Gazebo world and remove duplicates.");
  }
  implementation_ = implementation;
  return absl::OkStatus();
}

void SimInputsServiceSingleton::ShutdownServerTestOnly() {
  if (server_) {
    server_->Shutdown(absl::Now());
  }
}

}  // namespace intrinsic::simulation
