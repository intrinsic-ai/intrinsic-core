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

#include "absl/log/check.h"
#include "intrinsic/icon/actions/action_utils.h"
#include "intrinsic/icon/actions/native_hand_guiding_info.h"
#include "intrinsic/icon/proto/v1/types.pb.h"

namespace intrinsic::icon {

intrinsic_proto::icon::v1::ActionSignature GetNativeHandGuidingSignature() {
  ActionSignatureBuilder builder(NativeHandGuidingInfo::kActionTypeName,
                                 NativeHandGuidingInfo::kActionDescription);
  CHECK_OK(
      builder.SetFixedParametersType<NativeHandGuidingInfo::FixedParams>());
  CHECK_OK(builder.AddPartSlot(
      NativeHandGuidingInfo::kSlotName, NativeHandGuidingInfo::kSlotDescription,
      {intrinsic_proto::icon::v1::FeatureInterfaceTypes::
           FEATURE_INTERFACE_HAND_GUIDING}));
  return builder.Finish();
}

}  // namespace intrinsic::icon
