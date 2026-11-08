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

#include "intrinsic/icon/control/algorithms/constrained_second_order_virtual_dynamics.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>  // NOLINT
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/eigen_file_export.h"
#include "intrinsic/eigenmath/manifolds.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/rotation_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/control/algorithms/compute_critical_damping.h"
#include "intrinsic/icon/control/algorithms/testing/cartesian_impedance_controller_test_utils.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"
#include "intrinsic/util/eigen.h"

//
// Example how to run test and generate CSV files:
//
// ibazel test
// intrinsic/icon/control/algorithms:constrained_second_order_virtual_dynamics_test
// --test_arg=--alsologtostderr --test_output=streamed --test_arg=--write_to_csv
// --log_root_dir=/tmp/
//
ABSL_FLAG(bool, write_to_csv, false,
          "Set to true to generate *.csv files for debugging.");
ABSL_FLAG(std::string, log_root_dir, "/tmp/",
          "Directory to write log files to.");

namespace intrinsic::icon {
namespace {

using eigenmath::testing::IsApprox;
using ::testing::AllOf;
using ::testing::HasSubstr;

constexpr double kFrequencyHz = 500.0;
constexpr double kSmallTolerance = 1e-6;
constexpr double kDefaultMaxVelocity = 1.0;
constexpr double kDefaultMaxAcceleration = 5.0;
constexpr double kDefaultMaxJerk = 100.0;

// Tolerance how much limit violation we allow for a test to pass. Includes a
// bit of tolerance for numerical errors when computed using finite differences
// from positions.
const double kAccLimitViolationToleranceFactor = 1.01;
const double kJerkLimitViolationToleranceFactor = 1.05;

// A simple sign function to be used in signal generators below.
template <typename T>
int sgn(T val) {
  return (T(0) < val) - (val < T(0));
}

// Generates a random vector with elements in the range [min, max].
eigenmath::VectorNd UniformRandomWithinLimits(absl::BitGenRef gen, int ndof,
                                              double min, double max) {
  eigenmath::VectorNd result(ndof);
  for (int i = 0; i < ndof; ++i) {
    result(i) = absl::Uniform(gen, min, max);
  }
  return result;
}

TEST(ConstrainedCartesianVirtualDynamicsTest, ConstructorWorks) {
  ConstrainedCartesianVirtualDynamics controller(kFrequencyHz);
}

TEST(ConstrainedCartesianVirtualDynamicsTest, FailsIfUninitialized) {
  ConstrainedCartesianVirtualDynamics controller(kFrequencyHz);
  auto params = CartesianImpedanceMinimalTestParams();
  ASSERT_OK(cartesian_impedance::Preprocess(params.cartesian_target));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      auto rt_cartesian_target,
      cartesian_impedance::ToRealTimeCartesianTarget(
          /*base_t_tip=*/Pose3d::Identity(), params.cartesian_target));

  EXPECT_THAT(controller.Step(rt_cartesian_target,
                              /*sensed_wrench_in_tool_frame=*/Wrench::ZERO,
                              /*wrench_deadband_in_tool_frame=*/Wrench::ZERO,
                              /*max_cartesian_velocity=*/1.0,
                              /*max_cartesian_acceleration=*/1.0,
                              /*max_cartesian_jerk=*/1.0),
              icon::RealtimeStatusIs(absl::StatusCode::kFailedPrecondition,
                                     HasSubstr("not initialized")));
}

TEST(ConstrainedCartesianVirtualDynamicsTest, FailsForInvalidCartesianLimits) {
  ConstrainedCartesianVirtualDynamics controller(kFrequencyHz);
  auto params = CartesianImpedanceMinimalTestParams();
  ASSERT_OK(cartesian_impedance::Preprocess(params.cartesian_target));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      auto rt_cartesian_target,
      cartesian_impedance::ToRealTimeCartesianTarget(
          /*base_t_tip=*/Pose3d::Identity(), params.cartesian_target));

  controller.Initialize(Pose3d::Identity(), Twist::ZERO, Acceleration::ZERO);

  {
    EXPECT_THAT(controller.Step(rt_cartesian_target,
                                /*sensed_wrench_in_tool_frame=*/Wrench::ZERO,
                                /*wrench_deadband_in_tool_frame=*/Wrench::ZERO,
                                /*max_cartesian_velocity=*/0.0,
                                /*max_cartesian_acceleration=*/1.0,
                                /*max_cartesian_jerk=*/1.0),
                icon::RealtimeStatusIs(
                    absl::StatusCode::kInvalidArgument,
                    AllOf(HasSubstr("velocity"),
                          HasSubstr("has a negative or close to zero entry"))));
  }
  {
    EXPECT_THAT(controller.Step(rt_cartesian_target,
                                /*sensed_wrench_in_tool_frame=*/Wrench::ZERO,
                                /*wrench_deadband_in_tool_frame=*/Wrench::ZERO,
                                /*max_cartesian_velocity=*/1.0,
                                /*max_cartesian_acceleration=*/0.0,
                                /*max_cartesian_jerk=*/1.0),
                icon::RealtimeStatusIs(
                    absl::StatusCode::kInvalidArgument,
                    AllOf(HasSubstr("acceleration"),
                          HasSubstr("has a negative or close to zero entry"))));
  }
  {
    EXPECT_THAT(controller.Step(rt_cartesian_target,
                                /*sensed_wrench_in_tool_frame=*/Wrench::ZERO,
                                /*wrench_deadband_in_tool_frame=*/Wrench::ZERO,
                                /*max_cartesian_velocity=*/1.0,
                                /*max_cartesian_acceleration=*/1.0,
                                /*max_cartesian_jerk=*/0.0),
                icon::RealtimeStatusIs(
                    absl::StatusCode::kInvalidArgument,
                    AllOf(HasSubstr("jerk"),
                          HasSubstr("has a negative or close to zero entry"))));
  }
}

TEST(ConstrainedCartesianVirtualDynamicsTest,
     CombinationsOfInitializeWorkInSteadyState) {
  ConstrainedCartesianVirtualDynamics controller(kFrequencyHz);

  const Pose3d initial_base_t_target_frame = Pose3d(
      eigenmath::Quaterniond::UnitRandom(), eigenmath::Vector3d::Random());
  const Pose3d previous_base_t_target_reference_pose = Pose3d(
      eigenmath::Quaterniond::UnitRandom(), eigenmath::Vector3d::Random());

  {
    // Initialization in perfect steady state without previous reference pose.
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    controller.Initialize(initial_base_t_target_frame, Twist::ZERO,
                          Acceleration::ZERO);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(controller.q_k_minus_1(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));
    EXPECT_THAT(controller.q_k_minus_2(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));
    EXPECT_THAT(controller.q_k_minus_3(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));
    EXPECT_THAT(controller.p_d_k_minus_1(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));
    EXPECT_THAT(controller.p_d_k_minus_2(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));
  }

  {
    // Initialization in perfect steady state with previous reference pose.
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    controller.Initialize(initial_base_t_target_frame, Twist::ZERO,
                          Acceleration::ZERO,
                          previous_base_t_target_reference_pose);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(controller.q_k_minus_1(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));
    EXPECT_THAT(controller.q_k_minus_2(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));
    EXPECT_THAT(controller.q_k_minus_3(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));
    EXPECT_THAT(
        controller.p_d_k_minus_1(),
        IsApprox(previous_base_t_target_reference_pose, kSmallTolerance));
    EXPECT_THAT(
        controller.p_d_k_minus_2(),
        IsApprox(previous_base_t_target_reference_pose, kSmallTolerance));
  }

  {
    // Initialization in perfect steady state without previous reference pose
    // but with twist.
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    controller.Initialize(
        initial_base_t_target_frame, Twist::ZERO, Acceleration::ZERO,
        /*previous_base_t_tool_desired_pose=*/std::nullopt,
        /*previous_reference_twist_in_tool_frame=*/Twist::ZERO);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(controller.q_k_minus_1(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));
    EXPECT_THAT(controller.q_k_minus_2(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));
    EXPECT_THAT(controller.q_k_minus_3(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));
    EXPECT_THAT(controller.p_d_k_minus_1(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));
    EXPECT_THAT(controller.p_d_k_minus_2(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));
  }

  {
    // Initialization in perfect steady state with previous reference pose and
    // twist.
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    controller.Initialize(
        initial_base_t_target_frame, Twist::ZERO, Acceleration::ZERO,
        previous_base_t_target_reference_pose,
        /*previous_reference_twist_in_tool_frame=*/Twist::ZERO);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(controller.q_k_minus_1(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));
    EXPECT_THAT(controller.q_k_minus_2(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));
    EXPECT_THAT(controller.q_k_minus_3(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));
    EXPECT_THAT(
        controller.p_d_k_minus_1(),
        IsApprox(previous_base_t_target_reference_pose, kSmallTolerance));
    EXPECT_THAT(
        controller.p_d_k_minus_2(),
        IsApprox(previous_base_t_target_reference_pose, kSmallTolerance));
  }
}

