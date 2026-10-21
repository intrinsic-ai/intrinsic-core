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

#include "intrinsic/icon/cc_client/testing/channel_fake.h"

#include <any>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/cc_client/testing/rtcl_readonly_parts.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/control/actions/test_helpers.h"
#include "intrinsic/icon/control/parts/fake_feature_interfaces.h"
#include "intrinsic/icon/control/parts/io_block.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/check_joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/util/eigen.h"

namespace intrinsic {
namespace icon {

namespace {

constexpr int32_t kDofs = 6;

// Returns a set of realistic maximum joint limits for a 6-DoF robot.
JointLimits MaxJointLimits(double scaling = 1.0) {
  using VectorNd = eigenmath::VectorNd;
  VectorNd min_position = VectorToVectorNd({-2.8, -3.1, -1.9, -3.1, -1.8, -6});
  VectorNd max_position = VectorToVectorNd({2.7, 0.5, 2.6, 3.1, 1.8, 6});
  VectorNd max_velocity = VectorToVectorNd({6, 5, 6, 6, 6, 10}) * scaling;
  VectorNd max_acceleration = VectorNd::Constant(kDofs, 50) * scaling;
  VectorNd max_jerk = VectorNd::Constant(kDofs, 130) * scaling;
  VectorNd max_torque = VectorNd::Constant(kDofs, 130);
  return {.min_position = min_position,
          .max_position = max_position,
          .max_velocity = max_velocity,
          .max_acceleration = max_acceleration,
          .max_jerk = max_jerk,
          .max_torque = max_torque};
}

// CartesianLimits as defined in the legacy l2_config.textproto.
CartesianLimits DefaultCartesianLimits() {
  return CartesianLimits::Unlimited();
}

FakeADIO::FakeADIOState DefaultADIO() {
  return {.analog_inputs = {{test_io_block_names::kAnalogIn,
                             AnalogBlock::Create(/*size=*/2).value()}},
          .analog_outputs = {{test_io_block_names::kAnalogOut,
                              AnalogBlock::Create(/*size=*/2).value()}},
          .digital_inputs = {{test_io_block_names::kDigitalIn,
                              DioBlock::Create(/*size=*/2).value()}},
          .digital_outputs = {{test_io_block_names::kDigitalOut,
                               DioBlock::Create(/*size=*/2).value()}},
          .digital_input_signal_names = {{test_io_block_names::kDigitalIn,
                                          {"0", "fake_in"}}},
          .digital_output_signal_names = {{test_io_block_names::kDigitalOut,
                                           {"fake_out", "1"}}},
          .analog_input_signal_names = {{test_io_block_names::kAnalogIn,
                                         {"fake_pressure", "1"}}},
          .analog_output_signal_names = {
              {test_io_block_names::kAnalogOut, {"fake_flow", "1"}}}};
}

}  // namespace

std::vector<intrinsic_proto::icon::v1::PartConfig> DefaultPartConfigs(
    double application_limits_scaling) {
  return DefaultPartConfigs(MaxJointLimits(),
                            MaxJointLimits(application_limits_scaling))
      .value();
}

absl::StatusOr<std::vector<intrinsic_proto::icon::v1::PartConfig>>
DefaultPartConfigs(JointLimits system_limits,
                   std::optional<JointLimits> application_limits) {
  const JointLimits default_part_application_limits =
      application_limits.value_or(system_limits);

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      LimitCheckResult result,
      IsWithinLimits(default_part_application_limits, system_limits));
  // LimitCheckResult converts to bool implicitly and is falsy if
  // any limit (P, V, A, J) is violated.
  if (!result) {
    return InvalidArgumentError(
        "Default limits exceed max limits, cannot create default PartConfigs.");
  }
  return std::vector<intrinsic_proto::icon::v1::PartConfig>{
      PartConfigFromPart(
          test_part_names::kArmName,
          *FakeReadOnlyNDofArm(kDofs, default_part_application_limits,
                               /*system_limits=*/system_limits,
                               DefaultCartesianLimits())),
      PartConfigFromPart(test_part_names::kGripperName, *FakeReadOnlyGripper()),
      PartConfigFromPart(test_part_names::kADIOName,
                         *FakeReadOnlyADIO(DefaultADIO())),
      PartConfigFromPart(test_part_names::kLinearGripperName,
                         *FakeReadOnlyLinearGripper()),
      PartConfigFromPart(test_part_names::kForceTorqueSensorName,
                         *FakeReadOnlyForceTorqueSensor()),
      PartConfigFromPart(
          test_part_names::kArmForceTorqueSensorName,
          *FakeReadOnlyNDofArmForceTorqueSensorPart(
              kDofs, default_part_application_limits,
              /*system_limits=*/system_limits, DefaultCartesianLimits())),
      PartConfigFromPart(
          test_part_names::kRangefinderName,
          *FakeReadOnlyRangefinderPart(0.0, Pose3d::Identity()))};
}

ActionWill& ActionWill::ExpectParameter(
    std::function<absl::Status(const google::protobuf::Any& fixed_parameters)>
        parameter_matcher) {
  parameter_matcher_ = std::move(parameter_matcher);
  return *this;
}

ActionWill& ActionWill::ExpectStreamingInput(
    std::function<absl::Status(const google::protobuf::Any& fixed_parameters)>
        parameter_matcher) {
  streaming_input_parameter_matchers_.push_back(std::move(parameter_matcher));
  return *this;
}

ActionWill& ActionWill::PublishStreamingOutput(
    absl::Span<const std::any> streaming_output_sequence) {
  streaming_outputs_.insert(streaming_outputs_.end(),
                            streaming_output_sequence.begin(),
                            streaming_output_sequence.end());
  return *this;
}

absl::Span<const std::any> ActionWill::OrderedStreamingOutputs() {
  return streaming_outputs_;
};

ActionWill& ActionWill::ReportStateVariablesInOrder(
    absl::Span<const absl::flat_hash_map<std::string, StateVariableValue>>
        state_variable_map_sequence) {
  in_order_state_variable_maps_ = {state_variable_map_sequence.begin(),
                                   state_variable_map_sequence.end()};
  return *this;
}

ActionWill& ActionWill::ReportStateVariables(
    const absl::flat_hash_map<std::string, StateVariableValue>&
        state_variable_map) {
  in_order_state_variable_maps_.emplace_back(state_variable_map);
  return *this;
}

absl::Span<const absl::flat_hash_map<std::string, StateVariableValue>>
ActionWill::InOrderStateVariableMaps() const {
  return in_order_state_variable_maps_;
}

ActionWill& ActionWill::SetRobotStatus(
    const intrinsic_proto::icon::v1::GetStatusResponse& robot_status) {
  robot_status_ = robot_status;
  return *this;
}

ActionWill& ActionWill::SetOperationalStatus(
    const intrinsic_proto::icon::v1::GetOperationalStatusResponse&
        operational_status) {
  operational_status_ = operational_status;
  return *this;
}

std::optional<intrinsic_proto::icon::v1::GetStatusResponse>
ActionWill::GetRobotStatus() const {
  return robot_status_;
}

std::optional<intrinsic_proto::icon::v1::GetOperationalStatusResponse>
ActionWill::GetOperationalStatus() const {
  return operational_status_;
}

std::function<absl::Status(const google::protobuf::Any& parameter)>
ActionWill::GetParameterMatcher() const {
  return parameter_matcher_;
}

absl::Span<
    const std::function<absl::Status(const google::protobuf::Any& parameter)>>
ActionWill::GetOrderedStreamingInputParameterMatchers() {
  return streaming_input_parameter_matchers_;
}

ActionWill& ActionWill::OnStart(std::function<void()> on_start) {
  on_start_ = std::move(on_start);
  return *this;
}

std::function<void()> ActionWill::GetOnStart() const { return on_start_; }

SessionWill& SessionWill::OnAction(ActionInstanceId action_id,
                                   const ActionWill& action_behaviors) {
  in_order_action_behaviors_.push_back(ActionAndBehaviors{
      .action_id = action_id, .action_behaviors = action_behaviors});
  return *this;
}

SessionWill& SessionWill::ForAlreadyRunningAction(
    ActionInstanceId action_id, const ActionWill& action_behaviors) {
  in_order_action_behaviors_.push_back(
      ActionAndBehaviors{.action_id = action_id,
                         .action_behaviors = action_behaviors,
                         .is_already_running_action = true});
  return *this;
}

SessionWill& SessionWill::SetRobotStatus(
    const intrinsic_proto::icon::v1::GetStatusResponse& robot_status) {
  in_order_action_behaviors_.push_back(ActionAndBehaviors{
      .action_behaviors = ActionWill().SetRobotStatus(robot_status)});
  return *this;
}

absl::Span<const SessionWill::ActionAndBehaviors>
SessionWill::InOrderActionBehaviors() const {
  return in_order_action_behaviors_;
}

}  // namespace icon
}  // namespace intrinsic
