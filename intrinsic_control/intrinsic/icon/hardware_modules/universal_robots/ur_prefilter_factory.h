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

#ifndef INTRINSIC_ICON_HARDWARE_MODULES_UNIVERSAL_ROBOTS_UR_PREFILTER_FACTORY_H_
#define INTRINSIC_ICON_HARDWARE_MODULES_UNIVERSAL_ROBOTS_UR_PREFILTER_FACTORY_H_

#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/hardware_modules/universal_robots/config.pb.h"
#include "intrinsic/icon/hardware_modules/universal_robots/prefilter_interface.h"

namespace intrinsic::icon {

// Creates a PrefilterInterface instance for UniversalRobotsModule depending on
// the build target (Enterprise vs Core).
//
// Parameters:
//   robot_model_name: The robot model identifier (e.g., "UR5", "UR10").
//   proto_config: Module configuration protobuf containing advanced control
//                 options.
//
// Returns:
//   A unique_ptr to the constructed PrefilterInterface on success.
//   Returns `absl::UnimplementedError` on Core builds when custom advanced
//   control is requested.
absl::StatusOr<std::unique_ptr<PrefilterInterface>> CreateUrPrefilter(
    absl::string_view robot_model_name,
    const ::intrinsic_proto::icon::UniversalRobotsModuleConfig& proto_config);

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_HARDWARE_MODULES_UNIVERSAL_ROBOTS_UR_PREFILTER_FACTORY_H_
