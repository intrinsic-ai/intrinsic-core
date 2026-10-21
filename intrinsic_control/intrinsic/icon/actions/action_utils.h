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

#ifndef INTRINSIC_ICON_ACTIONS_ACTION_UTILS_H_
#define INTRINSIC_ICON_ACTIONS_ACTION_UTILS_H_

#include <string>
#include <type_traits>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/message.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/release/source_location.h"
#include "intrinsic/util/proto/descriptors.h"

namespace intrinsic {
namespace icon {

// Build up an ActionSignature proto, ensuring that there are no duplicate names
// for parameters, streaming inputs/outputs, or state variables.
class ActionSignatureBuilder {
 public:
  ActionSignatureBuilder(absl::string_view action_name,
                         absl::string_view action_description)

  {
    signature_.set_action_type_name(action_name);
    signature_.set_text_description(action_description);
  }
  // ActionSignatureBuilder cannot be constructed without action_name and
  // action_description.
  ActionSignatureBuilder() = delete;

  // Sets the action's fixed parameter to be of type ParamType. ParamType must
  // be a proto message.
  //
  // Returns AlreadyExistsError if this has already been called.
  template <class ParamType, typename = std::enable_if_t<std::is_base_of_v<
                                 google::protobuf::Message, ParamType>>>
  absl::Status SetFixedParametersType(
      intrinsic::SourceLocation loc = intrinsic::SourceLocation::current()) {
    return SetFixedParametersTypeImpl(
        /*fixed_parameters_message_type=*/ParamType::GetDescriptor()
            ->full_name(),
        /*fixed_parameters_descriptor_set=*/GenFileDescriptorSet<ParamType>(),
        /*loc=*/loc,
        /*dest_signature=*/signature_);
  }

  // Adds a streaming input of type InputType, with the given name and
  // (optional) description. ParamType must be a proto message.
  //
  // Returns AlreadyExistsError if another streaming input with the same name
  // has already been registered with this builder.
  template <class InputType, typename = std::enable_if_t<std::is_base_of_v<
                                 google::protobuf::Message, InputType>>>
  absl::Status AddStreamingInput(
      absl::string_view input_name, absl::string_view input_description = "",
      intrinsic::SourceLocation loc = intrinsic::SourceLocation::current()) {
    bool inserted = streaming_input_names_.emplace(input_name).second;
    if (!inserted) {
      return absl::AlreadyExistsError(
          absl::StrCat(loc.file_name(), ":", loc.line(),
                       " Duplicate streaming input name \"", input_name, "\""));
    }
    // Actually insert into proto.
    auto* info = signature_.mutable_streaming_input_infos()->Add();
    info->set_parameter_name(input_name);
    info->set_text_description(input_description);
    info->set_value_message_type(InputType::GetDescriptor()->full_name());
    *info->mutable_value_descriptor_set() = GenFileDescriptorSet<InputType>();

    return absl::OkStatus();
  }

  // Adds a streaming output of type OutputType, with the given name
  // and (optional) description. ParamType must be a proto message.
  //
  // Returns AlreadyExistsError if a streaming output has already been
  // registered with this builder.
  template <class OutputType, typename = std::enable_if_t<std::is_base_of_v<
                                  google::protobuf::Message, OutputType>>>
  absl::Status AddStreamingOutput(
      absl::string_view output_name, absl::string_view output_description = "",
      intrinsic::SourceLocation loc = intrinsic::SourceLocation::current()) {
    if (signature_.has_streaming_output_info()) {
      return absl::AlreadyExistsError(absl::StrCat(
          loc.file_name(), ":", loc.line(), " Action type \"",
          signature_.action_type_name(), "\" already has a streaming output: ",
          signature_.streaming_output_info().parameter_name(), " (",
          signature_.streaming_output_info().value_message_type(), ")"));
    }
    auto* output_info = signature_.mutable_streaming_output_info();
    output_info->set_parameter_name(output_name);
    output_info->set_text_description(output_description);
    output_info->set_value_message_type(
        OutputType::GetDescriptor()->full_name());
    *output_info->mutable_value_descriptor_set() =
        GenFileDescriptorSet<OutputType>();
    return absl::OkStatus();
  }

