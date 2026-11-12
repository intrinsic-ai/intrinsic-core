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

#include "intrinsic/kinematics/ik/ik_helper.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/kinematics/ik/inverse_kinematics_factory.h"
#include "intrinsic/kinematics/ik/inverse_kinematics_interface.h"
#include "intrinsic/kinematics/ik/sanitize_ik.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/util/status/status_macros.h"

ABSL_FLAG(bool, fan_out_joint_winding, false,
          "Return joint winding options as alternative ik solutions.");

namespace intrinsic {
namespace kinematics {

absl::StatusOr<std::vector<JointStateP>> GetIKSolutions(
    const std::string& solver_name, const Skeleton& skeleton,
    const Pose3d& base_t_tip, size_t max_ik_solutions,
    const eigenmath::VectorXd* q_init) {
  INTR_ASSIGN_OR_RETURN(Chain chain, CreateChainFromModel(skeleton));

  INTR_ASSIGN_OR_RETURN(
      const std::unique_ptr<InverseKinematicsInterface> ik_solver,
      GetGlobalInverseKinematicsFactory().CreateInverseKinematicsSolver(
          solver_name, std::move(chain)));

  const JointLimits joint_limits = skeleton.GetDofSystemLimits();

  return GetIKSolutions(ik_solver.get(), joint_limits, base_t_tip,
                        max_ik_solutions, q_init);
}

absl::StatusOr<std::vector<JointStateP>> GetIKSolutions(
    const InverseKinematicsInterface* ik_solver,
    const JointLimits& joint_limits, const Pose3d& base_t_tip,
    size_t max_ik_solutions, const eigenmath::VectorXd* q_init) {
  std::vector<JointStateP> solutions(max_ik_solutions);

  JointStateP hint_joint_state;
  INTR_RETURN_IF_ERROR(hint_joint_state.SetSize(joint_limits.size()));
  hint_joint_state.position =
      q_init != nullptr ? *q_init
                        : eigenmath::VectorXd::Zero(joint_limits.size());

  INTR_ASSIGN_OR_RETURN(
      const auto ik_result,
      ik_solver->ComputeIK(hint_joint_state, base_t_tip, joint_limits,
                           absl::MakeSpan(solutions)));

  if (ik_result.status == InverseKinematicsInterface::IKResult::NO_SOLUTION) {
    return std::vector<JointStateP>();
  }

  int num_solutions = ik_result.number_of_solutions;
  CHECK_GE(num_solutions, 1);

  // Expending the joint solution based on winding should be done outside of
  // this scope. This is kept temporary for getting Intrinsic milestone through.
  if (absl::GetFlag(FLAGS_fan_out_joint_winding)) {
    const int num_wrapped_solutions = sanitize_ik::WrapToLimits(
        absl::MakeSpan(solutions).subspan(0, num_solutions), joint_limits);
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        num_solutions,
        sanitize_ik::ExpandWindings(absl::MakeSpan(solutions), joint_limits,
                                    num_wrapped_solutions));
  }
  // Only return the actual solutions, since the other values are not
  // meaningful.
  CHECK_GE(num_solutions, 0);
  CHECK_LE(num_solutions, max_ik_solutions);
  solutions.erase(solutions.begin() + num_solutions, solutions.end());
  return solutions;
}

absl::StatusOr<std::optional<JointStateP>> GetSameBranchIKSolution(
    const std::string& solver_name, const Skeleton& skeleton,
    const Pose3d& base_t_tip, const eigenmath::VectorXd& q_init) {
  INTR_ASSIGN_OR_RETURN(Chain chain, CreateChainFromModel(skeleton));

  INTR_ASSIGN_OR_RETURN(
      const std::unique_ptr<InverseKinematicsInterface> ik_solver,
      GetGlobalInverseKinematicsFactory().CreateInverseKinematicsSolver(
          solver_name, std::move(chain)));

  const JointLimits joint_limits = skeleton.GetDofSystemLimits();

  JointStateP hint_joint_state;
  INTR_RETURN_IF_ERROR(hint_joint_state.SetSize(joint_limits.size()));
  hint_joint_state.position = q_init;

  JointStateP solution;
  INTR_ASSIGN_OR_RETURN(const auto ik_result,
                        ik_solver->ComputeIK(hint_joint_state, base_t_tip,
                                             joint_limits, true, &solution));

  if (ik_result.status == InverseKinematicsInterface::IKResult::NO_SOLUTION) {
    LOG(WARNING)
        << "Could not compute same branch IK solution for the provided pose. "
           "It is either unreachable or does not have an IK solution on the "
           "same kinematic branch as the hint joint state.";
    return std::nullopt;
  }

  return solution;
}

}  // namespace kinematics
}  // namespace intrinsic
