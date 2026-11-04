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

#ifndef INTRINSIC_HARDWARE_GRIPPER_SERVICE_PINCH_GRIPPER_SERVER_ASYNC_CALLBACK_IMPL_H_
#define INTRINSIC_HARDWARE_GRIPPER_SERVICE_PINCH_GRIPPER_SERVER_ASYNC_CALLBACK_IMPL_H_

#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/assets/services/proto/v1/service_state.pb.h"
#include "intrinsic/hardware/gripper/gripper.h"
#include "intrinsic/hardware/gripper/service/pinch_gripper_extended_status.h"
#include "intrinsic/hardware/gripper/service/proto/pinch_gripper_server.grpc.pb.h"
#include "intrinsic/hardware/gripper/service/proto/pinch_gripper_server.pb.h"
#include "intrinsic/resources/health/health_state_machine.h"
#include "intrinsic/util/thread/thread_pool.h"

namespace intrinsic::gripper {

// We use a Callback-based Asynchronous Server model for the Pinch Gripper gRPC
// because once a pinch gripper is created, it needs to keep sending the last
// command to the gripper hardware periodically to maintain connection with
// the gripper, and while doing so it cannot block on the RPC.
class PinchGripperServerAsyncCallbackImpl final
    : public ::intrinsic_proto::gripper::PinchGripperServer::CallbackService {
 public:
  // Creates and initializes the server, a thread pool is created to handle
  // periodic/cyclic gripper commander.
  // The provided config is always overrides the one provided by the client as
  // part of `CreatePinchGripper` call.
  explicit PinchGripperServerAsyncCallbackImpl(
      const intrinsic_proto::gripper::PinchGripperConfig& config,
      absl::string_view pinch_gripper_type,
      std::optional<absl::Duration> default_timeout = std::nullopt);

  // Create a pinch gripper based on the specified network address, config, and
  // a few additional parameters, and return a handle to the created gripper.
  // Once created, this call keeps on running as long as the created pinch
  // gripper stays alive, because it runs a CyclicRunner that periodically
  // sends out the last command to the gripper to maintain connection (and
  // avoids a timeout).
  // The config in `request` is ignored if a config is provided at the time of
  // instantiating the class object.
  grpc::ServerUnaryReactor* CreatePinchGripper(
      grpc::CallbackServerContext* context,
      const ::intrinsic_proto::gripper::CreatePinchGripperRequest* request,
      ::intrinsic_proto::gripper::CreatePinchGripperResponse* response) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Reset a created gripper as specified by the gripper handle.
  grpc::ServerUnaryReactor* ResetPinchGripper(
      grpc::CallbackServerContext* context,
      const ::intrinsic_proto::gripper::ResetPinchGripperRequest* request,
      ::intrinsic_proto::gripper::ResetPinchGripperResponse* response) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Send a command to a created gripper as specified by the gripper handle,
  // and return a status of the execution.
  grpc::ServerUnaryReactor* CommandPinchGripper(
      grpc::CallbackServerContext* context,
      const ::intrinsic_proto::gripper::CommandPinchGripperRequest* request,
      ::intrinsic_proto::gripper::CommandPinchGripperResponse* response)
      override ABSL_LOCKS_EXCLUDED(mutex_);

  // Get the status of a created gripper as specified by the gripper handle.
  grpc::ServerUnaryReactor* GetPinchGripperStatus(
      grpc::CallbackServerContext* context,
      const ::intrinsic_proto::gripper::GetPinchGripperStatusRequest* request,
      ::intrinsic_proto::gripper::GetPinchGripperStatusResponse* response)
      override ABSL_LOCKS_EXCLUDED(mutex_);

  // Initializes the gripper and, if successful, enables it.
  grpc::Status InitializeAndEnable() ABSL_LOCKS_EXCLUDED(mutex_);

  // Methods that implement ServiceState (previously resource health service).
  intrinsic_proto::services::v1::SelfState GetState() const
      ABSL_LOCKS_EXCLUDED(mutex_);
  grpc::Status Enable() ABSL_LOCKS_EXCLUDED(mutex_);
  grpc::Status Disable() ABSL_LOCKS_EXCLUDED(mutex_);
  grpc::Status ClearFaults() ABSL_LOCKS_EXCLUDED(mutex_);

 private:
  // Get a gripper given its handle, return an error if the gripper has not been
  // created previously.
  absl::StatusOr<PinchGripperInterface*> GetPinchGripper() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // The "Internal" versions of the public unary RPC methods are defined to
  // follow a pattern of invoking the core logic of each RPC on the thread pool
  // while keeping a clear control-flow. This makes it impossible for the
  // developers/maintainers of this code to forget to call reactor->Finish() if
  // an error occurs during execution.

  // Implements the internals of the CreatePinchGripper RPC.
  absl::Status CreatePinchGripperInternal(
      ::intrinsic_proto::gripper::CreatePinchGripperResponse* response)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Implements the internals of the ResetPinchGripper RPC.
  absl::Status ResetPinchGripperInternal(
      ::intrinsic_proto::gripper::ResetPinchGripperResponse* response)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Implements the internals of the CommandPinchGripper RPC.
  absl::Status CommandPinchGripperInternal(
      const ::intrinsic_proto::gripper::CommandPinchGripperRequest* request,
      ::intrinsic_proto::gripper::CommandPinchGripperResponse* response)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Implements the internals of the GetPinchGripperStatus RPC.
  absl::Status GetPinchGripperStatusInternal(
      const ::intrinsic_proto::gripper::GetPinchGripperStatusRequest* request,
      ::intrinsic_proto::gripper::GetPinchGripperStatusResponse* response)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Returns OK if the gripper is enabled, otherwise a FAILED_PRECONDITION
  // status carrying `kPinchGripperNotEnabled`.
  absl::Status RequireIsEnabled() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    return PinchGripperNotEnabledStatus(state_machine_.IsEnabled());
  }

  // Enables the underlying gripper.
  absl::Status EnableGripperAction() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Disables the underlying gripper.
  absl::Status DisableGripperAction() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Resets and disables the underlying gripper.
  absl::Status ClearFaultsAction() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  mutable absl::Mutex mutex_;

  // Handle to the underlying gripper implementation.
  std::unique_ptr<PinchGripperInterface> pinch_gripper_ ABSL_GUARDED_BY(mutex_);

  // Thread pool for handling RunCyclicGripperCommander invocations inside
  // CreatePinchGripper.
  // NOTE: This stores a raw pointer to `pinch_gripper_` and hence should be
  // destroyed before it (which is guaranteed by having it declared after
  // `pinch_gripper_` in the class declaration).
  std::unique_ptr<ThreadPool> cyclic_commander_pool_;

  // Type of the underlying pinch gripper. This is mainly useful in unit testing
  // to enable testing with a stub pinch gripper implementation.
  const std::string pinch_gripper_type_;

  // Configuration used to create a gripper handle.
  const intrinsic_proto::gripper::PinchGripperConfig config_;

  const absl::Duration default_timeout_;

  // Starts the state machine in an unspecified state.
  intrinsic::resources::HealthStateMachine state_machine_
      ABSL_GUARDED_BY(mutex_) = intrinsic::resources::HealthStateMachine(
          intrinsic::resources::OperationalState::kUnspecified,
          "pinch_gripper");
};

}  // namespace intrinsic::gripper

#endif  // INTRINSIC_HARDWARE_GRIPPER_SERVICE_PINCH_GRIPPER_SERVER_ASYNC_CALLBACK_IMPL_H_
