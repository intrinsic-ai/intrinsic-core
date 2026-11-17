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

#include "intrinsic/world/robot_payload/robot_payload.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/validate_link_parameters.h"
#include "intrinsic/math/almost_equals.h"
#include "intrinsic/math/inertia_utils.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/robot_payload.pb.h"

namespace intrinsic {

absl::Status RobotPayload::SetMass(double mass_kg) {
  // Allow a zero mass for robots without payload.
  if (!AlmostEquals(mass_kg, 0.0)) {
    INTR_RETURN_IF_ERROR(intrinsic::kinematics::ValidateMass(mass_kg));
  }
  mass_kg_ = mass_kg;
  return absl::OkStatus();
}

absl::Status RobotPayload::SetTipTCog(const Pose3d& tip_t_cog) {
  if (!tip_t_cog.translation().allFinite()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The center of gravity position should be finite, but got [",
        tip_t_cog.translation().x(), ", ", tip_t_cog.translation().y(), ", ",
        tip_t_cog.translation().z(), "]."));
  }
  tip_t_cog_ = tip_t_cog;
  return absl::OkStatus();
}

absl::Status RobotPayload::SetInertia(const eigenmath::Matrix3d& inertia) {
  // Allow a zero matrix to handle point masses.
  if (!inertia.isZero()) {
    INTR_RETURN_IF_ERROR(intrinsic::ValidateInertia(inertia));
  }

  inertia_in_cog_ = inertia;
  return absl::OkStatus();
}

absl::StatusOr<RobotPayload> RobotPayload::Create(
    double mass, const Pose3d& tip_t_cog, const eigenmath::Matrix3d& inertia) {
  RobotPayload payload;
  INTR_RETURN_IF_ERROR(payload.SetMass(mass));
  INTR_RETURN_IF_ERROR(payload.SetTipTCog(tip_t_cog));
  INTR_RETURN_IF_ERROR(payload.SetInertia(inertia));

  return payload;
}

absl::StatusOr<RobotPayload> FromProto(
    const intrinsic_proto::world::RobotPayload& proto) {
  Pose3d tip_t_cog;
  if (proto.has_tip_t_cog()) {
    INTR_ASSIGN_OR_RETURN(tip_t_cog,
                          intrinsic_proto::FromProto(proto.tip_t_cog()));
  }

  eigenmath::Matrix3d inertia = eigenmath::Matrix3d::Zero();
  if (proto.has_inertia()) {
    if (proto.inertia().rows() != 3 || proto.inertia().cols() != 3) {
      return absl::InvalidArgumentError("Inertia must be 3x3 matrix.");
    }
    INTR_ASSIGN_OR_RETURN(inertia, intrinsic_proto::FromProto(proto.inertia()));
  }

  return RobotPayload::Create(proto.mass_kg(), tip_t_cog, inertia);
}

intrinsic_proto::world::RobotPayload ToProto(const RobotPayload& payload) {
  intrinsic_proto::world::RobotPayload proto;
  proto.set_mass_kg(payload.mass());
  *proto.mutable_tip_t_cog() = ToProto(payload.tip_t_cog());
  *proto.mutable_inertia() = ToProto(payload.inertia());
  return proto;
}

}  // namespace intrinsic