TEST(ConstrainedCartesianVirtualDynamicsTest,
     InitializeWorksForConstantVelocityInitState) {
  ConstrainedCartesianVirtualDynamics controller(kFrequencyHz);

  // For the test case to remain intuitive, we restrict it to Cartesian
  // translation only.
  const eigenmath::Vector3d last_commanded_position(1, 2, 3);
  const Pose3d initial_base_t_target_frame(last_commanded_position);
  const Twist last_commanded_twist(0.1, 0.2, -0.3, 0, 0, 0);

  const eigenmath::Vector3d last_commanded_reference(4, 5, 6);
  const Pose3d previous_base_t_target_reference_pose(last_commanded_reference);
  const Twist last_reference_twist(0.1, 0.2, -0.3, 0, 0, 0);

  {
    // Initialization in a constant velocity initial state without acceleration.
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    controller.Initialize(
        initial_base_t_target_frame, last_commanded_twist, Acceleration::ZERO,
        previous_base_t_target_reference_pose, last_reference_twist);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    EXPECT_THAT(controller.q_k_minus_1(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));

    const eigenmath::Vector3d expected_position_command_2_cycles_ago =
        last_commanded_position - last_commanded_twist.head(3) / kFrequencyHz;
    EXPECT_THAT(controller.q_k_minus_2(),
                IsApprox(Pose3d(expected_position_command_2_cycles_ago),
                         kSmallTolerance));

    const eigenmath::Vector3d expected_position_command_3_cycles_ago =
        expected_position_command_2_cycles_ago -
        last_commanded_twist.head(3) / kFrequencyHz;
    EXPECT_THAT(controller.q_k_minus_3(),
                IsApprox(Pose3d(expected_position_command_3_cycles_ago),
                         kSmallTolerance));

    EXPECT_THAT(
        controller.p_d_k_minus_1(),
        IsApprox(previous_base_t_target_reference_pose, kSmallTolerance));

    const eigenmath::Vector3d expected_position_reference_2_cycles_ago =
        last_commanded_reference - last_reference_twist.head(3) / kFrequencyHz;
    EXPECT_THAT(controller.p_d_k_minus_2(),
                IsApprox(Pose3d(expected_position_reference_2_cycles_ago),
                         kSmallTolerance));
  }
}

TEST(ConstrainedCartesianVirtualDynamicsTest,
     InitializeWorksForAcceleratedInitState) {
  ConstrainedCartesianVirtualDynamics controller(kFrequencyHz);

  // For the test case to remain intuitive, we restrict it to Cartesian
  // translation only. We don't need to consider the reference history because
  // it does not hold acceleration.
  const eigenmath::Vector3d last_commanded_position(1, 2, 3);
  const Pose3d initial_base_t_target_frame(last_commanded_position);
  const Twist last_commanded_twist(0.1, 0.2, -0.3, 0, 0, 0);
  const Acceleration last_commanded_acceleration(-3.0, 10.0, -20, 0, 0, 0);

  {
    // Initialization in a constant velocity initial state without acceleration.
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    controller.Initialize(initial_base_t_target_frame, last_commanded_twist,
                          last_commanded_acceleration);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    EXPECT_THAT(controller.q_k_minus_1(),
                IsApprox(initial_base_t_target_frame, kSmallTolerance));

    const eigenmath::Vector3d expected_position_command_2_cycles_ago =
        last_commanded_position - last_commanded_twist.head(3) / kFrequencyHz;
    EXPECT_THAT(controller.q_k_minus_2(),
                IsApprox(Pose3d(expected_position_command_2_cycles_ago),
                         kSmallTolerance));

    // From a = q_k - 2. * in.q_k_minus_1 + in.q_k_minus_2) / (dt_sec)²;
    const eigenmath::Vector3d expected_position_command_3_cycles_ago =
        (last_commanded_acceleration.head(3) / kFrequencyHz) / kFrequencyHz -
        last_commanded_position + 2. * expected_position_command_2_cycles_ago;

    EXPECT_THAT(controller.q_k_minus_3(),
                IsApprox(Pose3d(expected_position_command_3_cycles_ago),
                         kSmallTolerance));
  }
}

ConstrainedVirtualDynamicsNd::Parameters GetValidParameters(int ndof) {
  return ConstrainedVirtualDynamicsNd::Parameters{
      .reference_position = eigenmath::VectorNd::Ones(ndof),
      .reference_velocity = eigenmath::VectorNd::Zero(ndof),
      .feedforward_acceleration = eigenmath::VectorNd::Zero(ndof),
      .reference_force = eigenmath::VectorNd::Zero(ndof),
      .virtual_inertia_inverse =
          5.0 * eigenmath::MatrixNd::Identity(ndof, ndof),
      .stiffness = 100.0 * eigenmath::MatrixNd::Identity(ndof, ndof),
      .damping = ComputeCriticalDamping(5.0, 100.0).value() *
                 eigenmath::MatrixNd::Identity(ndof, ndof),
      .force_selection_matrix = eigenmath::MatrixNd::Identity(ndof, ndof),
  };
}

// Hyperparameters defining a Ndof periodic signal.
struct PeriodicSignalHyperparameters {
  eigenmath::VectorNd amplitude;
  double omega_rad_per_sec = 2.0 * M_PI;
  eigenmath::VectorNd y_offset;
};

// Evaluates a discrete-time sinusoid signal with given amplitude and offset,
// generated from `params` at index `step` with time-step duration
// `time_step_sec`.
eigenmath::VectorNd SinusoidTestSignal(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  CHECK_EQ(params.amplitude.size(), params.y_offset.size());
  const double phi = step * time_step_sec * params.omega_rad_per_sec;
  return params.y_offset + params.amplitude * std::sin(phi);
}

// Returns an alternating signal with the given amplitude and offset generated
// from `params` at index `step` with time-step duration `time_step_sec`.
eigenmath::VectorNd SquareWaveTestSignal(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  CHECK_EQ(params.amplitude.size(), params.y_offset.size());
  return params.y_offset +
         params.amplitude *
             sgn(std::sin(step * time_step_sec * params.omega_rad_per_sec));
}

// A helper struct to pass time-varying input or reference changes to the
// controller rollout.
struct CartesianTimeVaryingControllerInputOrReference {
  // Test case name, used for logging.
  std::string name;
  // Optional reference pose, if not provided, the initial pose will be used as
  // reference pose instead.
  std::optional<Pose3d> base_t_tip_reference;
  // Optional reference twist, if not provided, zero twist will be used as
  // reference twist instead.
  Twist v_ref = Twist::ZERO;
  // Optional measured wrench, if not provided, zero wrench will be used as
  // measured wrench instead.
  Wrench f_measured = Wrench::ZERO;
  // Optional reference wrench, if not provided, zero wrench will be used as
  // reference wrench instead.
  Wrench f_ref = Wrench::ZERO;
};

// Definitions of sinusoid test input or reference signals that can be applied
// to the constrained Cartesian controller during a rollout.
CartesianTimeVaryingControllerInputOrReference GetSinusoidMeasuredWrench(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  return {
      .name = "GetSinusoidMeasuredWrench",
      .f_measured = Wrench(SinusoidTestSignal(step, time_step_sec, params))};
}
CartesianTimeVaryingControllerInputOrReference GetSinusoidReferenceWrench(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  return {.name = "GetSinusoidReferenceWrench",
          .f_ref = Wrench(SinusoidTestSignal(step, time_step_sec, params))};
}
CartesianTimeVaryingControllerInputOrReference GetSinusoidReferenceTwist(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  return {.name = "GetSinusoidReferenceTwist",
          .v_ref = Twist(SinusoidTestSignal(step, time_step_sec, params))};
}
CartesianTimeVaryingControllerInputOrReference GetSinusoidReferencePose(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  return {
      .name = "GetSinusoidReferencePose",
      .base_t_tip_reference = Pose3d(eigenmath::expRiemann(eigenmath::Vector6d(
          SinusoidTestSignal(step, time_step_sec, params))))};
}

// Definitions of square wave test input or reference signals that can be
// applied to the constrained Cartesian controller during a rollout.
CartesianTimeVaryingControllerInputOrReference GetSquareWaveMeasuredWrench(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  return {
      .name = "GetSquareWaveMeasuredWrench",
      .f_measured = Wrench(SquareWaveTestSignal(step, time_step_sec, params))};
}
CartesianTimeVaryingControllerInputOrReference GetSquareWaveReferenceWrench(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  return {.name = "GetSquareWaveReferenceWrench",
          .f_ref = Wrench(SquareWaveTestSignal(step, time_step_sec, params))};
}
CartesianTimeVaryingControllerInputOrReference GetSquareWaveReferenceTwist(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  return {.name = "GetSquareWaveReferenceTwist",
          .v_ref = Twist(SquareWaveTestSignal(step, time_step_sec, params))};
}
CartesianTimeVaryingControllerInputOrReference GetSquareWaveReferencePose(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  return {
      .name = "GetSquareWaveReferencePose",
      .base_t_tip_reference = Pose3d(eigenmath::expRiemann(eigenmath::Vector6d(
          SquareWaveTestSignal(step, time_step_sec, params))))};
}

// Returns a `CartesianTimeVaryingControllerInputOrReference` for the
// constrained Cartesian oscillator for discrete time step `idx` of length
// `time_step_sec` using the given `params`. We use std::function because we
// genuinely need copies of the callable object. See also
// go/totw/191#when-is-stdfunction-appropriate
using CartesianTimeVaryingControllerInputGenerator =
    std::function<CartesianTimeVaryingControllerInputOrReference(
        int idx, double time_step_sec,
        const PeriodicSignalHyperparameters& params)>;

// A results struct that contains the Cartesian rollout trajectory. Twist,
// acceleration and jerk are expressed in the local frames.
struct CartesianRolloutResult {
  std::vector<Pose3d> pose_trajectory;
  std::vector<eigenmath::Vector6d>
      local_twist_trajectory;  // at respective pose
  std::vector<eigenmath::Vector6d>
      local_acceleration_trajectory;                       // at respective pose
  std::vector<eigenmath::Vector6d> local_jerk_trajectory;  // at respective pose
};

