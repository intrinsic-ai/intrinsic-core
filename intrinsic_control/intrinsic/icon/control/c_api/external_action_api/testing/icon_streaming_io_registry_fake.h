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

#ifndef INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_TESTING_ICON_STREAMING_IO_REGISTRY_FAKE_H_
#define INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_TESTING_ICON_STREAMING_IO_REGISTRY_FAKE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "intrinsic/icon/control/c_api/c_action_factory_context.h"
#include "intrinsic/icon/control/c_api/c_realtime_status.h"
#include "intrinsic/icon/control/c_api/c_streaming_io_realtime_access.h"
#include "intrinsic/icon/control/c_api/c_types.h"
#include "intrinsic/icon/control/c_api/convert_c_realtime_status.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_streaming_io_access.h"
#include "intrinsic/icon/control/c_api/wrappers/streaming_io_wrapper.h"
#include "intrinsic/icon/control/c_api/wrappers/string_wrapper.h"
#include "intrinsic/icon/control/streaming_io_types.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/realtime_status_or.h"

namespace intrinsic::icon {

// Fake implementation of IconStreamingIoRegistry that implements the C API.
//
// Use this to test Realtime Action classes by passing it to their Create()
// methods (via IconActionFactoryContextFake) and Sense() methods (via
// MakeIconStreamingIoAccess()).
class IconStreamingIoRegistryFake {
 public:
  explicit IconStreamingIoRegistryFake(
      const ::intrinsic_proto::icon::v1::ActionSignature& signature)
      : signature_(signature) {}

  IconStreamingIoAccess MakeIconStreamingIoAccess();

  // Adds a C API streaming input parser to the registry. Takes ownership of the
  // pointer contained within.
  //
  // Returns AlreadyExistsError if there's already an input parser for
  // `input_name`.
  // Returns NotFoundError if the ActionSignature used to construct this
  // registry does not have a streaming input `input_name`.
  // Returns InvalidArgumentError if the ActionSignature used to construct this
  // registry has streaming input `input_name`, but its type name does not match
  // `input_proto_message_type_name`.
  absl::StatusOr<StreamingInputId> AddInputParser(
      absl::string_view input_name,
      absl::string_view input_proto_message_type_name,
      IntrinsicIconStreamingInputParserFnInstance raw_parser);

  // Adds a C API streaming output converter to the registry. Takes ownership of
  // the pointer contained within.
  //
  // Returns AlreadyExistsError if there's already an output converter for this
  // registry.
  // Returns FailedPreconditionError if the ActionSignature used to construct
  // this registry does not have a streaming output.
  // Returns InvalidArgumentError if the ActionSignature used to construct this
  // registry has streaming output, but its type name does not match
  // `output_proto_message_type_name`.
  absl::Status AddOutputConverter(
      absl::string_view output_proto_message_type_name,
      IntrinsicIconStreamingOutputConverterFnInstance raw_converter);

  // Returns true if there is a streaming output parser for this registry.
  bool HasStreamingOutput() const;
  // Returns true if there is a streaming input parser for `input_name`.
  bool HasStreamingInput(absl::string_view input_name) const;
  // Returns true if there is a streaming input parser for `input_id`.
  bool HasStreamingInput(StreamingInputId input_id) const;

  // Invokes the streaming input parser for `input_name` with `input_proto`.
  // Use this to test your input parsers *and* to write inputs to an ICON
  // Action that uses this registry.
  //
  // Returns the output of the input parser, *and* saves the result for Actions
  // to use.
  // Returns NotFoundError if there is no streaming input called `input_name`.
  // Returns FailedPreconditionError if there is an input called `input_name`,
  // but its parser returns a value that is not an `InputT`.
  template <typename InputT>
  absl::StatusOr<InputT> InvokeInputParser(
      absl::string_view input_name,
      const google::protobuf::Message& input_proto) {
    auto realtime_id = input_name_to_id_.find(input_name);
    if (realtime_id == input_name_to_id_.end()) {
      return absl::NotFoundError(absl::StrCat(
          "No input parser registered for input '", input_name, "'."));
    }
    return streaming_input_parser_map_[realtime_id->second].Invoke<InputT>(
        input_proto);
  }

  // Invokes the streaming output converter with `output`.
  // Use this to test your output converters.
  //
  // WARNING: There are no checks to ensure that the output converter actually
  // takes OutputT! If you call this with the wrong OutputT, expect memory
  // access violations.
  //
  // Returns the output of the output converter, *and* saves it so you can also
  // access it via GetLatestOutput().
  // Returns NotFoundError if there is no streaming output.
  template <typename OutputT>
  absl::StatusOr<google::protobuf::Any> InvokeOutputConverter(
      const OutputT& output) {
    return output_converter_.Invoke<OutputT>(output);
  }

