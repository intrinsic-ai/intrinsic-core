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

#include "intrinsic/icon/control/algorithms/cartesian_position_reflexxes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <random>
#include <vector>

#include "Eigen/Core"
#include "finite_difference_test.h"
#include "intrinsic/eigenmath/manifolds.h"
#include "intrinsic/eigenmath/so3.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/reflexxes/constants_external.h"
#include "intrinsic/icon/reflexxes/status.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic {
namespace icon {
namespace {

static constexpr double rot_limits_fudge_factor =
    0.1;  // TODO(rphilipp) b/27473089

class CartesianPositionReflexxesTest : public ::testing::Test {
 public:
  // A single function to call the setpoint generator for a given set of inputs
  // (this also EXPECTs the lack of errors, and ensures no memory allocations
  std::vector<CartStatePVA> GetCartesianTrajectory(
      double frequency_hz, const CartStatePVA& start,
      const CartesianLimits& limits, const CartStatePV& target,
      const eigenmath::Vector3b& translation_selection, bool rotation_selection,
      size_t max_size) {
    CartesianPositionReflexxes generator(frequency_hz);
    std::vector<CartStatePVA> trajectory;
    CartStatePVA previous = start;

    trajectory.push_back(start);
    do {
      if (trajectory.size() > max_size) {
        return std::vector<CartStatePVA>();
      }

      // inject wildly varying data into inactive DOFs
      for (size_t dof = 0; dof < 3; ++dof) {
        if (!translation_selection[dof]) {
          previous.pose.translation()[dof] =
              (trajectory.size() % 2) ? 1.23 : -1.23;
          previous.velocity[dof] = (trajectory.size() % 2)
                                       ? limits.min_translational_velocity[dof]
                                       : limits.max_translational_velocity[dof];
          previous.acceleration[dof] =
              (trajectory.size() % 2)
                  ? limits.min_translational_acceleration[dof]
                  : limits.max_translational_acceleration[dof];
        }
      }
      if (!rotation_selection) {
        double limits_factor = rot_limits_fudge_factor;
        if (trajectory.size() % 2) {
          previous.pose = Pose3d(start.pose.so3(), previous.pose.translation());
        } else {
          previous.pose =
              Pose3d(target.pose.so3(), previous.pose.translation());
          limits_factor *= -1.0;
        }
        previous.velocity.tail<3>().setConstant(limits_factor *
                                                limits.max_rotational_velocity);
        previous.acceleration.tail<3>().setConstant(
            limits_factor * limits.max_rotational_acceleration);
      }

      std::vector<bool> rets(2);
      IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();

      generator.SetSelection(translation_selection, rotation_selection);
      EXPECT_TRUE(rets[0] = generator.SetLimits(limits));
      generator.SetTarget(target);
      generator.SetPrevious(previous);

      EXPECT_TRUE(rets[1] = generator.ComputeSetpoint(&previous));
      IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

      if ((std::count(rets.begin(), rets.end(), false) > 0) ||
          ((generator.GetReflexxesStatus() !=
            reflexxes::Status::kFinalStateReached) &&
           (generator.GetReflexxesStatus() != reflexxes::Status::kWorking))) {
        DumpSetup(start, target, limits);
        return std::vector<CartStatePVA>();
      }

      trajectory.push_back(previous);
    } while (generator.GetReflexxesStatus() !=
             reflexxes::Status::kFinalStateReached);

    return trajectory;
  }

  std::vector<CartStatePVA> GetCartesianTrajectory(
      double frequency_hz, const CartStatePVA& start,
      const CartesianLimits& limits, const CartStatePV& target) {
    eigenmath::Vector3b translation_selection;
    translation_selection.setConstant(true);
    static constexpr bool rotation_selection = true;
    static constexpr size_t max_size = 60 * 60 * 1000;  // one hour at 1kHz
    return GetCartesianTrajectory(frequency_hz, start, limits, target,
                                  translation_selection, rotation_selection,
                                  max_size);
  }

  // generate random pose
  Pose3d RandomPose3d() {
    eigenmath::Vector3d axis;
    double length = 0;
    do {
      axis =
          eigenmath::Vector3d(rand_dir_(rand_engine_), rand_dir_(rand_engine_),
                              rand_dir_(rand_engine_));
      length = axis.norm();
    } while (length < 0.01);
    axis = axis / length;
    eigenmath::Matrix3d rotation(
        Eigen::AngleAxisd(rand_dir_(rand_engine_) * M_PI, axis).matrix());
    eigenmath::Vector3d translation(rand_dir_(rand_engine_) * 10,
                                    rand_dir_(rand_engine_) * 10,
                                    rand_dir_(rand_engine_) * 10);
    return Pose3d(rotation, translation);
  }

