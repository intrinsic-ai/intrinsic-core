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

#include "intrinsic/icon/skills/util/analog_digital_io.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "grpcpp/client_context.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.grpc.pb.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.pb.h"
#include "intrinsic/hardware/gpio/v1/signal.pb.h"
#include "intrinsic/icon/actions/adio.pb.h"
#include "intrinsic/icon/actions/adio_info.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/cc_client/session.h"
#include "intrinsic/icon/cc_client/state_variable_path.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/icon/equipment/icon_equipment.pb.h"
#include "intrinsic/icon/proto/generic_part_config.pb.h"
#include "intrinsic/icon/proto/io_block.pb.h"
#include "intrinsic/icon/release/grpc_time_support.h"
#include "intrinsic/icon/skills/util/adio_util.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/util/grpc/connection_params.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::skills {
namespace {

constexpr const absl::Duration kIconClientTimeout = absl::Seconds(5);
constexpr const absl::Duration kGpioClientTimeout = absl::Seconds(5);
constexpr const absl::Duration kIconSetOutputTimeout = absl::Seconds(5);

GpioAnalogDigitalIO::GpioRequests CreateGpioRequests(
    absl::Span<const AnalogDigitalIOInterface::DigitalOutputBlock>
        digital_output_blocks,
    absl::Span<const AnalogDigitalIOInterface::AnalogOutputBlock>
        analog_output_blocks) {
  GpioAnalogDigitalIO::GpioRequests requests;

  for (const auto& output_block : digital_output_blocks) {
    for (size_t i = 0; i < output_block.mask.size(); ++i) {
      if (!output_block.mask.test(i)) {
        continue;
      }
      const std::string signal_name = absl::StrCat(output_block.name, ".", i);

      requests.initial_request.mutable_initial_session_data()->add_signal_names(
          signal_name);

      intrinsic_proto::gpio::v1::SignalValue signal_value;
      signal_value.set_bool_value(output_block.values.test(i));
      requests.set_value_request.mutable_write_signals()
          ->mutable_signal_values()
          ->mutable_values()
          ->insert({signal_name, signal_value});
    }
  }

  for (const auto& output_block : analog_output_blocks) {
    for (size_t i = 0; i < output_block.mask.size(); ++i) {
      if (!output_block.mask.test(i)) {
        continue;
      }
      const std::string signal_name = absl::StrCat(output_block.name, ".", i);

      requests.initial_request.mutable_initial_session_data()->add_signal_names(
          signal_name);

      intrinsic_proto::gpio::v1::SignalValue signal_value;
      signal_value.set_double_value(output_block.values[i]);
      requests.set_value_request.mutable_write_signals()
          ->mutable_signal_values()
          ->mutable_values()
          ->insert({signal_name, signal_value});
    }
  }

  return requests;
}

// Creates an ICON condition as a conjunction of all given inputs and their
// desires values.
icon::Condition CreateWaitForInputValueCondition(
    absl::string_view adio_part_name, absl::string_view input_block_name,
    const IconAnalogDigitalIO::DigitalBlockMask& input_mask,
    const IconAnalogDigitalIO::DigitalBlockValues& values) {
  std::vector<icon::Condition> literals;
  for (uint32_t input_index = 0; input_index < input_mask.size();
       ++input_index) {
    if (input_mask.test(input_index)) {
      const std::string path = icon::ADIODigitalInputStateVariablePath(
          adio_part_name, input_block_name, input_index);
      literals.push_back(icon::Condition(
          values.test(input_index) ? icon::IsTrue(path) : icon::IsFalse(path)));
    }
  }
  return icon::AllOf(literals);
}

}  // namespace