struct TimeVaryingControllerInputOrReferenceNd {
  explicit TimeVaryingControllerInputOrReferenceNd(int ndof,
                                                   absl::string_view name)
      : name(name),
        v_ref(eigenmath::VectorNd::Zero(ndof)),
        f_measured(eigenmath::VectorNd::Zero(ndof)),
        f_ref(eigenmath::VectorNd::Zero(ndof)) {}
  // Test case name, used for logging.
  std::string name;
  // Optional reference position, if not provided, the initial position will be
  // used as reference position instead.
  std::optional<eigenmath::VectorNd> p_ref;
  // Optional reference velocity, if not provided, zero velocity will be used as
  // reference velocity instead.
  eigenmath::VectorNd v_ref;
  // Optional measured force, if not provided, zero force will be used as
  // measured force instead.
  eigenmath::VectorNd f_measured;
  // Optional reference force, if not provided, zero force will be used as
  // reference force instead.
  eigenmath::VectorNd f_ref;
};

// Definitions of sinusoid test input or reference signals that can be applied
// to the constrained Cartesian controller during a rollout.
TimeVaryingControllerInputOrReferenceNd GetSinusoidMeasuredForce(
    int ndof, int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  TimeVaryingControllerInputOrReferenceNd result(ndof,
                                                 "GetSinusoidMeasuredForce");
  result.f_measured = SinusoidTestSignal(step, time_step_sec, params);
  return result;
}
TimeVaryingControllerInputOrReferenceNd GetSinusoidReferenceForce(
    int ndof, int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  TimeVaryingControllerInputOrReferenceNd result(ndof,
                                                 "GetSinusoidReferenceForce");
  result.f_ref = SinusoidTestSignal(step, time_step_sec, params);
  return result;
}
TimeVaryingControllerInputOrReferenceNd GetSinusoidReferenceVelocity(
    int ndof, int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  TimeVaryingControllerInputOrReferenceNd result(
      ndof, "GetSinusoidReferenceVelocity");
  result.v_ref = SinusoidTestSignal(step, time_step_sec, params);
  return result;
}
TimeVaryingControllerInputOrReferenceNd GetSinusoidReferencePosition(
    int ndof, int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  TimeVaryingControllerInputOrReferenceNd result(
      ndof, "GetSinusoidReferencePosition");
  result.p_ref =
      eigenmath::VectorNd(SinusoidTestSignal(step, time_step_sec, params));
  return result;
}
// Definitions of sinusoid test input or reference signals that can be applied
// to the constrained Cartesian controller during a rollout.
TimeVaryingControllerInputOrReferenceNd GetSquareWaveMeasuredForce(
    int ndof, int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  TimeVaryingControllerInputOrReferenceNd result(ndof,
                                                 "GetSquareWaveMeasuredForce");
  result.f_measured = SquareWaveTestSignal(step, time_step_sec, params);
  return result;
}
TimeVaryingControllerInputOrReferenceNd GetSquareWaveReferenceForce(
    int ndof, int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  TimeVaryingControllerInputOrReferenceNd result(ndof,
                                                 "GetSquareWaveReferenceForce");
  result.f_ref = SquareWaveTestSignal(step, time_step_sec, params);
  return result;
}
TimeVaryingControllerInputOrReferenceNd GetSquareWaveReferenceVelocity(
    int ndof, int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  TimeVaryingControllerInputOrReferenceNd result(
      ndof, "GetSquareWaveReferenceVelocity");
  result.v_ref = SquareWaveTestSignal(step, time_step_sec, params);
  return result;
}
TimeVaryingControllerInputOrReferenceNd GetSquareWaveReferencePosition(
    int ndof, int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  TimeVaryingControllerInputOrReferenceNd result(
      ndof, "GetSquareWaveReferencePosition");
  result.p_ref =
      eigenmath::VectorNd(SquareWaveTestSignal(step, time_step_sec, params));
  return result;
}

// Returns a `CartesianTimeVaryingControllerInputOrReference` for the
// constrained Cartesian oscillator for discrete time step `idx` of length
// `time_step_sec` using the given `params`. We use std::function because we
// genuinely need copies of the callable object. See also
// go/totw/191#when-is-stdfunction-appropriate
using TimeVaryingControllerInputGeneratorNd =
    std::function<TimeVaryingControllerInputOrReferenceNd(
        int ndof, int idx, double time_step_sec,
        const PeriodicSignalHyperparameters& params)>;

// A results struct that contains the Cartesian rollout trajectory. Twist,
// acceleration and jerk are expressed in the local frames.
struct RolloutResultNd {
  bool satisfies_limits = true;
  std::vector<eigenmath::VectorNd> position_trajectory;
  std::vector<eigenmath::VectorNd> velocity_trajectory;
  std::vector<eigenmath::VectorNd> acceleration_trajectory;
  std::vector<eigenmath::VectorNd> jerk_trajectory;
  std::vector<eigenmath::VectorNd> sensed_forces;
  std::vector<eigenmath::VectorNd> commanded_positions;
  std::vector<eigenmath::VectorNd> commanded_forces;
  std::vector<eigenmath::VectorNd> commanded_velocities;
  eigenmath::VectorXd acceleration_limits;
  eigenmath::VectorXd jerk_limits;
  eigenmath::VectorXd timestamps;
};

// Rolls out the constrained Cartesian dynamics for `num_steps` using the
// given `time_varying_input_generator_function` to generate time-varying input
// signals. The `params` define a periodic signal that is used to generate
// time-varying reference signals. The `virtual_cartesian_stiffness`,
// `virtual_cartesian_inertia` and `selection_matrix` define the virtual
// Cartesian dynamics. The `initial_base_t_target`, `initial_twist` and
// `initial_acceleration` define the initial state of the oscillator. The
// `max_cartesian_twist_in_base_frame`,
// `max_cartesian_acceleration_in_base_frame` and
// `max_cartesian_jerk_in_base_frame` define the Cartesian limits as scalar
// values, so they define the radius of a "limit ball".
// Returns a `CartesianRolloutResult` struct that contains the Cartesian
// trajectory, or an error state in case of a limit violation.
absl::StatusOr<CartesianRolloutResult> RollOutAndTest(
    int num_steps,
    CartesianTimeVaryingControllerInputGenerator
        time_varying_input_generator_function,
    const PeriodicSignalHyperparameters& params,
    const eigenmath::Matrix6d& virtual_cartesian_stiffness,
    const eigenmath::Matrix6d& virtual_cartesian_inertia,
    const eigenmath::Matrix6d& selection_matrix,
    const Pose3d& initial_base_t_target, const Twist& initial_twist,
    const Acceleration& initial_acceleration,
    double max_cartesian_twist_in_base_frame,
    double max_cartesian_acceleration_in_base_frame,
    double max_cartesian_jerk_in_base_frame,
    std::optional<const eigenmath::Matrix6d> virtual_cartesian_damping) {
  ConstrainedCartesianVirtualDynamics controller(kFrequencyHz);
  controller.Initialize(initial_base_t_target, initial_twist,
                        initial_acceleration);

  // Use a constant deadband value throughout the test.
  const Wrench deadband_wrench = Wrench(eigenmath::Vector6d::Constant(0.01));
  CartesianRolloutResult result;
  result.pose_trajectory.reserve(num_steps + 1);
  result.pose_trajectory.push_back(initial_base_t_target);
  result.local_twist_trajectory.reserve(num_steps + 1);
  result.local_twist_trajectory.push_back(initial_twist);
  result.local_acceleration_trajectory.reserve(num_steps + 1);
  result.local_acceleration_trajectory.push_back(initial_acceleration);
  result.local_jerk_trajectory.reserve(num_steps + 1);
  result.local_jerk_trajectory.push_back(Jerk::ZERO);

  CartesianImpedanceParameters impedance_params;
  impedance_params.cartesian_target.cartesian_stiffness =
      virtual_cartesian_stiffness;
  impedance_params.cartesian_target.virtual_cartesian_inertia =
      eigenmath::Matrix6d::Identity();
  impedance_params.cartesian_target.wrench_selection_matrix = selection_matrix;
  // `tip_t_target` is not used in the constrained Cartesian controller, for
  // completeness we just set it Identity.
  impedance_params.cartesian_target.robot_tip_t_robot_tool = Pose3d::Identity();

  // Use existing damping matrix or compute critical damping otherwise.
  impedance_params.cartesian_target.cartesian_damping =
      virtual_cartesian_damping.value_or(*ComputeCriticalDamping(
          impedance_params.cartesian_target.virtual_cartesian_inertia,
          impedance_params.cartesian_target.cartesian_stiffness));

  const Pose3d base_t_tip = initial_base_t_target;
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      auto rt_cartesian_target,
      cartesian_impedance::ToRealTimeCartesianTarget(
          base_t_tip, impedance_params.cartesian_target));

  for (int i = 0; i < num_steps; ++i) {
    // Generate selected time-varying input signals.
    CartesianTimeVaryingControllerInputOrReference time_varying_input =
        time_varying_input_generator_function(i, 1.0 / kFrequencyHz, params);
    // If the reference pose is not provided, use the initial pose as
    // reference.
    if (time_varying_input.base_t_tip_reference.has_value()) {
      rt_cartesian_target.task_t_tool_reference_position =
          time_varying_input.base_t_tip_reference->translation();
      rt_cartesian_target.task_t_tool_reference_orientation =
          time_varying_input.base_t_tip_reference->quaternion();
    } else {
      rt_cartesian_target.task_t_tool_reference_position =
          initial_base_t_target.translation();
      rt_cartesian_target.task_t_tool_reference_orientation =
          initial_base_t_target.quaternion();
    }
    rt_cartesian_target.tool_reference_twist = time_varying_input.v_ref;
    rt_cartesian_target.tool_reference_wrench = time_varying_input.f_ref;

    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        Pose3d new_pose,
        controller.Step(rt_cartesian_target, time_varying_input.f_measured,
                        deadband_wrench, max_cartesian_twist_in_base_frame,
                        max_cartesian_acceleration_in_base_frame,
                        max_cartesian_jerk_in_base_frame));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    // Compute differential state, which is always expressed at the local frame.
    eigenmath::Vector6d local_twist =
        eigenmath::logRiemann(result.pose_trajectory.back(), new_pose) *
        kFrequencyHz;
    eigenmath::Vector6d local_acceleration =
        (local_twist - result.local_twist_trajectory.back()) * kFrequencyHz;
    eigenmath::Vector6d local_jerk =
        (local_acceleration - result.local_acceleration_trajectory.back()) *
        kFrequencyHz;

    if (local_acceleration.head(3).norm() >
        max_cartesian_acceleration_in_base_frame *
            kAccLimitViolationToleranceFactor) {
      return absl::InternalError(absl::StrCat(
          "Violated L2-norm acceleration limit at index ", i,
          " for translation. Acceleration is ",
          ::intrinsic::toString(local_acceleration.transpose()),
          " reference generator was '", time_varying_input.name, "'"));
    }
    if (local_acceleration.tail(3).norm() >
        max_cartesian_acceleration_in_base_frame *
            kAccLimitViolationToleranceFactor) {
      return absl::InternalError(absl::StrCat(
          "Violated L2-norm acceleration limit at index ", i,
          " for rotation. Acceleration is ",
          ::intrinsic::toString(local_acceleration.transpose()),
          " reference generator was '", time_varying_input.name, "'"));
    }
    if (local_jerk.head(3).norm() >
        max_cartesian_jerk_in_base_frame * kJerkLimitViolationToleranceFactor) {
      return absl::InternalError(absl::StrCat(
          "Violated L2-norm jerk limit at index ", i,
          " for translation. Jerk is ",
          ::intrinsic::toString(local_jerk.transpose()),
          " translational norm is ", local_jerk.head(3).norm(),
          " jerk limit is ", max_cartesian_jerk_in_base_frame,
          " reference generator was '", time_varying_input.name, "'"));
    }
    if (local_jerk.tail(3).norm() >
        max_cartesian_jerk_in_base_frame * kJerkLimitViolationToleranceFactor) {
      return absl::InternalError(absl::StrCat(
          "Violated L2-norm jerk limit at index ", i, " for rotation. Jerk is ",
          ::intrinsic::toString(local_jerk.transpose()), " rotational norm is ",
          local_jerk.tail(3).norm(), " jerk limit is ",
          max_cartesian_jerk_in_base_frame, " reference generator was '",
          time_varying_input.name, "'"));
    }

    result.pose_trajectory.push_back(new_pose);
    result.local_twist_trajectory.push_back(local_twist);
    result.local_acceleration_trajectory.push_back(local_acceleration);
    result.local_jerk_trajectory.push_back(local_jerk);
  }

  return result;
}