  void RandomSetup(CartStatePVA* initial, CartStatePV* target,
                   CartesianLimits* limits) {
    *limits = RandomLimits();
    initial->pose = RandomPose3d();
    target->pose = RandomPose3d();
    for (size_t dof = 0; dof < 3; ++dof) {
      auto dv = std::uniform_real_distribution<double>(
          limits->min_translational_velocity[dof],
          limits->max_translational_velocity[dof]);
      auto da = std::uniform_real_distribution<double>(
          limits->min_translational_acceleration[dof],
          limits->max_translational_acceleration[dof]);
      initial->velocity[dof] = dv(rand_engine_);
      initial->acceleration[dof] = da(rand_engine_);
      target->velocity[dof] = dv(rand_engine_);
    }
    auto dv = std::uniform_real_distribution<double>(
        0.0, limits->max_rotational_velocity);
    auto da = std::uniform_real_distribution<double>(
        0.0, limits->max_rotational_acceleration);
    for (size_t dof = 3; dof < 6; ++dof) {
      // TODO (rphilipp, buschmann) b/27473089 remove
      // rot_limits_fudge_factor. The problem analysis and
      // suggested solution approach is documented in the
      // buganizer ticket.
      initial->velocity[dof] = rot_limits_fudge_factor * dv(rand_engine_);
      initial->acceleration[dof] = rot_limits_fudge_factor * da(rand_engine_);
      target->velocity[dof] = rot_limits_fudge_factor * dv(rand_engine_);
    }
  }

  std::array<double, 4> ComputeAngleAxis(const eigenmath::SO3d& so3) {
    std::array<double, 4> aa;
    const eigenmath::Vector3d axis = intrinsic::eigenmath::logSO3(so3);
    aa[0] = axis.norm();
    static constexpr double epsilon = 1.0e-6;
    if (aa[0] < epsilon) {
      aa[0] = 0.0;
      aa[1] = 0.0 / 0.0;
      aa[2] = 0.0 / 0.0;
      aa[3] = 0.0 / 0.0;
    } else {
      aa[1] = axis[0] / aa[0];
      aa[2] = axis[1] / aa[0];
      aa[3] = axis[2] / aa[0];
    }
    return aa;
  }

  std::array<double, 4> ComputeMagnitudeAxis(const eigenmath::Vector3d& v3d) {
    std::array<double, 4> aa;
    aa[0] = v3d.norm();
    static constexpr double epsilon = 1.0e-6;
    if (aa[0] < epsilon) {
      aa[0] = 0.0;
      aa[1] = 0.0 / 0.0;
      aa[2] = 0.0 / 0.0;
      aa[3] = 0.0 / 0.0;
    } else {
      aa[1] = v3d[0] / aa[0];
      aa[2] = v3d[1] / aa[0];
      aa[3] = v3d[2] / aa[0];
    }
    return aa;
  }

  void DumpSetup(const CartStatePVA& initial, const CartStatePV& target,
                 const CartesianLimits& limits) {
    std::array<double, 4> initial_aa = ComputeAngleAxis(initial.pose.so3());
    std::array<double, 4> target_aa = ComputeAngleAxis(target.pose.so3());
    std::array<double, 4> relative_aa =
        ComputeAngleAxis(initial.pose.so3().inverse() * target.pose.so3());
    std::array<double, 4> initial_vel_ma =
        ComputeMagnitudeAxis(initial.velocity.tail<3>());
    std::array<double, 4> initial_acc_ma =
        ComputeMagnitudeAxis(initial.acceleration.tail<3>());
    std::array<double, 4> target_vel_ma =
        ComputeMagnitudeAxis(target.velocity.tail<3>());

    fprintf(
        stderr,
        "initial\n"
        "  pos   %10.5f %10.5f %10.5f   %10.5f  %6.2f %6.2f %6.2f\n"
        "  vel   %10.5f %10.5f %10.5f   %10.5f  %6.2f %6.2f %6.2f\n"
        "  acc   %10.5f %10.5f %10.5f   %10.5f  %6.2f %6.2f %6.2f\n"
        "target\n"
        "  pos   %10.5f %10.5f %10.5f   %10.5f  %6.2f %6.2f %6.2f\n"
        "                             wrt initial   %10.5f  %6.2f %6.2f "
        "%6.2f\n"
        "  vel   %10.5f %10.5f %10.5f   %10.5f  %6.2f %6.2f %6.2f\n"
        "lower limits\n"
        "  pos   %10.1e %10.1e %10.1e\n"
        "  vel   %10.5f %10.5f %10.5f\n"
        "  acc   %10.5f %10.5f %10.5f\n"
        "  jrk   %10.5f %10.5f %10.5f\n"
        "upper limits\n"
        "  pos   %10.1e %10.1e %10.1e\n"
        "  vel   %10.5f %10.5f %10.5f   %10.5f\n"
        "  acc   %10.5f %10.5f %10.5f   %10.5f\n"
        "  jrk   %10.5f %10.5f %10.5f   %10.5f\n",

        initial.pose.translation()[0], initial.pose.translation()[1],
        initial.pose.translation()[2], initial_aa[0], initial_aa[1],
        initial_aa[2], initial_aa[3],

        initial.velocity[0], initial.velocity[1], initial.velocity[2],
        initial_vel_ma[0], initial_vel_ma[1], initial_vel_ma[2],
        initial_vel_ma[3],

        initial.acceleration[0], initial.acceleration[1],
        initial.acceleration[2], initial_acc_ma[0], initial_acc_ma[1],
        initial_acc_ma[2], initial_acc_ma[3],

        target.pose.translation()[0], target.pose.translation()[1],
        target.pose.translation()[2], target_aa[0], target_aa[1], target_aa[2],
        target_aa[3],

        relative_aa[0], relative_aa[1], relative_aa[2], relative_aa[3],

        target.velocity[0], target.velocity[1], target.velocity[2],
        target_vel_ma[0], target_vel_ma[1], target_vel_ma[2], target_vel_ma[3],

        limits.min_translational_position[0],
        limits.min_translational_position[1],
        limits.min_translational_position[2],

        limits.min_translational_velocity[0],
        limits.min_translational_velocity[1],
        limits.min_translational_velocity[2],

        limits.min_translational_acceleration[0],
        limits.min_translational_acceleration[1],
        limits.min_translational_acceleration[2],

        limits.min_translational_jerk[0], limits.min_translational_jerk[1],
        limits.min_translational_jerk[2],

        limits.max_translational_position[0],
        limits.max_translational_position[1],
        limits.max_translational_position[2],

        limits.max_translational_velocity[0],
        limits.max_translational_velocity[1],
        limits.max_translational_velocity[2], limits.max_rotational_velocity,

        limits.max_translational_acceleration[0],
        limits.max_translational_acceleration[1],
        limits.max_translational_acceleration[2],
        limits.max_rotational_acceleration,

        limits.max_translational_jerk[0], limits.max_translational_jerk[1],
        limits.max_translational_jerk[2], limits.max_rotational_jerk);
  }

