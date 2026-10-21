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

#include "intrinsic/world/robot_payload/robot_payload_base.h"

#include <ostream>

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/almost_equals.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/world/proto/robot_payload.pb.h"

namespace intrinsic {

RobotPayloadBase::RobotPayloadBase()
    : mass_kg_(0.0),
      tip_t_cog_(Pose3d::Identity()),
      inertia_in_cog_(eigenmath::Matrix3d::Zero()) {}

RobotPayloadBase::RobotPayloadBase(double mass, const Pose3d& tip_t_cog,
                                   const eigenmath::Matrix3d& inertia)
    : mass_kg_(mass), tip_t_cog_(tip_t_cog), inertia_in_cog_(inertia) {}

bool RobotPayloadBase::operator==(const RobotPayloadBase& other) const {
  return IsApprox(other, kStdError);
}

std::ostream& operator<<(std::ostream& os, const RobotPayloadBase& payload) {
  os << "Payload: mass: " << payload.mass()
     << " tip_t_cog: " << payload.tip_t_cog()
     << " inertia: " << payload.inertia();
  return os;
}

bool RobotPayloadBase::IsApprox(const intrinsic::RobotPayloadBase& other,
                                const double precision) const {
  // Eigen's DenseBase::isApprox is purely relative
  // (||A - B||_F <= precision * min(||A||_F, ||B||_F)), which always returns
  // false when comparing a zero inertia (point mass) against a tiny non-zero
  // inertia, and scales `precision` down when inertia values in kg*m^2 are
  // small (< 1). Include an absolute Frobenius-norm check to match the
  // near-zero behavior of AlmostEquals(mass) and Pose3::isApprox(tip_t_cog).
  return intrinsic::AlmostEquals(mass(), other.mass(), precision) &&
         tip_t_cog().isApprox(other.tip_t_cog(), precision) &&
         ((inertia() - other.inertia()).norm() <= precision ||
          inertia().isApprox(other.inertia(), precision));
}
}  // namespace intrinsic
