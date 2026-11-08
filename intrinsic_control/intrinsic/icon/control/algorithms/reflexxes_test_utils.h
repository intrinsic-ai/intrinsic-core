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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_REFLEXXES_TEST_UTILS_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_REFLEXXES_TEST_UTILS_H_

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <random>
#include <vector>

#include "internal/testing.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/joint_position_reflexxes.h"
#include "intrinsic/icon/control/algorithms/joint_velocity_reflexxes.h"
#include "intrinsic/icon/reflexxes/status.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"

namespace intrinsic {
namespace icon {

template <typename TargetType>
struct TestTraits;  // remains undefined

template <>
struct TestTraits<JointStatePV> {
  using GeneratorType = intrinsic::icon::JointPositionReflexxes;
  using LimitsType = JointLimits;
  static constexpr bool ignore_position_enclosure = false;
};

template <>
struct TestTraits<JointStateV> {
  using GeneratorType = intrinsic::icon::JointVelocityReflexxes;
  using LimitsType = JointLimits;
  static constexpr bool ignore_position_enclosure = true;
};

// When checking whether Reflexxes brought us to the target state, it
// is important to understand that the underlying profile is
// continuous but we sample it at a fixed rate. Particularly for
// targets with non-zero velocity, this can lead to appreciable
// differences which depend on the sampling rate. The StateEnclosure
// class makes it easy to take that into account by worst-case
// reasoning around the target state, given some limits and the
// sampling timestep.
template <typename TargetType>
class StateEnclosure {
 public:
  bool Compute(double dt, const TargetType& state,
               const eigenmath::VectorNd& min_jerk,
               const eigenmath::VectorNd& max_jerk) {
    const size_t ndof = state.size();
    if (min_jerk.size() != ndof || max_jerk.size() != ndof) {
      fprintf(stderr, "ndof mismatch");
      return false;
    }

    CHECK_OK(lower_.SetSize(ndof));
    CHECK_OK(upper_.SetSize(ndof));
    const double dt2half = dt * dt / 2.0;
    const double dt3sixth = dt * dt * dt / 6.0;
    for (size_t dof = 0; dof < ndof; ++dof) {
      lower_.acceleration[dof] =
          std::min(min_jerk[dof] * dt, -max_jerk[dof] * dt);
      upper_.acceleration[dof] =
          std::max(max_jerk[dof] * dt, -min_jerk[dof] * dt);
      lower_.velocity[dof] = state.velocity[dof] + min_jerk[dof] * dt2half;
      upper_.velocity[dof] = state.velocity[dof] + max_jerk[dof] * dt2half;
      lower_.position[dof] =
          GetOrFakePosition(dof, state) +
          std::min(state.velocity[dof] * dt, -state.velocity[dof] * dt) +
          std::min(min_jerk[dof] * dt3sixth, -max_jerk[dof] * dt3sixth);
      upper_.position[dof] =
          GetOrFakePosition(dof, state) +
          std::max(state.velocity[dof] * dt, -state.velocity[dof] * dt) +
          std::max(max_jerk[dof] * dt3sixth, -min_jerk[dof] * dt3sixth);
    }
    return true;
  }

  int Contains(const JointStatePVA& state, const eigenmath::VectorNb& selection,
               bool verbose) {
    const size_t ndof = state.size();
    if (lower_.size() != ndof || selection.size() != ndof) {
      fprintf(stderr, "ndof mismatch");
      return -1;
    }

    int retval = 1;
    for (size_t dof = 0; dof < ndof; ++dof) {
      if (!selection[dof]) {
        if (verbose) {
          fprintf(stderr, "dof %zu is inactive\n", dof);
        }
        continue;
      }

      const bool acc = (state.acceleration[dof] >= lower_.acceleration[dof]) &&
                       (state.acceleration[dof] <= upper_.acceleration[dof]);
      const bool vel = (state.velocity[dof] >= lower_.velocity[dof]) &&
                       (state.velocity[dof] <= upper_.velocity[dof]);
      const bool pos =
          ignore_position || ((state.position[dof] >= lower_.position[dof]) &&
                              (state.position[dof] <= upper_.position[dof]));
      if (!(acc && vel && pos)) {
        retval = 0;
      }

      if (verbose) {
        fprintf(stderr,
                "dof %zu\n"
                "  acc: %10f < %10f < %10f %s\n"
                "  vel: %10f < %10f < %10f %s\n",
                dof, lower_.acceleration[dof], state.acceleration[dof],
                upper_.acceleration[dof], acc ? "valid" : "INVALID",
                lower_.velocity[dof], state.velocity[dof], upper_.velocity[dof],
                vel ? "valid" : "INVALID");
        if (!ignore_position) {
          fprintf(stderr, "  pos: %10f < %10f < %10f %s\n",
                  lower_.position[dof], state.position[dof],
                  upper_.position[dof], pos ? "valid" : "INVALID");
        }
      }
    }

    return retval;
  }

 private:
  JointStatePVA lower_, upper_;
  static constexpr bool ignore_position =
      TestTraits<TargetType>::ignore_position_enclosure;

  double GetOrFakePosition(size_t dof, const JointStatePV& state) const {
    return state.position[dof];
  }

  double GetOrFakePosition(size_t dof, const JointStateV& state) const {
    return 0.0;
  }
};

template <typename TargetType>
class JointReflexxesTest : public ::testing::Test {
 public:
  using Generator = typename TestTraits<TargetType>::GeneratorType;
  using LimitsType = typename TestTraits<TargetType>::LimitsType;
  using Enclosure = StateEnclosure<TargetType>;

