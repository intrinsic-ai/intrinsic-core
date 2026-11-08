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

#include "intrinsic/icon/apps/rtcl_sine_wave_action.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/duration.pb.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/apps/sine_wave_action.pb.h"
#include "intrinsic/icon/apps/sine_wave_action_info.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/control/action_factory_context.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/proto/generic_part_config.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/fixed_str_cat.h"
#include "intrinsic/icon/utils/realtime_guard.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/math/ipow.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

namespace {
// This converts a streaming output value from the data type used in
// the realtime loop (double in this case) to the proto message type that
// arrives in clients (google::protobuf::Duration).
//
// ICON calls this function in the non-realtime thread.
google::protobuf::Duration DoubleToDuration(double seconds) {
  INTRINSIC_ASSERT_NON_REALTIME();
  google::protobuf::Duration proto;
  proto.set_seconds(static_cast<int>(seconds));
  const int kNanosPerSecond = 1e9;
  proto.set_nanos(
      static_cast<int>(kNanosPerSecond * (seconds - proto.seconds())));
  return proto;
}
}  // namespace

// static
absl::StatusOr<RtclSineWaveAction::Params>
RtclSineWaveAction::Params::FromProto(
    const intrinsic_proto::icon::actions::SineWaveFixedParams& proto_params) {
  INTRINSIC_ASSERT_NON_REALTIME();
  RtclSineWaveAction::Params params;
  if (proto_params.joints_size() > kMaxJoints) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Fixed parameter proto has ", proto_params.joints_size(),
        " joint values, which is more than kMaxJoints (", kMaxJoints, ")"));
  }
  for (const auto& joint_params : proto_params.joints()) {
    params.amplitudes.emplace_back(joint_params.amplitude_rad());
    params.frequencies.emplace_back(joint_params.frequency_hz());
  }
  if (!params.SizeIs(proto_params.joints_size())) {
    return absl::InternalError(
        "Params object has inconsistent number of values.");
  }
  return params;
}

bool RtclSineWaveAction::Params::SizeIs(size_t ndof) const {
  return amplitudes.size() == ndof && frequencies.size() == ndof;
}

// Does not take ownership of the pointers, but keeps references, so the
// PartInterface objects must outlive the RtclSineWaveAction.
RtclSineWaveAction::RtclSineWaveAction(RealtimeSlotId slot_id,
                                       const JointLimits& application_limits,
                                       double frequency_hz,
                                       RtclSineWaveAction::Params params)
    : slot_id_(slot_id),
      frequency_hz_(frequency_hz),
      application_limits_(application_limits),
      command_(std::move(params)) {}

// static
absl::StatusOr<std::unique_ptr<RtclSineWaveAction>> RtclSineWaveAction::Create(
    const SineWaveActionInfo::FixedParams& params_proto,
    ActionFactoryContext& context) {
  INTRINSIC_ASSERT_NON_REALTIME();
  INTR_ASSIGN_OR_RETURN(SlotInfo arm_info,
                        context.GetSlotInfo(SineWaveActionInfo::kSlotName));

  if (!arm_info.config.generic_config().has_joint_limits_config()) {
    return absl::InvalidArgumentError("SineWaveAction requires joint limits.");
  }
  const ::intrinsic_proto::icon::GenericJointLimitsConfig& joint_limits_config =
      arm_info.config.generic_config().joint_limits_config();
  INTR_ASSIGN_OR_RETURN(
      JointLimits application_limits,
      ::intrinsic::FromProto(joint_limits_config.application_limits()));

  // Advertise a streaming output that takes a double in the realtime thread
  // (see the call to OutputWriter::Write() below), and converts it to a
  // Duration proto in the non-realtime thread using the supplied callback.
  //
  // Extra parentheses because the INTR_ASSIGN_OR_RETURN macro would otherwise
  // get confused by the template typename and method call, since those contain
  // commas.
  INTR_RETURN_IF_ERROR(
      (context.AddStreamingOutputConverter<double, google::protobuf::Duration>(
          &DoubleToDuration)));
  INTR_ASSIGN_OR_RETURN(Params command, Params::FromProto(params_proto));

  return std::make_unique<RtclSineWaveAction>(
      arm_info.slot_id, application_limits,
      context.ServerConfig().frequency_hz(), command);
}

RealtimeStatus RtclSineWaveAction::Sense(SenseParameters params) {
  const auto* joint_position =
      params.slot_map.GetInterfaceForSlot<JointPosition>(slot_id_);
  if (joint_position == nullptr) {
    return InternalError("Slot does not have JointPosition");
  }
  if (!initialized_) {
    time_since_start_ = 0.0;
    starting_position_ = joint_position->PreviousPositionSetpoints().position();
    initialized_ = true;
  } else {
    time_since_start_ += 1.0 / frequency_hz_;
  }
  // Submit the current value of time_since_start_ to the OutputWriter. Note
  // that our conversion function (DoubleToDuration()) is only invoked in the
  // non-realtime thread, when a client actually requests the output data.
  //
  // On the first cycle, calling Write() will also unblock any clients that
  // are waiting for output data from this Action.
  return params.streaming_io_access.WriteOutput<double>(time_since_start_);
}

RealtimeStatus RtclSineWaveAction::Control(ControlParameters params) {
  auto* joint_position =
      params.slot_map.GetMutableInterfaceForSlot<JointPosition>(slot_id_);
  if (joint_position == nullptr) {
    return InternalError("Slot does not have JointPosition");
  }
  eigenmath::VectorNd position_reference(starting_position_.size());
  for (size_t i = 0; i < starting_position_.size(); ++i) {
    double sine_value = ::intrinsic::IPow(
        std::sin(time_since_start_ * 2. * M_PI * command_.frequencies[i]), 3);
    position_reference(i) =
        starting_position_(i) + command_.amplitudes[i] * sine_value;
    // Clamp position reference to joint limits. This doesn't prevent
    // exceeding the other limits (velocity, acceleration, jerk)!
    position_reference(i) =
        std::clamp(position_reference(i), application_limits_.min_position(i),
                   application_limits_.max_position(i));
  }
  return joint_position->SetPositionSetpoints(
      JointPositionCommand(position_reference));
}

RealtimeStatus RtclSineWaveAction::OnEnter(OnEnterParameters params) {
  initialized_ = false;
  return OkStatus();
}

RealtimeStatusOr<StateVariableValue> RtclSineWaveAction::GetStateVariable(
    absl::string_view name) const {
  if (name == SineWaveActionInfo::kTime) {
    return StateVariableValue(time_since_start_);
  }
  return NotFoundError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
      "SineWaveAction, state variable not found ", name));
}

}  // namespace intrinsic::icon
