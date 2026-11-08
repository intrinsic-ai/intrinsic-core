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

#include "intrinsic/icon/control/c_api/external_action_api/icon_realtime_signal_access.h"

#include "intrinsic/icon/control/c_api/c_realtime_signal_access.h"
#include "intrinsic/icon/control/c_api/c_realtime_status.h"
#include "intrinsic/icon/control/c_api/c_types.h"
#include "intrinsic/icon/control/c_api/convert_c_realtime_status.h"
#include "intrinsic/icon/control/c_api/convert_c_types.h"
#include "intrinsic/icon/control/realtime_signal_types.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"

namespace intrinsic::icon {

RealtimeStatusOr<SignalValue> IconRealtimeSignalAccess::ReadSignal(
    RealtimeSignalId id) {
  IntrinsicIconSignalValue signal_value;
  IntrinsicIconRealtimeStatus read_signal_status =
      realtime_signal_access_vtable_.read_signal(realtime_signal_access_,
                                                 id.value(), &signal_value);
  INTRINSIC_RT_RETURN_IF_ERROR(ToRealtimeStatus(read_signal_status));
  return Convert(signal_value);
}

}  // namespace intrinsic::icon
