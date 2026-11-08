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

#include "intrinsic/icon/control/c_api/external_action_api/testing/icon_streaming_io_registry_fake.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/control/c_api/c_action_factory_context.h"
#include "intrinsic/icon/control/c_api/c_realtime_status.h"
#include "intrinsic/icon/control/c_api/c_streaming_io_realtime_access.h"
#include "intrinsic/icon/control/c_api/c_types.h"
#include "intrinsic/icon/control/c_api/convert_c_realtime_status.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_streaming_io_access.h"
#include "intrinsic/icon/control/streaming_io_types.h"
#include "intrinsic/icon/proto/v1/types.pb.h"

namespace intrinsic::icon {

// static
IntrinsicIconStreamingIoRealtimeAccessVtable
IconStreamingIoRegistryFake::GetCApiVtable() {
  return {
      .poll_input = [](IntrinsicIconStreamingIoRealtimeAccess* self,
                       uint64_t input_id,
                       IntrinsicIconRealtimeStatus* status_out)
          -> const IntrinsicIconStreamingInputType* {
        auto* registry = reinterpret_cast<IconStreamingIoRegistryFake*>(self);
        StreamingInputId id(input_id);
        if (!registry->HasStreamingInput(id)) {
          *status_out = FromAbslStatus(
              absl::NotFoundError(absl::StrCat("No input with ID ", input_id)));
          return nullptr;
        }
        *status_out = FromAbslStatus(absl::OkStatus());
        return registry->streaming_input_parser_map_.at(id).GetLatestInput();
      },
      .write_output = [](IntrinsicIconStreamingIoRealtimeAccess* self,
                         const IntrinsicIconStreamingOutputType* output,
                         size_t size) -> IntrinsicIconRealtimeStatus {
        auto* registry = reinterpret_cast<IconStreamingIoRegistryFake*>(self);
        // This is actually a bit different from a "live" system. In the actual
        // realtime system, the conversion function is decoupled from the
        // realtime thread, so we can't report errors back to the Action
        // immediately. But in this test helper we can, shortening the feedback
        // loop.
        return registry->output_converter_.Invoke(output, size);
      },
  };
}

IconStreamingIoAccess IconStreamingIoRegistryFake::MakeIconStreamingIoAccess() {
  return IconStreamingIoAccess(
      reinterpret_cast<IntrinsicIconStreamingIoRealtimeAccess*>(this),
      GetCApiVtable());
}

absl::StatusOr<StreamingInputId> IconStreamingIoRegistryFake::AddInputParser(
    absl::string_view input_name,
    absl::string_view input_proto_message_type_name,
    IntrinsicIconStreamingInputParserFnInstance raw_parser) {
  // Take ownership of raw_parser regardless of outcome (i.e. properly clean it
  // up if this returns an error).
  InputParser parser(raw_parser);
  auto input_info = absl::c_find_if(
      signature_.streaming_input_infos(),
      [&input_name](
          const ::intrinsic_proto::icon::v1::ActionSignature::ParameterInfo&
              input_info) {
        return input_info.parameter_name() == input_name;
      });
  if (input_info == signature_.streaming_input_infos().end()) {
    return absl::NotFoundError(
        absl::StrCat("No input with name '", input_name, "'."));
  }
  if (input_info->value_message_type() != input_proto_message_type_name) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Streaming input '", input_name, "' is a '",
        input_info->value_message_type(), "', but input parser takes '",
        input_proto_message_type_name, "'."));
  }
  if (HasStreamingInput(input_name)) {
    return absl::AlreadyExistsError(absl::StrCat(
        "Already have a parser for streaming input '", input_name, "'"));
  }
  StreamingInputId id(next_input_id_++);
  input_name_to_id_[input_name] = id;
  streaming_input_parser_map_[id] = std::move(parser);
  return id;
}

absl::Status IconStreamingIoRegistryFake::AddOutputConverter(
    absl::string_view output_proto_message_type_name,
    IntrinsicIconStreamingOutputConverterFnInstance raw_converter) {
  // Take ownership of raw_converter regardless of outcome (i.e. properly clean
  // it up if this returns an error).
  OutputConverter converter(raw_converter);
  if (!signature_.has_streaming_output_info()) {
    return absl::FailedPreconditionError(
        "No streaming output defined in action signature");
  }
  if (signature_.streaming_output_info().value_message_type() !=
      output_proto_message_type_name) {
    return absl::InvalidArgumentError(
        absl::StrCat("Streaming output is a '",
                     signature_.streaming_output_info().value_message_type(),
                     "', but output converter returns '",
                     output_proto_message_type_name, "'."));
  }
  if (HasStreamingOutput()) {
    return absl::AlreadyExistsError(
        "Already have a streaming output converter");
  }
  output_converter_ = std::move(converter);
  return absl::OkStatus();
}

bool IconStreamingIoRegistryFake::HasStreamingOutput() const {
  return output_converter_.has_value();
}

bool IconStreamingIoRegistryFake::HasStreamingInput(
    absl::string_view input_name) const {
  return input_name_to_id_.contains(input_name);
}

bool IconStreamingIoRegistryFake::HasStreamingInput(
    StreamingInputId input_id) const {
  return streaming_input_parser_map_.contains(input_id);
}

absl::StatusOr<std::optional<google::protobuf::Any>>
IconStreamingIoRegistryFake::GetLatestOutput() const {
  if (!output_converter_.has_value()) {
    return absl::NotFoundError("No output converter");
  }
  return output_converter_.GetLatestOutput();
}

IconStreamingIoRegistryFake::InputParser::~InputParser() {
  if (parser_.has_value()) {
    parser_->destroy(parser_->self);
  }
}

bool IconStreamingIoRegistryFake::InputParser::has_value() const {
  return parser_.has_value();
}

IconStreamingIoRegistryFake::OutputConverter::~OutputConverter() {
  if (converter_.has_value()) {
    converter_->destroy(converter_->self);
  }
}

bool IconStreamingIoRegistryFake::OutputConverter::has_value() const {
  return converter_.has_value();
}

IntrinsicIconRealtimeStatus
IconStreamingIoRegistryFake::OutputConverter::Invoke(
    const IntrinsicIconStreamingOutputType* output, size_t size) {
  if (!converter_.has_value()) {
    return FromAbslStatus(absl::FailedPreconditionError("No output converter"));
  }
  IntrinsicIconRealtimeStatus status;
  std::unique_ptr<IntrinsicIconString, void (*)(IntrinsicIconString*)>
      converter_result(
          converter_->invoke(converter_->self, output, size, &status),
          converter_->destroy_string);

  if (!ToAbslStatus(status).ok()) {
    return status;
  }
  google::protobuf::Any result_any;
  if (!result_any.ParseFromString(
          absl::string_view(converter_result->data, converter_result->size))) {
    return FromAbslStatus(absl::InternalError(
        "Failed to parse Any proto from output converter's output."));
  }
  latest_output_ = result_any;
  return status;
}

std::optional<google::protobuf::Any>
IconStreamingIoRegistryFake::OutputConverter::GetLatestOutput() const {
  return latest_output_;
}

}  // namespace intrinsic::icon
