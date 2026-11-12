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

#ifndef INTRINSIC_KINEMATICS_IK_IK_HELPER_H_
#define INTRINSIC_KINEMATICS_IK_IK_HELPER_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/ik/inverse_kinematics_interface.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic {
namespace kinematics {

absl::StatusOr<std::vector<JointStateP>> GetIKSolutions(
    const std::string& solver_name, const Skeleton& skeleton,
    const Pose3d& base_t_tip, size_t max_ik_solutions,
    const eigenmath::VectorXd* q_init = nullptr);

absl::StatusOr<std::vector<JointStateP>> GetIKSolutions(
    const InverseKinematicsInterface* ik_solver,
    const JointLimits& joint_limits, const Pose3d& base_t_tip,
    size_t max_ik_solutions, const eigenmath::VectorXd* q_init = nullptr);

absl::StatusOr<std::optional<JointStateP>> GetSameBranchIKSolution(
    const std::string& solver_name, const Skeleton& skeleton,
    const Pose3d& base_t_tip, const eigenmath::VectorXd& q_init);

}  // namespace kinematics
}  // namespace intrinsic

#endif  // INTRINSIC_KINEMATICS_IK_IK_HELPER_H_