  // generate random limits
  CartesianLimits RandomLimits() {
    CartesianLimits limits;
    const double vscale = 20;
    const double ascale = 400;
    const double jscale = 8000;

    limits.max_translational_position.setConstant(
        reflexxes::kMaxPositionalLimit);
    limits.min_translational_position.setConstant(
        -reflexxes::kMaxPositionalLimit);

    limits.max_translational_velocity[0] = vscale * rand_pos_(rand_engine_);
    limits.max_translational_velocity[1] = vscale * rand_pos_(rand_engine_);
    limits.max_translational_velocity[2] = vscale * rand_pos_(rand_engine_);
    limits.min_translational_velocity[0] = -vscale * rand_pos_(rand_engine_);
    limits.min_translational_velocity[1] = -vscale * rand_pos_(rand_engine_);
    limits.min_translational_velocity[2] = -vscale * rand_pos_(rand_engine_);

    limits.max_translational_acceleration[0] = ascale * rand_pos_(rand_engine_);
    limits.max_translational_acceleration[1] = ascale * rand_pos_(rand_engine_);
    limits.max_translational_acceleration[2] = ascale * rand_pos_(rand_engine_);
    limits.min_translational_acceleration[0] =
        -ascale * rand_pos_(rand_engine_);
    limits.min_translational_acceleration[1] =
        -ascale * rand_pos_(rand_engine_);
    limits.min_translational_acceleration[2] =
        -ascale * rand_pos_(rand_engine_);

    limits.max_translational_jerk[0] = jscale * rand_pos_(rand_engine_);
    limits.max_translational_jerk[1] = jscale * rand_pos_(rand_engine_);
    limits.max_translational_jerk[2] = jscale * rand_pos_(rand_engine_);
    limits.min_translational_jerk[0] = -jscale * rand_pos_(rand_engine_);
    limits.min_translational_jerk[1] = -jscale * rand_pos_(rand_engine_);
    limits.min_translational_jerk[2] = -jscale * rand_pos_(rand_engine_);

    limits.max_rotational_velocity = vscale * rand_pos_(rand_engine_);
    limits.max_rotational_acceleration = ascale * rand_pos_(rand_engine_);
    limits.max_rotational_jerk = jscale * rand_pos_(rand_engine_);

    return limits;
  }

  // generate symmetric, uniform limits
  CartesianLimits SymmetricUniformRandomLimits() {
    CartesianLimits limits;
    const double vscale = 20;
    const double ascale = 400;
    const double jscale = 8000;

    limits.max_translational_position.setConstant(
        reflexxes::kMaxPositionalLimit);
    limits.min_translational_position.setConstant(
        -reflexxes::kMaxPositionalLimit);

    const double max_vel = vscale * rand_pos_(rand_engine_);
    limits.max_translational_velocity.setConstant(max_vel);
    limits.min_translational_velocity.setConstant(-max_vel);

    const double max_acc = ascale * rand_pos_(rand_engine_);
    limits.max_translational_acceleration.setConstant(max_acc);
    limits.min_translational_acceleration.setConstant(-max_acc);

    const double max_jerk = jscale * rand_pos_(rand_engine_);
    limits.max_translational_jerk.setConstant(max_jerk);
    limits.min_translational_jerk.setConstant(-max_jerk);

    limits.max_rotational_velocity = max_vel;
    limits.max_rotational_acceleration = max_acc;
    limits.max_rotational_jerk = max_jerk;

    return limits;
  }

