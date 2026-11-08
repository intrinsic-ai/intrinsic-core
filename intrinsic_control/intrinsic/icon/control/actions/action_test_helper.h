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

#ifndef INTRINSIC_ICON_CONTROL_ACTIONS_ACTION_TEST_HELPER_H_
#define INTRINSIC_ICON_CONTROL_ACTIONS_ACTION_TEST_HELPER_H_

#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "absl/container/fixed_array.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "internal/testing.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/common/part_properties.h"
#include "intrinsic/icon/common/slot_part_map.h"
#include "intrinsic/icon/control/fake_realtime_slot_map_impl.h"
#include "intrinsic/icon/control/parts/realtime_part_interface.h"
#include "intrinsic/icon/control/parts/realtime_part_property_access.h"
#include "intrinsic/icon/control/reaction.h"
#include "intrinsic/icon/control/realtime_signal_access.h"
#include "intrinsic/icon/control/realtime_signal_storage.h"
#include "intrinsic/icon/control/realtime_signal_types.h"
#include "intrinsic/icon/control/realtime_slot_map.h"
#include "intrinsic/icon/control/rtcl_action.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/control/streaming_io_helpers.h"
#include "intrinsic/icon/control/streaming_io_realtime.h"
#include "intrinsic/icon/control/streaming_io_storage.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/clock.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

// Helper class that constructs and holds an instance of an RtclActionInterface
// type, as well as the supporting data types (StreamingIoStorage etc.).
//
// The user must supply any parts their action wants to interact with.
//
// Also, make sure that your test links your action's register target, and that
// the register target is marked `alwayslink = True`.
//
// Example usage:
//
// PositionLoopbackFakePart fake_part(/*ndof=*/5);
// const std::string kPartName = "my_part";
// INTR_ASSIGN_OR_RETURN(
//     auto test_helper,
//     ActionTestHelper::Create(
//         "my_action_type",
//         my_action_parameter_proto,
//         /*part_name=*/kPartName,
//         /*part_map=*/{{kPartName, {.part = fake_part}}}));
// EXPECT_OK(test_helper.EnterAction());
// EXPECT_OK(test_helper.SenseAndControlAction());
// // Inspect `fake_part` to make sure the action sent the expected commands
// EXPECT_EQ(
//     fake_part.JointPositionInterface().PreviousPositionSetpoints().position()(0),
//     42.0);
// // Check the action's streaming output:
// EXPECT_THAT(
//     action_test_helper.PollStreamingOutput<StreamingOutputProto(),
//     IsOkAndHolds(EqualsProto(expected_streaming_output)));
class ActionTestHelper {
 public:
  struct PartAndConfig {
    RealtimePartInterface& part;
    // If this is unset, ActionTestHelper attempts to automatically extract a
    // PartConfig proto from `part`.
    std::optional<::intrinsic_proto::icon::v1::PartConfig> config =
        std::nullopt;
    std::optional<RealtimePartInterface::ReadStatusParameters>
        read_status_params = std::nullopt;
    std::optional<RealtimePartInterface::ApplyCommandParameters>
        apply_command_params = std::nullopt;
  };

  struct PartAndSlotInfo {
    RealtimePartInterface& part;
    SlotInfo slot_info;
    std::optional<RealtimePartInterface::ReadStatusParameters>
        read_status_params = std::nullopt;
    std::optional<RealtimePartInterface::ApplyCommandParameters>
        apply_command_params = std::nullopt;
  };

  // Creates an ActionTestHelper that uses the parts in `part_map`, and the
  // given `control_frequency_hz`.
  // Then builds an action of type `action_type_name` with the given `params`
  // and `slot_part_map`.
  //
  // Returns InvalidArgumentError if any PartAndConfig struct in `part_map` is
  // inconsistent (i.e. the PartConfig has a name that differs from the map
  // key).
  // Returns NotFoundError if there is no factory for `action_type_name`.
  // Returns NotFoundError if `slot_part_map` refers to a non-existent part.
  // Returns any errors that arise building the action.
  static absl::StatusOr<ActionTestHelper> Create(
      absl::string_view action_type_name,
      const ::google::protobuf::Message& params,
      const SlotPartMap& slot_part_map,
      absl::flat_hash_map<std::string, PartAndConfig> part_map,
      double control_frequency_hz);