// Rolls out the constrained Cartesian dynamics for `num_steps` using the
// given `time_varying_input_generator_function` to generate time-varying input
// signals. The `params` define a periodic signal that is used to generate
// time-varying reference signals. The `virtual_cartesian_stiffness`,
// `virtual_cartesian_inertia` and `selection_matrix` define the virtual
// Cartesian dynamics. The `initial_base_t_target`, `initial_twist` and
// `initial_acceleration` define the initial state of the oscillator. The
// `max_cartesian_twist_in_base_frame`,
// `max_cartesian_acceleration_in_base_frame` and
// `max_cartesian_jerk_in_base_frame` define the Cartesian limits as scalar
// values, so they define the radius of a "limit ball".
// Returns a `CartesianRolloutResult` struct that contains the Cartesian
// trajectory, or an error state in case of a limit violation.
absl::StatusOr<RolloutResultNd> RollOutAndTest(
    int ndof, int num_steps,
    TimeVaryingControllerInputGeneratorNd time_varying_input_generator_function,
    const PeriodicSignalHyperparameters& params,
    const eigenmath::MatrixNd& stiffness, const eigenmath::MatrixNd& inertia,
    const eigenmath::MatrixNd& selection_matrix,
    const eigenmath::VectorNd& initial_position,
    const eigenmath::VectorNd& initial_velocity,
    const eigenmath::VectorNd& initial_acceleration,
    absl::Span<const double> max_velocity,
    absl::Span<const double> max_acceleration,
    absl::Span<const double> max_jerk) {
  if (max_velocity.size() != num_steps) {
    return absl::InvalidArgumentError(
        "max_velocity size does not match num_steps");
  }
  if (max_acceleration.size() != num_steps) {
    return absl::InvalidArgumentError(
        "max_acceleration size does not match num_steps");
  }
  if (max_jerk.size() != num_steps) {
    return absl::InvalidArgumentError("max_jerk size does not match num_steps");
  }

  ConstrainedVirtualDynamicsNd controller(ndof, kFrequencyHz);
  controller.Initialize(initial_position, initial_velocity,
                        initial_acceleration);

  // Use a constant deadband value throughout the test.
  const eigenmath::VectorNd deadband_force =
      eigenmath::VectorNd::Constant(ndof, 0.0);
  RolloutResultNd result;
  result.position_trajectory.reserve(num_steps + 1);
  result.position_trajectory.push_back(initial_position);
  result.velocity_trajectory.reserve(num_steps + 1);
  result.velocity_trajectory.push_back(initial_velocity);
  result.acceleration_trajectory.reserve(num_steps + 1);
  result.acceleration_trajectory.push_back(initial_acceleration);
  result.jerk_trajectory.reserve(num_steps + 1);
  result.jerk_trajectory.push_back(eigenmath::VectorNd::Zero(ndof));
  result.sensed_forces.reserve(num_steps);
  result.commanded_positions.reserve(num_steps);
  result.commanded_forces.reserve(num_steps);
  result.commanded_velocities.reserve(num_steps);
  result.acceleration_limits.resize(num_steps);
  result.jerk_limits.resize(num_steps);
  result.timestamps.resize(num_steps + 1);
  result.timestamps(0) = 0.0;

  auto parameters = GetValidParameters(ndof);
  parameters.stiffness = stiffness;
  parameters.virtual_inertia_inverse = inertia.inverse();
  parameters.force_selection_matrix = selection_matrix;
  INTRINSIC_RT_ASSIGN_OR_RETURN(parameters.damping,
                                ComputeCriticalDamping(inertia, stiffness));

  for (int i = 0; i < num_steps; ++i) {
    // Generate selected time-varying input signals.
    TimeVaryingControllerInputOrReferenceNd time_varying_input =
        time_varying_input_generator_function(ndof, i, 1.0 / kFrequencyHz,
                                              params);
    // If the reference pose is not provided, use the initial pose as
    // reference.
    if (time_varying_input.p_ref.has_value()) {
      parameters.reference_position = time_varying_input.p_ref.value();
    } else {
      parameters.reference_position = initial_position;
    }
    parameters.reference_velocity = time_varying_input.v_ref;
    parameters.feedforward_acceleration = eigenmath::VectorNd::Zero(ndof);
    parameters.reference_force = time_varying_input.f_ref;

    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        eigenmath::VectorNd new_position,
        controller.Step(parameters, time_varying_input.f_measured,
                        deadband_force, max_velocity[i], max_acceleration[i],
                        max_jerk[i]));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    // Compute differential state, which is always expressed at the local frame.
    eigenmath::VectorNd velocity =
        (new_position - result.position_trajectory.back()) * kFrequencyHz;
    eigenmath::VectorNd acceleration =
        (velocity - result.velocity_trajectory.back()) * kFrequencyHz;
    eigenmath::VectorNd jerk =
        (acceleration - result.acceleration_trajectory.back()) * kFrequencyHz;

    // TODO(giftthaler): selectively deactivate acceleration limit check if
    // limits are contracting.
    if (acceleration.norm() >
        max_acceleration[i] * kAccLimitViolationToleranceFactor) {
      result.satisfies_limits = false;
      LOG(ERROR) << absl::StrCat(
          "Violated L2-norm acceleration limit at index ", i,
          ". Acceleration is ", ::intrinsic::toString(acceleration.transpose()),
          " reference generator was '", time_varying_input.name, "'");
    }
    if (jerk.norm() > max_jerk[i] * kJerkLimitViolationToleranceFactor) {
      result.satisfies_limits = false;
      LOG(ERROR) << absl::StrCat(
          "Violated L2-norm jerk limit at index ", i, ". Jerk is ",
          ::intrinsic::toString(jerk.transpose()), " norm is ", jerk.norm(),
          " jerk limit is ", max_jerk[i], " reference generator was '",
          time_varying_input.name, "'");
    }

    result.position_trajectory.push_back(new_position);
    result.velocity_trajectory.push_back(velocity);
    result.acceleration_trajectory.push_back(acceleration);
    result.jerk_trajectory.push_back(jerk);
    result.sensed_forces.push_back(time_varying_input.f_measured);
    result.commanded_forces.push_back(time_varying_input.f_ref);
    result.commanded_velocities.push_back(time_varying_input.v_ref);
    result.commanded_positions.push_back(
        time_varying_input.p_ref.value_or(initial_position));
    result.timestamps(i + 1) = static_cast<double>(i + 1) / kFrequencyHz;
    result.acceleration_limits(i) = max_acceleration[i];
    result.jerk_limits(i) = max_jerk[i];
  }

  if (absl::GetFlag(FLAGS_write_to_csv)) {
    const auto test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    if (test_info == nullptr) {
      return absl::InternalError("No test info available.");
    }
    // Create a directory for the test case logs.
    const std::string log_path = absl::StrCat(
        absl::GetFlag(FLAGS_log_root_dir), test_info->test_case_name(), "/",
        test_info->name(), "/",
        time_varying_input_generator_function(ndof, 0, 0, params).name);
    LOG(INFO) << "Writing CSV to " << log_path;
    std::filesystem::create_directories(log_path);

    // Define helper to export a vector to a CSV file.
    auto VectorXdToFile = [log_path](std::string_view filename,
                                     eigenmath::VectorXd& data) {
      eigenmath::MatrixToFile(
          eigenmath::EigenFileExport::CSVFormat(Eigen::FullPrecision),
          absl::StrCat(log_path, "/", filename, ".csv"), data);
    };
    auto StdVectorToFile = [log_path](std::string_view filename,
                                      std::vector<eigenmath::VectorNd>& data) {
      eigenmath::VectorToFile(
          eigenmath::EigenFileExport::CSVFormat(Eigen::FullPrecision),
          absl::StrCat(log_path, "/", filename, ".csv"), data, true);
    };
    StdVectorToFile("position_trajectory", result.position_trajectory);
    StdVectorToFile("acceleration_trajectory", result.acceleration_trajectory);
    StdVectorToFile("jerk_trajectory", result.jerk_trajectory);
    StdVectorToFile("reference_velocity_trajectory",
                    result.commanded_velocities);
    StdVectorToFile("reference_force_trajectory", result.commanded_forces);
    StdVectorToFile("reference_position_trajectory",
                    result.commanded_positions);
    StdVectorToFile("sensed_force_trajectory", result.sensed_forces);
    VectorXdToFile("timestamps", result.timestamps);
    VectorXdToFile("acceleration_limits", result.acceleration_limits);
    VectorXdToFile("jerk_limits", result.jerk_limits);
    // VectorXdToFile("input_p_d_k", log_input_p_d_k);
  }

  return result;
}

