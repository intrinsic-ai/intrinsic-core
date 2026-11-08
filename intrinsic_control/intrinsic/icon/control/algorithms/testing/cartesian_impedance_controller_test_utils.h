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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_TESTING_CARTESIAN_IMPEDANCE_CONTROLLER_TEST_UTILS_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_TESTING_CARTESIAN_IMPEDANCE_CONTROLLER_TEST_UTILS_H_

#include <gmock/gmock.h>

#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/kinematics/types/joint_state.h"

namespace intrinsic::icon {

CartesianImpedanceParameters CartesianImpedanceMinimalTestParams();

CartesianImpedanceParameters CartesianImpedanceCompleteTestParams(int ndof);

inline auto StatesAreEqual(
    const cartesian_impedance::RealTimeCartesianTarget& other) {
  using eigenmath::testing::IsApprox;
  using ::testing::Field;

  return AllOf(
      Field(&cartesian_impedance::RealTimeCartesianTarget::
                task_t_tool_reference_position,
            IsApprox(other.task_t_tool_reference_position)),
      Field(&cartesian_impedance::RealTimeCartesianTarget::
                task_t_tool_reference_orientation,
            IsApprox(other.task_t_tool_reference_orientation)),
      Field(&cartesian_impedance::RealTimeCartesianTarget::tool_reference_twist,
            IsApprox(other.tool_reference_twist)),
      Field(
          &cartesian_impedance::RealTimeCartesianTarget::tool_reference_wrench,
          IsApprox(other.tool_reference_wrench)),
      Field(&cartesian_impedance::RealTimeCartesianTarget::cartesian_stiffness,
            IsApprox(other.cartesian_stiffness)),
      Field(&cartesian_impedance::RealTimeCartesianTarget::cartesian_damping,
            IsApprox(other.cartesian_damping)),
      Field(&cartesian_impedance::RealTimeCartesianTarget::
                wrench_selection_matrix,
            IsApprox(other.wrench_selection_matrix)),
      Field(&cartesian_impedance::RealTimeCartesianTarget::
                virtual_cartesian_inertia_inverse,
            IsApprox(other.virtual_cartesian_inertia_inverse)));
}

inline auto StatesAreEqual(
    const cartesian_impedance::RealTimeNullspaceTarget& other) {
  using eigenmath::testing::IsApprox;
  using ::testing::Eq;
  using ::testing::Field;

  return AllOf(
      Field(&cartesian_impedance::RealTimeNullspaceTarget::joint_state,
            Field(&JointStatePVA::position,
                  IsApprox(other.joint_state.position))),
      Field(&cartesian_impedance::RealTimeNullspaceTarget::joint_state,
            Field(&JointStatePVA::velocity,
                  IsApprox(other.joint_state.velocity))),
      Field(&cartesian_impedance::RealTimeNullspaceTarget::joint_state,
            Field(&JointStatePVA::acceleration,
                  IsApprox(other.joint_state.acceleration))),
      Field(&cartesian_impedance::RealTimeNullspaceTarget::nullspace_stiffness,
            Eq(other.nullspace_stiffness)),
      Field(&cartesian_impedance::RealTimeNullspaceTarget::nullspace_damping,
            Eq(other.nullspace_damping)));
}

// Returns a random symmetric positive definite dim x dim matrix.
eigenmath::MatrixNd GenerateSPDMatrix(int dim);

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_TESTING_CARTESIAN_IMPEDANCE_CONTROLLER_TEST_UTILS_H_
