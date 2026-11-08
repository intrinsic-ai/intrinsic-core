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

#ifndef INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_ICON_ACTION_FACTORY_CONTEXT_H_
#define INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_ICON_ACTION_FACTORY_CONTEXT_H_

#include <functional>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/message.h"
#include "intrinsic/icon/control/c_api/c_action_factory_context.h"
#include "intrinsic/icon/control/c_api/wrappers/streaming_io_wrapper.h"
#include "intrinsic/icon/control/c_api/wrappers/string_wrapper.h"
#include "intrinsic/icon/control/realtime_signal_types.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/control/streaming_io_types.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

// This class makes the methods of an IntrinsicActionFactoryContext pointer
// available via nicer C++ APIs.
//
// Actions receive a reference to an IconActionFactoryContext in their Create()
// function, which runs in a non-realtime environment.
class IconActionFactoryContext {
 public:
  IconActionFactoryContext(
      IntrinsicIconActionFactoryContext* icon_action_factory_context,
      IntrinsicIconActionFactoryContextVtable
          icon_action_factory_context_vtable)
      : icon_action_factory_context_(icon_action_factory_context),
        icon_action_factory_context_vtable_(
            std::move(icon_action_factory_context_vtable)) {}

  // Returns the ServerConfig for the server this context belongs to.
  intrinsic_proto::icon::v1::ServerConfig ServerConfig() const;

  // Returns a SlotInfo object for the given `slot_name`.
  // Use this to
  // * Learn the RealtimeSlotId for this slot. Your Action needs that ID to
  //   access the Slot in its Sense()/Control() methods.
  // * Read the PartConfig proto for this slot. That proto should have all the
  //   information you need to create an instance of your Action and, for
  //   example, pre-allocate buffers to the correct size based on Slot
  //   properties.
  //
  // Returns NotFoundError if there is no slot called `slot_name`.
  absl::StatusOr<intrinsic::icon::SlotInfo> GetSlotInfo(
      absl::string_view slot_name) const;

  // Returns the RealtimeSignalId for the given `realtime_signal_name`. This can
  // be used to access the signal value during an action Sense() cycle.
  //
  // Returns NotFoundError if there is no signal called `realtime_signal_name`.
  absl::StatusOr<RealtimeSignalId> GetRealtimeSignalId(
      absl::string_view realtime_signal_name);

  // Registers `parser` as the parser for the streaming input `input_name`.
  // Returns a StreamingInputId on success. Your Action needs to hold on to this
  // ID to use during realtime operation (i.e. in its Sense() method).
  //
  // NOTE: There are no checks that ensure your Action uses `RealtimeT` when
  // accessing this input in its Sense() method! Make sure to test this to avoid
  // segmentation faults.
  //
  // Returns NotFoundError if your Action's signature does not contain a
  // streaming input called `input_name`.
  // Returns InvalidArgumentError if the input `input_name` exists, but accepts
  // a different proto message type than `ProtoT`.
  template <typename ProtoT, typename RealtimeT,
            typename = std::enable_if_t<
                std::is_base_of_v<::google::protobuf::Message, ProtoT>>>
  absl::StatusOr<StreamingInputId> AddStreamingInputParser(
      absl::string_view input_name,
      std::function<absl::StatusOr<RealtimeT>(const ProtoT& streaming_input)>
          parser);

  // Registers `converter` as the conversion function for the streaming output
  // of this Action.
  //
  // It is the Action author's responsibility to ensure that the Action uses
  // `RealtimeT` when writing streaming output values. Using a different type
  // will lead to errors and potential crashes.
  //
  // Returns NotFoundError if your Action's signature does not contain a
  // streaming output.
  // Returns InvalidArgumentError if your Action's signature *has* a streaming
  // output, but that output has a proto message type other than `ProtoT`.
  template <typename RealtimeT, typename ProtoT,
            typename = std::enable_if_t<
                std::is_base_of_v<::google::protobuf::Message, ProtoT>>>
  absl::Status AddStreamingOutputConverter(
      std::function<absl::StatusOr<ProtoT>(const RealtimeT& streaming_output)>
          converter);

 private:
  IntrinsicIconActionFactoryContext* icon_action_factory_context_ = nullptr;
  IntrinsicIconActionFactoryContextVtable icon_action_factory_context_vtable_;
};

template <typename ProtoT, typename RealtimeT, typename>
absl::StatusOr<StreamingInputId>
IconActionFactoryContext::AddStreamingInputParser(
    absl::string_view input_name,
    std::function<absl::StatusOr<RealtimeT>(const ProtoT& streaming_input)>
        parser) {
  IntrinsicIconStreamingInputParserFnInstance parser_wrapped =
      WrapStreamingInputParser(std::move(parser));
  uint64_t streaming_input_id;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      icon_action_factory_context_vtable_.add_streaming_input_parser(
          icon_action_factory_context_, WrapView(input_name),
          WrapView(ProtoT::GetDescriptor()->full_name()), parser_wrapped,
          &streaming_input_id)));
  return StreamingInputId(streaming_input_id);
}

template <typename RealtimeT, typename ProtoT, typename>
absl::Status IconActionFactoryContext::AddStreamingOutputConverter(
    std::function<absl::StatusOr<ProtoT>(const RealtimeT& streaming_output)>
        converter) {
  IntrinsicIconStreamingOutputConverterFnInstance converter_wrapped =
      WrapStreamingOutputConverter(std::move(converter));
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      icon_action_factory_context_vtable_.add_streaming_output_converter(
          icon_action_factory_context_,
          WrapView(ProtoT::GetDescriptor()->full_name()), sizeof(RealtimeT),
          converter_wrapped)));
  return absl::OkStatus();
}

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_ICON_ACTION_FACTORY_CONTEXT_H_
