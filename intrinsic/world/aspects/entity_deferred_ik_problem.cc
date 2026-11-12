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

#include "intrinsic/world/aspects/entity_deferred_ik_problem.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/die_if_null.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/ik/ik_helper.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/state_rn.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/world/aspects/entity_kinematic_utils.h"
#include "intrinsic/world/aspects/entity_world_interface.h"
#include "intrinsic/world/cartesian_kinematic_view.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/kinematics_builder.h"

namespace intrinsic {
namespace entity_kinematic_world_details {

EntityDeferredIKProblem::EntityDeferredIKProblem(
    const entity_aspect_world_details::EntityWorld& world,
    AttachmentEntityId base, AttachmentEntityId tip, const Pose3d& base_t_tip,
    const eigenmath::VectorXd* q_init)
    : base_t_tip_(base_t_tip) {
  ASSIGN_OR_DIE(auto robot_ids, world.GetRobotIdsInChain(base, tip));
  CHECK_EQ(1, robot_ids.size())
      << "This constructor is only valid for a single robot";
  ASSIGN_OR_DIE(solver_key_, GetIKSolverKey(world, robot_ids.front()));
  CHECK(!solver_key_.empty());

  // TODO(b/153489923): Once the IK solvers have been switched over to use
  // Entities, remove this code that builds an Assembly on the fly.
  ASSIGN_OR_DIE(skeleton_, BuildChainSkeleton(world, base, tip));

  if (q_init != nullptr) {
    q_init_storage_ = *q_init;
    q_init_ = &q_init_storage_;
  }
}

EntityDeferredIKProblem::EntityDeferredIKProblem(
    const entity_aspect_world_details::EntityWorld& world,
    const std::string& solver_key, AttachmentEntityId base,
    AttachmentEntityId tip, const Pose3d& base_t_tip,
    const eigenmath::VectorXd* q_init)
    : solver_key_(solver_key), base_t_tip_(base_t_tip) {
  CHECK(!solver_key_.empty());

  // TODO(b/153489923): Once the IK solvers have been switched over to use
  // Entities, remove this code that builds an Assembly on the fly.
  ASSIGN_OR_DIE(skeleton_, BuildChainSkeleton(world, base, tip));

  if (q_init != nullptr) {
    q_init_storage_ = *q_init;
    q_init_ = &q_init_storage_;
  }
}

std::optional<eigenmath::VectorXd> EntityDeferredIKProblem::GetSingleSolution()
    const {
  auto solutions = GetSampledSolutions(1, std::nullopt);
  if (solutions.empty()) {
    return std::nullopt;
  }

  return solutions.front();
}

std::optional<eigenmath::VectorXd>
EntityDeferredIKProblem::GetSameBranchSolution() const {
  if (q_init_ == nullptr) {
    return std::nullopt;
  }
  absl::StatusOr<const std::optional<JointStateP>> solution =
      ::intrinsic::kinematics::GetSameBranchIKSolution(solver_key_, *skeleton_,
                                                       base_t_tip_, *q_init_);
  if (!solution.ok()) {
    LOG(ERROR) << solution.status();
    return std::nullopt;
  }

  if (!solution->has_value()) {
    return std::nullopt;
  }

  return solution->value().position;
}

std::vector<eigenmath::VectorXd> EntityDeferredIKProblem::GetSampledSolutions(
    size_t max_num_solutions, std::optional<const FilterPolicy*> filter) const {
  if (max_num_solutions == 0) {
    LOG(ERROR) << "Asking for no solutions, getting none.";
    return {};
  }

  absl::StatusOr<std::vector<JointStateP>> solutions =
      ::intrinsic::kinematics::GetIKSolutions(
          solver_key_, *skeleton_, base_t_tip_, max_num_solutions, q_init_);
  if (!solutions.ok()) {
    LOG(ERROR) << solutions.status();
    return {};
  }

  if (solutions->size() > max_num_solutions) {
    LOG(ERROR) << "Found more solutions than requested. (Wanted "
               << max_num_solutions << ", got " << solutions->size() << ")";
  }

  // Convert and filter the solutions to VectorXd from JointStateP
  std::vector<eigenmath::VectorXd> results;
  results.reserve(max_num_solutions);
  for (const auto& solution : *solutions) {
    if (!filter || ABSL_DIE_IF_NULL(*filter)->IsValid(solution.position)) {
      results.push_back(solution.position);
    }
  }

  return results;
}

}  // namespace entity_kinematic_world_details
}  // namespace intrinsic