  // Returns the latest streaming input value for `input_name`, if any.
  //
  // Returns NotFoundError if there's no input called `input_name`.
  // Returns std::nullopt if there *is* an input called `input_name`, but
  // nothing has been written to it yet. Write to the streaming input by calling
  // InvokeInputParser() above.
  // Returns FailedPreconditionError if there is an input called `input_name`,
  // it has a value, but that value is not an `InputT`.
  template <typename InputT>
  absl::StatusOr<std::optional<InputT>> GetLatestInput(
      absl::string_view input_name) const {
    auto realtime_id = input_name_to_id_.find(input_name);
    if (realtime_id == input_name_to_id_.end()) {
      return absl::NotFoundError(absl::StrCat(
          "No input parser registered for input '", input_name, "'."));
    }
    return streaming_input_parser_map_.at(realtime_id->second)
        .GetLatestInput<InputT>();
  }

  // Returns the latest streaming output value, if any.
  //
  // Returns NotFoundError if there's no streaming output called.
  // Returns std::nullopt if there *is* a streaming output, but nothing has been
  // written to it yet. Write to the streaming output by either
  //   * Calling InvokeOutputConverter() above.
  //   * Calling WriteOutput() on the IconActionFactoryContext returned from
  //     MakeIconActionFactoryContext().
  absl::StatusOr<std::optional<google::protobuf::Any>> GetLatestOutput() const;

 private:
  // Holds on to an IntrinsicIconStreamingInputParserFnInstance and manages its
  // lifetime. This means it calls the held instance's destroy() method in the
  // destructor, and also destroys any concrete input values that it is holding.
  class InputParser {
   public:
    InputParser() = default;
    explicit InputParser(IntrinsicIconStreamingInputParserFnInstance parser)
        : parser_(parser), latest_input_(nullptr, parser_->destroy_input) {}

    // Move-only
    InputParser(InputParser&& other) : parser_(std::move(other.parser_)) {
      other.parser_ = std::nullopt;

      absl::MutexLock lock(&input_mutex_);
      absl::MutexLock other_lock(&other.input_mutex_);
      latest_input_mailbox_ = std::move(other.latest_input_mailbox_);
      latest_input_ = std::move(other.latest_input_);
    }

    InputParser& operator=(InputParser&& other) {
      if (&other == this) return *this;
      parser_ = std::move(other.parser_);
      other.parser_ = std::nullopt;
      absl::MutexLock lock(&input_mutex_);
      absl::MutexLock other_lock(&other.input_mutex_);
      latest_input_ = std::move(other.latest_input_);
      latest_input_mailbox_ = std::move(other.latest_input_mailbox_);
      return *this;
    }

    ~InputParser();

    bool has_value() const;

    // Returns the latest streaming input value for this InputParser. The
    // returned pointer remains valid until the next call to GetLatestInput().
    //
    // Returns nullptr if nothing has been written to this streaming input yet.
    IntrinsicIconStreamingInputType* GetLatestInput() const {
      absl::MutexLock lock(&input_mutex_);
      latest_input_ = std::move(latest_input_mailbox_);
      return latest_input_.get();
    }

    // Returns a copy of the latest streaming input value for this InputParser.
    // This does not affect the validity of raw pointers returned by the
    // overload above!
    //
    // Returns nullopt if nothing has been written to this streaming input yet.
    // Returns FailedPreconditionError there is a streaming input value, but
    // that value is not an `InputT`.
    template <typename InputT>
    absl::StatusOr<std::optional<InputT>> GetLatestInput() const {
      absl::MutexLock lock(&input_mutex_);
      if (latest_input_ == nullptr) {
        return std::nullopt;
      }
      RealtimeStatusOr<const InputT*> input =
          UnwrapStreamingInput<InputT>(latest_input_.get());
      if (!input.ok()) {
        return input.status();
      }
      return *input.value();
    }

