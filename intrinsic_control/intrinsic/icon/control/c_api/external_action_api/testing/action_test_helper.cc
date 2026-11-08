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

#include "intrinsic/icon/control/c_api/external_action_api/testing/action_test_helper.h"

#include "absl/strings/string_view.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_action_interface.h"
#include "intrinsic/icon/control/c_api/external_action_api/testing/icon_streaming_io_registry_fake.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/realtime_status.h"

namespace intrinsic::icon {

ActionTestHelper::ActionTestHelper(
    double control_frequency_hz,
    const ::intrinsic_proto::icon::v1::ActionSignature& signature,
    absl::string_view server_name)
    : streaming_io_registry_(signature), signal_access_and_map_(signature) {
  server_config_.set_frequency_hz(control_frequency_hz);
  server_config_.set_name(server_name);
}

RealtimeStatus ActionTestHelper::EnterAction(IconActionInterface& action) {
  return action.OnEnter(slot_map_.MakeIconConstRealtimeSlotMap());
}

RealtimeStatus ActionTestHelper::SenseAndControlAction(
    IconActionInterface& action) {
  auto streaming_io_access = streaming_io_registry_.MakeIconStreamingIoAccess();
  auto signal_access = signal_access_and_map_.MakeIconRealtimeSignalAccess();
  if (RealtimeStatus s = action.Sense(slot_map_.MakeIconConstRealtimeSlotMap(),
                                      streaming_io_access, signal_access);
      !s.ok()) {
    return s;
  }
  auto mutable_rt_slot_map = slot_map_.MakeIconRealtimeSlotMap();
  return action.Control(mutable_rt_slot_map);
}

}  // namespace intrinsic::icon
