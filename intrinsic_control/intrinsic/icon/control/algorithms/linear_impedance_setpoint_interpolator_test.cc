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

#include "intrinsic/icon/control/algorithms/linear_impedance_setpoint_interpolator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/rotation_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/actions/impedance_trajectory_generators.pb.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/control/algorithms/testing/cartesian_impedance_controller_test_utils.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/to_fixed_string.h"
#include "intrinsic/math/ipow.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"
#include "intrinsic/util/proto_time.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using eigenmath::testing::IsApprox;
using ::intrinsic::icon::RealtimeStatusIs;
using ::protobuf_matchers::EqualsProto;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Not;

const int kDummyNdof = 6;
const double kSmallTolerance = 1.0e-6;

MATCHER_P2(RtCartesianTargetIsApprox, expected, tolerance,
           absl::StrCat(
               "RealtimeCartesianTargetis approximately equal with tolerance ",
               tolerance)) {
  if (!expected.robot_tip_t_robot_tool.isApprox(arg.robot_tip_t_robot_tool,
                                                tolerance)) {
    *result_listener << "\n robot_tip_t_robot_tool differ, have "
                     << std::string_view(eigenmath::ToFixedString(
                            arg.robot_tip_t_robot_tool))
                     << "vs \n"
                     << std::string_view(eigenmath::ToFixedString(
                            expected.robot_tip_t_robot_tool));
    return false;
  }
  if (!expected.task_t_tool_reference_position.isApprox(
          arg.task_t_tool_reference_position, tolerance)) {
    *result_listener << "\n task_t_tool_reference_position differ, have "
                     << std::string_view(eigenmath::ToFixedString(
                            arg.task_t_tool_reference_position))
                     << "vs \n"
                     << std::string_view(eigenmath::ToFixedString(
                            expected.task_t_tool_reference_position));
    return false;
  }
  if (!expected.task_t_tool_reference_orientation.isApprox(
          arg.task_t_tool_reference_orientation, tolerance)) {
    *result_listener << "\n task_t_tool_reference_orientation differ, have "
                     << std::string_view(eigenmath::ToFixedString(
                            arg.task_t_tool_reference_orientation))
                     << "vs \n"
                     << std::string_view(eigenmath::ToFixedString(
                            expected.task_t_tool_reference_orientation));
    return false;
  }
  if (!expected.tool_reference_acceleration.isApprox(
          arg.tool_reference_acceleration, tolerance)) {
    *result_listener << "\n tool_reference_acceleration differ, have "
                     << std::string_view(eigenmath::ToFixedString(
                            arg.tool_reference_acceleration))
                     << "vs \n"
                     << std::string_view(eigenmath::ToFixedString(
                            expected.tool_reference_acceleration));
    return false;
  }
  if (!expected.tool_reference_twist.isApprox(arg.tool_reference_twist,
                                              tolerance)) {
    *result_listener << "\n tool_reference_twist differ, have "
                     << std::string_view(
                            eigenmath::ToFixedString(arg.tool_reference_twist))
                     << "vs \n"
                     << std::string_view(eigenmath::ToFixedString(
                            expected.tool_reference_twist));
    return false;
  }
  if (!expected.tool_reference_wrench.isApprox(arg.tool_reference_wrench,
                                               tolerance)) {
    *result_listener << "\n tool_reference_wrench differ, have "
                     << std::string_view(
                            eigenmath::ToFixedString(arg.tool_reference_wrench))
                     << "vs \n"
                     << std::string_view(eigenmath::ToFixedString(
                            expected.tool_reference_wrench));
    return false;
  }
  if (!expected.wrench_selection_matrix.isApprox(arg.wrench_selection_matrix,
                                                 tolerance)) {
    *result_listener << "\n wrench_selection_matrix differ";
    return false;
  }
  if (!expected.cartesian_damping.isApprox(arg.cartesian_damping, tolerance)) {
    *result_listener << "\n cartesian_damping differ";
    return false;
  }
  if (!expected.cartesian_stiffness.isApprox(arg.cartesian_stiffness,
                                             tolerance)) {
    *result_listener << "\n cartesian_stiffness differ";
    return false;
  }
  if (!expected.virtual_cartesian_inertia_inverse.isApprox(
          arg.virtual_cartesian_inertia_inverse, tolerance)) {
    *result_listener << "\n virtual_cartesian_inertia_inverse differ";
    return false;
  }
  return true;
}

