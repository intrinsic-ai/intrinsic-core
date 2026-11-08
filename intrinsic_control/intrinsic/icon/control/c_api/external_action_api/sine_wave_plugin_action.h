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

#ifndef INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_SINE_WAVE_PLUGIN_ACTION_H_
#define INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_SINE_WAVE_PLUGIN_ACTION_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/duration.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_action_factory_context.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_action_interface.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_realtime_signal_access.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_realtime_slot_map.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_streaming_io_access.h"
#include "intrinsic/icon/control/c_api/external_action_api/sine_wave_action.pb.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/control/streaming_io_types.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/joint_limits.h"

namespace intrinsic::icon {

// This Action implements a pretty simple control law:
// 1. Save the initial position when the Action becomes active.
// 2. For each joint, calculate the value of a sine wave based on the number
//    of seconds that have elapsed since the Action started, with a
//    customizable frequency and amplitude. Then apply that as an offset to
//    the initial joint position.
//
// In addition, it demonstrates the use of streaming input and output values:
//
// * Reads a number from a streaming input (check the signature or the string
//   constants below for the name). Then echoes that number as a State
//   Variable, which can trigger Realtime Reactions.
// * Publishes the time since Action start as a streaming output.
class SineWavePluginAction final : public IconActionInterface {
 public:
  // The INTRINSIC_ICON_REGISTER_ICON_ACTION_PLUGIN macro uses these using
  // declarations to build the wrapper functions required for an ICON Action
  // plugin.
  using ParameterProto =
      ::intrinsic_proto::icon::external_actions::SineWaveFixedParams;
  using StreamingOutputProto = ::google::protobuf::Duration;
  using StreamingInputProto = ::google::protobuf::FloatValue;

  // We use these constants in GetSignature() below. Refer to them to make sure
  // the names used in client code match the signature.
  static constexpr absl::string_view kName = "sine_wave_action";
  static constexpr absl::string_view kSlotName = "arm";
  static constexpr absl::string_view kStateVariableTimeSinceStart =
      "time_since_start";
  static constexpr absl::string_view kStateVariableNumber = "number";
  static constexpr absl::string_view kStreamingInputName = "number";

  // Safe to access, but not move and copy in realtime (which is why the
  // Action's member below is const).
  struct SolvedParams {
    std::vector<double> amplitudes;
    std::vector<double> frequencies;

    static absl::StatusOr<SolvedParams> FromProto(
        const ParameterProto& proto_params, size_t expected_num_joints);
    // Checks that all of the vectors above have ndof members.
    bool IsValid(size_t ndof) const;
  };

  SineWavePluginAction(RealtimeSlotId slot_id,
                       const JointLimits& application_limits,
                       double frequency_hz, SolvedParams params,
                       StreamingInputId number_id)
      : slot_id_(slot_id),
        frequency_hz_(frequency_hz),
        application_limits_(application_limits),
        params_(std::move(params)),
        number_id_(number_id) {}

  // Fills an ActionSignature proto with the signature for SineWavePluginAction.
  //
  // INTRINSIC_ICON_REGISTER_ICON_ACTION_PLUGIN requires this function to work
  // (and will fail to compile if it is missing)!
  static intrinsic_proto::icon::v1::ActionSignature GetSignature();

  // Creates an instance of SineWavePluginAction with the given `parameters`.
  // Uses `context` to
  // * Retrieve the RealtimeSlotId for the part we control
  // * Retrieve joint limits from the "GenericConfig" for our part
  // * Register a streaming input and output (see class comment for details on
  //   those).
  static absl::StatusOr<std::unique_ptr<SineWavePluginAction>> Create(
      const ParameterProto& parameters, IconActionFactoryContext& context);

  // Resets the internal state upon entering the Action. Note that an Action can
  // be entered more than once – either because a user manually requests it to
  // start, or because a Reaction switches to it.
  RealtimeStatus OnEnter(const IconConstRealtimeSlotMap& slot_map) override;

  RealtimeStatus Sense(const IconConstRealtimeSlotMap& slot_map,
                       IconStreamingIoAccess& io_access,
                       IconRealtimeSignalAccess& signal_access) override;

  RealtimeStatus Control(IconRealtimeSlotMap& slot_map) override;

  RealtimeStatusOr<StateVariableValue> GetStateVariable(
      absl::string_view name) const override;

 private:
  // Holds internal Action state, bundled for convenient resetting in OnEnter().
  struct State {
    double time_since_start = 0.0;
    eigenmath::VectorNd starting_position;
    std::optional<float> current_number = std::nullopt;
  };

  std::optional<State> current_state_ = std::nullopt;
  RealtimeSlotId slot_id_;
  double frequency_hz_;
  const JointLimits application_limits_;
  const SolvedParams params_;
  const StreamingInputId number_id_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_SINE_WAVE_PLUGIN_ACTION_H_
