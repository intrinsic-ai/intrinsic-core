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

#ifndef INTRINSIC_ICON_SKILLS_UTIL_ADIO_UTIL_H_
#define INTRINSIC_ICON_SKILLS_UTIL_ADIO_UTIL_H_

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/icon/cc_client/robot_config.h"
#include "intrinsic/icon/equipment/icon_equipment.pb.h"

namespace intrinsic::skills {

struct BlockTypeOptions {
  bool digital_in = false;
  bool digital_out = false;
  bool analog_in = false;
  bool analog_out = false;
};

// GetBlockToPartNameMap returns a map of ADIO block names to ICON part names
// based on the provided ADIO part names and robot config.
//
// The function takes a list of ADIO `part_names`, a `RobotConfig`, and a
// `BlockTypeOptions` struct as input. The `BlockTypeOptions` struct specifies
// which types of blocks to include in the map (digital input, digital output,
// analog input, analog output).
//
// The function returns a map of block names to part names. The block names are
// the names of the ADIO blocks as configured in the ICON part configuration.
// The part names are the names of the ICON parts that contain the ADIO blocks.
//
// If `part_names` contains any ICON parts that are not present in the robot
// config, or if the ICON parts do not have configured IO signals, the function
// will return an error.
//
// If the parts contain any ADIO blocks that have the same name as another ADIO
// block, the function will return an error.
absl::StatusOr<absl::flat_hash_map<std::string, std::string>>
GetBlockToPartNameMap(absl::Span<const std::string> part_names,
                      const icon::RobotConfig& robot_config,
                      const BlockTypeOptions& block_type_options);

// Overload that extracts the ADIO part names from an `Icon2AdioPart` equipment
// config and delegates to `GetBlockToPartNameMap`.
absl::StatusOr<absl::flat_hash_map<std::string, std::string>>
GetBlockToPartNameMap(
    const intrinsic_proto::icon::Icon2AdioPart& adio_equipment_config,
    const icon::RobotConfig& robot_config,
    const BlockTypeOptions& block_type_options);

}  // namespace intrinsic::skills

#endif  // INTRINSIC_ICON_SKILLS_UTIL_ADIO_UTIL_H_