TEST(LinearImpedanceSetpointInterpolatorTest, CustomMatcherWorks) {
  auto complete_params =
      CartesianImpedanceCompleteTestParams(kDummyNdof).cartesian_target;
  auto minimal_params = CartesianImpedanceMinimalTestParams().cartesian_target;
  ASSERT_OK(cartesian_impedance::Preprocess(complete_params));
  ASSERT_OK(cartesian_impedance::Preprocess(minimal_params));

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      auto rt_params_complete, cartesian_impedance::ToRealTimeCartesianTarget(
                                   Pose3d::Identity(), complete_params));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      auto rt_params_minimal, cartesian_impedance::ToRealTimeCartesianTarget(
                                  Pose3d::Identity(), minimal_params));

  // Matcher matches identical params.
  EXPECT_THAT(rt_params_complete,
              RtCartesianTargetIsApprox(rt_params_complete, kSmallTolerance));
  // Matcher fails for different params.
  EXPECT_THAT(rt_params_complete, Not(RtCartesianTargetIsApprox(
                                      rt_params_minimal, kSmallTolerance)));
}

TEST(LinearImpedanceSetpointInterpolatorTest, FailsForEmptyData) {
  EXPECT_THAT(LinearCartesianImpedanceSetpointInterpolator::Create(
                  {}, absl::Duration(absl::Seconds(1))),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("empty")));
}

TEST(LinearImpedanceSetpointInterpolatorTest, FailsForInconsistentInputSizes) {
  std::vector<cartesian_impedance::CartesianTarget> impedance_waypoints(
      2, CartesianImpedanceCompleteTestParams(kDummyNdof).cartesian_target);
  EXPECT_THAT(LinearCartesianImpedanceSetpointInterpolator::Create(
                  {
                      .time_stamps = {absl::ZeroDuration()},
                      .impedance_waypoints = impedance_waypoints,
                  },
                  /*interpolation_time_step=*/absl::Duration(absl::Seconds(1))),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("must have the same size")));
}

TEST(LinearImpedanceSetpointInterpolatorTest, FailsForNegativeTimestep) {
  std::vector<cartesian_impedance::CartesianTarget> cartesian_target = {
      CartesianImpedanceCompleteTestParams(kDummyNdof).cartesian_target};
  EXPECT_THAT(
      LinearCartesianImpedanceSetpointInterpolator::Create(
          {
              .time_stamps = {absl::ZeroDuration()},
              .impedance_waypoints = cartesian_target,
          },
          /*interpolation_time_step=*/absl::Duration(absl::Seconds(-1))),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Interpolation time step must be positive")));
}

TEST(LinearImpedanceSetpointInterpolatorTest,
     FailsIfTimestampDoesNotStartAtZero) {
  std::vector<cartesian_impedance::CartesianTarget> cartesian_target = {
      CartesianImpedanceCompleteTestParams(kDummyNdof).cartesian_target};
  EXPECT_THAT(LinearCartesianImpedanceSetpointInterpolator::Create(
                  {
                      .time_stamps = {absl::Seconds(-1)},
                      .impedance_waypoints = cartesian_target,
                  },
                  /*interpolation_time_step=*/absl::Duration(absl::Seconds(1))),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("must start at zero")));
}

TEST(LinearImpedanceSetpointInterpolatorTest,
     FailsIfTimestampsAreNotIncreasing) {
  std::vector<cartesian_impedance::CartesianTarget> cartesian_target(
      2, CartesianImpedanceCompleteTestParams(kDummyNdof).cartesian_target);
  EXPECT_THAT(
      LinearCartesianImpedanceSetpointInterpolator::Create(
          {
              .time_stamps = {absl::Seconds(0.0), absl::Seconds(0.0)},
              .impedance_waypoints = cartesian_target,
          },
          /*interpolation_time_step=*/absl::Duration(absl::Seconds(1))),
      StatusIs(
          absl::StatusCode::kFailedPrecondition,
          HasSubstr("Time stamps must be strictly monotonically increasing.")));
}

