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

#ifndef INTRINSIC_SIMULATION_GAZEBO_GAZEBO_SERVICE_STATE_H_
#define INTRINSIC_SIMULATION_GAZEBO_GAZEBO_SERVICE_STATE_H_

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/assets/services/proto/v1/service_state.grpc.pb.h"
#include "intrinsic/assets/services/proto/v1/service_state.pb.h"
#include "intrinsic/simulation/gazebo/simulator_control_service.h"

namespace intrinsic {
namespace simulation {

// Implementation of the `intrinsic_proto.services.v1.ServiceState` service for
// the Gazebo simulator Service asset.
//
// The state of the asset is derived from the run state of the simulator
// managed by a `SimulatorControlService`:
// - STOPPED: The simulator failed to start or reset and is not running. This
//   is reported as STATE_CODE_ERROR along with the cause of the failure.
//   `Enable` resolves the error by resetting the simulator with the most
//   recently used simulator world.
// - RUNNING or PAUSED: Reported as STATE_CODE_ENABLED. Pausing is a regular
//   operating mode of the simulator.
//
// Disabling the simulator is not supported; `Disable` returns UNIMPLEMENTED.
//
// This class is thread-safe.
class GazeboServiceState final
    : public intrinsic_proto::services::v1::ServiceState::Service {
 public:
  // `simulator_control_service` must be non-null and outlive this instance.
  explicit GazeboServiceState(
      SimulatorControlService* simulator_control_service);

  GazeboServiceState(const GazeboServiceState&) = delete;
  GazeboServiceState& operator=(const GazeboServiceState&) = delete;

  grpc::Status GetState(
      grpc::ServerContext* context,
      const intrinsic_proto::services::v1::GetStateRequest* request,
      intrinsic_proto::services::v1::SelfState* response) override;

  grpc::Status Enable(
      grpc::ServerContext* context,
      const intrinsic_proto::services::v1::EnableRequest* request,
      intrinsic_proto::services::v1::EnableResponse* response) override
      ABSL_LOCKS_EXCLUDED(enable_mutex_);

  grpc::Status Disable(
      grpc::ServerContext* context,
      const intrinsic_proto::services::v1::DisableRequest* request,
      intrinsic_proto::services::v1::DisableResponse* response) override;

 private:
  SimulatorControlService& simulator_control_service_;

  // Serializes `Enable` calls to avoid concurrent simulator resets.
  absl::Mutex enable_mutex_;
};

}  // namespace simulation
}  // namespace intrinsic

#endif  // INTRINSIC_SIMULATION_GAZEBO_GAZEBO_SERVICE_STATE_H_
