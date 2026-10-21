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

#ifndef INTRINSIC_ICON_CC_CLIENT_TESTING_CHANNEL_FAKE_H_
#define INTRINSIC_ICON_CC_CLIENT_TESTING_CHANNEL_FAKE_H_

#include <any>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "google/protobuf/any.pb.h"
#include "internal/testing.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/control/parts/feature_interfaces/joint_limits_constants.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/kinematics/types/joint_limits.h"

namespace intrinsic {
namespace icon {
namespace test_part_names {
static constexpr char kArmName[] = "robot_arm";
static constexpr char kGripperName[] = "gripper";
static constexpr char kLinearGripperName[] = "linear_gripper";
static constexpr char kForceTorqueSensorName[] = "ftsensor";
static constexpr char kArmForceTorqueSensorName[] = "arm_and_ftsensor";
static constexpr char kADIOName[] = "adio";
static constexpr char kRangefinderName[] = "rangefinder";
}  // namespace test_part_names

namespace test_io_block_names {
static constexpr char kAnalogIn[] = "analog_in";
static constexpr char kAnalogOut[] = "analog_out";
static constexpr char kDigitalIn[] = "digital_in";
static constexpr char kDigitalOut[] = "digital_out";
}  // namespace test_io_block_names

// Use this to construct RtclControllerChannelFakes for tests that do not
// require custom configuration.
// The default configuration has the following parts (with the names above):
// * A 6-DoF robot arm with the given limits (or sensible defaults if omitted)
// * A gripper
// * A force torque sensor
// * An analog-/digital-IO part that consists of:
//     * two value analog input block named "analog_in"
//     * two value analog output block named "analog_out"
//     * two bit digital input block named "digital_in"
//     * two bit digital output block named "digital_out"
// TODO(b/210456981): It is currently not possible to set the expected
// RobotStatus for ADIO parts.
absl::StatusOr<std::vector<intrinsic_proto::icon::v1::PartConfig>>
DefaultPartConfigs(
    JointLimits system_limits,
    std::optional<JointLimits> application_limits = std::nullopt);

// Same as above, but using known-good application limits, so will never return
// an error.
// See ../../control/parts/l1_arm_part.cc for limitations on system limits
// relative to application limits.
std::vector<intrinsic_proto::icon::v1::PartConfig> DefaultPartConfigs(
    double application_limits_scaling = kMaxApplicationLimitsMultiplier);

// Stores default behaviors for a single use of an action.
class ActionWill {
 public:
  ActionWill() = default;

  // Copyable and movable
  ActionWill(const ActionWill& other) = default;
  ActionWill& operator=(const ActionWill& other) = default;

  // `parameter_matcher` is invoked with the fixed parameter Any proto when the
  // Action becomes active. If it returns non-OK, that error is propagated to
  // the client, aborting any other client actions (particularly any
  // RunWatcherLoop calls).
  //
  // Example (N.B. that usually, you'd want to supply parameters that do
  // satisfy the expectation matcher. In that case, you need a Reaction or other
  // trigger to finish the RunWatcherLoop call cleanly):
  //
  // constexpr int kExpectedValue = 5;
  // auto parameter_matcher =
  //     [=](const google::protobuf::Any& params) {
  //       my_action::proto::Params unpacked;
  //       if (!params.UnpackTo(&unpacked)) {
  //         return absl::InvalidArgumentError("Failed to unpack parameters.");
  //       }
  //       if (unpacked.value() != kExpectedValue) {
  //         return absl::InvalidArgumentError("Did not get expected
  //         parameters.");
  //       }
  //       return absl::OkStatus();
  //     };
  //
  // ASSERT_OK_AND_ASSIGN(
  //     auto channel_fake,
  //     icon::ChannelFakeBuilder(icon::ChannelFakeBuilder::BackendType::kRtcl)
  //         .OnNextSession(icon::SessionWill().OnAction(
  //             icon::ActionInstanceId(0),
  //             icon::ActionWill().ExpectParameter(parameter_matcher)))
  //         .Build());
  //
  // ASSERT_OK(icon::Client(channel_fake).Enable());
  // std::unique_ptr<Session> session =
  //     Session::Start(*channel_fake, {"robot_arm"}).value();
  //
  // ASSERT_OK_AND_ASSIGN(
  //     Action action,
  //     session->AddAction(MyActionWithParams(
  //         ActionInstanceId(0),
  //         /*value=*/kExpectedValue+1)));
  //
  // session->StartAction(action);
  // EXPECT_THAT(session->RunWatcherLoop(),
  //             StatusIs(absl::StatusCode::kInvalidArgument,
  //                      HasSubstr("Did not get expected parameters")));
  ActionWill& ExpectParameter(
      std::function<absl::Status(const google::protobuf::Any& fixed_parameters)>
          parameter_matcher);