absl::StatusOr<std::unique_ptr<AnalogDigitalIOInterface>>
IconAnalogDigitalIO::Create(
    const intrinsic_proto::icon::Icon2AdioPart& adio_equipment_config,
    const icon::ChannelFactory& channel_factory,
    const intrinsic_proto::resources::ResourceConnectionInfo* connection_info) {
  if (!adio_equipment_config.has_icon_target()) {
    return absl::InvalidArgumentError(
        "Attempted to create IconDigitalIO without ICON target.");
  }

  const ConnectionParams connection = ConnectionParams{
      .address = connection_info->grpc().address(),
      .instance_name = connection_info->grpc().server_instance(),
      .header = connection_info->grpc().header(),
  };

  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<ChannelInterface> channel,
      channel_factory.MakeChannel(connection, kIconClientTimeout));
  icon::Client client(channel);
  INTR_ASSIGN_OR_RETURN(auto robot_config, client.GetConfig());

  INTR_ASSIGN_OR_RETURN(
      auto block_to_part_name,
      GetBlockToPartNameMap(adio_equipment_config, robot_config,
                            {.digital_in = true,
                             .digital_out = true,
                             .analog_in = true,
                             .analog_out = true}));

  return std::make_unique<IconAnalogDigitalIO>(std::move(channel),
                                               std::move(block_to_part_name));
}

IconAnalogDigitalIO::IconAnalogDigitalIO(
    std::shared_ptr<ChannelInterface> icon_channel,
    absl::flat_hash_map<std::string, std::string> block_to_part_name)
    : icon_channel_{std::move(icon_channel)},
      block_to_part_name_{std::move(block_to_part_name)} {}

absl::Status IconAnalogDigitalIO::SetDigitalOutputs(
    absl::Span<const AnalogDigitalIOInterface::DigitalOutputBlock>
        output_blocks) {
  INTR_ASSIGN_OR_RETURN(auto parts_and_action_parameters,
                        CreateSetValueParams(output_blocks, {}));
  return SetOutputs(parts_and_action_parameters);
}

absl::Status IconAnalogDigitalIO::SetAnalogOutputs(
    absl::Span<const AnalogDigitalIOInterface::AnalogOutputBlock>
        output_blocks) {
  INTR_ASSIGN_OR_RETURN(auto parts_and_action_parameters,
                        CreateSetValueParams({}, output_blocks));
  return SetOutputs(parts_and_action_parameters);
}

absl::Status IconAnalogDigitalIO::SetOutputs(
    const absl::btree_map<std::string, icon::ADIOActionInfo::FixedParams>&
        part_name_and_action_parameters) {
  std::vector<std::string> parts;
  parts.reserve(part_name_and_action_parameters.size());
  for (const auto& [key, _] : part_name_and_action_parameters) {
    parts.push_back(key);
  }
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<icon::Session> session,
                        icon::Session::Start(icon_channel_, parts));

  icon::ReactionHandle outputs_set_handle(0);

  std::vector<icon::ActionDescriptor> adio_action_descriptors;
  std::vector<icon::ActionInstanceId> action_instance_ids;
  int i = 0;
  for (const auto& [part_name, action_params] :
       part_name_and_action_parameters) {
    auto action_instance_id = icon::ActionInstanceId(++action_instance_id_);
    icon::ActionDescriptor adio_action =
        icon::ActionDescriptor(icon::ADIOActionInfo::kActionTypeName,
                               action_instance_id, part_name)
            .WithFixedParams(action_params);
    if (i != part_name_and_action_parameters.size() - 1) {
      // This action will be followed by another action.
      adio_action.WithReaction(
          icon::ReactionDescriptor(
              icon::IsTrue(icon::ADIOActionInfo::kOutputsSet))
              .WithRealtimeActionOnCondition(
                  icon::ActionInstanceId(action_instance_id.value() + 1)));
    } else {
      // This is the terminal action.
      adio_action.WithReaction(
          icon::ReactionDescriptor(
              icon::IsTrue(icon::ADIOActionInfo::kOutputsSet))
              .WithHandle(outputs_set_handle));
    }
    adio_action_descriptors.push_back(adio_action);
    action_instance_ids.push_back(action_instance_id);
    LOG(INFO) << "Added Action: " << action_instance_id;
    i++;
  }

  INTR_ASSIGN_OR_RETURN(std::vector<icon::Action> actions,
                        session->AddActions(adio_action_descriptors));
  INTR_RETURN_IF_ERROR(session->StartActions({*actions.begin()}));
  INTR_RETURN_IF_ERROR(session->RunWatcherLoopUntilReaction(
      outputs_set_handle, absl::Now() + kIconSetOutputTimeout));

  return absl::OkStatus();
}