  // Creates an ActionTestHelper that uses the parts in `part_map`, and the
  // given `control_frequency_hz`.
  // Then builds an action of type `action_type_name` with the given `params`
  // and `part_name`.
  //
  // Returns InvalidArgumentError if any PartAndConfig struct in `part_map` is
  // inconsistent (i.e. the PartConfig has a name that differs from the map
  // key).
  // Returns NotFoundError if there is no factory for `action_type_name`.
  // Returns NotFoundError if `part_name` refers to a non-existent part.
  // Returns any errors that arise building the action.
  static absl::StatusOr<ActionTestHelper> Create(
      absl::string_view action_type_name,
      const ::google::protobuf::Message& params, absl::string_view part_name,
      absl::flat_hash_map<std::string, PartAndConfig> part_map,
      double control_frequency_hz);

  // Same as above, for action types that do not take any parameters.
  static absl::StatusOr<ActionTestHelper> Create(
      absl::string_view action_type_name, const SlotPartMap& slot_part_map,
      absl::flat_hash_map<std::string, PartAndConfig> part_map,
      double control_frequency_hz);

  static absl::StatusOr<ActionTestHelper> Create(
      absl::string_view action_type_name, absl::string_view part_name,
      absl::flat_hash_map<std::string, PartAndConfig> part_map,
      double control_frequency_hz);

  // Calls EnterAction() on the action instance, and verifies that the call does
  // not malloc.
  //
  // NOTE: This is inline to ensure that the malloc macros respect the test
  // target's settings (INTRINSIC_MALLOC_TEST does not propagate into
  // dependencies).
  inline absl::Status EnterAction();

  // Simulates a full cycle of ICON operation:
  // 1. Calls ReadStatus() on the parts that were used to create the
  //    ActionTestHelper (unless skip_read_status==true)
  // 2. Calls Sense() on the action instance, and verifies that the call does
  //    not malloc.
  // 3. Calls Control() on the action instance, and verifies that the call does
  //    not malloc.
  // 4. Calls ApplyCommand() on the parts that were used to create the
  //    ActionTestHelper.
  //
  // Returns any errors that happen throughout.
  //
  // NOTE: This is inline to ensure that the malloc macros respect the test
  // target's settings (INTRINSIC_MALLOC_TEST does not propagate into
  // dependencies).
  inline absl::Status SenseAndControlAction(bool skip_read_status = false);

  // Simulates a half cycle of ICON operation:
  // 1. Calls ReadStatus() on the parts that were used to create the
  //    ActionTestHelper (unless skip_read_status==true)
  // 2. Calls Sense() on the action instance, and verifies that the call does
  //    not malloc.
  //
  // This is useful to validate fine-grained behavior like when exactly an
  // action updates state variable values.
  //
  // Returns any errors that happen throughout.
  //
  // NOTE: This is inline to ensure that the malloc macros respect the test
  // target's settings (INTRINSIC_MALLOC_TEST does not propagate into
  // dependencies).
  inline absl::Status SenseAction(bool skip_read_status = false);

  // Simulates a half cycle of ICON operation:
  // 1. Calls Control() on the action instance, and verifies that the call does
  //    not malloc.
  // 2. Calls ApplyCommand() on the parts that were used to create the
  //    ActionTestHelper.
  //
  // This is useful to validate fine-grained behavior like when exactly an
  // action updates state variable values.
  //
  // Returns any errors that happen throughout.
  //
  // NOTE: This is inline to ensure that the malloc macros respect the test
  // target's settings (INTRINSIC_MALLOC_TEST does not propagate into
  // dependencies).
  inline absl::Status ControlAction();

  // Writes `data` to the streaming input `input_name` of our action instance.
  template <typename ProtoT, typename = std::enable_if_t<std::is_base_of_v<
                                 google::protobuf::Message, ProtoT>>>
  absl::Status WriteStreamingInput(absl::string_view input_name,
                                   const ProtoT& data) {
    return ::intrinsic::icon::WriteStreamingInput(input_name, data,
                                                  *streaming_io_storage_);
  }

