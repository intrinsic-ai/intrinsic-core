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

#ifndef INTRINSIC_HARDWARE_GRIPPER_EOAT_GRIPPER_IMPL_H_
#define INTRINSIC_HARDWARE_GRIPPER_EOAT_GRIPPER_IMPL_H_

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/support/status.h"
#include "intrinsic/assets/services/proto/v1/service_state.pb.h"
#include "intrinsic/hardware/gpio/gpio_client.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.grpc.pb.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.pb.h"
#include "intrinsic/hardware/gripper/eoat/eoat_service.pb.h"
#include "intrinsic/hardware/gripper/eoat/gripper_extended_status.h"
#include "intrinsic/hardware/gripper/eoat/gripper_utils.h"
#include "intrinsic/resources/health/health_state_machine.h"
#include "intrinsic/resources/health/operational_status.h"
#include "intrinsic/util/grpc/connection_params.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros_grpc.h"

namespace intrinsic::gripper {

// Implementation of gripper class that talks to the GPIO service to control the
// gripper, manages health reporting and fault clearing. The type of gripper
// (suction or pinch) is inferred from the gripper `Config` (e.g.
// `SuctionGripperConfig`) passed during instantiation.
// All public APIs are thread safe.

template <typename Config>
class GripperImpl {
 public:
  // Returns the GPIO signals that the gripper intends to claim exclusive access
  // to during the GRPC session. This function is only called during the
  // constructor and not copied for later use.
  using SignalsToClaimFunc =
      std::function<absl::flat_hash_set<std::string>(const Config& config)>;

  // Delays creating of client channel to the GPIO service.
  explicit GripperImpl(const Config& config,
                       const SignalsToClaimFunc& signals_func,
                       absl::string_view gpio_grpc_address)
      : config_(config),
        gpio_client_(ConnectionParams::NoIngress(gpio_grpc_address),
                     signals_func(config)) {}

  // Connects to an already running GPIO service
  explicit GripperImpl(
      const Config& config, const SignalsToClaimFunc& signals_func,
      absl::string_view gpio_service_name,
      std::unique_ptr<intrinsic_proto::gpio::v1::GPIOService::StubInterface>
          stub)
      : config_(config),
        gpio_client_(std::move(stub), gpio_service_name, signals_func(config)) {
  }

  GripperImpl(const GripperImpl&) = delete;
  GripperImpl& operator=(const GripperImpl&) = delete;
  GripperImpl(const GripperImpl&&) = delete;
  GripperImpl& operator=(const GripperImpl&&) = delete;

  ::grpc::Status Grasp() ABSL_LOCKS_EXCLUDED(public_api_mutex_) {
    absl::MutexLock lock(public_api_mutex_);
    INTR_RETURN_IF_ERROR_GRPC(RequireIsEnabled());
    return ToGrpcStatus(
        GpioWriteStatus(gpio_client_.Write(config_.grasp().value_set())));
  }

  ::grpc::Status Release() ABSL_LOCKS_EXCLUDED(public_api_mutex_) {
    absl::MutexLock lock(public_api_mutex_);
    INTR_RETURN_IF_ERROR_GRPC(RequireIsEnabled());
    return ToGrpcStatus(
        GpioWriteStatus(gpio_client_.Write(config_.release().value_set())));
  }

  // Conditionally enables the `BlowOff` method for suction grippers.
  template <typename U = Config,
            typename std::enable_if_t<std::is_same_v<
                U, intrinsic_proto::eoat::SuctionGripperConfig>>* = nullptr>
  ::grpc::Status BlowOff(const intrinsic_proto::eoat::BlowOffRequest& request)
      ABSL_LOCKS_EXCLUDED(public_api_mutex_) {
    absl::MutexLock lock(public_api_mutex_);
    INTR_RETURN_IF_ERROR_GRPC(RequireIsEnabled());
    if (request.turn_on()) {
      return ToGrpcStatus(GpioWriteStatus(
          gpio_client_.Write(config_.blowoff_on().value_set())));
    } else {
      return ToGrpcStatus(GpioWriteStatus(
          gpio_client_.Write(config_.blowoff_off().value_set())));
    }
  }

