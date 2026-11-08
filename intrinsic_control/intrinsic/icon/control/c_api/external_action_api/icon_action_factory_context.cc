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

#include "intrinsic/icon/control/c_api/external_action_api/icon_action_factory_context.h"

#include <cstdint>
#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/control/c_api/c_action_factory_context.h"
#include "intrinsic/icon/control/c_api/c_types.h"
#include "intrinsic/icon/control/c_api/convert_c_realtime_status.h"
#include "intrinsic/icon/control/c_api/wrappers/string_wrapper.h"
#include "intrinsic/icon/control/realtime_signal_types.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {
namespace {
struct DestroyIconString {
  void operator()(IntrinsicIconString* ptr) { destroy(ptr); }
  IntrinsicIconStringDestroy destroy;
};
}  // namespace

intrinsic_proto::icon::v1::ServerConfig IconActionFactoryContext::ServerConfig()
    const {
  std::unique_ptr<IntrinsicIconString, DestroyIconString> server_config_string(
      icon_action_factory_context_vtable_.server_config(
          icon_action_factory_context_),
      DestroyIconString{icon_action_factory_context_vtable_.destroy_string});
  intrinsic_proto::icon::v1::ServerConfig config_proto;
  static_cast<void>(config_proto.ParseFromString(absl::string_view(
      server_config_string->data,
      server_config_string->size)));  // TODO: Handle return value
  return config_proto;
}

absl::StatusOr<intrinsic::icon::SlotInfo> IconActionFactoryContext::GetSlotInfo(
    absl::string_view slot_name) const {
  IntrinsicIconSlotInfo slot_info_c;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(icon_action_factory_context_vtable_.get_slot_info(
          icon_action_factory_context_, WrapView(slot_name), &slot_info_c)));
  std::unique_ptr<IntrinsicIconString, DestroyIconString> part_config_string(
      slot_info_c.part_config_buffer,
      DestroyIconString{icon_action_factory_context_vtable_.destroy_string});
  intrinsic::icon::SlotInfo slot_info;
  slot_info.slot_id = RealtimeSlotId(slot_info_c.realtime_slot_id);
  if (!slot_info.config.ParseFromString(absl::string_view(
          part_config_string->data, part_config_string->size))) {
    return absl::InternalError("Failed to parse part config proto.");
  }
  return slot_info;
}

absl::StatusOr<RealtimeSignalId> IconActionFactoryContext::GetRealtimeSignalId(
    absl::string_view realtime_signal_name) {
  uint64_t signal_id_out;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(icon_action_factory_context_vtable_.get_realtime_signal_id(
          icon_action_factory_context_, WrapView(realtime_signal_name),
          &signal_id_out)));
  return RealtimeSignalId(signal_id_out);
}

}  // namespace intrinsic::icon
