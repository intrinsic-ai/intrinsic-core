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

#ifndef INTRINSIC_ICON_APPS_RTCL_SINE_WAVE_ACTION_H_
#define INTRINSIC_ICON_APPS_RTCL_SINE_WAVE_ACTION_H_

#include <cstddef>
#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/apps/sine_wave_action.pb.h"
#include "intrinsic/icon/apps/sine_wave_action_info.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/control/action_factory_context.h"
#include "intrinsic/icon/control/reaction.h"
#include "intrinsic/icon/control/rtcl_action.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/util/fixed_vector.h"

namespace intrinsic::icon {

// A custom realtime Action that moves each joint according to a sine wave
// defined by the parameters.
// All sine waves begin at the position each joint is in when the Action becomes
// active, so that no discontinuous commands are sent to the hardware.
//
// The Action also demonstrates the use of a streaming output by reporting the
// time since it became active (which is used to calculate the sine waves).
class RtclSineWaveAction final : public RtclActionInterface {
 public:
  struct Params {
    static constexpr size_t kMaxJoints = 16;
    intrinsic::FixedVector<double, kMaxJoints> amplitudes;
    intrinsic::FixedVector<double, kMaxJoints> frequencies;

    static absl::StatusOr<Params> FromProto(
        const intrinsic_proto::icon::actions::SineWaveFixedParams&
            proto_params);
    // Checks that the amplitude and frequency vectors both have `ndof` members.
    bool SizeIs(size_t ndof) const;
  };

  RtclSineWaveAction(RealtimeSlotId slot_id,
                     const JointLimits& application_limits, double frequency_hz,
                     RtclSineWaveAction::Params params);

  static absl::StatusOr<std::unique_ptr<RtclSineWaveAction>> Create(
      const SineWaveActionInfo::FixedParams& params_proto,
      ActionFactoryContext& context);

  RealtimeStatus OnEnter(OnEnterParameters params) override;
  RealtimeStatus Sense(SenseParameters params) override;

  RealtimeStatus Control(ControlParameters params) override;

  RealtimeStatusOr<StateVariableValue> GetStateVariable(
      absl::string_view name) const override;

 private:
  const RealtimeSlotId slot_id_;
  const double frequency_hz_;
  const JointLimits application_limits_;
  eigenmath::VectorNd starting_position_;
  double time_since_start_ = 0.0;
  bool initialized_ = false;
  Params command_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_APPS_RTCL_SINE_WAVE_ACTION_H_
