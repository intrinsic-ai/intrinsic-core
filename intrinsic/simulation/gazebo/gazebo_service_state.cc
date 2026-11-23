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

#include "intrinsic/simulation/gazebo/gazebo_service_state.h"

#include "absl/log/die_if_null.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/assets/services/proto/v1/service_state.pb.h"
#include "intrinsic/simulation/gazebo/simulator_control_service.h"
#include "intrinsic/simulation/simulator/proto/v1/simulator_control_service.pb.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"

namespace intrinsic {
namespace simulation {

namespace {

using ::intrinsic_proto::services::v1::SelfState;
using ::intrinsic_proto::simulation::v1::GetSimulatorStatusRequest;
using ::intrinsic_proto::simulation::v1::GetSimulatorStatusResponse;
using ::intrinsic_proto::simulation::v1::SimulatorState;

absl::StatusOr<GetSimulatorStatusResponse> GetSimulatorStatus(
    SimulatorControlService& simulator_control_service,
    grpc::ServerContext* context) {
  GetSimulatorStatusRequest request;
  GetSimulatorStatusResponse response;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(simulator_control_service.GetSimulatorStatus(
          context, &request, &response)));
  return response;
}

SelfState StoppedState(const GetSimulatorStatusResponse& status) {
  SelfState state;
  state.set_state_code(SelfState::STATE_CODE_ERROR);
  intrinsic_proto::status::ExtendedStatus& extended_status =
      *state.mutable_extended_status();
  extended_status.set_title("Gazebo simulator is not running");
  const absl::Status run_status = ToAbslStatus(status.status());
  extended_status.mutable_user_report()->set_message(
      run_status.ok() ? "The simulator is stopped."
                      : absl::StrCat("The simulator failed to run: ",
                                     run_status.message()));
  extended_status.mutable_user_report()->set_instructions(
      "Enable the service or reset the simulation to restart the simulator. "
      "If the error persists, inspect the logs of the service.");
  return state;
}

SelfState EnabledState() {
  SelfState state;
  state.set_state_code(SelfState::STATE_CODE_ENABLED);
  return state;
}

}  // namespace

GazeboServiceState::GazeboServiceState(
    SimulatorControlService* simulator_control_service)
    : simulator_control_service_(*ABSL_DIE_IF_NULL(simulator_control_service)) {
}

grpc::Status GazeboServiceState::GetState(
    grpc::ServerContext* context,
    const intrinsic_proto::services::v1::GetStateRequest* /*request*/,
    SelfState* response) {
  INTR_ASSIGN_OR_RETURN_GRPC(
      const GetSimulatorStatusResponse status,
      GetSimulatorStatus(simulator_control_service_, context));
  switch (status.simulator_state()) {
    case SimulatorState::STOPPED:
      *response = StoppedState(status);
      break;
    case SimulatorState::PAUSED:
    case SimulatorState::RUNNING:
      *response = EnabledState();
      break;
    default:
      response->set_state_code(SelfState::STATE_CODE_UNSPECIFIED);
      break;
  }
  return grpc::Status::OK;
}

grpc::Status GazeboServiceState::Enable(
    grpc::ServerContext* context,
    const intrinsic_proto::services::v1::EnableRequest* /*request*/,
    intrinsic_proto::services::v1::EnableResponse* /*response*/) {
  absl::MutexLock lock(enable_mutex_);
  INTR_ASSIGN_OR_RETURN_GRPC(
      const GetSimulatorStatusResponse status,
      GetSimulatorStatus(simulator_control_service_, context));
  if (status.simulator_state() != SimulatorState::STOPPED) {
    // Already enabled.
    return grpc::Status::OK;
  }

  LOG(INFO) << "Enabling the Gazebo simulator service by resetting the "
               "stopped simulator.";
  intrinsic_proto::simulation::v1::ResetSimulatorRequest request;
  request.set_simulator_world_id(
      simulator_control_service_.GetSimulatorWorldId());
  request.set_start_paused(false);
  intrinsic_proto::simulation::v1::ResetSimulatorResponse response;
  return simulator_control_service_.ResetSimulator(context, &request,
                                                   &response);
}

grpc::Status GazeboServiceState::Disable(
    grpc::ServerContext* /*context*/,
    const intrinsic_proto::services::v1::DisableRequest* /*request*/,
    intrinsic_proto::services::v1::DisableResponse* /*response*/) {
  return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                      "Disabling the Gazebo simulator service is not "
                      "supported.");
}

}  // namespace simulation
}  // namespace intrinsic
