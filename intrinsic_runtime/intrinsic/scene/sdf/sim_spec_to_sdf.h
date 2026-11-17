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

#ifndef INTRINSIC_SCENE_SDF_SIM_SPEC_TO_SDF_H_
#define INTRINSIC_SCENE_SDF_SIM_SPEC_TO_SDF_H_

#include <string>

#include "absl/status/statusor.h"
#include "intrinsic/scene/proto/v1/simulation_spec.pb.h"

namespace intrinsic {
namespace sdf {

// Converts a SimulationSpec proto to a string containing SDF <plugin> elements.
//
// NOTE: `RobotSimPluginSpec` support in SimulationSpec is deprecated and won't
// be processed even if it is populated.
absl::StatusOr<std::string> SimSpecToSdf(
    const intrinsic_proto::scene_object::v1::SimulationSpec& sim_spec);

}  // namespace sdf
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SDF_SIM_SPEC_TO_SDF_H_