    // Invokes the held streaming input parser with `input_proto`, and returns a
    // copy of the result cast to `InputT`.
    //
    // Forwards any errors from the streaming input parser.
    // Returns FailedPreconditionError if this InputParser object is not
    // currently holding a streaming input parser.
    // Returns FailedPreconditionError if the parser returns a value that is not
    // an `InputT`.
    template <typename InputT>
    absl::StatusOr<InputT> Invoke(
        const google::protobuf::Message& input_proto) {
      if (!parser_.has_value()) {
        return absl::FailedPreconditionError("No streaming input parser");
      }
      google::protobuf::Any input_any;
      if (!input_any.PackFrom(input_proto)) {
        return absl::InternalError("Failed to pack input proto.");
      }

      IntrinsicIconRealtimeStatus status;
      absl::MutexLock lock(&input_mutex_);
      latest_input_mailbox_ = {
          parser_->invoke(parser_->self,
                          WrapView(input_any.SerializeAsString()), &status),
          parser_->destroy_input};
      if (absl::Status status_out = ToAbslStatus(status); !status_out.ok()) {
        return status_out;
      }
      RealtimeStatusOr<const InputT*> input =
          UnwrapStreamingInput<InputT>(latest_input_mailbox_.get());
      if (!input.ok()) {
        return input.status();
      }
      return *input.value();
    }

   private:
    using StreamingInputPtr =
        std::unique_ptr<IntrinsicIconStreamingInputType,
                        void (*)(IntrinsicIconStreamingInputType*)>;

    std::optional<IntrinsicIconStreamingInputParserFnInstance> parser_ =
        std::nullopt;

    // Using two mutable StreamingInputPtr objects to implement the API contract
    // of GetLatestInput:
    // The pointer it returns is guaranteed to stay valid until the next call to
    // GetLatestInput, so we must "cache" any subsequent writes to the streaming
    // input  in latest_input_mailbox_.
    mutable absl::Mutex input_mutex_;
    mutable StreamingInputPtr latest_input_mailbox_
        ABSL_GUARDED_BY(input_mutex_){nullptr, nullptr};
    mutable StreamingInputPtr latest_input_ ABSL_GUARDED_BY(input_mutex_){
        nullptr, nullptr};
  };

  // Holds on to an IntrinsicIconStreamingOutputConverterFnInstance and manages
  // its lifetime. This means it calls the held instance's destroy() method in
  // the destructor.
  class OutputConverter {
   public:
    OutputConverter() = default;
    explicit OutputConverter(
        IntrinsicIconStreamingOutputConverterFnInstance converter)
        : converter_(converter) {}

    // Move-only
    OutputConverter(OutputConverter&& other) {
      converter_ = std::move(other.converter_);
      other.converter_ = std::nullopt;
    }
    OutputConverter& operator=(OutputConverter&& other) {
      if (&other == this) return *this;
      converter_ = std::move(other.converter_);
      other.converter_ = std::nullopt;
      return *this;
    }

    ~OutputConverter();

    bool has_value() const;

    // Invokes the held output converter (if any) with `output` and `size`.
    // Returns FailedPrecondition if not holding an output converter.
    // Forwards any errors from the converter.
    IntrinsicIconRealtimeStatus Invoke(
        const IntrinsicIconStreamingOutputType* output, size_t size);

    // Invokes the held output converter (if any) with `output`. This is a
    // convenience wrapper around the overload above, for use in unit tests.
    template <typename OutputT>
    absl::StatusOr<google::protobuf::Any> Invoke(const OutputT& output) {
      if (absl::Status invoke_result = ToAbslStatus(
              Invoke(reinterpret_cast<const IntrinsicIconStreamingOutputType*>(
                         &output),
                     sizeof(output)));
          !invoke_result.ok()) {
        return invoke_result;
      }
      if (!latest_output_.has_value()) {
        return absl::InternalError(
            "Output Converter succeeded, but did not write output.");
      }
      return latest_output_.value();
    }

    // Returns the latest output of the held output converter, if any. Use this
    // in a unit test to verify that an Action has written an expected streaming
    // output.
    //
    // Returns nullopt if the output converter has not been invoked yet.
    std::optional<google::protobuf::Any> GetLatestOutput() const;

   private:
    std::optional<IntrinsicIconStreamingOutputConverterFnInstance> converter_ =
        std::nullopt;
    std::optional<google::protobuf::Any> latest_output_;
  };

  // Returns a C API vtable struct for use with IconStreamingIoAccess – this is
  // a class member rather than a free function so that it can access private
  // member variables.
  static IntrinsicIconStreamingIoRealtimeAccessVtable GetCApiVtable();

  const ::intrinsic_proto::icon::v1::ActionSignature signature_;
  absl::flat_hash_map<StreamingInputId, InputParser>
      streaming_input_parser_map_;
  absl::flat_hash_map<std::string, StreamingInputId> input_name_to_id_;
  OutputConverter output_converter_;
  uint64_t next_input_id_ = 0;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_TESTING_ICON_STREAMING_IO_REGISTRY_FAKE_H_