// Test that we can rollout the oscillator without error messages.
TEST(VirtualCartesianDynamicsRolloutTest, RolloutDoesNotFail) {
  const int kNumSteps = 100;
  // Test rollout with all reference signal generators.
  for (const auto& input_generator :
       {GetSinusoidMeasuredWrench, GetSinusoidReferenceWrench,
        GetSinusoidReferenceTwist, GetSinusoidReferencePose,
        GetSquareWaveMeasuredWrench, GetSquareWaveReferenceWrench,
        GetSquareWaveReferenceTwist, GetSquareWaveReferencePose}) {
    EXPECT_OK(
        RollOutAndTest(kNumSteps, input_generator,
                       PeriodicSignalHyperparameters{
                           .amplitude = eigenmath::Vector6d::Ones(),
                           .omega_rad_per_sec = 2.0 * M_PI,  // 1 Hz.
                           .y_offset = eigenmath::Vector6d::Zero(),
                       },
                       /*virtual_cartesian_stiffness=*/GenerateSPDMatrix(6),
                       /*virtual_cartesian_inertia=*/GenerateSPDMatrix(6),
                       /*selection_matrix=*/eigenmath::Matrix6d::Identity(),
                       /*initial_base_t_target=*/Pose3d::Identity(),
                       /*initial_twist=*/Twist::ZERO,
                       /*initial_acceleration=*/Acceleration::ZERO,
                       /*max_cartesian_twist_in_base_frame=*/1.0,
                       /*max_cartesian_acceleration_in_base_frame=*/1.0,
                       /*max_cartesian_jerk_in_base_frame=*/1.0,
                       /*virtual_cartesian_damping=*/std::nullopt));
  }
}

TEST(VirtualDynamicsNdRolloutTest, RolloutSticksToLimits) {
  const int kNumSteps = 2500;
  const int kNdof = 2;
  absl::BitGen gen;
  // Test rollout with all reference signal generators.
  for (const auto& input_generator :
       {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
        GetSinusoidReferenceVelocity, GetSinusoidReferencePosition,
        GetSquareWaveMeasuredForce, GetSquareWaveReferenceForce,
        GetSquareWaveReferenceVelocity, GetSquareWaveReferencePosition}) {
    ASSERT_OK_AND_ASSIGN(
        const auto rollout_result,
        RollOutAndTest(
            kNdof, kNumSteps, input_generator,
            PeriodicSignalHyperparameters{
                .amplitude = 20 * eigenmath::VectorNd::Ones(kNdof),
                .omega_rad_per_sec = 2.0 * M_PI,  // 1 Hz.
                .y_offset = eigenmath::VectorNd::Zero(kNdof),
            },
            /*stiffness=*/1000 * eigenmath::MatrixNd::Identity(kNdof, kNdof),
            /*inertia=*/15.0 * eigenmath::MatrixNd::Identity(kNdof, kNdof),
            /*selection_matrix=*/eigenmath::MatrixNd::Identity(kNdof, kNdof),
            /*initial_position=*/
            UniformRandomWithinLimits(gen, kNdof, -0.5, .5),
            /*initial_velocity=*/
            UniformRandomWithinLimits(gen, kNdof, -0.5, .5),
            /*initial_acceleration=*/
            UniformRandomWithinLimits(gen, kNdof, -0.5, .5),
            /*max_velocity=*/std::vector<double>(kNumSteps, 1.0),
            /*max_acceleration=*/std::vector<double>(kNumSteps, 5.0),
            /*max_jerk=*/std::vector<double>(kNumSteps, 20.0)));
    EXPECT_TRUE(rollout_result.satisfies_limits);
  }
}

TEST(VirtualDynamicsNdRolloutTest,
     RolloutSticksToLimitsWithGenericSPDMatrices) {
  const int kNumSteps = 2500;
  const int kNdof = 2;
  absl::BitGen gen;
  // Test rollout with all reference signal generators.
  for (const auto& input_generator :
       {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
        GetSinusoidReferenceVelocity, GetSinusoidReferencePosition,
        GetSquareWaveMeasuredForce, GetSquareWaveReferenceForce,
        GetSquareWaveReferenceVelocity, GetSquareWaveReferencePosition}) {
    ASSERT_OK_AND_ASSIGN(
        const auto rollout_result,
        RollOutAndTest(
            kNdof, kNumSteps, input_generator,
            PeriodicSignalHyperparameters{
                .amplitude = 20 * eigenmath::VectorNd::Ones(kNdof),
                .omega_rad_per_sec = .5 * M_PI,  // 1 Hz.
                .y_offset = eigenmath::VectorNd::Zero(kNdof),
            },
            /*stiffness=*/10 * GenerateSPDMatrix(kNdof),
            /*inertia=*/GenerateSPDMatrix(kNdof),
            /*selection_matrix=*/eigenmath::MatrixNd::Identity(kNdof, kNdof),
            /*initial_position=*/
            UniformRandomWithinLimits(gen, kNdof, -0.5, .5),
            /*initial_velocity=*/
            UniformRandomWithinLimits(gen, kNdof, -0.5, .5),
            /*initial_acceleration=*/
            UniformRandomWithinLimits(gen, kNdof, -0.5, .5),
            /*max_velocity=*/std::vector<double>(kNumSteps, 1.0),
            /*max_acceleration=*/std::vector<double>(kNumSteps, 5.0),
            /*max_jerk=*/std::vector<double>(kNumSteps, 20.0)));
    EXPECT_TRUE(rollout_result.satisfies_limits);
  }
}

TEST(VirtualDynamicsNdRolloutTest,
     RolloutSticksToLimitsWithShrinkingAccelerationLimits) {
  const int kNumSteps = 2500;
  const int kNdof = 2;
  absl::BitGen gen;

  // Design an acceleration limit trajectory that gets linearly smaller.
  std::vector<double> max_acceleration_limits(kNumSteps);
  const int kMaxAcceleration = 5.0;
  const int kMinAcceleration = 1.0;
  for (int i = 0; i < kNumSteps; ++i) {
    const double alpha =
        (1.0 - (static_cast<double>(i) / static_cast<double>(kNumSteps)));
    max_acceleration_limits[i] =
        alpha * kMaxAcceleration + (1.0 - alpha) * kMinAcceleration;
  }

  // Test rollout with all reference signal generators.
  for (const auto& input_generator :
       {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
        GetSinusoidReferenceVelocity, GetSinusoidReferencePosition,
        GetSquareWaveMeasuredForce, GetSquareWaveReferenceForce,
        GetSquareWaveReferenceVelocity, GetSquareWaveReferencePosition}) {
    ASSERT_OK_AND_ASSIGN(
        const auto rollout_result,
        RollOutAndTest(
            kNdof, kNumSteps, input_generator,
            PeriodicSignalHyperparameters{
                .amplitude = 20 * eigenmath::VectorNd::Ones(kNdof),
                .omega_rad_per_sec = .5 * M_PI,  // 1 Hz.
                .y_offset = eigenmath::VectorNd::Zero(kNdof),
            },
            /*stiffness=*/10 * GenerateSPDMatrix(kNdof),
            /*inertia=*/GenerateSPDMatrix(kNdof),
            /*selection_matrix=*/eigenmath::MatrixNd::Identity(kNdof, kNdof),
            /*initial_position=*/
            UniformRandomWithinLimits(gen, kNdof, -0.5, .5),
            /*initial_velocity=*/
            UniformRandomWithinLimits(gen, kNdof, -0.5, .5),
            /*initial_acceleration=*/
            UniformRandomWithinLimits(gen, kNdof, -0.5, .5),
            /*max_velocity=*/std::vector<double>(kNumSteps, 1.0),
            max_acceleration_limits,
            /*max_jerk=*/std::vector<double>(kNumSteps, 20.0)));
    EXPECT_TRUE(rollout_result.satisfies_limits);
  }
}