  struct StateEnclosure {
    struct TranslationalStatePVA {
      eigenmath::Vector3d position, velocity, acceleration;
    };
    struct RotationalMagnitudePVA {
      double position, velocity, acceleration;
    };

    TranslationalStatePVA trans_upper_, trans_lower_;
    RotationalMagnitudePVA rot_upper_;
    eigenmath::SO3d target_orientation_;

    StateEnclosure(double dt, const CartStatePV& state,
                   const eigenmath::Vector3d& min_trans_jerk,
                   const eigenmath::Vector3d& max_trans_jerk,
                   double max_rot_jerk) {
      Compute(dt, state, min_trans_jerk, max_trans_jerk, max_rot_jerk);
    }

    void Compute(double dt, const CartStatePV& state,
                 const eigenmath::Vector3d& min_trans_jerk,
                 const eigenmath::Vector3d& max_trans_jerk,
                 double max_rot_jerk) {
      const double dt2half = dt * dt / 2.0;
      const double dt3sixth = dt * dt * dt / 6.0;
      target_orientation_ = state.pose.so3();

      for (size_t dof = 0; dof < 3; ++dof) {
        trans_lower_.acceleration[dof] =
            std::min(min_trans_jerk[dof] * dt, -max_trans_jerk[dof] * dt);
        trans_upper_.acceleration[dof] =
            std::max(max_trans_jerk[dof] * dt, -min_trans_jerk[dof] * dt);
        trans_lower_.velocity[dof] =
            state.velocity[dof] + min_trans_jerk[dof] * dt2half;
        trans_upper_.velocity[dof] =
            state.velocity[dof] + max_trans_jerk[dof] * dt2half;
        trans_lower_.position[dof] =
            state.pose.translation()[dof] +
            std::min(state.velocity[dof] * dt, -state.velocity[dof] * dt) +
            std::min(min_trans_jerk[dof] * dt3sixth,
                     -max_trans_jerk[dof] * dt3sixth);
        trans_upper_.position[dof] =
            state.pose.translation()[dof] +
            std::max(state.velocity[dof] * dt, -state.velocity[dof] * dt) +
            std::max(max_trans_jerk[dof] * dt3sixth,
                     -min_trans_jerk[dof] * dt3sixth);
      }

      // rotations have a rather loose isotropic enclosure
      rot_upper_.acceleration = max_rot_jerk * dt;
      rot_upper_.velocity =
          state.velocity.tail<3>().norm() + max_rot_jerk * dt2half;
      rot_upper_.position =  // relative to target_orientation_
          state.velocity.tail<3>().norm() * dt + max_rot_jerk * dt3sixth;
    }

    bool Contains(const CartStatePVA& state, bool verbose) {
      eigenmath::Vector3b trans_selection;
      trans_selection.setConstant(true);
      static constexpr bool rot_selection = true;
      return Contains(state, trans_selection, rot_selection, verbose);
    }

    bool Contains(const CartStatePVA& state,
                  const eigenmath::Vector3b& trans_selection,
                  bool rot_selection, bool verbose) {
      static const char* dofname[] = {
          "translation along X", "translation along Y", "translation along Z"};
      bool retval = true;
      for (size_t dof = 0; dof < 3; ++dof) {
        if (!trans_selection[dof]) {
          if (verbose) {
            fprintf(stderr, "%s is inactive\n", dofname[dof]);
          }
          continue;
        }

        const bool acc =
            (state.acceleration[dof] >= trans_lower_.acceleration[dof]) &&
            (state.acceleration[dof] <= trans_upper_.acceleration[dof]);
        const bool vel = (state.velocity[dof] >= trans_lower_.velocity[dof]) &&
                         (state.velocity[dof] <= trans_upper_.velocity[dof]);
        const bool pos =
            (state.pose.translation()[dof] >= trans_lower_.position[dof]) &&
            (state.pose.translation()[dof] <= trans_upper_.position[dof]);
        if (!(acc && vel && pos)) {
          retval = false;
        }

        if (verbose) {
          fprintf(stderr,
                  "%s\n"
                  "  acc: %10f < %10f < %10f %s\n"
                  "  vel: %10f < %10f < %10f %s\n"
                  "  pos: %10f < %10f < %10f %s\n",
                  dofname[dof], trans_lower_.acceleration[dof],
                  state.acceleration[dof], trans_upper_.acceleration[dof],
                  acc ? "valid" : "INVALID", trans_lower_.velocity[dof],
                  state.velocity[dof], trans_upper_.velocity[dof],
                  vel ? "valid" : "INVALID", trans_lower_.position[dof],
                  state.pose.translation()[dof], trans_upper_.position[dof],
                  pos ? "valid" : "INVALID");
        }
      }

      if (!rot_selection) {
        if (verbose) {
          fprintf(stderr, "rotations are inactive\n");
        }
      } else {
        const double acc_magnitude = state.acceleration.tail<3>().norm();
        const double vel_magnitude = state.velocity.tail<3>().norm();
        const double pos_magnitude =
            intrinsic::eigenmath::logSO3(state.pose.so3().inverse() *
                                         target_orientation_)
                .norm();

        const bool acc = (acc_magnitude <= rot_upper_.acceleration);
        const bool vel = (vel_magnitude <= rot_upper_.velocity);
        const bool pos = (pos_magnitude <= rot_upper_.position);
        if (!(acc && vel && pos)) {
          retval = false;
        }

        if (verbose) {
          fprintf(stderr,
                  "magnitude of rotation\n"
                  "  acc: %10f < %10f %s\n"
                  "  vel: %10f < %10f %s\n"
                  "  pos: %10f < %10f %s\n",
                  acc_magnitude, rot_upper_.acceleration,
                  acc ? "valid" : "INVALID", vel_magnitude, rot_upper_.velocity,
                  vel ? "valid" : "INVALID", pos_magnitude, rot_upper_.position,
                  pos ? "valid" : "INVALID");
        }
      }

      return retval;
    }
  };

