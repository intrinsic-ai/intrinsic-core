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

#ifndef INTRINSIC_SIMULATION_SERVICE_SIMULATOR_V1_H_
#define INTRINSIC_SIMULATION_SERVICE_SIMULATOR_V1_H_

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/client_context.h"
#include "intrinsic/simulation/service/proto/first_party/simulation_service.pb.h"
#include "intrinsic/simulation/service/simulator.h"
#include "intrinsic/simulation/simulator/proto/v1/simulator_control_service.grpc.pb.h"
#include "intrinsic/util/grpc/connection_params.h"

namespace intrinsic {
namespace simulation {

// Implements the Simulator interface for the v1 simulator gRPC APIs.
class SimulatorV1 : public Simulator {
 public:
  struct Config {
    ConnectionParams sim_control_connection_params;
    std::string_view simulator_name = "gazebo";
  };

  explicit SimulatorV1(const Config& config);

  // Disallow copy and assign.
  SimulatorV1(const SimulatorV1&) = delete;
  const SimulatorV1& operator=(const SimulatorV1&) = delete;

  std::string GetName() const override;

  absl::Status Reset(const Simulator::ResetParams& reset_request,
                     std::unique_ptr<::grpc::ClientContext> ctx) override;

  absl::Status Pause(std::unique_ptr<::grpc::ClientContext> ctx) override;

  absl::Status Unpause(std::unique_ptr<::grpc::ClientContext> ctx) override;

  absl::StatusOr<
      intrinsic_proto::simulation::first_party::GetSimulationStatusResponse>
  GetStatus(std::unique_ptr<::grpc::ClientContext> ctx) override;

 private:
  // Returns the stub, connecting to the simulator first if there is none yet.
  // Waits for the connection until the deadline of `ctx`, but at most
  // `kSimulatorRPCWaitForConnected`.
  absl::StatusOr<
      intrinsic_proto::simulation::v1::SimulatorControlService::Stub*>
  GetStub(const ::grpc::ClientContext& ctx) ABSL_LOCKS_EXCLUDED(stub_mutex_);

  const ConnectionParams sim_control_connection_params_;
  const std::string name_;

  mutable absl::Mutex stub_mutex_;
  std::unique_ptr<
      intrinsic_proto::simulation::v1::SimulatorControlService::Stub>
      stub_ ABSL_GUARDED_BY(stub_mutex_);
};

}  // namespace simulation
}  // namespace intrinsic

#endif  // INTRINSIC_SIMULATION_SERVICE_SIMULATOR_V1_H_
