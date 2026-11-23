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

#include "intrinsic/simulation/gazebo/simulator_control_service.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "gz/sim/Entity.hh"
#include "gz/sim/System.hh"
#include "intrinsic/simulation/gazebo/asset_instances_service_address_flag.h"
#include "intrinsic/simulation/gazebo/gazebo_runner.h"
#include "intrinsic/simulation/gazebo/plugins/add_hardware_module_launchers_system.h"
#include "intrinsic/simulation/gazebo/profiler.h"
#include "intrinsic/simulation/simulator/proto/v1/simulator_control_service.pb.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/ret_check_grpc.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"

ABSL_FLAG(
    bool, skip_asset_instances_lookup_testonly, false,
    "If true, skip querying Asset instances service for hardware module and "
    "gripper plugin specs");

namespace intrinsic {
namespace simulation {
constexpr int kStepsBeforeResetIsDone = 2;

// Time to wait for the simulator to take kStepsBeforeResetIsDone before
// finishing reset.
constexpr absl::Duration kWaitForStepsTimeout = absl::Seconds(5);

namespace {

// Adds an `AddHardwareModuleLaunchersSystem` to Gazebo, and calls
// its `AddHardwareModuleLaunchers()` method.
//
// Returns InternalError if adding the system fails.
// Forwards any errors from
// `add_hwm_launchers_system->AddHardwareModuleLaunchers()`.
absl::Status AddHardwareModuleLaunchersToSimServer(
    GazeboRunner& gazebo_runner) {
  auto add_hwm_launchers_system =
      std::make_shared<AddHardwareModuleLaunchersSystem>();
  INTR_RET_CHECK_OK(gazebo_runner.AddSystem(add_hwm_launchers_system))
          .LogError()
      << "Failed to add AddHardwareModuleLaunchersSystem to Gazebo.";

  INTR_RETURN_IF_ERROR(
      add_hwm_launchers_system->AddHardwareModuleLaunchers(
          [&gazebo_runner](std::shared_ptr<gz::sim::System> system,
                           gz::sim::Entity entity) -> absl::Status {
            return gazebo_runner.AddSystem(system, entity);
          }))
          .LogError()
      << "Failed to add HardwareModuleLauncher systems to Gazebo.";

  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::unique_ptr<SimulatorControlService>>
SimulatorControlService::Create(
    const GazeboRunnerFactoryFunction& gazebo_runner_factory_fn,
    const Config& config) {
  absl::StatusOr<std::unique_ptr<GazeboRunner>> gazebo_runner_maybe =
      gazebo_runner_factory_fn(GazeboRunnerInitParams{
          .is_reset = false,
          .simulator_world_id = config.simulator_world_id,
          .asset_instances_service_address_flag =
              absl::GetFlag(FLAGS_skip_asset_instances_lookup_testonly)
                  ? ""
                  : absl::GetFlag(FLAGS_asset_instances_service_address)});
  if (!gazebo_runner_maybe.ok() &&
      !config.ignore_server_create_error_at_startup) {
    return gazebo_runner_maybe.status();
  }

  absl::Status create_gazebo_error_status = absl::OkStatus();
  if (!gazebo_runner_maybe.ok()) {
    LOG(WARNING) << "Failed to create GazeboRunner at startup: "
                 << gazebo_runner_maybe.status() << ". Continuing anyway.";
    create_gazebo_error_status = gazebo_runner_maybe.status();
  }

  std::unique_ptr<GazeboRunner> gazebo_runner =
      std::move(gazebo_runner_maybe).value_or(nullptr);

  Profiler* sim_step_profiler = nullptr;
  if (config.measure_performance) {
    // Create profiler before running the simulation server so that the
    // first step is captured in performance metrics published from the
    // profiler.
    INTR_ASSIGN_OR_RETURN(sim_step_profiler,
                          Profiler::CreateAndRegisterProfiler());
    if (gazebo_runner != nullptr) {
      sim_step_profiler->SetSimulationStepSize(gazebo_runner->TimeStepSize());
    }
  }

  if (gazebo_runner != nullptr) {
    if (!absl::GetFlag(FLAGS_skip_asset_instances_lookup_testonly) &&
        !absl::GetFlag(FLAGS_asset_instances_service_address).empty()) {
      INTR_RETURN_IF_ERROR(
          AddHardwareModuleLaunchersToSimServer(*gazebo_runner));
    }
    INTR_RET_CHECK_OK(gazebo_runner->Run(config.start_paused));
  }

  return absl::WrapUnique(new SimulatorControlService(
      std::move(gazebo_runner), gazebo_runner_factory_fn, sim_step_profiler,
      std::move(create_gazebo_error_status), config.simulator_world_id));
}

SimulatorControlService::SimulatorControlService(
    std::unique_ptr<GazeboRunner> gazebo_runner,
    const GazeboRunnerFactoryFunction& gazebo_runner_factory_fn,
    Profiler* sim_step_profiler, absl::Status run_gazebo_status,
    std::string simulator_world_id)
    : gazebo_runner_(std::move(gazebo_runner)),
      gazebo_runner_factory_fn_(gazebo_runner_factory_fn),
      sim_step_profiler_(sim_step_profiler),
      last_run_gazebo_status_(std::move(run_gazebo_status)),
      simulator_world_id_(std::move(simulator_world_id)) {}

std::string SimulatorControlService::GetSimulatorWorldId() {
  absl::MutexLock lock(gazebo_mutex_);
  return simulator_world_id_;
}

SimulatorControlService::~SimulatorControlService() = default;

::grpc::Status SimulatorControlService::ResetSimulator(
    ::grpc::ServerContext* context,
    const intrinsic_proto::simulation::v1::ResetSimulatorRequest* request,
    intrinsic_proto::simulation::v1::ResetSimulatorResponse* /*unused*/) {
  LOG(INFO) << "Received simulation reset command.";

  absl::MutexLock lock(gazebo_mutex_);
  // Create and run a new simulation server. But need to delete the current
  // one first to clean up shared resources properly.
  gazebo_runner_.reset();

  if (sim_step_profiler_ != nullptr) {
    // Reset the profiler before starting a new simulation server.
    sim_step_profiler_->Reset();
  }

  if (context->IsCancelled()) {
    LOG(INFO) << "Simulation reset was cancelled after Gazebo was stopped.";
    return ToGrpcStatus(absl::CancelledError());
  }

  simulator_world_id_ = request->simulator_world_id();

  auto set_last_error = [this](absl::Status status) {
    if (!status.ok()) {
      last_run_gazebo_status_ = status;
    }
    return ToGrpcStatus(status);
  };

  INTR_ASSIGN_OR_RETURN_GRPC(
      gazebo_runner_,
      gazebo_runner_factory_fn_(GazeboRunnerInitParams{
          .is_reset = true,
          .simulator_world_id = request->simulator_world_id(),
          .asset_instances_service_address_flag =
              absl::GetFlag(FLAGS_skip_asset_instances_lookup_testonly)
                  ? ""
                  : absl::GetFlag(FLAGS_asset_instances_service_address)}),
      _.With(set_last_error));
  if (!absl::GetFlag(FLAGS_skip_asset_instances_lookup_testonly) &&
      !absl::GetFlag(FLAGS_asset_instances_service_address).empty()) {
    INTR_RETURN_IF_ERROR_GRPC(
        AddHardwareModuleLaunchersToSimServer(*gazebo_runner_))
        .With(set_last_error);
  }
  INTR_RET_CHECK_OK_GRPC(
      gazebo_runner_->Run(/*start_paused=*/request->start_paused()))
      .With(set_last_error);

  if (gazebo_runner_->IsRunning()) {
    last_run_gazebo_status_ = absl::OkStatus();
  }

  if (context->IsCancelled()) {
    bool is_paused = gazebo_runner_->IsPaused();
    LOG(INFO) << "Simulation reset was cancelled after Gazebo started running "
              << (is_paused ? "paused." : "unpaused.");
    return ToGrpcStatus(absl::CancelledError());
  }

  // Wait for the server to run a few iterations so that connected resource
  // services have a chance to process state changes due to the reset before
  // finishing the call. We need at least 2 iterations for this because
  // - Gazebo systems will finish initializing and update the ECM updates on the
  //   first iteration,
  // - resource services will read these changes and apply updates, and
  // - these updates will be applied to the ECM on the second iteration.
  absl::StatusOr<uint64_t> iterations_or =
      gazebo_runner_->WaitForAtLeastIterationsUntil(
          kStepsBeforeResetIsDone,
          /*deadline=*/absl::Now() + kWaitForStepsTimeout);

  if (iterations_or.status().code() == absl::StatusCode::kDeadlineExceeded) {
    // TODO(b/305305692): There are a few cases today where sim reset is
    // expected to work even if the simulator won't step at all. Once these
    // cases are fixed, we can return an error.
    LOG(ERROR) << "Failed to step simulation within deadline";
  } else if (!iterations_or.ok()) {
    return ToGrpcStatus(iterations_or.status());
  }

  LOG(INFO) << "Finished resetting simulation";
  return ::grpc::Status::OK;
}

::grpc::Status SimulatorControlService::PauseSimulator(
    ::grpc::ServerContext* context,
    const intrinsic_proto::simulation::v1::PauseSimulatorRequest* /*unused*/,
    intrinsic_proto::simulation::v1::PauseSimulatorResponse* /*unused*/) {
  LOG(INFO) << "Pausing simulation";
  absl::MutexLock lock(gazebo_mutex_);
  if (gazebo_runner_ == nullptr) {
    return ToGrpcStatus(absl::FailedPreconditionError(
        "Simulation server is stopped. Call Reset() before trying again."));
  }
  gazebo_runner_->Pause();
  return ::grpc::Status::OK;
}

::grpc::Status SimulatorControlService::UnpauseSimulator(
    ::grpc::ServerContext* context,
    const intrinsic_proto::simulation::v1::UnpauseSimulatorRequest* /*unused*/,
    intrinsic_proto::simulation::v1::UnpauseSimulatorResponse* /*unused*/) {
  LOG(INFO) << "Unpausing simulation";
  absl::MutexLock lock(gazebo_mutex_);
  if (gazebo_runner_ == nullptr) {
    return ToGrpcStatus(
        absl::FailedPreconditionError("Simulation server is stopped. Call "
                                      "`ResetSimulator` before trying again."));
  }
  gazebo_runner_->Unpause();
  return ::grpc::Status::OK;
}

::grpc::Status SimulatorControlService::GetSimulatorStatus(
    ::grpc::ServerContext* context,
    const intrinsic_proto::simulation::v1::GetSimulatorStatusRequest* request,
    intrinsic_proto::simulation::v1::GetSimulatorStatusResponse* response) {
  absl::MutexLock lock(gazebo_mutex_);
  if (gazebo_runner_ == nullptr || !gazebo_runner_->IsRunning()) {
    response->set_simulator_state(
        intrinsic_proto::simulation::v1::SimulatorState::STOPPED);
    if (!last_run_gazebo_status_.ok()) {
      *response->mutable_status() = ToGoogleRpcStatus(last_run_gazebo_status_);
    }
    return ::grpc::Status::OK;
  }

  if (gazebo_runner_->IsPaused()) {
    response->set_simulator_state(
        intrinsic_proto::simulation::v1::SimulatorState::PAUSED);
    return ::grpc::Status::OK;
  }

  response->set_simulator_state(
      intrinsic_proto::simulation::v1::SimulatorState::RUNNING);
  return ::grpc::Status::OK;
}

}  // namespace simulation
}  // namespace intrinsic