  // Adds a state variable of type StateVariableType, with the given name and
  // (optional) description.
  //
  // Note that unlike the other functions, StateVariableType is not a type, but
  // an enum value. It is supplied as a template parameter for consistency with
  // the other Add*() methods.
  //
  // Returns AlreadyExistsError if another state variable with the same name has
  // already been registered with this builder.
  template <intrinsic_proto::icon::v1::ActionSignature::StateVariableInfo::Type
                StateVariableType>
  absl::Status AddStateVariable(
      absl::string_view state_variable_name,
      absl::string_view state_variable_description = "",
      intrinsic::SourceLocation loc = intrinsic::SourceLocation::current()) {
    bool inserted = state_variable_names_.emplace(state_variable_name).second;
    if (!inserted) {
      return absl::AlreadyExistsError(absl::StrCat(
          loc.file_name(), ":", loc.line(), " Duplicate state variable name \"",
          state_variable_name, "\""));
    }
    // Actually insert into proto.
    auto* info = signature_.add_state_variable_infos();
    info->set_state_variable_name(state_variable_name);
    info->set_text_description(state_variable_description);
    info->set_type(StateVariableType);

    return absl::OkStatus();
  }

  // Adds a Part Slot with the given name, description and Feature Interfaces.
  //
  // This indicates that any Part that a client maps to that slot must support
  // all of the Feature Interfaces in `required_feature_interfaces`, and
  // optionally can support `optional_feature_interfaces`. If
  // `required_feature_interfaces` is empty but `optional_feature_interfaces`
  // is not, the Part must support at least one of
  // `optional_feature_interfaces`.
  //
  // This registers a required slot (`is_optional = false`), even if
  // `required_feature_interfaces` is empty. To register an optional slot, use
  // `AddOptionalPartSlot()`.
  //
  // Returns AlreadyExistsError if `slot_name` is already taken.
  absl::Status AddPartSlot(
      absl::string_view slot_name, absl::string_view slot_description,
      const absl::flat_hash_set<
          intrinsic_proto::icon::v1::FeatureInterfaceTypes>&
          required_feature_interfaces,
      const absl::flat_hash_set<
          intrinsic_proto::icon::v1::FeatureInterfaceTypes>&
          optional_feature_interfaces = {},
      intrinsic::SourceLocation loc = intrinsic::SourceLocation::current());

  // Adds an optional Part Slot with the given name, description and Feature
  // Interfaces.
  //
  // The slot is marked optional (`is_optional = true`) and may be omitted from
  // `SlotPartMap`. Any Part that a client maps to this slot must satisfy the
  // same Feature Interface rules as for `AddPartSlot()`.
  absl::Status AddOptionalPartSlot(
      absl::string_view slot_name, absl::string_view slot_description,
      const absl::flat_hash_set<
          intrinsic_proto::icon::v1::FeatureInterfaceTypes>&
          required_feature_interfaces,
      const absl::flat_hash_set<
          intrinsic_proto::icon::v1::FeatureInterfaceTypes>&
          optional_feature_interfaces = {},
      intrinsic::SourceLocation loc = intrinsic::SourceLocation::current());

  // Adds a Real-time signal with the given `signal_name` and description.
  //
  // This indicates that a Reaction associated to this action may trigger this
  // signal.
  //
  // Returns AlreadyExistsError if `signal_name` is already taken.
  absl::Status AddRealtimeSignal(
      absl::string_view signal_name, absl::string_view signal_description,
      intrinsic::SourceLocation loc = intrinsic::SourceLocation::current());

  // Adds a BehaviorOverrideCapability to the action.
  // Supported Behaviors can be requested by the framework at any time during
  // realtime execution.
  // The action is expected to resume once the request is done.
  //
  // Returns AlreadyExistsError if a supported `behavior` is already registered.
  absl::Status AddSupportedBehaviorOverride(
      intrinsic_proto::icon::v1::BehaviorOverrideRequest behavior_override,
      absl::string_view description,
      intrinsic::SourceLocation loc = intrinsic::SourceLocation::current());

  intrinsic_proto::icon::v1::ActionSignature Finish() const {
    return signature_;
  }

 private:
  static absl::Status SetFixedParametersTypeImpl(
      absl::string_view fixed_parameters_message_type,
      const google::protobuf::FileDescriptorSet&
          fixed_parameters_descriptor_set,
      intrinsic::SourceLocation loc,
      intrinsic_proto::icon::v1::ActionSignature& dest_signature);

  intrinsic_proto::icon::v1::ActionSignature signature_;
  absl::flat_hash_set<std::string> fixed_param_names_;
  absl::flat_hash_set<std::string> streaming_input_names_;
  absl::flat_hash_set<std::string> state_variable_names_;
  absl::flat_hash_set<std::string> part_slot_names_;
  absl::flat_hash_set<std::string> realtime_signal_names_;
  absl::flat_hash_set<intrinsic_proto::icon::v1::BehaviorOverrideRequest>
      supported_behavior_overrides_;
};

}  // namespace icon
}  // namespace intrinsic

#endif  // INTRINSIC_ICON_ACTIONS_ACTION_UTILS_H_