// A simple system with reference pose input converges to that reference pose.
TEST(VirtualCartesianDynamicsRolloutTest, RolloutConvergesToReferencePose) {
  const Pose3d kTargetPose =
      Pose3d(eigenmath::QuaternionFromRPY(0.3, -0.4, 0.2),
             eigenmath::Vector3d(1, 2, 3));

  const int kNumSteps = 7000;
  ASSERT_OK_AND_ASSIGN(
      auto result,
      RollOutAndTest(
          kNumSteps, GetSinusoidReferencePose,
          // Create a constant pose reference (which is generated from a
          // periodic signal with zero amplitude)
          PeriodicSignalHyperparameters{
              .amplitude = eigenmath::Vector6d::Zero(),
              .y_offset = eigenmath::logRiemann(kTargetPose),
          },
          /*virtual_cartesian_stiffness=*/
          eigenmath::Vector6d{{100, 101, 102, 10, 11, 12}}.asDiagonal(),
          /*virtual_cartesian_inertia=*/
          eigenmath::Vector6d{{6, 5, 4, 3, 2, 1}}.asDiagonal(),
          /*selection_matrix=*/eigenmath::Matrix6d::Identity(),
          /*initial_base_t_target=*/Pose3d::Identity(),
          /*initial_twist=*/Twist::ZERO,
          /*initial_acceleration=*/Acceleration::ZERO,
          /*max_cartesian_twist_in_base_frame=*/kDefaultMaxVelocity,
          /*max_cartesian_acceleration_in_base_frame=*/kDefaultMaxAcceleration,
          /*max_cartesian_jerk_in_base_frame=*/kDefaultMaxJerk,
          /*virtual_cartesian_damping=*/std::nullopt));

  const Pose3d final_pose = result.pose_trajectory.back();
  EXPECT_THAT(final_pose, IsApprox(kTargetPose, 1e-5));
}

TEST(VirtualDynamicsNdRolloutTest, RolloutConvergesToReferencePosition) {
  const int kNumSteps = 3000;
  const int kNdof = 2;
  absl::BitGen gen;
  const eigenmath::Vector2d kTargetPosition =
      eigenmath::Vector2d::Constant(2.0);
  ASSERT_OK_AND_ASSIGN(
      const auto rollout_result,
      RollOutAndTest(
          kNdof, kNumSteps, GetSquareWaveReferencePosition,
          PeriodicSignalHyperparameters{
              .amplitude = eigenmath::VectorNd::Zero(kNdof),
              .y_offset = kTargetPosition,
          },
          /*stiffness=*/1000 * eigenmath::MatrixNd::Identity(kNdof, kNdof),
          /*inertia=*/15.0 * eigenmath::MatrixNd::Identity(kNdof, kNdof),
          /*selection_matrix=*/eigenmath::MatrixNd::Identity(kNdof, kNdof),
          /*initial_position=*/
          UniformRandomWithinLimits(gen, kNdof, -0.5, .5),
          /*initial_velocity=*/
          UniformRandomWithinLimits(gen, kNdof, -0.5, .5),
          /*initial_acceleration=*/
          UniformRandomWithinLimits(gen, kNdof, -0.5, .5),
          /*max_velocity=*/std::vector<double>(kNumSteps, 1.0),
          /*max_acceleration=*/std::vector<double>(kNumSteps, 5.0),
          /*max_jerk=*/std::vector<double>(kNumSteps, 100.0)));
  EXPECT_TRUE(rollout_result.satisfies_limits);
  EXPECT_THAT(rollout_result.position_trajectory.back(),
              IsApprox(kTargetPosition, kSmallTolerance));
}

// TODO(b/369172643): this test illustrates that closed loop motion is currently
// not "homothetic". Different DoFs will go into saturation independently and
// the motion to the target will be nonlinear.
TEST(VirtualDynamicsRolloutTest, DISABLED_ConstrainedMotionIsHomothetic) {
  const Pose3d kTargetPose =
      Pose3d(eigenmath::QuaternionFromRPY(0.3, -0.4, 0.2),
             eigenmath::Vector3d(1, 2, 3));

  // With current settings, after 2500 steps we have roughly covered
  // half of the distance to the target.
  const int kNumSteps = 2500;
  ASSERT_OK_AND_ASSIGN(
      auto result,
      RollOutAndTest(
          kNumSteps, GetSinusoidReferencePose,
          // Create a constant pose reference (which is generated from a
          // periodic signal with zero amplitude)
          PeriodicSignalHyperparameters{
              .amplitude = eigenmath::Vector6d::Zero(),
              .y_offset = eigenmath::logRiemann(kTargetPose),
          },
          /*virtual_cartesian_stiffness=*/
          eigenmath::Vector6d{{100, 101, 102, 10, 11, 12}}.asDiagonal(),
          /*virtual_cartesian_inertia=*/
          eigenmath::Vector6d{{6, 5, 4, 3, 2, 1}}.asDiagonal(),
          /*selection_matrix=*/eigenmath::Matrix6d::Identity(),
          /*initial_base_t_target=*/Pose3d::Identity(),
          /*initial_twist=*/Twist::ZERO,
          /*initial_acceleration=*/Acceleration::ZERO,
          /*max_cartesian_twist_in_base_frame=*/kDefaultMaxVelocity,
          /*max_cartesian_acceleration_in_base_frame=*/kDefaultMaxAcceleration,
          /*max_cartesian_jerk_in_base_frame=*/kDefaultMaxJerk,
          /*virtual_cartesian_damping=*/std::nullopt));

  const Pose3d final_pose = result.pose_trajectory.back();
  // Translations should be roughly collinear.
  EXPECT_LE(final_pose.translation().cross(kTargetPose.translation())(0, 0),
            1e-2);
}

struct VirtualCartesianDynamicsTestCase {
  std::string test_name;
  std::vector<CartesianTimeVaryingControllerInputGenerator>
      input_generator_selection = {GetSinusoidMeasuredWrench};
  PeriodicSignalHyperparameters input_generator_params;
  Pose3d initial_base_t_target = Pose3d::Identity();
  Twist initial_velocity = Twist::ZERO;
  Acceleration initial_acceleration = Acceleration::ZERO;
  eigenmath::Matrix6d virtual_cartesian_stiffness =
      eigenmath::Matrix6d::Identity();
  eigenmath::Matrix6d virtual_cartesian_inertia =
      eigenmath::Matrix6d::Identity();
  // Damping will be auto-derived if not set.
  std::optional<const eigenmath::Matrix6d> virtual_cartesian_damping =
      std::nullopt;
  eigenmath::Matrix6d selection_matrix = eigenmath::Matrix6d::Identity();
  double v_max = kDefaultMaxVelocity;
  double a_max = kDefaultMaxAcceleration;
  double j_max = kDefaultMaxJerk;
};

using VirtualCartesianDynamicsTest =
    ::testing::TestWithParam<VirtualCartesianDynamicsTestCase>;

// A test case in which the limits are held constant over the entire time
// horizon.
TEST_P(VirtualCartesianDynamicsTest, ConstantLimitsTest) {
  const int kNumSteps = 1000;
  for (const auto& input_generator : GetParam().input_generator_selection) {
    // Rollout the oscillator and check its trajectory against
    // test case limits.
    EXPECT_OK(RollOutAndTest(
        kNumSteps, input_generator, GetParam().input_generator_params,
        GetParam().virtual_cartesian_stiffness,
        GetParam().virtual_cartesian_inertia, GetParam().selection_matrix,
        GetParam().initial_base_t_target, GetParam().initial_velocity,
        GetParam().initial_acceleration, GetParam().v_max, GetParam().a_max,
        GetParam().j_max, GetParam().virtual_cartesian_damping));
  }
}