  // Same as above, but automatically unpacks the any proto to a ProtoT,
  // returning an error if that fails.
  //
  // This makes `parameter_matcher` quite a bit simpler:
  //
  // auto parameter_matcher = [](const my_action::proto::Params& params) {
  //   if (params.value() != kExpectedvalue) {
  //     return absl::InvalidArgumentError("Wrong value!");
  //   }
  //   return absl::OkStatus();
  // };
  template <typename ProtoT, typename = std::enable_if_t<std::is_base_of_v<
                                 google::protobuf::Message, ProtoT>>>
  ActionWill& ExpectParameter(
      std::function<absl::Status(const ProtoT& fixed_parameters)>
          parameter_matcher);

  // `parameter_matcher` is invoked with the streaming input Any proto when the
  // Action becomes active. If it returns non-OK, that error is propagated to
  // the client, aborting any other client actions (particularly any
  // RunWatcherLoop calls).
  //
  // Usage is the same as for ExpectParameter, so see above for examples.
  //
  // The matchers will be checked in the order they are added and matched with
  // streaming inputs in the order they are received. If more streaming inputs
  // are received than matchers, the extra streaming inputs are not checked.
  //
  // In strict validation mode, any matchers which are not checked before the
  // action is switched will cause a failure. Also any streaming inputs which
  // are received but not matched will cause a failure.
  ActionWill& ExpectStreamingInput(
      std::function<
          absl::Status(const google::protobuf::Any& streaming_parameters)>
          parameter_matcher);

  // Same as above, but automatically unpacks the any proto to a ProtoT,
  // returning an error if that fails.
  template <typename ProtoT, typename = std::enable_if_t<std::is_base_of_v<
                                 google::protobuf::Message, ProtoT>>>
  ActionWill& ExpectStreamingInput(
      std::function<absl::Status(const ProtoT& streaming_parameters)>
          parameter_matcher);

  // Sets a sequence of streaming outputs. When the Action becomes active, it
  // will advance through this sequence and publish the given streaming outputs
  // in order.
  //
  // Please note that a streaming output converter must be registered via the
  // AddStreamingOutputConverter method in the action's factory via the
  // ActionFactoryContext. This defines the types which the any is attempted to
  // be cast to and then converts to the required proto output type.
  ActionWill& PublishStreamingOutput(
      absl::Span<const std::any> streaming_output_sequence);

  // Returns the streaming outputs set by `PublishStreamingOutput(...)` in the
  // order set.
  absl::Span<const std::any> OrderedStreamingOutputs();

  // Sets a sequence of state variable maps. When the Action becomes active, it
  // will advance through this sequence and report the given state variable
  // values, triggering any Conditions that are satisfied by those values in
  // order.
  // Replaces any existing variable maps (no matter whether they were supplied
  // via ReportStateVariablesInOrder or ReportStateVariables).
  ActionWill& ReportStateVariablesInOrder(
      absl::Span<const absl::flat_hash_map<std::string, StateVariableValue>>
          state_variable_map_sequence);

  // Appends a state variable map that will be applied when this action next
  // becomes active. Appended maps will be applied in the order they are
  // appended, and the total list is treated as if it were supplied to
  // ReportStateVariablesInOrder.
  ActionWill& ReportStateVariables(
      const absl::flat_hash_map<std::string, StateVariableValue>&
          state_variable_map);

  // Returns the state variable maps appended via
  // ReportStateVariablesInOrder(...) and ReportStateVariables(...) in the order
  // appended.
  absl::Span<const absl::flat_hash_map<std::string, StateVariableValue>>
  InOrderStateVariableMaps() const;

  // When this action becomes active, sets the fake robot status (obtainable
  // with `icon_client.GetStatus()`) to `robot_status`.
  //
  // If called more than once on a particular `ActionWill`, only the last call
  // has an effect and a warning is logged.
  ActionWill& SetRobotStatus(
      const intrinsic_proto::icon::v1::GetStatusResponse& robot_status);

  // When this action becomes active, sets the fake operational status
  // (obtainable with `icon_client.GetOperationalStatus()`) to
  // `operational_status`.
  //
  // If called more than once on a particular `ActionWill`, only the last call
  // has an effect and a warning is logged.
  ActionWill& SetOperationalStatus(
      const intrinsic_proto::icon::v1::GetOperationalStatusResponse&
          operational_status);

  // Returns the robot status set by `SetRobotStatus(...)`.
  std::optional<intrinsic_proto::icon::v1::GetStatusResponse> GetRobotStatus()
      const;

  // Returns the robot status set by `SetOperationalStatus(...)`.
  std::optional<intrinsic_proto::icon::v1::GetOperationalStatusResponse>
  GetOperationalStatus() const;

  // Returns the parameter matcher function set in the constructor, or nullptr
  // if there isn't one.
  std::function<absl::Status(const google::protobuf::Any& parameter)>
  GetParameterMatcher() const;

  // Returns the parameter matcher functions set in the constructor, or nullptr
  // if there aren't any.
  absl::Span<
      const std::function<absl::Status(const google::protobuf::Any& parameter)>>
  GetOrderedStreamingInputParameterMatchers();

