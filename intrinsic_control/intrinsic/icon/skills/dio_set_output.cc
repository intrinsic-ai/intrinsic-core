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

#include "intrinsic/icon/skills/dio_set_output.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/message.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/icon/skills/dio_set_output.pb.h"
#include "intrinsic/icon/skills/util/analog_digital_io.h"
#include "intrinsic/icon/skills/util/analog_digital_io_factory.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::skills {

std::unique_ptr<SkillInterface> DioSetOutput::CreateSkill() {
  return std::make_unique<DioSetOutput>(
      std::make_unique<icon::DefaultChannelFactory>());
}

DioSetOutput::DioSetOutput(
    std::unique_ptr<icon::ChannelFactory> icon_channel_factory)
    : icon_channel_factory_(std::move(icon_channel_factory)) {}

absl::StatusOr<intrinsic_proto::skills::Footprint> DioSetOutput::GetFootprint(
    const GetFootprintRequest& request, GetFootprintContext& context) const {
  intrinsic_proto::skills::Footprint result;
  // Do not lock the universe to allow parallel skill execution.
  result.set_lock_the_universe(false);
  return std::move(result);
}

absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
DioSetOutput::Execute(const ExecuteRequest& request, ExecuteContext& context) {
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<AnalogDigitalIOInterface> digital_io,
      CreateAnalogDigitalIO(kEquipmentSlot, context.equipment(),
                            icon_channel_factory_.get()),
      _.LogError());

  INTR_ASSIGN_OR_RETURN(
      auto params,
      request.params<intrinsic_proto::skills::DioSetOutputParams>(),
      _.LogError());

  if (params.dio_output_blocks_size() == 0) {
    return absl::InvalidArgumentError("No DIO output block set.");
  }

  std::vector<AnalogDigitalIOInterface::DigitalOutputBlock> output_blocks;
  output_blocks.reserve(params.dio_output_blocks_size());

  for (const auto& p : params.dio_output_blocks()) {
    if (p.indices_size() != p.values_size()) {
      return absl::InvalidArgumentError(
          "The skill requires the same number of DIO indices and values.");
    }

    AnalogDigitalIOInterface::DigitalOutputBlock block;
    block.name = p.block_name();
    // Error checking (including duplicate block names) is done in the helper
    // class.
    for (uint32_t i = 0; i < p.indices_size(); ++i) {
      const uint32_t dio_index = p.indices(i);
      const bool dio_value = p.values(i);
      block.mask.set(dio_index);
      block.values.set(dio_index, dio_value);
    }

    output_blocks.emplace_back(std::move(block));
  }

  INTR_RETURN_IF_ERROR(digital_io->SetDigitalOutputs(output_blocks)).LogError();

  return nullptr;
}

absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
DioSetOutput::Preview(const PreviewRequest& request, PreviewContext& context) {
  // Preview does nothing. If you wish to represent some world changes with this
  // skill in preview, then you should model the behavior in the process, or
  // with a more specific skill capable of representing the behavior.
  return nullptr;
}

}  // namespace intrinsic::skills