  // Reads the latest streaming output value from our action instance (if any).
  template <typename ProtoT, typename = std::enable_if_t<std::is_base_of_v<
                                 google::protobuf::Message, ProtoT>>>
  absl::StatusOr<ProtoT> PollStreamingOutput() {
    return ::intrinsic::icon::PollStreamingOutput<ProtoT>(
        absl::Now(), *streaming_io_storage_);
  }

  // Returns the state variable `state_variable_name`, or an error if that state
  // variable is unavailable.
  //
  // NOTE: This is inline to ensure that the malloc macros respect the test
  // target's settings (INTRINSIC_MALLOC_TEST does not propagate into
  // dependencies).
  inline RealtimeStatusOr<StateVariableValue> GetStateVariable(
      absl::string_view state_variable_name) const;

  std::optional<JointTrajectoryPVA> GetTrajectory() const;

  // Sets the speed override for subsequent calls to `EnterAction()`,
  // `SenseAction()`, `ControlAction()` and `SenseAndControlAction()`.
  //
  // Returns an error if `speed_override` is NOT between 0 and 1.
  absl::Status SetSpeedOverride(double speed_override);

  // Sets the requested behavior override for subsequent calls to
  // `EnterAction()`, `SenseAction()`, `ControlAction()` and
  // `SenseAndControlAction()`.
  absl::Status SetRequestedBehaviorOverride(
      intrinsic_proto::icon::v1::BehaviorOverrideRequest
          behavior_override_request);

  // Triggers a real-time signal with the given real-time signal name.
  //
  // Returns an error if the signal is not found.
  absl::Status TriggerRealtimeSignal(absl::string_view signal_name);

 private:
  explicit ActionTestHelper(
      intrinsic_proto::icon::v1::ServerConfig server_config,
      absl::flat_hash_map<std::string, PartAndSlotInfo> part_data_map,
      std::unique_ptr<FakeRealtimeSlotMapImpl> fake_slot_map_impl,
      std::unique_ptr<StreamingIoStorage> streaming_io_storage,
      std::unique_ptr<RealtimeSignalStorage> rt_signal_storage,
      std::unique_ptr<absl::flat_hash_map<std::string, RealtimeSignalId>>
          signal_id_map,
      std::unique_ptr<absl::flat_hash_map<ActionInstanceId, JointTrajectoryPVA>>
          trajectory_map,
      std::unique_ptr<RtclActionInterface> action);

  // Internal implementation behind all overloads of Create.
  static absl::StatusOr<ActionTestHelper> CreateImpl(
      absl::string_view action_type_name,
      const ::google::protobuf::Any& any_params,
      const SlotPartMap& slot_part_map,
      absl::flat_hash_map<std::string, PartAndConfig> part_map,
      double frequency_hz);

  intrinsic_proto::icon::v1::ServerConfig server_config_;
  absl::flat_hash_map<std::string, PartAndSlotInfo> part_data_map_;
  std::unique_ptr<FakeRealtimeSlotMapImpl> fake_slot_map_impl_;
  RealtimeSlotMap rt_slot_map_;
  std::unique_ptr<StreamingIoStorage> streaming_io_storage_ = nullptr;
  RealtimeStreamingIoStorage rt_streaming_io_storage_;
  std::unique_ptr<RealtimeSignalStorage> rt_signal_storage_ = nullptr;
  std::unique_ptr<absl::flat_hash_map<std::string, RealtimeSignalId>>
      rt_signal_id_map_ = nullptr;
  std::unique_ptr<absl::flat_hash_map<ActionInstanceId, JointTrajectoryPVA>>
      trajectory_map_;
  std::unique_ptr<RtclActionInterface> action_;
  intrinsic::Time current_time_;
  int sense_calls_ = 0;
  int control_calls_ = 0;
  double speed_override_ = 1.0;
  intrinsic_proto::icon::v1::BehaviorOverrideRequest
      requested_behavior_override_ = intrinsic_proto::icon::v1::
          BehaviorOverrideRequest::BEHAVIOR_OVERRIDE_REQUEST_UNKNOWN;
  // If the PartAndSlotInfo structs passed to the create functions don't have
  // {ReadStatus,ApplyCommand}Parameters values, use this array to build a
  // RealtimePartPropertyAccess (below). We'll use that to create our own
  // Parameter structs in this case.
  absl::FixedArray<PartPropertyValue> empty_part_properties_{};
  RealtimePartPropertyAccess empty_realtime_part_property_access_{
      empty_part_properties_, empty_part_properties_};
  RealtimePartInterface::ReadStatusParameters fallback_read_status_parameters_{
      .part_properties = empty_realtime_part_property_access_};
  RealtimePartInterface::ApplyCommandParameters
      fallback_apply_command_parameters_{
          .part_properties = empty_realtime_part_property_access_};
};