TEST(LinearImpedanceSetpointInterpolatorTest,
     InterpolationFailsForInvalidSpeedOverride) {
  std::vector<cartesian_impedance::CartesianTarget> single_cartesian_target = {
      CartesianImpedanceCompleteTestParams(kDummyNdof).cartesian_target};
  ASSERT_OK_AND_ASSIGN(
      auto interpolator,
      LinearCartesianImpedanceSetpointInterpolator::Create(
          {
              .time_stamps = {absl::ZeroDuration()},
              .impedance_waypoints = single_cartesian_target,
          },
          /*interpolation_time_step=*/absl::Duration(absl::Seconds(1.0))));

  CartStatePVA kUnusedPreviousCartState;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(interpolator->Initialize(kUnusedPreviousCartState));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  EXPECT_THAT(interpolator->Step(/*speed_override=*/-0.01),
              RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                               HasSubstr("override must be in [0, 1]")));

  EXPECT_THAT(interpolator->Step(/*speed_override=*/1.01),
              RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                               HasSubstr("override must be in [0, 1]")));
}

TEST(LinearImpedanceSetpointInterpolatorTest, GetInitialRobotTipTRobotTool) {
  std::vector<cartesian_impedance::CartesianTarget> single_cartesian_target = {
      CartesianImpedanceCompleteTestParams(kDummyNdof).cartesian_target};
  single_cartesian_target[0].robot_tip_t_robot_tool.setQuaternion(
      eigenmath::Quaterniond::UnitRandom());
  ASSERT_OK_AND_ASSIGN(
      auto interpolator,
      LinearCartesianImpedanceSetpointInterpolator::Create(
          {
              .time_stamps = {absl::ZeroDuration()},
              .impedance_waypoints = single_cartesian_target,
          },
          /*interpolation_time_step=*/absl::Duration(absl::Seconds(1.0))));

  EXPECT_THAT(interpolator->GetInitialRobotTipTRobotTool(),
              IsApprox(single_cartesian_target[0].robot_tip_t_robot_tool,
                       kSmallTolerance));
}

TEST(LinearImpedanceSetpointInterpolatorTest, WorksForVectorsWithOneElement) {
  std::vector<cartesian_impedance::CartesianTarget> single_cartesian_target = {
      CartesianImpedanceCompleteTestParams(kDummyNdof).cartesian_target};
  ASSERT_OK_AND_ASSIGN(
      auto interpolator,
      LinearCartesianImpedanceSetpointInterpolator::Create(
          {
              .time_stamps = {absl::ZeroDuration()},
              .impedance_waypoints = single_cartesian_target,
          },
          /*interpolation_time_step=*/absl::Duration(absl::Seconds(1.0))));

  CartStatePVA kUnusedPreviousCartState;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(interpolator->Initialize(kUnusedPreviousCartState));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(const auto constant_impedance,
                                    interpolator->Step(/*speed_override=*/1.0));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  // Returns constant output regardless of speed override.
  const int kNumSteps = 10;
  for (int i = 0; i < kNumSteps; ++i) {
    // Always call with same speed override.
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        const auto output, interpolator->Step(/*speed_override=*/1.0));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(output,
                RtCartesianTargetIsApprox(constant_impedance, kSmallTolerance));
    EXPECT_NEAR(interpolator->GetPhase(), 1.0, kSmallTolerance);
  }
}

