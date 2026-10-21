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

#include "intrinsic/icon/hardware_modules/kuka_rsi/kuka_rsi_client.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/hal/proto/hardware_module_inspection.pb.h"
#include "intrinsic/icon/hardware_modules/kuka_rsi/kuka_rsi_inspection.pb.h"
#include "intrinsic/icon/hardware_modules/kuka_rsi/kuka_rsi_util.h"
#include "intrinsic/icon/hardware_modules/kuka_rsi/kuka_system_control/kuka_system_control_factory.h"
#include "intrinsic/icon/hardware_modules/kuka_rsi/kuka_system_control/kuka_system_control_interface.h"
#include "intrinsic/icon/hardware_modules/kuka_rsi/kuka_system_status/kuka_system_status_factory.h"
#include "intrinsic/icon/interprocess/lockable_binary_futex.h"
#include "intrinsic/icon/proto/safety_status.pb.h"
#include "intrinsic/icon/testing/realtime_annotations.h"
#include "intrinsic/icon/utils/bitmask_enums.h"
#include "intrinsic/icon/utils/fixed_string.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/icon/utils/xml/realtime_xml_parser_generator_util.h"
#include "intrinsic/util/fixed_vector.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/rt_thread.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/util/thread/thread_options.h"

namespace intrinsic::kuka {

using intrinsic::icon::RealtimeStatus;

constexpr absl::Duration kStartRSITimeout = absl::Seconds(10);
constexpr absl::Duration kRsiActiveStatePollInterval = absl::Milliseconds(1);
constexpr double kFaultHandlingFrequency = 2.0;
constexpr size_t kEventHistoryMaxSize = 20;

namespace {
icon::FixedString<RealtimeStatus::kMaxMessageLength> ModesToString(
    absl::Span<const OpMode> modes) {
  icon::FixedString<RealtimeStatus::kMaxMessageLength> string;
  absl::string_view separator = ", ";
  for (size_t i = 0; i < modes.size(); ++i) {
    string.append(absl::string_view(OpModeToString(modes.at(i))));
    if (i < modes.size() - 1) {
      string.append(separator);
    }
  }
  return string;
}
}  // namespace

RealtimeKukaRsiClient::~RealtimeKukaRsiClient() {
  if (!shutdown_requested_) {
    Shutdown().IgnoreError();
  }
  if (kuka_system_status_monitor_thread_.joinable()) {
    kuka_system_status_monitor_thread_.join();
  }
}

absl::Status RealtimeKukaRsiClient::Init(const KukaConfig& config) {
  // This function must only return fatal errors since the whole HWM will stop
  // on an init error.
  config_ = config;
  if (!kuka_system_control_) {
    INTR_ASSIGN_OR_RETURN(kuka_system_control_,
                          CreateKukaSystemControl(config));
  }

  if (!kuka_system_status_) {
    INTR_ASSIGN_OR_RETURN(kuka_system_status_, CreateKukaSystemStatus(config));
  }

  INTR_RETURN_IF_ERROR(rsi_communicator_.Init(config));

  // This is not technically a real-time thread and when intrinsic::Thread
  // offers an API to specify a thread name, that API is recommended instead.
  INTR_ASSIGN_OR_RETURN(
      kuka_system_status_monitor_thread_,
      CreateRealtimeCapableThread(
          intrinsic::ThreadOptions().SetName("kuka_sys_status"),
          [this] { NonRealtimeThreadJob(); }));

  return absl::OkStatus();
}

void RealtimeKukaRsiClient::NonRealtimeThreadJob() {
  while (!this->shutdown_requested_) {
    absl::SleepFor(absl::Seconds(1.0 / kFaultHandlingFrequency));
    if (reset_fault_to_report_) {
      icon::BinaryFutexLock lock(&fault_to_report_mutex_);
      reset_fault_to_report_ = false;
      fault_to_report_ = icon::OkStatus();
    }

    KukaErrorFlagsMask error_flags = KukaErrorFlagsMask::kNone;
    std::vector<std::string> additional_error_messages;

    // Explicitly wait for new data, so that we do not read old faults which
    // might not be active anymore.
    if (kuka_system_status_->WaitForNewData(absl::Seconds(5))) {
      absl::StatusOr<KukaErrorFlagsMask> status_error_flags =
          kuka_system_status_->ActiveErrorFlags();
      if (!status_error_flags.ok()) {
        LOG_EVERY_N_SEC(WARNING, 5) << "Failed to read KUKA error flags";
      }
      error_flags |= status_error_flags.value();
    } else {
      // We reset the fault storage to make sure that we do not report old
      // faults which might not be active anymore. Since we are on a local
      // network, the network connection should not be unstable. So, if the
      // cable is plugged in and the configuration is correct, we should never
      // time out. In the rare case that the connection is unstable (e.g. system
      // overload), the fault storage will be refilled in the next cycle.
      kuka_system_status_->ResetFaultStorage();
      LOG(WARNING)
          << "Timed out waiting for '" << kuka_system_status_->Name()
          << "' system status interface - faults might not get reported";
    }
    additional_error_messages = kuka_system_status_->ErrorMessages();
    absl::StatusOr<KukaErrorFlagsMask> control_error_flags =
        kuka_system_control_->ActiveErrorFlags();
    if (!control_error_flags.ok()) {
      LOG_EVERY_N_SEC(WARNING, 5) << "Failed to read KUKA error flags: "
                                  << control_error_flags.status();
    } else {
      error_flags |= control_error_flags.value();
    }

    std::vector<std::string> error_messages;

    absl::StatusOr<OpMode> current_op_mode =
        kuka_system_status_->CurrentOpMode();
    FixedVector<OpMode, OpModeCount> compatible_modes =
        kuka_system_control_->CompatibleOperationModes();
    if (current_op_mode.ok() &&
        absl::c_find(compatible_modes, *current_op_mode) ==
            compatible_modes.end()) {
      error_messages.push_back(absl::StrCat(
          "Wrong operation mode: ", OpModeToString(*current_op_mode),
          " Compatible modes: ",
          absl::string_view(ModesToString(compatible_modes))));
    }

    if (HasBitSet(error_flags, KukaErrorFlagsMask::kMessagesPresent)) {
      error_messages.push_back("KUKA messages present");
    }
    if (HasBitSet(error_flags,
                  KukaErrorFlagsMask::kEmergencyStopActiveOrWrongOpMode) &&
        // Don't report emergency stop twice.
        !HasBitSet(error_flags, KukaErrorFlagsMask::kEmergencyStopActive) &&
        !HasBitSet(error_flags,
                   KukaErrorFlagsMask::kInternalEmergencyStopActive)) {
      error_messages.push_back("Emergency stop or wrong op mode");
    }
    if (HasBitSet(error_flags, KukaErrorFlagsMask::kEmergencyStopActive)) {
      error_messages.push_back("Emergency stop active");
    }
    if (HasBitSet(error_flags,
                  KukaErrorFlagsMask::kInternalEmergencyStopActive)) {
      error_messages.push_back("Internal emergency stop active");
    }
    if (HasBitSet(error_flags, KukaErrorFlagsMask::kOperatorSafetyOpen)) {
      error_messages.push_back("Operator safety open");
    }
    if (HasBitSet(error_flags, KukaErrorFlagsMask::kVersionMismatch)) {
      error_messages.push_back("Version mismatch (see logs for details)");
    }

    // Insert additional messages last since the other messages are likely more
    // interesting.
    error_messages.insert(error_messages.end(),
                          additional_error_messages.begin(),
                          additional_error_messages.end());

    if (!error_messages.empty()) {
      // If locked, it is most likely that the
      // faults are currently being cleared. Then we should skip and get errors
      // when fault clearing is done.
      if (fault_to_report_mutex_.TryLock()) {
        if (!reset_fault_to_report_) {
          std::string full_fault_str = absl::StrCat(
              "KUKA fault(s): ", absl::StrJoin(error_messages, "; "));
          fault_to_report_ = icon::UnavailableError(
              GetLimitedString<RealtimeStatus::kMaxMessageLength>(
                  full_fault_str));
        }
        if (!fault_to_report_mutex_.Unlock().ok()) {
          LOG_EVERY_N_SEC(ERROR, 5)
              << "Failed to unlock fault_to_report_mutex_.";
        }
      }
    }
    {
      const bool emergency_stop_active =
          HasBitSet(error_flags, KukaErrorFlagsMask::kEmergencyStopActive) ||
          HasBitSet(error_flags,
                    KukaErrorFlagsMask::kInternalEmergencyStopActive);
      UpdateInspectionData(
          error_messages, emergency_stop_active,
          current_op_mode.ok() ? *current_op_mode : OpMode::kNone);
    }
  }
}

absl::Status RealtimeKukaRsiClient::Shutdown() {
  shutdown_requested_ = true;
  LOG(INFO) << "Shutting down RealtimeKukaRsiClient";
  absl::Status result = StopRSI();
  INTR_RETURN_IF_ERROR(rsi_communicator_.Shutdown());
  if (kuka_system_status_monitor_thread_.joinable()) {
    kuka_system_status_monitor_thread_.join();
  }
  return result;
}

RsiTelemetry RealtimeKukaRsiClient::GetTelemetry() const {
  RsiTelemetry telemetry = rsi_communicator_.GetTelemetry();
  // If RSI is not active anymore, no new data will arrive. However, some
  // KukaSystemStatusInterface implementations can deliver.
  // In this case, overwrite the outdated data of RsiTelemetry.
  if (!rsi_communicator_.IsActive()) {
    icon::RealtimeStatusOr<eigenmath::Vectord<kKukaNumJoints>> position =
        kuka_system_status_->CurrentPosition();
    if (position.ok()) {
      const eigenmath::Vectord<kKukaNumJoints> position_value =
          position.value();
      static_assert(telemetry.position.size() ==
                    position_value.MaxSizeAtCompileTime);
      for (size_t i = 0; i < kKukaNumJoints; ++i) {
        telemetry.position[i] = position.value()[i];
      }
    }
  }
  return telemetry;
}

intrinsic::icon::RealtimeStatus RealtimeKukaRsiClient::SendCommand(
    absl::Span<const double> joint_positions,
    absl::Span<const bool> digital_outputs) {
  auto failure_mode_cleanup = absl::MakeCleanup([this] {
    // Things to do in every failure case.
    rsi_communicator_.Disable();
  });

  const OpMode op_mode = rsi_communicator_.GetTelemetry().op_mode;
  FixedVector<OpMode, OpModeCount> compatible_modes =
      kuka_system_control_->CompatibleOperationModes();
  if (absl::c_find(compatible_modes, op_mode) == compatible_modes.end()) {
    return intrinsic::icon::FailedPreconditionError(RealtimeStatus::StrCat(
        "Wrong operation mode: ", OpModeToString(op_mode),
        " Compatible modes: ", ModesToString(compatible_modes)));
  }
  INTRINSIC_RT_RETURN_IF_ERROR(
      rsi_communicator_.SetCommand(joint_positions, digital_outputs));

  std::move(failure_mode_cleanup).Cancel();
  return intrinsic::icon::OkStatus();
}

absl::Status RealtimeKukaRsiClient::ClearFaults() {
  if (!kuka_system_control_) {
    return absl::InternalError(
        "Cannot clear faults because KUKA system control is not initialized.");
  }
  icon::BinaryFutexLock lock(
      &fault_to_report_mutex_);  // Keep mutex for whole function so that no
                                 // new faults can be reported until clearing
                                 // faults is finished.
  fault_to_report_ = icon::OkStatus();
  auto status = kuka_system_control_->ClearFaults();
  kuka_system_status_->ResetFaultStorage();
  reset_fault_to_report_ = true;  // Explicitly reset fault during next update
                                  // cycle so that there is no fault lingering.
  return status;
}

absl::Status RealtimeKukaRsiClient::StartRSI(absl::Time deadline) {
  if (!kuka_system_control_) {
    return absl::InternalError(
        "Cannot start RSI because KUKA system control is not initialized.");
  }
  LOG(INFO) << "Starting RSI";
  INTR_RETURN_IF_ERROR(kuka_system_control_->StartRSI(deadline - absl::Now()));

  LOG(INFO) << "Blocking until observing data from the RSI "
               "interface or timeout reached.";
  INTR_RETURN_IF_ERROR(WaitForRSIStart(deadline));
  LOG(INFO) << "RSI is now running.";
  return absl::OkStatus();
}

absl::Status RealtimeKukaRsiClient::StopRSI() {
  if (!kuka_system_control_) {
    return absl::InternalError(
        "Cannot stop RSI because KUKA system control is not initialized.");
  }
  return kuka_system_control_->StopRSI();
}

absl::Status RealtimeKukaRsiClient::WaitForRSIStart(absl::Time deadline) {
  // Wait for RSI data.
  while (!rsi_communicator_.IsActive()) {
    if (absl::Now() >= deadline) {
      return absl::DeadlineExceededError("RSI start timed out.");
    }
    absl::SleepFor(kRsiActiveStatePollInterval);
  }
  return absl::OkStatus();
}

absl::Status RealtimeKukaRsiClient::Prepare() INTRINSIC_NON_REALTIME_ONLY {
  return rsi_communicator_.Prepare();
}

icon::RealtimeStatus RealtimeKukaRsiClient::Activate() {
  reset_fault_to_report_ = true;
  INTRINSIC_RT_LOG(INFO) << "Activating RSI client";
  return rsi_communicator_.Activate();
}

icon::RealtimeStatus RealtimeKukaRsiClient::Deactivate() {
  INTRINSIC_RT_LOG(INFO) << "Deactivating RSI client";
  return rsi_communicator_.Deactivate();
}

absl::Status RealtimeKukaRsiClient::Enable() {
  LOG(INFO) << "Enabling RSI client";
  INTR_RETURN_IF_ERROR(StartRSI(absl::Now() + kStartRSITimeout));
  // Only request enabling when we actually received RSI data.
  return icon::OkStatus();
}

RealtimeStatus RealtimeKukaRsiClient::Enabled() {
  rsi_communicator_.Enable();
  return icon::OkStatus();
}

RealtimeStatus RealtimeKukaRsiClient::Disabled() {
  rsi_communicator_.Disable();
  return icon::OkStatus();
}

absl::Status RealtimeKukaRsiClient::Disable() { return StopRSI(); }

icon::RealtimeStatus RealtimeKukaRsiClient::GetFault() const {
  // We try to lock the variable. If not possible, we will try again next
  // cycle to keep real-time behavior.
  if (fault_to_report_mutex_.TryLock()) {
    icon::RealtimeStatus fault = fault_to_report_;
    INTRINSIC_RT_RETURN_IF_ERROR(fault_to_report_mutex_.Unlock());
    return fault;
  }
  return icon::OkStatus();
}

void RealtimeKukaRsiClient::UpdateInspectionData(
    const std::vector<std::string>& error_messages, bool emergency_stop_active,
    std::optional<OpMode> op_mode) {
  absl::MutexLock lock(inspection_data_mutex_);
  inspection_data_.error_messages = error_messages;
  inspection_data_.emergency_stop_active = emergency_stop_active;
  inspection_data_.op_mode = op_mode;
  for (const auto& error_message : error_messages) {
    if (error_message.empty()) {
      continue;
    }

    // Only add the error message to the event history if it is not already
    // present.
    if (absl::c_count_if(inspection_data_.event_history,
                         [&error_message](const auto& event) {
                           return absl::EqualsIgnoreCase(event.message(),
                                                         error_message);
                         }) != 0) {
      continue;
    }

    intrinsic_proto::icon::v1::Event event;
    event.set_message(error_message);
    // Since we cannot get the timestamp when the error occurred, we set
    // the timestamp to the current time.
    if (auto status =
            intrinsic::FromAbslTime(absl::Now(), event.mutable_timestamp());
        !status.ok()) {
      LOG_EVERY_N_SEC(WARNING, 10) << "Failed to convert absl::Now() to "
                                      "google::protobuf::Timestamp: "
                                   << status;
    }
    event.set_severity(intrinsic_proto::icon::v1::Event::ERROR);
    inspection_data_.event_history.push_front(event);
  }

  // Remove old events to keep the history size bounded.
  while (inspection_data_.event_history.size() > kEventHistoryMaxSize) {
    inspection_data_.event_history.pop_back();
  }
}

absl::Status RealtimeKukaRsiClient::ProvideInspectionData(
    intrinsic_proto::icon::v1::HardwareModuleInspectionData& data) {
  absl::MutexLock lock(inspection_data_mutex_);
  const auto& error_messages = inspection_data_.error_messages;
  const bool emergency_stop_active = inspection_data_.emergency_stop_active;
  const std::optional<OpMode> op_mode = inspection_data_.op_mode;
  const auto& event_history = inspection_data_.event_history;
  if (!error_messages.empty()) {
    auto active_alarms = data.mutable_active_alarms();
    for (const auto& error_message : error_messages) {
      if (!error_message.empty()) {
        active_alarms->add_events()->set_message(error_message);
      }
    }
  }
  for (const auto& event : event_history) {
    *data.mutable_event_history()->add_events() = event;
  }

  auto safety_status = data.mutable_safety_status();
  safety_status->set_estop_button_status(
      emergency_stop_active ? intrinsic_proto::icon::BUTTON_STATUS_ENGAGED
                            : intrinsic_proto::icon::BUTTON_STATUS_DISENGAGED);

  if (op_mode.has_value()) {
    intrinsic_proto::icon::KukaRsiInspectionData kuka_rsi_inspection_data;
    switch (*op_mode) {
      case OpMode::kExternal:
        safety_status->set_mode_of_safe_operation(
            intrinsic_proto::icon::MODE_OF_SAFE_OPERATION_AUTOMATIC);
        kuka_rsi_inspection_data.set_automatic_operation_mode(
            intrinsic_proto::icon::KukaRsiInspectionData::EXTERNAL);
        break;
      case OpMode::kAutomatic:
        safety_status->set_mode_of_safe_operation(
            intrinsic_proto::icon::MODE_OF_SAFE_OPERATION_AUTOMATIC);
        kuka_rsi_inspection_data.set_automatic_operation_mode(
            intrinsic_proto::icon::KukaRsiInspectionData::AUTOMATIC);
        break;
      case OpMode::kT1:
        safety_status->set_mode_of_safe_operation(
            intrinsic_proto::icon::MODE_OF_SAFE_OPERATION_TEACHING_1);
        kuka_rsi_inspection_data.set_automatic_operation_mode(
            intrinsic_proto::icon::KukaRsiInspectionData::NONE);
        break;
      case OpMode::kT2:
        safety_status->set_mode_of_safe_operation(
            intrinsic_proto::icon::MODE_OF_SAFE_OPERATION_TEACHING_2);
        kuka_rsi_inspection_data.set_automatic_operation_mode(
            intrinsic_proto::icon::KukaRsiInspectionData::NONE);
        break;
      default:
        break;
    }
    kuka_rsi_inspection_data.set_rsi_active(rsi_communicator_.IsActive());

    if (!data.mutable_hardware_specific_data()->PackFrom(
            kuka_rsi_inspection_data)) {
      return absl::InternalError("Failed to pack KukaRsiInspectionData");
    }
  }

  return absl::OkStatus();
}

}  // namespace intrinsic::kuka
