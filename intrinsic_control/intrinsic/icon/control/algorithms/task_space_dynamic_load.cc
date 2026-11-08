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

#include "intrinsic/icon/control/algorithms/task_space_dynamic_load.h"

#include "intrinsic/eigenmath/skew_symmetric_matrix.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {

Wrench ComputeDynamicLoad(double mass_kg, const eigenmath::Vector3d& fr_p_cog,
                          const Twist& fr_twist_in_fr,
                          const Acceleration& fr_acceleration_in_fr) {
  // [1] "Tool Compensation in Walk-Through Programming for
  // Admittance-Controlled Robots", Chiara Talignani Landi, Federica Ferraguti,
  // Cristian Secchi and Cesare Fantuzzi, 2016.

  // Linear acceleration of frame `fr`.
  const eigenmath::Vector3d& a_fr = fr_acceleration_in_fr.head<3>();
  // Angular velocity of frame `fr`.
  const eigenmath::Vector3d& omega_fr = fr_twist_in_fr.tail<3>();
  // Angular acceleration of frame `fr`.
  const eigenmath::Vector3d& alpha_fr = fr_acceleration_in_fr.tail<3>();

  Wrench result = Wrench::ZERO;

  // Compute forces due to acceleration and rotation of frame `fr`, see Equation
  // (3) in [1].
  result.head<3>() = mass_kg * a_fr /*-mass_kg * g*/ +
                     alpha_fr.cross(mass_kg * fr_p_cog) +
                     omega_fr.cross(omega_fr.cross(mass_kg * fr_p_cog));

  // Compose inertia tensor w.r.t. frame `fr`. The external load is not assumed
  // to have a full inertia tensor, it is only a point mass. The rotational 3d
  // inertia matrix therefore is diagonal and consists of the support_mass
  // multiplied by the inertia tensor of an offset point-mass (see parallel axes
  // theorem).
  eigenmath::Matrix3d inertia_fr =
      mass_kg * eigenmath::SkewSymmetricMatrix(fr_p_cog).transpose() *
      eigenmath::SkewSymmetricMatrix(fr_p_cog);

  // Compute torques due to acceleration and rotation of frame `fr`, see
  // Equation (3) in [1].
  result.tail<3>() =
      inertia_fr * alpha_fr + omega_fr.cross(inertia_fr * omega_fr) +
      mass_kg * fr_p_cog.cross(a_fr) /*-mass_kg * fr_p_cog.cross(g)*/;

  return result;
}

}  // namespace intrinsic::icon
