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

#include "intrinsic/icon/skills/util/adio_util.h"

#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "grpcpp/support/time.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.pb.h"
#include "intrinsic/icon/actions/adio.pb.h"
#include "intrinsic/icon/cc_client/robot_config.h"
#include "intrinsic/icon/equipment/icon_equipment.pb.h"
#include "intrinsic/icon/proto/generic_part_config.pb.h"
#include "intrinsic/icon/proto/io_block.pb.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::skills {
namespace {

template <typename T>
absl::Status AddBlockToMap(
    std::string part_name, T block,
    absl::flat_hash_map<std::string, std::string>& block_to_part_name) {
  if (block_to_part_name.contains(block.first)) {
    return absl::AlreadyExistsError(absl::StrFormat(
        "Block '%s' cannot be mapped to part '%s'. It is "
        "already mapped to part '%s'. Please check the ICON "
        "part configuration.",
        block.first, part_name, block_to_part_name[block.first]));
  }
  block_to_part_name[block.first] = part_name;
  return absl::OkStatus();
};
}  // namespace

absl::StatusOr<absl::flat_hash_map<std::string, std::string>>
GetBlockToPartNameMap(
    const intrinsic_proto::icon::Icon2AdioPart& adio_equipment_config,
    const icon::RobotConfig& robot_config,
    const BlockTypeOptions& block_type_options) {
  absl::flat_hash_map<std::string, std::string> block_to_part_name;

  for (const auto& part_name : adio_equipment_config.icon_parts()) {
    INTR_ASSIGN_OR_RETURN(intrinsic_proto::icon::GenericPartConfig part_config,
                          robot_config.GetGenericPartConfig(part_name));
    if (!part_config.has_adio_config()) {
      return absl::NotFoundError(absl::StrFormat(
          "Part '%s' does not have configured IO signals.", part_name));
    }
    // Add all blocks to the map, checking for duplicates.
    if (block_type_options.digital_in) {
      for (const auto& block :
           part_config.adio_config().digital_input_blocks()) {
        INTR_RETURN_IF_ERROR(
            AddBlockToMap(part_name, block, block_to_part_name));
      }
    }
    if (block_type_options.digital_out) {
      for (const auto& block :
           part_config.adio_config().digital_output_blocks()) {
        INTR_RETURN_IF_ERROR(
            AddBlockToMap(part_name, block, block_to_part_name));
      }
    }
    if (block_type_options.analog_in) {
      for (const auto& block :
           part_config.adio_config().analog_input_blocks()) {
        INTR_RETURN_IF_ERROR(
            AddBlockToMap(part_name, block, block_to_part_name));
      }
    }
    if (block_type_options.analog_out) {
      for (const auto& block :
           part_config.adio_config().analog_output_blocks()) {
        INTR_RETURN_IF_ERROR(
            AddBlockToMap(part_name, block, block_to_part_name));
      }
    }
  }

  return block_to_part_name;
}

}  // namespace intrinsic::skills