  ::grpc::Status GrippingIndicated(
      intrinsic_proto::eoat::GrippingIndicatedResponse& response)
      ABSL_LOCKS_EXCLUDED(public_api_mutex_) {
    absl::MutexLock lock(public_api_mutex_);
    INTR_RETURN_IF_ERROR_GRPC(RequireIsEnabled());
    const absl::StatusOr<bool> indicated =
        gpio_client_.ReadAndMatch(config_.gripping_indicated().value_set());
    if (!indicated.ok()) {
      return ToGrpcStatus(GpioReadStatus(indicated.status()));
    }
    response.set_indicated(*indicated);
    return grpc::Status::OK;
  }

  intrinsic_proto::services::v1::SelfState GetState() const
      ABSL_LOCKS_EXCLUDED(public_api_mutex_) {
    absl::MutexLock lock(public_api_mutex_);
    return state_machine_.GetState();
  }

  ::grpc::Status ClearFaults() ABSL_LOCKS_EXCLUDED(public_api_mutex_) {
    absl::MutexLock lock(public_api_mutex_);

    // The state machine replaces a failed action's status by its own, so the
    // cause is captured here to report it in the extended status.
    absl::Status clear_faults_result;
    auto clear_faults_action = [this, &clear_faults_result]() {
      clear_faults_result = ToAbslStatus(this->ClearFaultsAction());
      return clear_faults_result;
    };
    auto disable_action = [this]() {
      return ToAbslStatus(this->DisableGripperAction());
    };
    const absl::Status status = state_machine_.ClearFaultsAndDisable(
        clear_faults_action, disable_action);
    if (!status.ok() && !clear_faults_result.ok()) {
      return ToGrpcStatus(ClearFaultsStatus(status, clear_faults_result));
    }
    return ToGrpcStatus(status);
  }

  ::grpc::Status Enable() ABSL_LOCKS_EXCLUDED(public_api_mutex_) {
    absl::MutexLock lock(public_api_mutex_);
    return ToGrpcStatus(state_machine_.Enable());
  }

  ::grpc::Status Disable() ABSL_LOCKS_EXCLUDED(public_api_mutex_) {
    absl::MutexLock lock(public_api_mutex_);
    auto disable_action = [this]() {
      return ToAbslStatus(this->DisableGripperAction());
    };
    return ToGrpcStatus(state_machine_.Disable(disable_action));
  }

 private:
  // Returns OK if the gripper is enabled, otherwise a FAILED_PRECONDITION
  // status carrying `kGripperNotEnabled`. Not thread-safe.
  absl::Status RequireIsEnabled() const {
    return NotEnabledStatus(state_machine_.IsEnabled());
  }

  // Action to clear the fault. Not thread safe.
  ::grpc::Status ClearFaultsAction() {
    // For now, the gripper service is considered fault-free if all the signal
    // names listed in `config` are present in the signal descriptions.
    INTR_RETURN_IF_ERROR_GRPC(gpio_client_.GetSignalDescriptions().status());
    // TODO(b/356479998): Re-enable the health check.
    // return ToGrpcStatus(SignalsAreValid(config_, signal_des));
    return ::grpc::Status::OK;
  }

  // Action that needs to succeed before the gripper can be set to disabled
  // state. Not thread safe.
  ::grpc::Status DisableGripperAction() const {
    // TODO(b/262820564): Figure out the blow off vacuum for suction gripper
    // should be turned off here.
    return ::grpc::Status::OK;
  }

  const Config config_;
  ::intrinsic::gpio::GPIOClient gpio_client_;

  // Lock to make public APIs thread-safe
  mutable absl::Mutex public_api_mutex_;

  // Starts in an unspecified state until signal descriptions are retrieved from
  // the GPIO service.
  intrinsic::resources::HealthStateMachine state_machine_ =
      intrinsic::resources::HealthStateMachine(
          intrinsic::resources::OperationalState::kUnspecified, "gripper");
};

}  // namespace intrinsic::gripper

#endif  // INTRINSIC_HARDWARE_GRIPPER_EOAT_GRIPPER_IMPL_H_
