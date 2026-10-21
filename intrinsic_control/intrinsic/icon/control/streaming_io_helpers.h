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

#ifndef INTRINSIC_ICON_CONTROL_STREAMING_IO_HELPERS_H_
#define INTRINSIC_ICON_CONTROL_STREAMING_IO_HELPERS_H_

#include <stddef.h>

#include <any>
#include <atomic>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

#include "absl/algorithm/container.h"
#include "absl/container/fixed_array.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/control/initialized_async_buffer.h"
#include "intrinsic/icon/control/streaming_io_storage.h"
#include "intrinsic/icon/control/streaming_io_types.h"
#include "intrinsic/icon/proto/streaming_output.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/async_buffer.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

// Determines the index of the streaming input `input_name` in `io_storage`.
//
// Returns NotFoundError if there is no streaming input `input_name` in
// the signature stored in `io_storage`.
// Returns InvalidArgumentError if there is a streaming input in the signature,
// but its type is not `ProtoT`.
// Returns InternalError if there is a matching streaming input in
// `io_storage`'s signature, but there is no corresponding StreamingInputChannel
// object.
template <typename ProtoT, typename = std::enable_if_t<std::is_base_of_v<
                               google::protobuf::Message, ProtoT>>>
absl::StatusOr<size_t> GetStreamingInputIndex(absl::string_view input_name,
                                              StreamingIoStorage& io_storage) {
  const auto streaming_input_it = absl::c_find_if(
      io_storage.signature_.streaming_input_infos(),
      [&input_name](
          const intrinsic_proto::icon::v1::ActionSignature::ParameterInfo&
              streaming_input_info) {
        return streaming_input_info.parameter_name() == input_name;
      });
  if (streaming_input_it ==
      io_storage.signature_.streaming_input_infos().end()) {
    return absl::NotFoundError(absl::StrCat(
        "Action ", io_storage.id_.value(), " of type '",
        io_storage.signature_.action_type_name(),
        "' does not have a streaming input named '", input_name, "'"));
  }
  const auto& streaming_input_info = *streaming_input_it;
  if (ProtoT::GetDescriptor()->full_name() !=
      streaming_input_info.value_message_type()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Streaming input '", input_name, "' of Action ", io_storage.id_.value(),
        " of type '", io_storage.signature_.action_type_name(),
        "' has the proto message type '",
        streaming_input_info.value_message_type(), "', but we expected '",
        ProtoT::GetDescriptor()->full_name(), "'."));
  }
  auto input_channel_it = absl::c_find_if(
      io_storage.input_channels_,
      [&input_name](const StreamingInputChannel& input_channel) {
        return input_channel.input_name == input_name;
      });
  if (input_channel_it == io_storage.input_channels_.end()) {
    return absl::InternalError(
        absl::StrCat("No InputChannel object for streaming input named '",
                     input_name, "' of Action ", io_storage.id_.value(),
                     " of type '", io_storage.signature_.action_type_name(),
                     "'. This is a programming error in ICON!"));
  }
  return std::distance(io_storage.input_channels_.begin(), input_channel_it);
}

// Determines the index of the streaming input `input_name` in `io_storage`.
//
// Returns NotFoundError if there is no streaming input `input_name` in
// the signature stored in `io_storage`.
// Returns InvalidArgumentError if there is a streaming input in the signature,
// but its type name is not `input_proto_type_name`.
// Returns InternalError if there is a matching streaming input in
// `io_storage`'s signature, but there is no corresponding StreamingInputChannel
// object.
absl::StatusOr<size_t> GetStreamingInputIndexAny(
    absl::string_view input_name, absl::string_view input_proto_type_name,
    StreamingIoStorage& io_storage);

// Attempts to convert `input_proto` to the realtime data type for `input_name`,
// and write the result into the corresponding buffer in `io_storage`.
//
// Returns the same errors as GetStreamingInputIndex under the same conditions.
// Returns FailedPreconditionError if there is no parser in the
// StreamingInputChannel object.
// Forwards any errors from the streaming input parser.
template <typename ProtoT, typename = std::enable_if_t<std::is_base_of_v<
                               google::protobuf::Message, ProtoT>>>
absl::Status WriteStreamingInput(absl::string_view input_name,
                                 const ProtoT& input_proto,
                                 StreamingIoStorage& io_storage) {
  INTR_ASSIGN_OR_RETURN(size_t input_channel_index,
                        GetStreamingInputIndex<ProtoT>(input_name, io_storage));
  StreamingInputChannel& input_channel =
      io_storage.input_channels_[input_channel_index];
  // Make sure that we have a parser for this input.
  if (input_channel.input_parser == nullptr) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Cannot write to streaming input '", input_name, "' of Action ",
        io_storage.id_.value(), " of type '",
        io_storage.signature_.action_type_name(), "': No parser registered."));
  }
  google::protobuf::Any input_any_proto;
  if (!input_any_proto.PackFrom(input_proto)) {
    return absl::InternalError("Failed to pack streaming input proto.");
  }
  std::any* input_buffer = input_channel.input_buffer.GetFreeBuffer();
  INTR_ASSIGN_OR_RETURN(*input_buffer,
                        input_channel.input_parser(input_any_proto));
  input_channel.input_buffer.CommitFreeBuffer();
  input_channel.has_value = true;
  return absl::OkStatus();
}

