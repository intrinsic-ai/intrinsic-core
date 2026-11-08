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
#include "intrinsic/icon/apps/sine_wave_action_info.h"
#include "intrinsic/icon/apps/sine_wave_action_signature.h"
#include "intrinsic/icon/control/rtcl_action_factory_registry.h"

namespace intrinsic::icon {
namespace {

const auto kUnused =
    GetGlobalRtclActionFactoryRegistry()
        .RegisterTyped<SineWaveActionInfo::FixedParams>(
            SineWaveActionInfo::kActionTypeName, &RtclSineWaveAction::Create,
            GetSineWaveActionSignature());

}  // namespace
}  // namespace intrinsic::icon
