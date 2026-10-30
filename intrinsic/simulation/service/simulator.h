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

#ifndef INTRINSIC_SIMULATION_SERVICE_SIMULATOR_H_
#define INTRINSIC_SIMULATION_SERVICE_SIMULATOR_H_

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "grpcpp/client_context.h"
#include "intrinsic/simulation/service/proto/first_party/simulation_service.pb.h"

namespace intrinsic {
namespace simulation {

// Client interface for controlling and querying a remote simulator.
class Simulator {
 public:
  Simulator() = default;
  virtual ~Simulator() = default;

  // Disallow copy and assign.
  Simulator(const Simulator&) = delete;
  const Simulator& operator=(const Simulator&) = delete;

  // Returns a human readable name that describes the type of simulator.
  virtual std::string GetName() const = 0;

  struct ResetParams {
    // Whether the simulator should be in a `Paused` state after `Reset`.
    bool start_paused = false;

    // The world in World Service from which scene data should be fetched to
    // reset the simulator.
    std::string simulator_world_id;
  };

  virtual absl::Status Reset(const ResetParams& reset_request,
                             std::unique_ptr<::grpc::ClientContext> ctx) = 0;

  // The given client contexts determine the deadline and cancellation of the
  // requests to the simulator, e.g., via
  // `grpc::ClientContext::FromServerContext`.
  virtual absl::Status Pause(std::unique_ptr<::grpc::ClientContext> ctx) = 0;

  virtual absl::Status Unpause(std::unique_ptr<::grpc::ClientContext> ctx) = 0;

  virtual absl::StatusOr<
      ::intrinsic_proto::simulation::first_party::GetSimulationStatusResponse>
  GetStatus(std::unique_ptr<::grpc::ClientContext> ctx) = 0;
};

}  // namespace simulation
}  // namespace intrinsic

#endif  // INTRINSIC_SIMULATION_SERVICE_SIMULATOR_H_
