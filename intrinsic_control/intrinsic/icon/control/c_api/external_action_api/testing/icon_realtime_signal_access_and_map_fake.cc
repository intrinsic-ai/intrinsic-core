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

#include "intrinsic/icon/control/c_api/external_action_api/testing/icon_realtime_signal_access_and_map_fake.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/container/fixed_array.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/control/c_api/c_realtime_signal_access.h"
#include "intrinsic/icon/control/c_api/c_realtime_status.h"
#include "intrinsic/icon/control/c_api/c_types.h"
#include "intrinsic/icon/control/c_api/convert_c_realtime_status.h"
#include "intrinsic/icon/control/c_api/convert_c_types.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_realtime_signal_access.h"
#include "intrinsic/icon/control/realtime_signal_types.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"

namespace intrinsic::icon {

IconRealtimeSignalAccessAndMapFake::IconRealtimeSignalAccessAndMapFake(
    const ::intrinsic_proto::icon::v1::ActionSignature& signature)
    : signature_(signature) {
  size_t next_index = 0;
  for (const ::intrinsic_proto::icon::v1::ActionSignature::RealtimeSignalInfo&
           signal_info : signature.realtime_signal_infos()) {
    auto [signal_id_it, inserted] =
        signal_id_map_.try_emplace(signal_info.signal_name());
    CHECK_EQ(inserted, true);
    signal_id_it->second = RealtimeSignalId(next_index++);
  }
  signal_ids_ =
      std::make_unique<absl::FixedArray<SignalValue>>(signal_id_map_.size());
};

RealtimeStatusOr<SignalValue> IconRealtimeSignalAccessAndMapFake::ReadSignal(
    RealtimeSignalId id) {
  if (id.value() >= signal_ids_->size()) {
    return icon::NotFoundError(RealtimeStatus::StrCat(
        "No realtime signal found with id: ", id.value()));
  }
  return signal_ids_->at(id.value());
};

absl::StatusOr<RealtimeSignalId>
IconRealtimeSignalAccessAndMapFake::GetRealtimeSignalId(
    absl::string_view realtime_signal_name) {
  auto signal_name_and_id = signal_id_map_.find(realtime_signal_name);
  if (signal_name_and_id == signal_id_map_.end()) {
    return absl::NotFoundError(absl::StrCat(
        "Action type '", signature_.action_type_name(),
        "' does not declare realtime signal '", realtime_signal_name,
        "' in its signature. Available signals: [",
        absl::StrJoin(signal_id_map_, ", ",
                      [](std::string* str, const auto& name_and_id) {
                        absl::StrAppend(str, name_and_id.first);
                      }),
        "]"));
  }
  return signal_name_and_id->second;
};

IconRealtimeSignalAccess
IconRealtimeSignalAccessAndMapFake::MakeIconRealtimeSignalAccess() {
  return IconRealtimeSignalAccess(
      reinterpret_cast<IntrinsicIconRealtimeSignalAccess*>(this),
      GetCApiVtable());
}

// static
IntrinsicIconRealtimeSignalAccessVtable
IconRealtimeSignalAccessAndMapFake::GetCApiVtable() {
  return {
      .read_signal = [](IntrinsicIconRealtimeSignalAccess* self, uint64_t id,
                        IntrinsicIconSignalValue* signal_value)
          -> IntrinsicIconRealtimeStatus {
        auto signal_access =
            reinterpret_cast<IconRealtimeSignalAccessAndMapFake*>(self);
        auto signal = signal_access->ReadSignal(RealtimeSignalId(id));
        if (!signal.ok()) {
          return FromAbslStatus(signal.status());
        }
        *signal_value = Convert(signal.value());
        return FromAbslStatus(OkStatus());
      },
  };
}

}  // namespace intrinsic::icon
