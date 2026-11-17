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

#include "intrinsic/icon/control/parts/realtime_robot_payload.h"

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/fixed_str_cat.h"
#include "intrinsic/icon/utils/fixed_string.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/to_fixed_string.h"
#include "intrinsic/kinematics/validate_link_parameters.h"
#include "intrinsic/math/almost_equals.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/world/robot_payload/robot_payload_base.h"

namespace intrinsic::icon {

RealtimeStatusOr<RealtimeRobotPayload> RealtimeRobotPayload::Create(
    double mass, const Pose3d& tip_t_cog, const eigenmath::Matrix3d& inertia) {
  RealtimeRobotPayload payload;
  if (!AlmostEquals(mass, 0.0)) {
    INTRINSIC_RT_RETURN_IF_ERROR(kinematics::ValidateMass(mass));
  }
  if (!inertia.isZero()) {
    INTRINSIC_RT_RETURN_IF_ERROR(kinematics::ValidateInertia(inertia));
  }
  if (!tip_t_cog.translation().allFinite()) {
    return InvalidArgumentError(RealtimeStatus::StrCat(
        "The center of gravity position should be finite, but got [",
        tip_t_cog.translation().x(), ", ", tip_t_cog.translation().y(), ", ",
        tip_t_cog.translation().z(), "]."));
  }
  payload.mass_kg_ = mass;
  payload.tip_t_cog_ = tip_t_cog;
  payload.inertia_in_cog_ = inertia;

  return payload;
}

FixedString<kRobotPayloadStrSize> ToFixedString(
    const RobotPayloadBase& payload) {
  return FixedStrCat<kRobotPayloadStrSize>(
      "Payload: mass: ", payload.mass(),
      " tip_t_cog: ", eigenmath::ToFixedString(payload.tip_t_cog()),
      " inertia: xx: ", payload.inertia().coeffRef(0, 0),
      " yy: ", payload.inertia().coeffRef(1, 1),
      " zz: ", payload.inertia().coeffRef(2, 2),
      " xy: ", payload.inertia().coeffRef(0, 1),
      " xz: ", payload.inertia().coeffRef(0, 2),
      " yz: ", payload.inertia().coeffRef(1, 2));
}

}  // namespace intrinsic::icon