absl::StatusOr<absl::btree_map<std::string, icon::ADIOActionInfo::FixedParams>>
IconAnalogDigitalIO::CreateSetValueParams(
    absl::Span<const AnalogDigitalIOInterface::DigitalOutputBlock>
        digital_output_blocks,
    absl::Span<const AnalogDigitalIOInterface::AnalogOutputBlock>
        analog_output_blocks) {
  absl::btree_map<std::string, icon::ADIOActionInfo::FixedParams>
      parts_and_params;
  for (const auto& output_block : digital_output_blocks) {
    intrinsic_proto::icon::actions::proto::DigitalBlock block;
    for (uint32_t output_index = 0; output_index < output_block.mask.size();
         ++output_index) {
      if (output_block.mask.test(output_index)) {
        (*block.mutable_values_by_index())[output_index] =
            output_block.values.test(output_index);
      }
    }
    if (!block_to_part_name_.contains(output_block.name)) {
      return absl::NotFoundError(absl::StrFormat(
          "Block '%s' is not mapped to a part. Please check the ICON part "
          "configuration.",
          output_block.name));
    }
    auto* digital_outputs =
        parts_and_params[block_to_part_name_[output_block.name]]
            .mutable_outputs()
            ->mutable_digital_outputs();
    if (digital_outputs->contains(output_block.name)) {
      return absl::AlreadyExistsError(absl::StrFormat(
          "Multiple digital output blocks with the same name '%s' are not "
          "allowed.",
          output_block.name));
    }
    (*digital_outputs)[output_block.name] = std::move(block);
  }
  for (const auto& output_block : analog_output_blocks) {
    intrinsic_proto::icon::actions::proto::AnalogOutputBlock block;
    for (uint32_t output_index = 0; output_index < output_block.mask.size();
         ++output_index) {
      if (output_block.mask.test(output_index)) {
        (*block.mutable_values_by_index())[output_index] =
            output_block.values[output_index];
      }
    }
    if (!block_to_part_name_.contains(output_block.name)) {
      return absl::NotFoundError(absl::StrFormat(
          "Block '%s' is not mapped to a part. Please check the ICON part "
          "configuration.",
          output_block.name));
    }
    auto* analog_outputs =
        parts_and_params[block_to_part_name_[output_block.name]]
            .mutable_outputs()
            ->mutable_analog_outputs();
    if (analog_outputs->contains(output_block.name)) {
      return absl::AlreadyExistsError(absl::StrFormat(
          "Multiple analog output blocks with the same name '%s' are not "
          "allowed.",
          output_block.name));
    }
    (*analog_outputs)[output_block.name] = std::move(block);
  }
  return parts_and_params;
}

absl::Status IconAnalogDigitalIO::WaitForInput(
    absl::string_view input_block_name,
    const IconAnalogDigitalIO::DigitalBlockMask& input_mask,
    const IconAnalogDigitalIO::DigitalBlockValues& values,
    absl::Duration timeout) {
  const absl::Time deadline = absl::Now() + timeout;
  if (!block_to_part_name_.contains(input_block_name)) {
    return absl::NotFoundError(absl::StrFormat(
        "Block '%s' is not mapped to a part. Please check the ICON part "
        "configuration.",
        input_block_name));
  }
  // Use part status conditions and an ICON monitoring session to observe the
  // ADIOs.
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<icon::Session> session,
                        icon::Session::Start(icon_channel_, {}));
  const icon::Condition all_of_inputs_condition =
      CreateWaitForInputValueCondition(block_to_part_name_[input_block_name],
                                       input_block_name, input_mask, values);

  constexpr icon::ReactionHandle kInputObservedSetHandle(0);

  INTR_RETURN_IF_ERROR(session->AddFreestandingReactions(
      {icon::ReactionDescriptor(all_of_inputs_condition)
           .WithHandle(kInputObservedSetHandle)}));
  return session->RunWatcherLoopUntilReaction(kInputObservedSetHandle,
                                              deadline);
}