  // Registers a callback to be invoked when the Action becomes active.
  //
  // Note: `on_start` is called while the fake session mutex is held. It is safe
  // to use for notifications (e.g. `absl::Notification`) and setting flags, but
  // must not call back into session methods or modify session behaviors, as
  // doing so may deadlock.
  ActionWill& OnStart(std::function<void()> on_start);

  // Returns the callback set by `OnStart(...)`, or nullptr if there isn't one.
  std::function<void()> GetOnStart() const;

 private:
  std::function<absl::Status(const google::protobuf::Any& parameter)>
      parameter_matcher_ = nullptr;
  std::vector<
      std::function<absl::Status(const google::protobuf::Any& parameter)>>
      streaming_input_parameter_matchers_ = {};
  std::vector<std::any> streaming_outputs_ = {};
  std::vector<absl::flat_hash_map<std::string, StateVariableValue>>
      in_order_state_variable_maps_;
  std::optional<intrinsic_proto::icon::v1::GetStatusResponse> robot_status_ =
      std::nullopt;
  std::optional<intrinsic_proto::icon::v1::GetOperationalStatusResponse>
      operational_status_ = std::nullopt;
  std::function<void()> on_start_ = nullptr;
};

// Stores default behaviors for a single session.
// While the session is active, calls to
// `RtclControllerFakeSession::PollReactions()` advance through behaviors in the
// same order they were added. Behaviors may trigger reactions that change the
// active action, or otherwise change the observable state of the session (i.e.
// update the robot status).

// There are two validation modes:

// Strict mode: The active action ID must always match the ID that the next
// behavior expects (if any). That is, if the current action has ID 2, but the
// next behavior expects 23, the ChannelFake returns an error. If the next
// behavior is not associated with an action, it will be applied as soon as the
// previous behavior is consumed (an ActionWill with multiple StateVariableMaps
// takes as many cycles as there are maps).

// Nice mode: ChannelFake applies the next behavior as soon as it is applicable
// (i.e. the action ID is the same as the active action or immediately if it is
// not associated with an action).
class SessionWill {
 public:
  struct ActionAndBehaviors {
    std::optional<ActionInstanceId> action_id;
    ActionWill action_behaviors;
    bool is_already_running_action = false;
  };

  SessionWill() = default;

  // Copyable and movable
  SessionWill(const SessionWill& other) = default;
  SessionWill& operator=(const SessionWill& other) = default;

  // Specifies the behavior to take the next time `action_id` becomes the active
  // action during this session. If validation mode is set to strict, this
  // action must be active when this behavior becomes active. Subsequent
  // calls append behaviors for subsequent activations of the actions during
  // this session. Note that it is valid for subsequent calls to use the same
  // `action_id`; this is useful when an action may become active multiple times
  // during a session.
  SessionWill& OnAction(ActionInstanceId action_id,
                        const ActionWill& action_behaviors);

  // Same as above, but the behavior will be applied as soon as the previous
  // behavior is consumed if the action is already active rather than waiting
  // for the action to become active. This allows interlacing of behaviours for
  // actions running in parallel.
  SessionWill& ForAlreadyRunningAction(ActionInstanceId action_id,
                                       const ActionWill& action_behaviors);

  // Performs a robot status change after the previously added behavior
  // independent of any action. This is intended for changes not caused by the
  // robot itself, e.g. simulating a human pressed a button.
  //
  // Consecutive calls to this function will add more robot status changes. In
  // strict validation mode, all `SetRobotStatus()` calls must be used before
  // the end of the test.
  SessionWill& SetRobotStatus(
      const intrinsic_proto::icon::v1::GetStatusResponse& robot_status);

  // Returns the actions appended via OnAction() in the order appended.
  absl::Span<const ActionAndBehaviors> InOrderActionBehaviors() const;

 private:
  std::vector<ActionAndBehaviors> in_order_action_behaviors_;
};

template <typename ProtoT, typename>
ActionWill& ActionWill::ExpectParameter(
    std::function<absl::Status(const ProtoT& fixed_parameters)>
        parameter_matcher) {
  return ExpectParameter(
      [parameter_matcher](
          const google::protobuf::Any& fixed_parameters) -> absl::Status {
        ProtoT parameters;
        if (!fixed_parameters.UnpackTo(&parameters)) {
          return absl::InvalidArgumentError(
              "Fixed parameters cannot be unpacked to expected type.");
        }
        return parameter_matcher(parameters);
      });
}

template <typename ProtoT, typename>
ActionWill& ActionWill::ExpectStreamingInput(
    std::function<absl::Status(const ProtoT& streaming_parameters)>
        parameter_matcher) {
  return ExpectStreamingInput(
      [parameter_matcher](
          const google::protobuf::Any& streaming_parameters) -> absl::Status {
        ProtoT parameters;
        if (!streaming_parameters.UnpackTo(&parameters)) {
          return absl::InvalidArgumentError(
              "Streaming parameters cannot be unpacked to expected type.");
        }
        return parameter_matcher(parameters);
      });
}

}  // namespace icon
}  // namespace intrinsic

#endif  // INTRINSIC_ICON_CC_CLIENT_TESTING_CHANNEL_FAKE_H_
