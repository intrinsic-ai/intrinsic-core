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

#ifndef INTRINSIC_KINEMATICS_TYPES_JOINT_STATE_MATCHER_H_
#define INTRINSIC_KINEMATICS_TYPES_JOINT_STATE_MATCHER_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <type_traits>

#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/state_rn.h"
#include "intrinsic/kinematics/types/to_fixed_string.h"

namespace intrinsic {
namespace testing {

namespace joint_state_matcher_internal {

template <typename T, typename M, typename Scalar>
bool GenericIsApprox(const T& arg, const M& other_joint_state, Scalar tolerance,
                     ::testing::MatchResultListener* match_result_listener) {
  bool made_any_comparisons = false;

  if constexpr (std::is_base_of<state_rn_details::StateRnBaseP<>, M>::value) {
    made_any_comparisons = true;
    if ((arg.position - other_joint_state.position).cwiseAbs().maxCoeff() >
        tolerance) {
      *match_result_listener
          << "\n position error is "
          << (arg.position - other_joint_state.position).transpose();
      return false;
    }
  }

  if constexpr (std::is_base_of<state_rn_details::StateRnBaseV<>, M>::value) {
    made_any_comparisons = true;
    if ((arg.velocity - other_joint_state.velocity).cwiseAbs().maxCoeff() >
        tolerance) {
      *match_result_listener
          << "\n velocity error is "
          << (arg.velocity - other_joint_state.velocity).transpose();
      return false;
    }
  }

  if constexpr (std::is_base_of<state_rn_details::StateRnBaseA<>, M>::value) {
    made_any_comparisons = true;
    if ((arg.acceleration - other_joint_state.acceleration)
            .cwiseAbs()
            .maxCoeff() > tolerance) {
      *match_result_listener
          << "\n acceleration error is "
          << (arg.acceleration - other_joint_state.acceleration).transpose();
      return false;
    }
  }

  if constexpr (std::is_base_of<state_rn_details::StateRnBaseJ<>, M>::value) {
    made_any_comparisons = true;
    if ((arg.jerk - other_joint_state.jerk).cwiseAbs().maxCoeff() > tolerance) {
      *match_result_listener << "\n jerk error is "
                             << (arg.jerk - other_joint_state.jerk).transpose();
      return false;
    }
  }

  if constexpr (std::is_base_of<state_rn_details::StateRnBaseT<>, M>::value) {
    made_any_comparisons = true;
    if ((arg.torque - other_joint_state.torque).cwiseAbs().maxCoeff() >
        tolerance) {
      *match_result_listener
          << "\n torque error is "
          << (arg.torque - other_joint_state.torque).transpose();
      return false;
    }
  }

  return made_any_comparisons;
}

}  // namespace joint_state_matcher_internal

// This matcher currently works with the 'default' fixed size JointState types,
// e.g. JointStateP, JointStatePV, etc. JointStateWithMaxSize<N> types (as no
// comparisons will be made, c.f. implementation of 'GenericIsApprox').
MATCHER_P2(JointStateIsApprox, other_joint_state, tolerance,
           absl::StrCat("is approximately equal to\n",
                        std::string(ToFixedString(other_joint_state)),
                        " with tolerance ", tolerance)) {
  return joint_state_matcher_internal::GenericIsApprox(
      arg, other_joint_state, tolerance, result_listener);
}

}  // namespace testing
}  // namespace intrinsic

#endif  // INTRINSIC_KINEMATICS_TYPES_JOINT_STATE_MATCHER_H_