TEST(LinearImpedanceSetpointInterpolatorTest,
     ScalesTwistAndAccelerationWithSpeedOverride) {
  std::vector<cartesian_impedance::CartesianTarget> single_cartesian_target = {
      CartesianImpedanceCompleteTestParams(kDummyNdof).cartesian_target};
  ASSERT_OK_AND_ASSIGN(
      auto interpolator,
      LinearCartesianImpedanceSetpointInterpolator::Create(
          {
              .time_stamps = {absl::ZeroDuration()},
              .impedance_waypoints = single_cartesian_target,
          },
          /*interpolation_time_step=*/absl::Duration(absl::Seconds(1.0))));

  CartStatePVA kUnusedPreviousCartState;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(interpolator->Initialize(kUnusedPreviousCartState));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(auto constant_impedance,
                                    interpolator->Step(/*speed_override=*/1.0));
  // Returns constant output regardless of speed override.
  const int kNumSteps = 100;
  for (int i = 1; i < kNumSteps; ++i) {
    const double speed_override = 1.0 / static_cast<double>(i);
    SCOPED_TRACE(absl::StrCat("Run with speed override ", speed_override));
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(const auto output,
                                      interpolator->Step(speed_override));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(
        output.tool_reference_twist,
        IsApprox(constant_impedance.tool_reference_twist * speed_override));
    EXPECT_THAT(output.tool_reference_acceleration,
                IsApprox(constant_impedance.tool_reference_acceleration *
                         ::intrinsic::IPow(speed_override, 2)));
    EXPECT_NEAR(interpolator->GetPhase(), 1.0, kSmallTolerance);
  }
}

std::vector<cartesian_impedance::CartesianTarget> GetThreeImpedanceWaypoints() {
  return {
      // Waypoint 1
      cartesian_impedance::CartesianTarget{
          .robot_tip_t_robot_tool =
              Pose3d(eigenmath::QuaternionFromRPY(0.1, 0.0, 0.0),
                     eigenmath::Vector3d(.1, 0.0, 0.0)),
          .task_t_tool_reference_pose =
              Pose3d(eigenmath::QuaternionFromRPY(1.0, 0.0, 0.0),
                     eigenmath::Vector3d(1.0, 0.0, 0.0)),
          .cartesian_stiffness = 100.0 * eigenmath::Matrix6d::Identity(),
          .virtual_cartesian_inertia = eigenmath::Matrix6d::Identity(),
          .cartesian_damping = eigenmath::Matrix6d::Identity(),
          .wrench_selection_matrix = eigenmath::Matrix6d::Identity(),
          .tool_reference_twist = Twist::ZERO,
          .tool_reference_wrench = Wrench::ZERO,
          .tool_reference_acceleration = Acceleration::ZERO,
      },
      // Waypoint 2
      cartesian_impedance::CartesianTarget{
          .robot_tip_t_robot_tool =
              Pose3d(eigenmath::QuaternionFromRPY(0.2, 0.0, 0.0),
                     eigenmath::Vector3d(.2, 0.0, 0.0)),
          .task_t_tool_reference_pose =
              Pose3d(eigenmath::QuaternionFromRPY(2.0, 0.0, 0.0),
                     eigenmath::Vector3d(2.0, 0.0, 0.0)),
          .cartesian_stiffness = 200.0 * eigenmath::Matrix6d::Identity(),
          .virtual_cartesian_inertia = 3.0 * eigenmath::Matrix6d::Identity(),
          .cartesian_damping = 4.0 * eigenmath::Matrix6d::Identity(),
          .wrench_selection_matrix =
              eigenmath::Vector6d{0, 0, 1, 0, 0, 0}.asDiagonal(),
          .tool_reference_twist = Twist(2.0 * eigenmath::Vector6d::Ones()),
          .tool_reference_wrench = Wrench(3.0 * eigenmath::Vector6d::Ones()),
          .tool_reference_acceleration =
              Acceleration(4.0 * eigenmath::Vector6d::Ones()),
      },
      // Waypoint 3
      cartesian_impedance::CartesianTarget{
          .robot_tip_t_robot_tool =
              Pose3d(eigenmath::QuaternionFromRPY(0.3, 0.0, 0.0),
                     eigenmath::Vector3d(.3, 0.0, 0.0)),
          .task_t_tool_reference_pose =
              Pose3d(eigenmath::QuaternionFromRPY(3.0, 0.0, 0.0),
                     eigenmath::Vector3d(3.0, 0.0, 0.0)),
          .cartesian_stiffness = 400.0 * eigenmath::Matrix6d::Identity(),
          .virtual_cartesian_inertia = 6 * eigenmath::Matrix6d::Identity(),
          .cartesian_damping = 8.0 * eigenmath::Matrix6d::Identity(),
          .wrench_selection_matrix =
              eigenmath::Vector6d{0, 1, 0, 0, 0, 0}.asDiagonal(),
          .tool_reference_twist = Twist(4.0 * eigenmath::Vector6d::Ones()),
          .tool_reference_wrench = Wrench(6.0 * eigenmath::Vector6d::Ones()),
          .tool_reference_acceleration =
              Acceleration(8.0 * eigenmath::Vector6d::Ones()),
      },
  };
}

