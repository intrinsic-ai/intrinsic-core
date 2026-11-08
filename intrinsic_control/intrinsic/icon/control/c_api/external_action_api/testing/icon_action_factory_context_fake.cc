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

#include "intrinsic/icon/control/c_api/external_action_api/testing/icon_action_factory_context_fake.h"

#include <cstddef>
#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/control/c_api/c_action_factory_context.h"
#include "intrinsic/icon/control/c_api/c_realtime_status.h"
#include "intrinsic/icon/control/c_api/c_types.h"
#include "intrinsic/icon/control/c_api/convert_c_realtime_status.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_action_factory_context.h"
#include "intrinsic/icon/control/c_api/wrappers/string_wrapper.h"
#include "intrinsic/icon/control/streaming_io_types.h"

namespace intrinsic::icon {

IconActionFactoryContext
IconActionFactoryContextFake::MakeIconActionFactoryContext() {
  return IconActionFactoryContext(
      reinterpret_cast<IntrinsicIconActionFactoryContext*>(this),
      GetCApiVtable());
}

IntrinsicIconActionFactoryContextVtable
IconActionFactoryContextFake::GetCApiVtable() {
  // The lambdas defined in this method are implicitly friends of
  // IconActionFactoryContextFake, so they can access its private members.
  return {
      .destroy_string = &DestroyString,
      .server_config = [](const IntrinsicIconActionFactoryContext* self)
          -> IntrinsicIconString* {
        return Wrap(reinterpret_cast<const IconActionFactoryContextFake*>(self)
                        ->server_config_.SerializeAsString());
      },
      .get_slot_info = [](IntrinsicIconActionFactoryContext* self,
                          IntrinsicIconStringView slot_name,
                          IntrinsicIconSlotInfo* slot_info_out)
          -> IntrinsicIconRealtimeStatus {
        absl::string_view slot_name_view(slot_name.data, slot_name.size);
        auto* fake = reinterpret_cast<IconActionFactoryContextFake*>(self);
        auto slot_info = fake->slot_map_.GetSlotInfoForSlot(slot_name_view);
        if (!slot_info.ok()) {
          return FromAbslStatus(slot_info.status());
        }
        slot_info_out->realtime_slot_id = slot_info->slot_id.value();
        slot_info_out->part_config_buffer =
            Wrap(slot_info->config.SerializeAsString());
        return FromAbslStatus(absl::OkStatus());
      },
      .get_realtime_signal_id =
          [](IntrinsicIconActionFactoryContext* self,
             IntrinsicIconStringView signal_name,
             uint64_t* signal_id_out) -> IntrinsicIconRealtimeStatus {
        auto* fake = reinterpret_cast<IconActionFactoryContextFake*>(self);
        absl::string_view signal_name_view(signal_name.data, signal_name.size);
        auto id = fake->realtime_signal_access_and_map_.GetRealtimeSignalId(
            signal_name_view);
        if (!id.ok()) {
          return FromAbslStatus(id.status());
        }
        *signal_id_out = id.value().value();
        return FromAbslStatus(absl::OkStatus());
      },
      .add_streaming_input_parser =
          [](IntrinsicIconActionFactoryContext* self,
             IntrinsicIconStringView input_name,
             IntrinsicIconStringView input_proto_message_type_name,
             IntrinsicIconStreamingInputParserFnInstance parser,
             uint64_t* streaming_input_id_out) -> IntrinsicIconRealtimeStatus {
        auto* fake = reinterpret_cast<IconActionFactoryContextFake*>(self);
        absl::string_view input_name_view(input_name.data, input_name.size);
        absl::string_view input_proto_message_type_name_view(
            input_proto_message_type_name.data,
            input_proto_message_type_name.size);
        absl::StatusOr<StreamingInputId> input_id =
            fake->streaming_io_registry_.AddInputParser(
                input_name_view, input_proto_message_type_name_view, parser);
        if (!input_id.ok()) {
          return FromAbslStatus(input_id.status());
        }
        *streaming_input_id_out = input_id->value();
        return FromAbslStatus(absl::OkStatus());
      },
      .add_streaming_output_converter =
          [](IntrinsicIconActionFactoryContext* self,
             IntrinsicIconStringView output_proto_message_type_name,
             size_t realtime_type_size,
             IntrinsicIconStreamingOutputConverterFnInstance converter)
          -> IntrinsicIconRealtimeStatus {
        auto fake = reinterpret_cast<IconActionFactoryContextFake*>(self);
        return FromAbslStatus(fake->streaming_io_registry_.AddOutputConverter(
            absl::string_view(output_proto_message_type_name.data,
                              output_proto_message_type_name.size),
            converter));
      },
  };
}
}  // namespace intrinsic::icon