 private:
  std::default_random_engine rand_engine_;
  std::uniform_real_distribution<double> rand_dir_ =
      std::uniform_real_distribution<double>(-1.0, 1.0);
  std::uniform_real_distribution<double> rand_pos_ =
      std::uniform_real_distribution<double>(0.1, 1.0);
};

// Test if checks for invalid input
TEST_F(CartesianPositionReflexxesTest, TestInputErrors) {
  CartesianPositionReflexxes generator(1000);
  CartesianLimits limits;

  limits.max_translational_position.setConstant(10);
  limits.min_translational_position.setConstant(-10);

  limits.max_translational_velocity.setConstant(1);
  limits.min_translational_velocity.setConstant(-1);
  limits.max_translational_acceleration.setConstant(1);
  limits.min_translational_acceleration.setConstant(-1);
  limits.max_translational_jerk.setConstant(1);
  limits.min_translational_jerk.setConstant(-1);
  limits.max_rotational_velocity = 1;
  limits.max_rotational_acceleration = 1;
  limits.max_rotational_jerk = 1;

  EXPECT_TRUE(generator.SetLimits(limits));

  {
    CartesianLimits lmt = limits;
    lmt.max_translational_position[0] = -10;
    lmt.min_translational_position[0] = 10;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }

  {
    CartesianLimits lmt = limits;
    lmt.max_translational_velocity[0] = -1;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }

  {
    CartesianLimits lmt = limits;
    lmt.min_translational_velocity[0] = 1;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }

  {
    CartesianLimits lmt = limits;
    lmt.max_translational_acceleration[0] = -1;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }

  {
    CartesianLimits lmt = limits;
    lmt.min_translational_acceleration[0] = 1;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }

  {
    CartesianLimits lmt = limits;
    lmt.max_translational_jerk[0] = -1;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }

  {
    CartesianLimits lmt = limits;
    lmt.min_translational_jerk[0] = 1;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }

  {
    CartesianLimits lmt = limits;
    lmt.max_rotational_velocity = -1;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }
  {
    CartesianLimits lmt = limits;
    lmt.max_rotational_acceleration = -1;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }
  {
    CartesianLimits lmt = limits;
    lmt.max_rotational_jerk = -1;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }
}

// Test that a simple motion (with very high limits on derivatives) is completed
// in the requested duration and the target point is reached.
TEST_F(CartesianPositionReflexxesTest, TestSimpleTrajectory) {
  CartStatePVA initial;
  initial.pose = Pose3d(
      Eigen::AngleAxisd(0.5 * M_PI, eigenmath::Vector3d::UnitX()).matrix(),
      eigenmath::Vector3d(1.0, 2.0, 3.0));
  initial.velocity.setZero();
  initial.acceleration.setZero();

  CartStatePV target;
  target.pose = Pose3d(
      Eigen::AngleAxisd(1.5 * M_PI, eigenmath::Vector3d::UnitY()).matrix(),
      eigenmath::Vector3d(-1.0, -2.0, -3.0));
  target.velocity.setZero();

  CartesianLimits limits;
  limits.max_rotational_velocity = 10;
  limits.max_rotational_acceleration = 100;
  limits.max_rotational_jerk = 1000;

  limits.max_translational_position.setConstant(reflexxes::kMaxPositionalLimit);
  limits.min_translational_position.setConstant(
      -reflexxes::kMaxPositionalLimit);
  limits.min_translational_velocity.setConstant(
      -limits.max_rotational_velocity);
  limits.max_translational_velocity.setConstant(limits.max_rotational_velocity);
  limits.min_translational_acceleration.setConstant(
      -limits.max_rotational_acceleration);
  limits.max_translational_acceleration.setConstant(
      limits.max_rotational_acceleration);
  limits.min_translational_jerk.setConstant(-limits.max_rotational_jerk);
  limits.max_translational_jerk.setConstant(limits.max_rotational_jerk);

  double frequency_hz = 1000.0;
  double dt_s = 1 / frequency_hz;
  auto trajectory =
      GetCartesianTrajectory(frequency_hz, initial, limits, target);

  double total_time = dt_s * trajectory.size();
  EXPECT_GT(0.9, total_time);
  EXPECT_LT(0.8, total_time);

  EXPECT_GT(1e-12, (trajectory.back().pose.rotationMatrix() *
                        target.pose.rotationMatrix().inverse() -
                    eigenmath::Matrix3d::Identity())
                       .norm());
  EXPECT_GT(1e-12,
            (trajectory.back().pose.translation() - target.pose.translation())
                .norm());
  EXPECT_GT(1e-12, (trajectory.back().velocity - target.velocity).norm());
}

// Test that a random motion starts at the initial state and ends at the target
// state.
TEST_F(CartesianPositionReflexxesTest, TestRandomSetup) {
  static constexpr size_t ntrials = 10;
  static constexpr double frequency_hz = 100.0;
  static constexpr double dt = 1.0 / frequency_hz;
  for (size_t trial = 0; trial < ntrials; ++trial) {
    CartStatePVA initial;
    CartStatePV target;
    CartesianLimits limits;
    RandomSetup(&initial, &target, &limits);

    StateEnclosure enclosure(dt, target, limits.min_translational_jerk,
                             limits.max_translational_jerk,
                             limits.max_rotational_jerk);

    const auto trajectory =
        GetCartesianTrajectory(frequency_hz, initial, limits, target);
    ASSERT_LE(1, trajectory.size());
    ASSERT_TRUE(enclosure.Contains(trajectory.back(), false));
  }
}

// tests that point-to-point motion (rest to rest) generates a straight line
// for translation coordinates and a planar rotation.
TEST_F(CartesianPositionReflexxesTest, TestPointToPointStraightLine) {
  CartStatePVA initial_state;
  CartStatePV target_state;
  CartesianLimits limits;

  static constexpr size_t ntries = 10;
  for (size_t i = 0; i < ntries; ++i) {
    initial_state.pose = RandomPose3d();
    initial_state.velocity.setZero();
    initial_state.acceleration.setZero();

    target_state.pose = RandomPose3d();
    target_state.velocity.setZero();

    // Reflexxes only guarantees straight lines when using symmetric uniform
    // limits.
    limits = SymmetricUniformRandomLimits();

    double frequency_hz = 100.0;
    std::vector<CartStatePVA> trajectory = GetCartesianTrajectory(
        frequency_hz, initial_state, limits, target_state);

    ASSERT_LE(1, trajectory.size());
    const auto& last_pose = trajectory.back().pose;
    // make sure we arrive at the target
    EXPECT_GE(
        1e-12,
        (target_state.pose.translation() - last_pose.translation()).norm());
    EXPECT_GT(1e-12, (trajectory.back().pose.rotationMatrix() *
                          target_state.pose.rotationMatrix().inverse() -
                      eigenmath::Matrix3d::Identity())
                         .norm());

    // check for straight line
    const eigenmath::Matrix3d Rstart =
        initial_state.pose.rotationMatrix().transpose();
    eigenmath::Vector3d start_axis_angle = intrinsic::eigenmath::logSO3(
        eigenmath::SO3d(Rstart * initial_state.pose.rotationMatrix()));

    EXPECT_NEAR(0, start_axis_angle.norm(), 1E-12);
    eigenmath::Vector3d end_axis_angle = intrinsic::eigenmath::logSO3(
        eigenmath::SO3d(Rstart * target_state.pose.rotationMatrix()));

    eigenmath::Vector3d start_to_end =
        target_state.pose.translation() - initial_state.pose.translation();
    eigenmath::Vector3d mid_omega =
        trajectory[trajectory.size() / 2].velocity.tail<3>();
    double end_dist = start_to_end.norm();
    constexpr double kAlmostZero = 3e-12;
    for (const CartStatePVA& state : trajectory) {
      // current position is on line from start to end
      const eigenmath::Vector3d x_from_start =
          state.pose.translation() - initial_state.pose.translation();
      const eigenmath::Vector3d cross_pos = x_from_start.cross(start_to_end);
      EXPECT_GE(kAlmostZero, cross_pos.norm());
      // point doesn't overshoot
      EXPECT_TRUE(end_dist + kAlmostZero >= x_from_start.norm());
      const eigenmath::Vector3d vel = state.velocity.head<3>();
      //  velocity directed towards goal
      EXPECT_LE(0, start_to_end.dot(vel));

      // velocity colinear to straight line
      const eigenmath::Vector3d cross_vel = vel.cross(start_to_end);
      EXPECT_GE(kAlmostZero, cross_vel.norm());
      // same for accelerations
      const eigenmath::Vector3d accel = state.acceleration.head<3>();
      const eigenmath::Vector3d cross_accel = accel.cross(start_to_end);
      EXPECT_GE(kAlmostZero, cross_accel.norm());
      // rotation planar?
      const eigenmath::Vector3d x_axis_angle = intrinsic::eigenmath::logSO3(
          eigenmath::SO3d(Rstart * state.pose.rotationMatrix()));
      const eigenmath::Vector3d cross_axis_angle =
          x_axis_angle.cross(end_axis_angle);

      EXPECT_GE(kAlmostZero, cross_axis_angle.norm());
      // angular velocity in correct plane
      eigenmath::Vector3d omega = state.velocity.tail<3>();
      const eigenmath::Vector3d cross_omega = omega.cross(mid_omega);
      EXPECT_NEAR(0, cross_omega.norm(), kAlmostZero);

      // angular velocity directed towards goal
      EXPECT_LE(0, end_axis_angle.dot(Rstart * omega));
      // angular acceleration in correct plane
      const eigenmath::Vector3d alpha = state.acceleration.tail<3>();
      const eigenmath::Vector3d cross_alpha = alpha.cross(mid_omega);
      EXPECT_GE(kAlmostZero, cross_alpha.norm());
    }
  }
}

// Check that the angular velocity and orientation are kinematically consistent.
TEST_F(CartesianPositionReflexxesTest, TestKinematicConsistency) {
  CartStatePVA initial_state;
  CartStatePV target_state;
  CartesianLimits limits;

  static constexpr size_t ntries = 10;
  for (size_t i = 0; i < ntries; ++i) {
    initial_state.pose = RandomPose3d();
    initial_state.velocity.setZero();
    initial_state.acceleration.setZero();

    target_state.pose = RandomPose3d();
    target_state.velocity.setZero();
    limits = RandomLimits();

    double frequency_hz =
        40000.0;  // use higher value for lower fd-approximation error
    double dt_s = 1 / frequency_hz;
    std::vector<CartStatePVA> trajectory = GetCartesianTrajectory(
        frequency_hz, initial_state, limits, target_state);

    ASSERT_LE(1, trajectory.size());

    control::test::FiniteDifferenceTest<eigenmath::Vector3d,
                                        eigenmath::Vector3d>
        fd_linear_velocity;
    control::test::FiniteDifferenceTest<eigenmath::Matrix3d,
                                        eigenmath::Vector3d>
        fd_angular_velocity;
    control::test::FiniteDifferenceTest<eigenmath::Vector3d,
                                        eigenmath::Vector3d>
        fd_linear_acceleration;
    control::test::FiniteDifferenceTest<eigenmath::Vector3d,
                                        eigenmath::Vector3d>
        fd_angular_acceleration;

    fd_linear_velocity.init("linear_velocity", dt_s);
    fd_angular_velocity.init("angular_velocity", dt_s);
    fd_linear_acceleration.init("linear_acceleration", dt_s);
    fd_angular_acceleration.init("angular_acceleration", dt_s);

    for (const CartStatePVA& state : trajectory) {
      // velocity vector layout is [translation;rotation]
      fd_linear_velocity.update(state.pose.translation(),
                                state.velocity.head<3>());
      fd_angular_velocity.update(state.pose.quaternion().toRotationMatrix(),
                                 state.velocity.tail<3>());
      // fd_linear_velocity.printCurrent();
      // fd_angular_velocity.printCurrent();

      fd_linear_acceleration.update(state.velocity.head<3>(),
                                    state.acceleration.head<3>());
      fd_angular_acceleration.update(state.velocity.tail<3>(),
                                     state.acceleration.tail<3>());
      // fd_linear_acceleration.printCurrent();
      // fd_angular_acceleration.printCurrent();
    }
    // fd_linear_velocity.printMaxError();
    // fd_angular_velocity.printMaxError();
    // fd_linear_acceleration.printMaxError();
    // fd_angular_acceleration.printMaxError();

    // This is really dependent on time step and trajectory.
    // A more complete test would check how this changes as dt decreases to
    // confirm error order of finite difference approximation ...
    EXPECT_LE(fd_linear_velocity.getMaxError(), 1e-5);
    EXPECT_LE(fd_linear_acceleration.getMaxError(), 1e-1);
    EXPECT_LE(fd_angular_velocity.getMaxError(), 1e-5);
    EXPECT_LE(fd_angular_acceleration.getMaxError(), 1e-1);
  }
}

// TODO(buschmann): When position limit checking works, add test for that here

// test the selection mechanism
TEST_F(CartesianPositionReflexxesTest, TestSelection) {
  static constexpr size_t ntrials = 5;
  static constexpr double frequency_hz = 100.0;
  static constexpr double dt = 1.0 / frequency_hz;
  for (size_t trial = 0; trial < ntrials; ++trial) {
    CartStatePVA initial;
    CartStatePV target;
    CartesianLimits limits;
    RandomSetup(&initial, &target, &limits);

    StateEnclosure enclosure(dt, target, limits.min_translational_jerk,
                             limits.max_translational_jerk,
                             limits.max_rotational_jerk);

    const std::vector<CartStatePVA> full =
        GetCartesianTrajectory(frequency_hz, initial, limits, target);
    ASSERT_LE(1, full.size());

    // DOF enable bitfield: (tx,ty,tz,rxyz) all combos except all-true (0xf) and
    // all-false (0x0)
    for (int enable = 0x1; enable < 0xf; ++enable) {
      const eigenmath::Vector3b translation_selection(
          enable & 0x8, enable & 0x4, enable & 0x2);
      const bool rotation_selection = enable & 0x1;

      const std::vector<CartStatePVA> partial = GetCartesianTrajectory(
          frequency_hz, initial, limits, target, translation_selection,
          rotation_selection, full.size());
      ASSERT_LE(1, partial.size());
      ASSERT_TRUE(enclosure.Contains(partial.back(), translation_selection,
                                     rotation_selection, false));
    }
  }
}

TEST_F(CartesianPositionReflexxesTest, LogErrorNoAlloc) {
  static constexpr double frequency_hz = 100.0;
  CartStatePVA initial;
  CartStatePV target;
  CartesianLimits limits;
  RandomSetup(&initial, &target, &limits);
  target.velocity[0] =
      10.0 * limits.max_translational_velocity[0];  // make it invalid

  CartesianPositionReflexxes generator(frequency_hz);
  CartStatePVA setpoint;

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();

  ASSERT_TRUE(generator.SetLimits(limits));
  generator.SetTarget(target);
  generator.SetPrevious(initial);
  ASSERT_FALSE(generator.ComputeSetpoint(&setpoint));

  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST_F(CartesianPositionReflexxesTest, LimitErrorChecking) {
  CartesianLimits limits;
  limits.min_translational_position.setConstant(-1.0);
  limits.max_translational_position.setConstant(1.0);
  limits.min_translational_velocity.setConstant(-1.0);
  limits.max_translational_velocity.setConstant(1.0);
  limits.min_translational_acceleration.setConstant(-1.0);
  limits.max_translational_acceleration.setConstant(1.0);
  limits.min_translational_jerk.setConstant(-1.0);
  limits.max_translational_jerk.setConstant(1.0);
  limits.max_rotational_velocity = 1.0;
  limits.max_rotational_acceleration = 1.0;
  limits.max_rotational_jerk = 1.0;

  CartesianPositionReflexxes generator(100.0);

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();

  ASSERT_TRUE(generator.SetLimits(limits));

  limits.min_translational_position[0] = 1.1;
  ASSERT_FALSE(generator.SetLimits(limits));

  limits.min_translational_position[0] = -reflexxes::kMaxPositionalLimit;
  ASSERT_TRUE(generator.SetLimits(limits));

  limits.min_translational_position[0] = -1.01 * reflexxes::kMaxPositionalLimit;
  ASSERT_TRUE(generator.SetLimits(limits));
  limits.min_translational_position[0] = -1.0;
  ASSERT_TRUE(generator.SetLimits(limits));

  for (size_t dof = 0; dof < 3; ++dof) {
    limits.max_translational_position[dof] = reflexxes::kMaxPositionalLimit;
    ASSERT_TRUE(generator.SetLimits(limits));
    limits.max_translational_position[dof] =
        1.01 * reflexxes::kMaxPositionalLimit;
    ASSERT_TRUE(generator.SetLimits(limits));
    limits.max_translational_position[dof] = -1.1;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.max_translational_position[dof] = 1.0;
    ASSERT_TRUE(generator.SetLimits(limits));

    limits.min_translational_velocity[dof] = 0.0;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.min_translational_velocity[dof] = 0.01;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.min_translational_velocity[dof] = -1.0;
    ASSERT_TRUE(generator.SetLimits(limits));

    limits.max_translational_velocity[dof] = 0.0;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.max_translational_velocity[dof] = -0.01;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.max_translational_velocity[dof] = 1.0;
    ASSERT_TRUE(generator.SetLimits(limits));

    limits.min_translational_acceleration[dof] = 0.0;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.min_translational_acceleration[dof] = 0.01;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.min_translational_acceleration[dof] = -1.0;
    ASSERT_TRUE(generator.SetLimits(limits));

    limits.max_translational_acceleration[dof] = 0.0;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.max_translational_acceleration[dof] = -0.01;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.max_translational_acceleration[dof] = 1.0;
    ASSERT_TRUE(generator.SetLimits(limits));

    limits.min_translational_jerk[dof] = 0.0;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.min_translational_jerk[dof] = 0.01;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.min_translational_jerk[dof] = -1.0;
    ASSERT_TRUE(generator.SetLimits(limits));

    limits.max_translational_jerk[dof] = 0.0;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.max_translational_jerk[dof] = -0.01;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.max_translational_jerk[dof] = 1.0;
    ASSERT_TRUE(generator.SetLimits(limits));
  }

  limits.max_rotational_velocity = 0.0;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_rotational_velocity = -0.01;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_rotational_velocity = 1.0;
  ASSERT_TRUE(generator.SetLimits(limits));

  limits.max_rotational_acceleration = 0.0;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_rotational_acceleration = -0.01;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_rotational_acceleration = 1.0;
  ASSERT_TRUE(generator.SetLimits(limits));

  limits.max_rotational_jerk = 0.0;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_rotational_jerk = -0.01;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_rotational_jerk = 1.0;
  ASSERT_TRUE(generator.SetLimits(limits));

  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}
}  // namespace
}  // namespace icon
}  // namespace intrinsic