TEST(LinearImpedanceSetpointInterpolatorTest, LinearInterpolationWorks) {
  std::vector<cartesian_impedance::CartesianTarget> cartesian_targets =
      GetThreeImpedanceWaypoints();
  ASSERT_OK_AND_ASSIGN(
      auto interpolator,
      LinearCartesianImpedanceSetpointInterpolator::Create(
          {
              .time_stamps = {absl::ZeroDuration(), absl::Seconds(1.0),
                              absl::Seconds(2.0)},
              .impedance_waypoints = cartesian_targets,
          },
          /*interpolation_time_step=*/absl::Duration(absl::Seconds(0.5))));

  CartStatePVA kUnusedPreviousCartState;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(interpolator->Initialize(kUnusedPreviousCartState));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  for (auto& cartesian_target : cartesian_targets) {
    ASSERT_OK(cartesian_impedance::Preprocess(cartesian_target));
  }

  std::vector<cartesian_impedance::RealTimeCartesianTarget>
      rt_cartesian_targets;
  rt_cartesian_targets.reserve(cartesian_targets.size());
  for (const auto& cartesian_target : cartesian_targets) {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        auto rt_params, cartesian_impedance::ToRealTimeCartesianTarget(
                            Pose3d::Identity(), cartesian_target));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    rt_cartesian_targets.push_back(rt_params);
  }

  const int kNumSteps = 4;
  for (int i = 0; i < kNumSteps; ++i) {
    SCOPED_TRACE(absl::StrCat("Run step ", i));
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        const auto output, interpolator->Step(/*speed_override=*/1.0));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    if (i % 2 == 1) {
      // Hit a waypoint exactly.
      EXPECT_THAT(output,
                  RtCartesianTargetIsApprox(rt_cartesian_targets[(i + 1) / 2],
                                            kSmallTolerance));
    } else {
      // Hit exactly the middle between two waypoints, check against linear
      // interpolation.
      const int lhs_idx = (i + 1) / 2;
      const int rhs_idx = (i + 2) / 2;
      EXPECT_THAT(
          output.tool_reference_twist,
          IsApprox(0.5 * (rt_cartesian_targets[lhs_idx].tool_reference_twist +
                          rt_cartesian_targets[rhs_idx].tool_reference_twist)));
      EXPECT_THAT(
          output.tool_reference_acceleration,
          IsApprox(
              0.5 *
              (rt_cartesian_targets[lhs_idx].tool_reference_acceleration +
               rt_cartesian_targets[rhs_idx].tool_reference_acceleration)));
      EXPECT_THAT(
          output.tool_reference_wrench,
          IsApprox(0.5 *
                   (rt_cartesian_targets[lhs_idx].tool_reference_wrench +
                    rt_cartesian_targets[rhs_idx].tool_reference_wrench)));
      // Selection matrix is interpolated in ZOH fashion.
      EXPECT_THAT(
          output.wrench_selection_matrix,
          IsApprox(rt_cartesian_targets[lhs_idx].wrench_selection_matrix));
    }
    EXPECT_NEAR(interpolator->GetPhase(),
                static_cast<double>(i + 1) / static_cast<double>(kNumSteps),
                kSmallTolerance);
  }
  EXPECT_NEAR(interpolator->GetPhase(), 1.0, kSmallTolerance);

  // Any additional step will return the last waypoint and phase 1.0.
  for (int i = 0; i < 10; ++i) {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        const auto output, interpolator->Step(/*speed_override=*/1.0));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(output, RtCartesianTargetIsApprox(rt_cartesian_targets.back(),
                                                  kSmallTolerance));
    EXPECT_NEAR(interpolator->GetPhase(), 1.0, kSmallTolerance);
  }
}

