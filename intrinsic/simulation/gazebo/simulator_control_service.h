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

#ifndef INTRINSIC_SIMULATION_GAZEBO_SIMULATOR_CONTROL_SERVICE_H_
#define INTRINSIC_SIMULATION_GAZEBO_SIMULATOR_CONTROL_SERVICE_H_

#include <functional>
#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/simulation/gazebo/gazebo_runner.h"
#include "intrinsic/simulation/gazebo/profiler.h"
#include "intrinsic/simulation/simulator/proto/v1/simulator_control_service.grpc.pb.h"
#include "intrinsic/simulation/simulator/proto/v1/simulator_control_service.pb.h"

namespace intrinsic {
namespace simulation {

// Implementation of the simulator control gRPC service for Gazebo.
class SimulatorControlService final
    : public intrinsic_proto::simulation::v1::SimulatorControlService::Service {
 public:
  struct GazeboRunnerInitParams {
    // Whether gzserver is being reset. Will be false if it is being
    // initialized.
    bool is_reset;

    // The world in World service from which `GazeboRunner` should be
    // initialized. May be ignored if the server is not connected to the world
    // service.
    std::string simulator_world_id;

    // Address of the Asset Instances service. If empty, the simulation server
    // will skip querying the service for plugin specs.
    std::string asset_instances_service_address_flag;
  };

  // Function to instantiate a new GazeboRunner.
  typedef std::function<absl::StatusOr<std::unique_ptr<GazeboRunner>>(
      const GazeboRunnerInitParams&)>
      GazeboRunnerFactoryFunction;

  struct Config {
    // Whether to start Gazebo paused. By default, Gazebo will start running.
    bool start_paused = false;

    // If the `GazeboRunner` factory function returns an error when `Create` is
    // called, by default, `Create` fails and the error is returned.
    // Set to true if the error should be ignored instead. If set to true,
    // and the factory function returns an error when `Create` is called,
    // `GazeboRunner` will not be instantiated in `Create`. The error will be
    // logged and `GetSimulatorStatus` will indicate a `Stopped` state.
    // Calling `ResetSimulator` will attempt to instantiate `GazeboRunner`
    // and run Gazebo again.
    bool ignore_server_create_error_at_startup = false;

    // Whether to start Gazebo profiler to measure performance.
    bool measure_performance = false;

    // The world in World service from which `GazeboRunner` should be
    // initialized. This id is not used in this class directly, it is passed to
    // the input `GazeboRunnerFactoryFunction` as-is.
    // Subsequent `ResetSimulator` calls may pass a different simulator world
    // id.
    std::string simulator_world_id;
  };

  static absl::StatusOr<std::unique_ptr<SimulatorControlService>> Create(
      const GazeboRunnerFactoryFunction& gazebo_runner_factory_fn,
      const Config& config);

  SimulatorControlService(const SimulatorControlService&) = delete;
  SimulatorControlService& operator=(const SimulatorControlService&) = delete;
  ~SimulatorControlService() override;

  ::grpc::Status ResetSimulator(
      ::grpc::ServerContext* context,
      const intrinsic_proto::simulation::v1::ResetSimulatorRequest* request,
      intrinsic_proto::simulation::v1::ResetSimulatorResponse* response)
      override ABSL_LOCKS_EXCLUDED(gazebo_mutex_);

  ::grpc::Status PauseSimulator(
      ::grpc::ServerContext* context,
      const intrinsic_proto::simulation::v1::PauseSimulatorRequest* request,
      intrinsic_proto::simulation::v1::PauseSimulatorResponse* response)
      override ABSL_LOCKS_EXCLUDED(gazebo_mutex_);

  ::grpc::Status UnpauseSimulator(
      ::grpc::ServerContext* context,
      const intrinsic_proto::simulation::v1::UnpauseSimulatorRequest* request,
      intrinsic_proto::simulation::v1::UnpauseSimulatorResponse* response)
      override ABSL_LOCKS_EXCLUDED(gazebo_mutex_);

  ::grpc::Status GetSimulatorStatus(
      ::grpc::ServerContext* context,
      const intrinsic_proto::simulation::v1::GetSimulatorStatusRequest* request,
      intrinsic_proto::simulation::v1::GetSimulatorStatusResponse* response)
      override ABSL_LOCKS_EXCLUDED(gazebo_mutex_);

  // Returns the simulator world id that was used most recently to initialize
  // `GazeboRunner`, i.e. the id from `Config` or from the latest
  // `ResetSimulator` request.
  std::string GetSimulatorWorldId() ABSL_LOCKS_EXCLUDED(gazebo_mutex_);

 private:
  SimulatorControlService(
      std::unique_ptr<GazeboRunner> gazebo_runner,
      const GazeboRunnerFactoryFunction& gazebo_runner_factory_fn,
      Profiler* sim_step_profiler, absl::Status run_gazebo_status,
      std::string simulator_world_id);

  absl::Mutex gazebo_mutex_;
  std::unique_ptr<GazeboRunner> gazebo_runner_ ABSL_GUARDED_BY(gazebo_mutex_);
  GazeboRunnerFactoryFunction gazebo_runner_factory_fn_;
  Profiler* sim_step_profiler_ = nullptr;
  absl::Status last_run_gazebo_status_ = absl::OkStatus();
  std::string simulator_world_id_ ABSL_GUARDED_BY(gazebo_mutex_);
};

}  // namespace simulation
}  // namespace intrinsic

#endif  // INTRINSIC_SIMULATION_GAZEBO_SIMULATOR_CONTROL_SERVICE_H_