INSTANTIATE_TEST_SUITE_P(
    VirtualCartesianDynamicsTests, VirtualCartesianDynamicsTest,
    ::testing::Values(
        VirtualCartesianDynamicsTestCase{
            .test_name = "DefaultParameterSet",
            .input_generator_selection =
                {GetSinusoidMeasuredWrench, GetSinusoidReferenceWrench,
                 GetSinusoidReferenceTwist, GetSquareWaveMeasuredWrench,
                 GetSquareWaveReferenceWrench, GetSquareWaveReferenceTwist,
                 GetSquareWaveReferencePose, GetSinusoidReferencePose},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 10 * eigenmath::Vector6d::Ones(),
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                    .y_offset = eigenmath::Vector6d::Zero(),
                },
            .virtual_cartesian_stiffness =
                eigenmath::Vector6d{{100, 101, 102, 10, 11, 12}}.asDiagonal(),
            .virtual_cartesian_inertia =
                eigenmath::Vector6d{{6, 5, 4, 3, 2, 1}}.asDiagonal(),
        },
        VirtualCartesianDynamicsTestCase{
            .test_name = "DefaultParameterSetWithSPDMatrices",
            .input_generator_selection =
                {GetSinusoidMeasuredWrench, GetSinusoidReferenceWrench,
                 GetSinusoidReferenceTwist, GetSquareWaveMeasuredWrench,
                 GetSquareWaveReferenceWrench, GetSquareWaveReferenceTwist,
                 GetSquareWaveReferencePose, GetSinusoidReferencePose},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 10 * eigenmath::Vector6d::Ones(),
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                    .y_offset = eigenmath::Vector6d::Zero(),
                },
            .virtual_cartesian_stiffness = GenerateSPDMatrix(6),
            .virtual_cartesian_inertia = GenerateSPDMatrix(6),
        },
        VirtualCartesianDynamicsTestCase{
            .test_name = "NonZeroTipStartingPosition",
            .input_generator_selection =
                {GetSinusoidMeasuredWrench, GetSinusoidReferenceWrench,
                 GetSinusoidReferenceTwist, GetSquareWaveMeasuredWrench,
                 GetSquareWaveReferenceWrench, GetSquareWaveReferenceTwist,
                 GetSquareWaveReferencePose, GetSinusoidReferencePose},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 10 * eigenmath::Vector6d::Ones(),
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                    .y_offset = eigenmath::Vector6d::Zero(),
                },
            .initial_base_t_target =
                Pose3d(eigenmath::Vector3d(eigenmath::Vector3d::Random())),
            .virtual_cartesian_stiffness = GenerateSPDMatrix(6),
            .virtual_cartesian_inertia = GenerateSPDMatrix(6),
        },
        VirtualCartesianDynamicsTestCase{
            .test_name = "NonIdentityTipStartingOrientation",
            .input_generator_selection =
                {GetSinusoidMeasuredWrench, GetSinusoidReferenceWrench,
                 GetSinusoidReferenceTwist, GetSquareWaveMeasuredWrench,
                 GetSquareWaveReferenceWrench, GetSquareWaveReferenceTwist,
                 GetSquareWaveReferencePose, GetSinusoidReferencePose},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 10 * eigenmath::Vector6d::Ones(),
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                    .y_offset = eigenmath::Vector6d::Zero(),
                },
            .initial_base_t_target =
                Pose3d(eigenmath::QuaternionFromRPY(3.0, 0.4, 0.5)),
            .virtual_cartesian_stiffness = GenerateSPDMatrix(6),
            .virtual_cartesian_inertia = GenerateSPDMatrix(6),
        },
        VirtualCartesianDynamicsTestCase{
            .test_name = "NonZeroInitialVelocity",
            .input_generator_selection =
                {GetSinusoidMeasuredWrench, GetSinusoidReferenceWrench,
                 GetSinusoidReferenceTwist, GetSquareWaveMeasuredWrench,
                 GetSquareWaveReferenceWrench, GetSquareWaveReferenceTwist,
                 GetSquareWaveReferencePose, GetSinusoidReferencePose},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 10 * eigenmath::Vector6d::Ones(),
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                    .y_offset = eigenmath::Vector6d::Zero(),
                },
            .initial_base_t_target =
                Pose3d(eigenmath::Quaterniond::UnitRandom(),
                       eigenmath::Vector3d::Random()),
            .initial_velocity =
                Twist(eigenmath::Vector6d::Constant(kDefaultMaxVelocity) * 0.5),
            .initial_acceleration = Acceleration::ZERO,
            .virtual_cartesian_stiffness = GenerateSPDMatrix(6),
            .virtual_cartesian_inertia = GenerateSPDMatrix(6),
        },
        VirtualCartesianDynamicsTestCase{
            .test_name = "NonZeroInitialVelocityAndAcceleration",
            .input_generator_selection =
                {GetSinusoidMeasuredWrench, GetSinusoidReferenceWrench,
                 GetSinusoidReferenceTwist, GetSquareWaveMeasuredWrench,
                 GetSquareWaveReferenceWrench, GetSquareWaveReferenceTwist,
                 GetSquareWaveReferencePose, GetSinusoidReferencePose},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 10 * eigenmath::Vector6d::Ones(),
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                    .y_offset = eigenmath::Vector6d::Zero(),
                },
            .initial_base_t_target =
                Pose3d(eigenmath::Quaterniond::UnitRandom(),
                       eigenmath::Vector3d::Random()),
            .initial_velocity =
                Twist(eigenmath::Vector6d::Constant(kDefaultMaxVelocity) * 0.5),
            .initial_acceleration = Acceleration(
                eigenmath::Vector6d::Constant(kDefaultMaxAcceleration) * 0.5),
            .virtual_cartesian_stiffness = GenerateSPDMatrix(6),
            .virtual_cartesian_inertia = GenerateSPDMatrix(6),
        },
        VirtualCartesianDynamicsTestCase{
            .test_name = "WithPartiallyZeroStiffness",
            .input_generator_selection =
                {GetSinusoidMeasuredWrench, GetSinusoidReferenceWrench,
                 GetSinusoidReferenceTwist, GetSquareWaveMeasuredWrench,
                 GetSquareWaveReferenceWrench, GetSquareWaveReferenceTwist,
                 GetSinusoidReferencePose, GetSquareWaveReferencePose},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 5.0 * eigenmath::Vector6d::Ones(),
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                    .y_offset = eigenmath::Vector6d::Zero(),
                },
            .initial_base_t_target =
                Pose3d(eigenmath::Quaterniond::UnitRandom(),
                       eigenmath::Vector3d::Random()),
            .initial_velocity =
                Twist(eigenmath::Vector6d::Constant(kDefaultMaxVelocity) * 0.5),
            .initial_acceleration = Acceleration(
                eigenmath::Vector6d::Constant(kDefaultMaxAcceleration) * 0.5),
            .virtual_cartesian_stiffness =
                eigenmath::Vector6d{{100, 100, 0, 0, 100, 100}}.asDiagonal(),
            .virtual_cartesian_inertia =
                eigenmath::Vector6d::Constant(5).asDiagonal(),
            // Due to zero stiffness elements we need to explicitly specify
            // damping, use mass and stiffness consistent with the lines above.
            .virtual_cartesian_damping = *ComputeCriticalDamping(
                /*M=*/eigenmath::Vector6d::Constant(5).asDiagonal(),
                /*K=*/eigenmath::Vector6d::Constant(100).asDiagonal()),
        },
        VirtualCartesianDynamicsTestCase{
            .test_name = "ConstantUnitSignals",
            .input_generator_selection =
                {GetSinusoidMeasuredWrench, GetSinusoidReferenceWrench,
                 GetSinusoidReferenceTwist, GetSquareWaveMeasuredWrench,
                 GetSquareWaveReferenceWrench, GetSquareWaveReferenceTwist,
                 GetSquareWaveReferencePose, GetSinusoidReferencePose},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude =
                        eigenmath::Vector6d::Zero(),  // zero amplitude to get
                                                      // constant signal with
                                                      // offset.
                    .y_offset = eigenmath::Vector6d::Ones(),
                },
            .initial_base_t_target =
                Pose3d(eigenmath::Quaterniond::UnitRandom(),
                       eigenmath::Vector3d::Random()),
            .initial_velocity =
                Twist(eigenmath::Vector6d::Constant(kDefaultMaxVelocity) * 0.5),
            .initial_acceleration = Acceleration(
                eigenmath::Vector6d::Constant(kDefaultMaxAcceleration) * 0.5),
            .virtual_cartesian_stiffness = GenerateSPDMatrix(6),
            .virtual_cartesian_inertia = GenerateSPDMatrix(6),
        },
        VirtualCartesianDynamicsTestCase{
            .test_name = "IdentityParameterSet",
            .input_generator_selection =
                {GetSinusoidMeasuredWrench, GetSinusoidReferenceWrench,
                 GetSinusoidReferenceTwist, GetSquareWaveMeasuredWrench,
                 GetSquareWaveReferenceWrench, GetSquareWaveReferenceTwist,
                 GetSquareWaveReferencePose, GetSinusoidReferencePose},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 10 * eigenmath::Vector6d::Ones(),
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                    .y_offset = eigenmath::Vector6d::Zero(),
                },
            .initial_base_t_target = Pose3d(
                eigenmath::Quaterniond(0.982683502048, 0.170905423727,
                                       -0.0107979892609, 0.0707663364254),
                eigenmath::Vector3d{
                    {0.549236373386, -0.0114218153977, 0.591367499923}}),
            .virtual_cartesian_stiffness = eigenmath::Matrix6d::Identity(),
            .virtual_cartesian_inertia = eigenmath::Matrix6d::Identity(),
        }),
    [](const ::testing::TestParamInfo<VirtualCartesianDynamicsTest::ParamType>&
           info) { return info.param.test_name; });

TEST(ConstrainedVirtualDynamicsNdTest, ConstructorWorks) {
  ConstrainedVirtualDynamicsNd controller(/*ndof=*/2, kFrequencyHz);
}

TEST(ConstrainedVirtualDynamicsNdTest, FailsIfUninitialized) {
  ConstrainedVirtualDynamicsNd controller(/*ndof=*/2, kFrequencyHz);

  EXPECT_THAT(controller.Step(GetValidParameters(2),
                              /*sensed_force=*/eigenmath::VectorNd::Zero(2),
                              /*deadband_force=*/eigenmath::VectorNd::Zero(2),
                              /*max_velocity=*/1.0,
                              /*max_acceleration=*/1.0,
                              /*max_jerk=*/1.0),
              icon::RealtimeStatusIs(absl::StatusCode::kFailedPrecondition,
                                     HasSubstr("not initialized")));
}