TEST(LinearImpedanceSetpointInterpolatorTest,
     FromProtoFailsForInconsistentSizes) {
  const int kDummyNdof = 6;
  const auto cartesian_target =
      CartesianImpedanceCompleteTestParams(kDummyNdof).cartesian_target;
  const auto cartesian_target_proto = ToProto(cartesian_target);

  intrinsic_proto::icon::actions::proto::LinearImpedanceSetpointInterpolator
      interpolator_proto;
  *interpolator_proto.add_cartesian_impedance_waypoints() =
      cartesian_target_proto;
  *interpolator_proto.mutable_time_stamps()->Add() =
      *FromAbslDuration(absl::ZeroDuration());
  *interpolator_proto.mutable_time_stamps()->Add() =
      *FromAbslDuration(absl::Seconds(2));

  EXPECT_THAT(FromProto(interpolator_proto),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("sizes do not match")));
}

TEST(LinearImpedanceSetpointInterpolatorTest,
     ToProtoFailsForInconsistentSizes) {
  const int kDummyNdof = 6;
  const auto cartesian_target =
      CartesianImpedanceCompleteTestParams(kDummyNdof).cartesian_target;
  ImpedanceTrajectory trajectory;
  trajectory.time_stamps = {absl::ZeroDuration(), absl::Seconds(2)};
  trajectory.impedance_waypoints.push_back(cartesian_target);
  EXPECT_THAT(ToProto(trajectory), StatusIs(absl::StatusCode::kInvalidArgument,
                                            HasSubstr("sizes do not match")));
}

TEST(LinearImpedanceSetpointInterpolatorTest,
     ProtoConversionWorksForOneWaypoint) {
  const int kDummyNdof = 6;
  const auto cartesian_target =
      CartesianImpedanceCompleteTestParams(kDummyNdof).cartesian_target;
  const auto cartesian_target_proto = ToProto(cartesian_target);

  intrinsic_proto::icon::actions::proto::LinearImpedanceSetpointInterpolator
      interpolator_proto;
  *interpolator_proto.add_cartesian_impedance_waypoints() =
      cartesian_target_proto;
  *interpolator_proto.mutable_time_stamps()->Add() =
      *FromAbslDuration(absl::ZeroDuration());

  ASSERT_OK_AND_ASSIGN(const auto params_out, FromProto(interpolator_proto));
  ASSERT_OK_AND_ASSIGN(const auto proto_reconverted, ToProto(params_out));
  EXPECT_THAT(proto_reconverted, EqualsProto(interpolator_proto));
}

TEST(LinearImpedanceSetpointInterpolatorTest,
     ProtoConversionWorksForMultipleWaypoints) {
  auto waypoints = GetThreeImpedanceWaypoints();
  const std::vector<absl::Duration> time_stamps = {
      absl::ZeroDuration(), absl::Seconds(1), absl::Seconds(2)};

  ImpedanceTrajectory trajectory;
  trajectory.time_stamps = time_stamps;
  trajectory.impedance_waypoints = waypoints;

  ASSERT_OK_AND_ASSIGN(const auto proto, ToProto(trajectory));
  ASSERT_OK_AND_ASSIGN(auto params_reconverted, FromProto(proto));

  ASSERT_EQ(params_reconverted.impedance_waypoints.size(), waypoints.size());
  ASSERT_EQ(params_reconverted.time_stamps.size(), time_stamps.size());
  EXPECT_THAT(params_reconverted.time_stamps, Eq(time_stamps));

  // For convenience, we use the matcher for the realtime cartesian targets.
  const Pose3d kIdentity = Pose3d::Identity();
  for (int i = 0; i < waypoints.size(); ++i) {
    ASSERT_OK(cartesian_impedance::Preprocess(waypoints[i]));
    ASSERT_OK(cartesian_impedance::Preprocess(
        params_reconverted.impedance_waypoints[i]));
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        auto rt_params, cartesian_impedance::ToRealTimeCartesianTarget(
                            kIdentity, waypoints[i]));
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        auto rt_params_reconverted,
        cartesian_impedance::ToRealTimeCartesianTarget(
            kIdentity, params_reconverted.impedance_waypoints[i]));
    EXPECT_THAT(rt_params_reconverted,
                RtCartesianTargetIsApprox(rt_params, kSmallTolerance));
  }
}

}  // namespace
}  // namespace intrinsic::icon