  // A single function to call the setpoint generator for a given set of
  // inputs (this also EXPECTs the lack of errors, and ensures no memory
  // allocations
  std::vector<JointStatePVA> GetJointTrajectory(
      std::size_t ndof, double frequency_hz, const JointStatePVA& start,
      const LimitsType& limits, const TargetType& target,
      const eigenmath::VectorNb& selection, size_t max_size) {
    Generator generator(ndof, frequency_hz);
    JointStatePVA previous = start;
    std::vector<JointStatePVA> trajectory;
    trajectory.push_back(previous);

    while (generator.GetReflexxesStatus() == reflexxes::Status::kWorking ||
           generator.GetReflexxesStatus() !=
               reflexxes::Status::kFinalStateReached) {
      // inject wildly varying data into inactive DOFs
      for (size_t dof = 0; dof < ndof; ++dof) {
        if (!selection[dof]) {
          previous.position[dof] = (trajectory.size() % 2) ? 1.23 : -1.23;
          previous.velocity[dof] = (trajectory.size() % 2)
                                       ? GetOrFakeMinVelocity(dof, limits)
                                       : GetOrFakeMaxVelocity(dof, limits);
          previous.acceleration[dof] = (trajectory.size() % 2)
                                           ? -limits.max_acceleration[dof]
                                           : limits.max_acceleration[dof];
        }
      }

      std::vector<bool> rets(5);
      IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
      EXPECT_TRUE(rets[0] = generator.SetSelection(selection));
      EXPECT_TRUE(rets[1] = generator.SetLimits(limits));
      EXPECT_TRUE(rets[2] = generator.SetTarget(target));
      EXPECT_TRUE(rets[3] = generator.SetPrevious(previous));
      EXPECT_TRUE(rets[4] = generator.ComputeSetpoint(&previous));
      IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

      if ((std::count(rets.begin(), rets.end(), false) > 0)) {
        return std::vector<JointStatePVA>();
      }

      trajectory.push_back(previous);
    }

    return trajectory;
  }

  std::vector<JointStatePVA> GetJointTrajectory(std::size_t ndof,
                                                double frequency_hz,
                                                const JointStatePVA& start,
                                                const LimitsType& limits,
                                                const TargetType& target) {
    eigenmath::VectorNb selection(ndof);
    selection.setConstant(true);
    static const size_t max_size = std::numeric_limits<size_t>::max();
    return GetJointTrajectory(ndof, frequency_hz, start, limits, target,
                              selection, max_size);
  }

  void RandomSetup(std::size_t ndof, JointStatePVA* initial, TargetType* target,
                   LimitsType* limits) {
    using dist = std::uniform_real_distribution<double>;
    std::default_random_engine rand_engine;
    auto rand_vel = dist(0.1, 10.0);
    auto rand_acc = dist(1.0, 100.0);
    auto rand_jrk = dist(10.0, 1000.0);

    ASSERT_OK(limits->SetSize(ndof));
    for (size_t dof = 0; dof < ndof; ++dof) {
      limits->min_position[dof] = -reflexxes::kMaxPositionalLimit;
      limits->max_position[dof] = reflexxes::kMaxPositionalLimit;
      SampleVelocityLimits(dof, limits, &rand_vel, &rand_engine);
      limits->max_acceleration[dof] = rand_acc(rand_engine);
      limits->max_jerk[dof] = rand_jrk(rand_engine);
    }

    CHECK_OK(initial->SetSize(ndof));
    ASSERT_OK(target->SetSize(ndof));
    for (size_t dof = 0; dof < ndof; ++dof) {
      auto rand_pos = dist(-5.0, 5.0);
      initial->position[dof] = rand_pos(rand_engine);
      SampleTargetPosition(dof, target, &rand_pos, &rand_engine);
      rand_vel = dist(GetOrFakeMinVelocity(dof, *limits),
                      GetOrFakeMaxVelocity(dof, *limits));
      initial->velocity[dof] = rand_vel(rand_engine);
      target->velocity[dof] = rand_vel(rand_engine);
      rand_acc =
          dist(-limits->max_acceleration[dof], limits->max_acceleration[dof]);
      initial->acceleration[dof] = rand_acc(rand_engine);
    }
  }

 private:
  template <typename dist_t, typename engine_t>
  void SampleTargetPosition(size_t dof, JointStatePV* target, dist_t* rand_pos,
                            engine_t* rand_engine) {
    target->position[dof] = rand_pos->operator()(*rand_engine);
  }

  template <typename dist_t, typename engine_t>
  void SampleTargetPosition(size_t dof, JointStateV* target, dist_t* rand_pos,
                            engine_t* rand_engine) {
    // nop
  }

  template <typename dist_t, typename engine_t>
  void SampleVelocityLimits(size_t dof, JointLimits* limits, dist_t* rand_vel,
                            engine_t* rand_engine) {
    limits->max_velocity[dof] = rand_vel->operator()(*rand_engine);
  }

  double GetOrFakeMinVelocity(size_t dof, const JointLimits& limits) const {
    return -limits.max_velocity[dof];
  }

  double GetOrFakeMaxVelocity(size_t dof, const JointLimits& limits) const {
    return limits.max_velocity[dof];
  }
};

using JointPositionReflexxesTest = JointReflexxesTest<JointStatePV>;
using JointVelocityReflexxesTest = JointReflexxesTest<JointStateV>;

}  // namespace icon
}  // namespace intrinsic

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_REFLEXXES_TEST_UTILS_H_
