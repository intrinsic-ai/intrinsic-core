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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_TASK_SPACE_DYNAMIC_LOAD_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_TASK_SPACE_DYNAMIC_LOAD_H_

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {

// Computes the dynamic load of single rigid body attached to frame `fr`,
// subject to spatial acceleration `fr_acceleration_in_fr` and velocity
// `fr_twist_in_fr` expressed in `fr`. Implementation follows reference [1].
// Main intended use is post-sensor inertia compensation in compliance control.
// Resulting wrench can be interpreted as "the wrench one needs to apply to the
// rigid body in `fr` to achieve the desired motion". Example: accelerating in
// positive x-direction will result in a positive force in x-direction.
//
// Implementation notes:
// * omits gravity effects, since this is currently already compensated for in
// the force_sensor_controller.cc. Missing terms due to gravity are inlined, but
// commented.
// * assumes a point `mass_kg` located at the center of gravity `fr_p_cog`
// w.r.t. frame `fr`. The implementation can be extended to cover a full inertia
// tensor, please consult [1].
//
// [1] "Tool Compensation in Walk-Through Programming for Admittance-Controlled
// Robots", Chiara Talignani Landi, Federica Ferraguti, Cristian Secchi and
// Cesare Fantuzzi, 2016.
Wrench ComputeDynamicLoad(double mass_kg, const eigenmath::Vector3d& fr_p_cog,
                          const Twist& fr_twist_in_fr,
                          const Acceleration& fr_acceleration_in_fr);

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_TASK_SPACE_DYNAMIC_LOAD_H_
