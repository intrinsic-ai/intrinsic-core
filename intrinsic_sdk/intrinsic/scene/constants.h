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

#ifndef INTRINSIC_SCENE_CONSTANTS_H_
#define INTRINSIC_SCENE_CONSTANTS_H_

#include <array>

#include "absl/strings/string_view.h"
#include "intrinsic/scene/user_data_keys.h"

namespace intrinsic {
namespace scene_object {

// LINT.IfChange
constexpr absl::string_view kSceneObjectAssetUserDataKey =
    "FLOWSTATE_ASSET_USER_DATA";
// LINT.ThenChange(
// //intrinsic/frontend/world_viewer/core/model/world_ids.ts
// )
constexpr int kSceneObjectNumReservedUserDataKeys = 4;
constexpr std::array<absl::string_view, kSceneObjectNumReservedUserDataKeys>
    kSceneObjectReservedUserDataKeys = {
        sdf::kGazeboPlugins,
        sdf::kGazeboCollisionSurface,
        sdf::kGazeboJointPhysics,
        sdf::kSdfLights,
};

// LINT.IfChange
constexpr double kMaxApplicationLimitsMultiplier = 0.95;
// LINT.ThenChange(//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces/joint_limits_constants.h)

}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_CONSTANTS_H_