// Attempts to convert `input_proto` to the realtime data type for `input_name`,
// and write the result into the corresponding buffer in `io_storage`.
//
// Returns the same errors as GetStreamingInputIndex under the same conditions.
// Returns FailedPreconditionError if there is no parser in the
// StreamingInputChannel object.
// Forwards any errors from the streaming input parser.
absl::Status WriteStreamingInputAny(absl::string_view input_name,
                                    const google::protobuf::Any& input_proto,
                                    StreamingIoStorage& io_storage);

// Waits until `deadline` to read the current value of the streaming output
// for `io_storage`.
//
// Returns FailedPreconditionError if `io_storage` does not have a streaming
// output.
// Returns InvalidArgumentError if there is a streaming output, but its
// proto message type is different from `ProtoT`. Returns
// FailedPreconditionError if there is no converter function for the
// streaming output.
// Returns DeadlineExceededError if no streaming output value is available
// before `deadline`.
// Forwards any errors from the converter function.
template <typename ProtoT, typename = std::enable_if_t<std::is_base_of_v<
                               google::protobuf::Message, ProtoT>>>
absl::StatusOr<ProtoT> PollStreamingOutput(absl::Time deadline,
                                           StreamingIoStorage& io_storage) {
  if (!io_storage.signature_.has_streaming_output_info()) {
    return absl::FailedPreconditionError(
        absl::StrCat("Action ", io_storage.id_.value(), " of type '",
                     io_storage.signature_.action_type_name(),
                     "' does not have a streaming output."));
  }
  if (io_storage.signature_.streaming_output_info().value_message_type() !=
      ProtoT::GetDescriptor()->full_name()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Action ", io_storage.id_.value(), " of type '",
        io_storage.signature_.action_type_name(),
        "' does not have a streaming output of type '",
        ProtoT::GetDescriptor()->full_name(),
        "'. Its streaming output is of type '",
        io_storage.signature_.streaming_output_info().value_message_type(),
        "'."));
  }
  // Make sure that we have an output converter.
  if (io_storage.output_channel_ == nullptr) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Cannot poll streaming output of Action ", io_storage.id_.value(),
        " of type '", io_storage.signature_.action_type_name(),
        "': No converter registered."));
  }
  std::optional<StreamingOutputChannel::PayloadAndTimestamp>
      realtime_streaming_output =
          io_storage.output_channel_->output_buffer_.GetCurrentValue(deadline);
  if (!realtime_streaming_output.has_value()) {
    return absl::DeadlineExceededError(
        absl::StrCat("Timed out waiting for streaming output of Action ",
                     io_storage.id_.value(), " of type ",
                     io_storage.signature_.action_type_name()));
  }
  INTR_ASSIGN_OR_RETURN(google::protobuf::Any output_any_proto,
                        io_storage.output_channel_->output_converter_(
                            realtime_streaming_output.value().payload));
  ProtoT output_proto;
  if (!output_any_proto.UnpackTo(&output_proto)) {
    return absl::InternalError(
        absl::StrCat("Failed to unpack the result of the conversion for "
                     "streaming output of Action ",
                     io_storage.id_.value(), " of type ",
                     io_storage.signature_.action_type_name()));
  }
  return output_proto;
}

struct StreamingOutputWithCycle {
  intrinsic_proto::icon::StreamingOutput output;
  uint64_t cycle_index;
};

// Waits until `deadline` to read the current value of the streaming output
// for `io_storage`.
//
// Returns FailedPreconditionError if `io_storage` does not have a streaming
// output.
// Returns InvalidArgumentError if there is a streaming output, but its
// proto message type is different from `ProtoT`. Returns
// FailedPreconditionError if there is no converter function for the
// streaming output.
// Returns DeadlineExceededError if no streaming output value is available
// before `deadline`.
// Forwards any errors from the converter function.
absl::StatusOr<StreamingOutputWithCycle> PollStreamingOutputAny(
    absl::Time deadline, StreamingIoStorage& io_storage);

// Attempts to pull the next oldest output from io_storage.
//
// If multiple callers are calling this in parallel, each will only see a
// fraction of the streaming outputs.
//
// Returns std::nullopt if there has been no new output since this function was
// last called.
// Returns FailedPreconditionError if `io_storage` does not have a streaming
// output.
// Returns InvalidArgumentError if there is a streaming output, but its
// proto message type is different from `ProtoT`. Returns
// FailedPreconditionError if there is no converter function for the
// streaming output.
// Forwards any errors from the converter function.
absl::StatusOr<std::optional<StreamingOutputWithCycle>> GetNextStreamingOutput(
    StreamingIoStorage& io_storage);

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_STREAMING_IO_HELPERS_H_