TEST(ConstrainedVirtualDynamicsNdTest, FailsForInvalidCartesianLimits) {
  ConstrainedVirtualDynamicsNd controller(/*ndof=*/2, kFrequencyHz);

  controller.Initialize(eigenmath::VectorNd::Zero(2),
                        eigenmath::VectorNd::Zero(2),
                        eigenmath::VectorNd::Zero(2));

  {
    EXPECT_THAT(controller.Step(GetValidParameters(2),
                                /*sensed_force=*/eigenmath::VectorNd::Zero(2),
                                /*deadband_force=*/eigenmath::VectorNd::Zero(2),
                                /*max_velocity=*/0.0,
                                /*max_acceleration=*/1.0,
                                /*max_jerk=*/1.0),
                icon::RealtimeStatusIs(
                    absl::StatusCode::kInvalidArgument,
                    AllOf(HasSubstr("velocity"),
                          HasSubstr("has a negative or close to zero entry"))));
  }
  {
    EXPECT_THAT(controller.Step(GetValidParameters(2),
                                /*sensed_force=*/eigenmath::VectorNd::Zero(2),
                                /*deadband_force=*/eigenmath::VectorNd::Zero(2),
                                /*max_velocity=*/1.0,
                                /*max_acceleration=*/0.0,
                                /*max_jerk=*/1.0),
                icon::RealtimeStatusIs(
                    absl::StatusCode::kInvalidArgument,
                    AllOf(HasSubstr("acceleration"),
                          HasSubstr("has a negative or close to zero entry"))));
  }
  {
    EXPECT_THAT(controller.Step(GetValidParameters(2),
                                /*sensed_force=*/eigenmath::VectorNd::Zero(2),
                                /*deadband_force=*/eigenmath::VectorNd::Zero(2),
                                /*max_velocity=*/1.0,
                                /*max_acceleration=*/1.0,
                                /*max_jerk=*/0.0),
                icon::RealtimeStatusIs(
                    absl::StatusCode::kInvalidArgument,
                    AllOf(HasSubstr("jerk"),
                          HasSubstr("has a negative or close to zero entry"))));
  }
}

TEST(ConstrainedVirtualDynamicsNdTest,
     CombinationsOfInitializeWorkInSteadyState) {
  ConstrainedVirtualDynamicsNd controller(/*ndof=*/2, kFrequencyHz);

  const eigenmath::VectorNd initial_position = eigenmath::VectorNd::Random(2);
  const eigenmath::VectorNd previous_reference_position =
      eigenmath::VectorNd::Random(2);

  const eigenmath::VectorNd kZero = eigenmath::VectorNd::Zero(2);

  {
    // Initialization in perfect steady state without previous reference
    // position.
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    controller.Initialize(initial_position, kZero, kZero);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(controller.q_k_minus_1(),
                IsApprox(initial_position, kSmallTolerance));
    EXPECT_THAT(controller.q_k_minus_2(),
                IsApprox(initial_position, kSmallTolerance));
    EXPECT_THAT(controller.q_k_minus_3(),
                IsApprox(initial_position, kSmallTolerance));
    EXPECT_THAT(controller.p_d_k_minus_1(),
                IsApprox(initial_position, kSmallTolerance));
    EXPECT_THAT(controller.p_d_k_minus_2(),
                IsApprox(initial_position, kSmallTolerance));
  }

  {
    // Initialization in perfect steady state with previous reference pose.
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    controller.Initialize(initial_position, kZero, kZero,
                          previous_reference_position);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(controller.q_k_minus_1(),
                IsApprox(initial_position, kSmallTolerance));
    EXPECT_THAT(controller.q_k_minus_2(),
                IsApprox(initial_position, kSmallTolerance));
    EXPECT_THAT(controller.q_k_minus_3(),
                IsApprox(initial_position, kSmallTolerance));
    EXPECT_THAT(controller.p_d_k_minus_1(),
                IsApprox(previous_reference_position, kSmallTolerance));
    EXPECT_THAT(controller.p_d_k_minus_2(),
                IsApprox(previous_reference_position, kSmallTolerance));
  }

  {
    // Initialization in perfect steady state without previous reference
    // position but with twist.
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    controller.Initialize(initial_position, kZero, kZero,
                          /*previous_reference_position=*/std::nullopt,
                          /*previous_reference_velocity=*/kZero);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(controller.q_k_minus_1(),
                IsApprox(initial_position, kSmallTolerance));
    EXPECT_THAT(controller.q_k_minus_2(),
                IsApprox(initial_position, kSmallTolerance));
    EXPECT_THAT(controller.q_k_minus_3(),
                IsApprox(initial_position, kSmallTolerance));
    EXPECT_THAT(controller.p_d_k_minus_1(),
                IsApprox(initial_position, kSmallTolerance));
    EXPECT_THAT(controller.p_d_k_minus_2(),
                IsApprox(initial_position, kSmallTolerance));
  }

  {
    // Initialization in perfect steady state with previous reference pose and
    // twist.
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    controller.Initialize(initial_position, kZero, kZero,
                          previous_reference_position, kZero);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(controller.q_k_minus_1(),
                IsApprox(initial_position, kSmallTolerance));
    EXPECT_THAT(controller.q_k_minus_2(),
                IsApprox(initial_position, kSmallTolerance));
    EXPECT_THAT(controller.q_k_minus_3(),
                IsApprox(initial_position, kSmallTolerance));
    EXPECT_THAT(controller.p_d_k_minus_1(),
                IsApprox(previous_reference_position, kSmallTolerance));
    EXPECT_THAT(controller.p_d_k_minus_2(),
                IsApprox(previous_reference_position, kSmallTolerance));
  }
}

TEST(ConstrainedVirtualDynamicsNdTest,
     InitializeWorksForConstantVelocityInitState) {
  ConstrainedVirtualDynamicsNd controller(/*ndof=*/2, kFrequencyHz);

  const eigenmath::VectorNd initial_position = eigenmath::VectorNd::Random(2);
  const eigenmath::VectorNd initial_velocity = eigenmath::VectorNd::Random(2);
  const eigenmath::VectorNd previous_reference_position =
      eigenmath::VectorNd::Random(2);
  const eigenmath::VectorNd previous_reference_velocity =
      eigenmath::VectorNd::Random(2);
  const eigenmath::VectorNd kZero = eigenmath::VectorNd::Zero(2);

  {
    // Initialization in a constant velocity initial state without acceleration.
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    controller.Initialize(initial_position, initial_velocity, kZero,
                          previous_reference_position,
                          previous_reference_velocity);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    EXPECT_THAT(controller.q_k_minus_1(),
                IsApprox(initial_position, kSmallTolerance));

    const eigenmath::VectorNd expected_position_command_2_cycles_ago =
        initial_position - initial_velocity / kFrequencyHz;
    EXPECT_THAT(
        controller.q_k_minus_2(),
        IsApprox(expected_position_command_2_cycles_ago, kSmallTolerance));

    const eigenmath::VectorNd expected_position_command_3_cycles_ago =
        expected_position_command_2_cycles_ago -
        initial_velocity / kFrequencyHz;
    EXPECT_THAT(
        controller.q_k_minus_3(),
        IsApprox(expected_position_command_3_cycles_ago, kSmallTolerance));

    EXPECT_THAT(controller.p_d_k_minus_1(),
                IsApprox(previous_reference_position, kSmallTolerance));

    const eigenmath::VectorNd expected_position_reference_2_cycles_ago =
        previous_reference_position -
        previous_reference_velocity / kFrequencyHz;
    EXPECT_THAT(
        controller.p_d_k_minus_2(),
        IsApprox(expected_position_reference_2_cycles_ago, kSmallTolerance));
  }
}

TEST(ConstrainedVirtualDynamicsNdTest, InitializeWorksForAcceleratedInitState) {
  ConstrainedVirtualDynamicsNd controller(/*ndof=*/2, kFrequencyHz);

  // For the test case to remain intuitive, we restrict it to Cartesian
  // translation only. We don't need to consider the reference history because
  // it does not hold acceleration.
  const eigenmath::Vector2d last_commanded_position(1, 2);
  const eigenmath::Vector2d last_commanded_twist(0.1, 0.2);
  const eigenmath::Vector2d last_commanded_acceleration(-3.0, 10.0);

  {
    // Initialization in a constant velocity initial state without acceleration.
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    controller.Initialize(last_commanded_position, last_commanded_twist,
                          last_commanded_acceleration);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    EXPECT_THAT(controller.q_k_minus_1(),
                IsApprox(last_commanded_position, kSmallTolerance));

    const eigenmath::VectorNd expected_position_command_2_cycles_ago =
        last_commanded_position - last_commanded_twist / kFrequencyHz;
    EXPECT_THAT(
        controller.q_k_minus_2(),
        IsApprox(expected_position_command_2_cycles_ago, kSmallTolerance));

    // From a = q_k - 2. * in.q_k_minus_1 + in.q_k_minus_2) / (dt_sec)²;
    const eigenmath::VectorNd expected_position_command_3_cycles_ago =
        (last_commanded_acceleration / kFrequencyHz) / kFrequencyHz -
        last_commanded_position + 2. * expected_position_command_2_cycles_ago;

    EXPECT_THAT(
        controller.q_k_minus_3(),
        IsApprox(expected_position_command_3_cycles_ago, kSmallTolerance));
  }
}

}  // namespace
}  // namespace intrinsic::icon