// Implementations of inline methods below, to keep the header easier to skim.

inline absl::Status ActionTestHelper::EnterAction() {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatus on_enter_status = action_->OnEnter(
      {.slot_map = rt_slot_map_,
       .speed_override = speed_override_,
       .requested_behavior_override = requested_behavior_override_});
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  return on_enter_status;
}

inline absl::Status ActionTestHelper::SenseAndControlAction(
    bool skip_read_status) {
  INTR_RETURN_IF_ERROR(SenseAction(skip_read_status));
  return ControlAction();
}

inline absl::Status ActionTestHelper::SenseAction(bool skip_read_status) {
  if (sense_calls_ != control_calls_) {
    return absl::FailedPreconditionError(
        "You must call Sense and Control alternatingly! If you're sure "
        "that's what your test is doing, a Sense or Control call might have "
        "failed. Please use a new ActionTestHelper for the remainder of your "
        "test case, instead of reusing this one. The Action is in an "
        "undefined state and likely won't give you helpful test results "
        "anyway.");
  }
  if (!skip_read_status) {
    for (auto& [_, part_and_slot_info] : part_data_map_) {
      INTRINSIC_RT_RETURN_IF_ERROR(part_and_slot_info.part.ReadStatus(
          part_and_slot_info.read_status_params.value_or(
              fallback_read_status_parameters_)));
    }
  }
  StreamingIoRealtimeAccess rt_io_access(current_time_,
                                         rt_streaming_io_storage_);
  RealtimeSignalAccess rt_signal_access(*rt_signal_storage_);

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatus sense_status = action_->Sense(
      {.slot_map = rt_slot_map_,
       .streaming_io_access = rt_io_access,
       .signal_access = rt_signal_access,
       .speed_override = speed_override_,
       .requested_behavior_override = requested_behavior_override_});
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  INTRINSIC_RT_RETURN_IF_ERROR(sense_status);

  // Update the previous value of each signal.
  if (rt_signal_storage_ != nullptr) {
    for (auto& signal : rt_signal_storage_->signal_values) {
      signal.previous_value = signal.current_value;
    }
  }

  sense_calls_++;
  return OkStatus();
}

inline absl::Status ActionTestHelper::ControlAction() {
  if (sense_calls_ != control_calls_ + 1) {
    return absl::FailedPreconditionError(
        "You must call Sense before Control! If you're sure that's what your "
        "test is doing, a Sense or Control call might have failed. Please "
        "use a new ActionTestHelper for the remainder of your test case, "
        "instead of reusing this one. The Action is in an undefined state "
        "and likely won't give you helpful test results anyway.");
  }
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatus control_status = action_->Control(
      {.slot_map = rt_slot_map_,
       .speed_override = speed_override_,
       .requested_behavior_override = requested_behavior_override_});
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  INTRINSIC_RT_RETURN_IF_ERROR(control_status);
  for (auto& [_, part_and_slot_info] : part_data_map_) {
    INTRINSIC_RT_RETURN_IF_ERROR(part_and_slot_info.part.ApplyCommand(
        part_and_slot_info.apply_command_params.value_or(
            fallback_apply_command_parameters_)));
  }
  control_calls_++;
  return OkStatus();
}

inline RealtimeStatusOr<StateVariableValue> ActionTestHelper::GetStateVariable(
    absl::string_view state_variable_name) const {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatusOr<StateVariableValue> value =
      action_->GetStateVariable(state_variable_name);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  return value;
}

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ACTIONS_ACTION_TEST_HELPER_H_
