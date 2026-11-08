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

#ifndef INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_TESTING_ICON_REALTIME_SIGNAL_ACCESS_AND_MAP_FAKE_H_
#define INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_TESTING_ICON_REALTIME_SIGNAL_ACCESS_AND_MAP_FAKE_H_

#include <memory>
#include <string>

#include "absl/container/fixed_array.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/control/c_api/c_realtime_signal_access.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_realtime_signal_access.h"
#include "intrinsic/icon/control/realtime_signal_types.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/realtime_status_or.h"

namespace intrinsic::icon {

class IconRealtimeSignalAccessAndMapFake {
 public:
  explicit IconRealtimeSignalAccessAndMapFake(
      const ::intrinsic_proto::icon::v1::ActionSignature& signature);

  RealtimeStatusOr<SignalValue> ReadSignal(RealtimeSignalId id);

  absl::StatusOr<RealtimeSignalId> GetRealtimeSignalId(
      absl::string_view realtime_signal_name);

  // Creates an IconRealtimeSignalAccess that is backed by this
  // IconRealtimeSignalAccessAndMapFake. You can then pass that object to the
  // Sense() method of an Action under test.
  IconRealtimeSignalAccess MakeIconRealtimeSignalAccess();

 private:
  static IntrinsicIconRealtimeSignalAccessVtable GetCApiVtable();

  const ::intrinsic_proto::icon::v1::ActionSignature signature_;
  std::unique_ptr<absl::FixedArray<SignalValue>> signal_ids_;
  absl::flat_hash_map<std::string, RealtimeSignalId> signal_id_map_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_TESTING_ICON_REALTIME_SIGNAL_ACCESS_AND_MAP_FAKE_H_