absl::Status GpioAnalogDigitalIO::SetDigitalOutputs(
    absl::Span<const AnalogDigitalIOInterface::DigitalOutputBlock>
        output_blocks) {
  return SetOutputs(CreateGpioRequests(output_blocks, {}));
}

absl::Status GpioAnalogDigitalIO::SetAnalogOutputs(
    absl::Span<const AnalogDigitalIOInterface::AnalogOutputBlock>
        output_blocks) {
  return SetOutputs(CreateGpioRequests({}, output_blocks));
}

absl::Status GpioAnalogDigitalIO::SetOutputs(const GpioRequests& requests) {
  ::grpc::ClientContext ctx;
  auto session_stream = stub_->OpenWriteSession(&ctx);

  if (!session_stream->Write(requests.initial_request)) {
    auto error =
        absl::InternalError("Failed to write initial request to GPIO session.");
    LOG(ERROR) << error;
    return error;
  }

  intrinsic_proto::gpio::v1::OpenWriteSessionResponse initial_resp;
  if (!session_stream->Read(&initial_resp)) {
    auto error = absl::InternalError(
        "Failed to read initial response from GPIO session.");
    LOG(ERROR) << error;
    return error;
  }
  INTR_RETURN_IF_ERROR(
      intrinsic::MakeStatusFromRpcStatus(initial_resp.status()))
      .LogError();

  if (!session_stream->Write(requests.set_value_request)) {
    auto error = absl::InternalError(
        "Failed to write request to set value to GPIO session.");
    LOG(ERROR) << error;
    return error;
  }
  if (!session_stream->Read(&initial_resp)) {
    auto error = absl::InternalError(
        "Failed to read response from setting value from GPIO session.");
    LOG(ERROR) << error;
    return error;
  }
  INTR_RETURN_IF_ERROR(
      intrinsic::MakeStatusFromRpcStatus(initial_resp.status()))
      .LogError();

  if (!session_stream->WritesDone()) {
    auto error =
        absl::InternalError("Failed to close signal close of GPIO session.");
    LOG(ERROR) << error;
    return error;
  }

  auto session_result = ToAbslStatus(session_stream->Finish());
  if (!session_result.ok()) {
    LOG(ERROR) << session_result;
  }
  return session_result;
}

absl::Status GpioAnalogDigitalIO::WaitForInput(
    absl::string_view input_block_name, const DigitalBlockMask& input_mask,
    const DigitalBlockValues& values, absl::Duration timeout) {
  ::grpc::ClientContext ctx;
  // WaitForValue uses the gRPC deadline to handle timeout.
  ctx.set_deadline(absl::Now() + timeout);

  intrinsic_proto::gpio::v1::WaitForValueRequest req;

  for (size_t i = 0; i < input_mask.size(); ++i) {
    if (!input_mask.test(i)) {
      continue;
    }
    const std::string signal_name = absl::StrCat(input_block_name, ".", i);

    intrinsic_proto::gpio::v1::SignalValue signal_value;
    signal_value.set_bool_value(values.test(i));
    req.mutable_all_of()->mutable_values()->insert({signal_name, signal_value});
  }

  intrinsic_proto::gpio::v1::WaitForValueResponse resp;
  INTR_RETURN_IF_ERROR(ToAbslStatus(stub_->WaitForValue(&ctx, req, &resp)))
      .LogError();

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<AnalogDigitalIOInterface>>
GpioAnalogDigitalIO::Create(
    const intrinsic_proto::gpio::GPIOServiceTarget& gpio_config,
    absl::string_view gpio_instance_name) {
  INTR_ASSIGN_OR_RETURN(auto channel,
                        intrinsic::connect::CreateClientChannel(
                            gpio_config.gpio_service_grpc_target(),
                            absl::Now() + kGpioClientTimeout));
  auto stub =
      std::make_unique<intrinsic_proto::gpio::v1::GPIOService::Stub>(channel);
  auto gpio_digital_io = std::make_unique<GpioAnalogDigitalIO>();
  gpio_digital_io->stub_ = std::move(stub);
  gpio_digital_io->gpio_instance_name_ = gpio_instance_name;
  return gpio_digital_io;
}

}  // namespace intrinsic::skills
