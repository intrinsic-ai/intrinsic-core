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

#include "intrinsic/icon/apps/sine_wave_action_signature.h"

#include "absl/log/check.h"
#include "google/protobuf/duration.pb.h"
#include "intrinsic/icon/actions/action_utils.h"
#include "intrinsic/icon/apps/sine_wave_action_info.h"
#include "intrinsic/icon/proto/v1/types.pb.h"

namespace intrinsic::icon {

intrinsic_proto::icon::v1::ActionSignature GetSineWaveActionSignature() {
  ActionSignatureBuilder b(SineWaveActionInfo::kActionTypeName,
                           "Moves each joint in a sine wave centered on the "
                           "position it is in when starting the Action.");
  CHECK_OK(b.AddPartSlot(SineWaveActionInfo::kSlotName,
                         SineWaveActionInfo::kSlotDescription,
                         /*required_feature_interfaces=*/
                         {
                             intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                 FEATURE_INTERFACE_JOINT_POSITION,
                             intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                 FEATURE_INTERFACE_JOINT_POSITION_SENSOR,
                             intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                 FEATURE_INTERFACE_JOINT_LIMITS,
                         }));
  CHECK_OK(b.SetFixedParametersType<SineWaveActionInfo::FixedParams>());
  CHECK_OK(b.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                  StateVariableInfo::TYPE_DOUBLE>(
      SineWaveActionInfo::kTime, SineWaveActionInfo::kTimeDescription));
  CHECK_OK(b.AddStreamingOutput<google::protobuf::Duration>(
      "time_since_start", "Reports the time since the Action was started."));
  return b.Finish();
}

}  // namespace intrinsic::icon
