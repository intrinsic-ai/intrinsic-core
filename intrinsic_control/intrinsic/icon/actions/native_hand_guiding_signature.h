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

#ifndef INTRINSIC_ICON_ACTIONS_NATIVE_HAND_GUIDING_SIGNATURE_H_
#define INTRINSIC_ICON_ACTIONS_NATIVE_HAND_GUIDING_SIGNATURE_H_

#include "intrinsic/icon/proto/v1/types.pb.h"

namespace intrinsic::icon {

// Populates an ActionSignature proto for the NativeHandGuiding Action.
// This can be used by Application Layer servers to fill the response to the
// ListActionSignatures() RPC.
intrinsic_proto::icon::v1::ActionSignature GetNativeHandGuidingSignature();

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_ACTIONS_NATIVE_HAND_GUIDING_SIGNATURE_H_
