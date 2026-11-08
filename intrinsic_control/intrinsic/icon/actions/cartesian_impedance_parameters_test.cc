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

#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/rotation_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/actions/cartesian_impedance.pb.h"
#include "intrinsic/icon/actions/cartesian_impedance_reference_generators.pb.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using eigenmath::testing::IsApprox;
using ::protobuf_matchers::EqualsProto;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Optional;

const size_t kNdof = 7;

CartesianImpedanceParameters CreateMinimalTestParameters() {
  CartesianImpedanceParameters params;
  params.cartesian_target.cartesian_stiffness.setConstant(1.0);
  params.cartesian_target.virtual_cartesian_inertia.setConstant(0.5);
  return params;
}

CartesianImpedanceParameters CreateCompleteTestParameters() {
  CartesianImpedanceParameters params = CreateMinimalTestParameters();
  params.cartesian_target.robot_tip_t_robot_tool.translation() << 0.1, 0.2, 0.3;
  params.cartesian_target.robot_tip_t_robot_tool.setQuaternion(
      eigenmath::QuaternionFromRPY(0.2, 0.3, 0.4));
  params.cartesian_target.robot_base_t_task.translation() << 0.2, 0.3, 0.4;
  params.cartesian_target.robot_base_t_task.setQuaternion(
      eigenmath::QuaternionFromRPY(0.3, 0.4, 0.5));
  params.cartesian_target.task_t_tool_reference_pose = Pose3d();
  params.cartesian_target.task_t_tool_reference_pose->translation().setConstant(
      3.0);
  eigenmath::Matrix3d test_rotation;
  test_rotation << 0, 1, 0,  //
      1, 0, 0,               //
      0, 0, 1;
  params.cartesian_target.task_t_tool_reference_pose->setQuaternion(
      eigenmath::Quaterniond(test_rotation));
  params.cartesian_target.tool_reference_wrench = -Wrench::Ones();
  params.cartesian_target.cartesian_damping = eigenmath::Matrix6d::Ones();
  params.cartesian_target.cartesian_damping->setConstant(2.0);
  params.cartesian_target.wrench_selection_matrix = eigenmath::Matrix6d::Zero();
  params.cartesian_target.cartesian_damping->diagonal() << 1, 2, 3, 4, 5, 6;
  params.cartesian_target.tool_reference_twist = Twist();
  params.cartesian_target.tool_reference_twist.setConstant(8.0);
  params.cartesian_target.tool_reference_acceleration =
      intrinsic::Acceleration();
  params.cartesian_target.tool_reference_acceleration.setConstant(0.1);

  params.nullspace_target.joint_position_nullspace =
      eigenmath::VectorNd::Zero(kNdof);
  params.nullspace_target.nullspace_damping = 4.0;
  params.nullspace_target.nullspace_stiffness = 5.0;

  params.algorithm_configuration.jacobian_pinv_damping = 3.0;
  params.algorithm_configuration.sensed_wrench_deadband = 0.01 * Wrench::Ones();
  params.algorithm_configuration.pose_error_integrator_gain.setConstant(0.1);
  params.algorithm_configuration.pose_error_integrator_bound.setConstant(10);
  params.algorithm_configuration.post_sensor_dynamic_load_multiplier = 11.0;

  params.constraints.joint_limits = JointLimits();
  (void)params.constraints.joint_limits->SetSize(kNdof);
  params.constraints.joint_limits->SetUnlimited();
  params.constraints.cartesian_limits = CartesianLimits();
  params.constraints.cartesian_limits->SetUnlimited();
  params.constraints.enforce_cartesian_and_joint_limits = true;

  params.state_variable_configuration.position_error_threshold = 0.1;
  params.state_variable_configuration.orientation_error_threshold = 0.2;
  params.state_variable_configuration.translational_velocity_error_threshold =
      0.3;
  params.state_variable_configuration.angular_velocity_error_threshold = 0.4;
  params.state_variable_configuration.joint_velocity_threshold = 0.5;
  params.state_variable_configuration.translational_distance_along =
      eigenmath::Vector3d::Ones().normalized();
  params.state_variable_configuration.time_window_for_maximum_displacement =
      0.9;
  params.state_variable_configuration.force_settled_band = 3.0;
  params.state_variable_configuration.force_direction_1_in_task = {1.1, 0.0,
                                                                   0.0};
  params.state_variable_configuration.force_direction_2_in_task = {1.1, 2.2,
                                                                   0.0};
  params.state_variable_configuration.force_direction_3_in_task = {1.1, 2.2,
                                                                   3.3};
  params.state_variable_configuration.torque_axis_1_in_task = {1.2, 0.0, 0.0};
  params.state_variable_configuration.torque_axis_2_in_task = {1.3, 2.4, 0.0};
  params.state_variable_configuration.torque_axis_3_in_task = {1.5, 2.6, 3.7};
  params.state_variable_configuration.position_error_direction_1_in_task = {
      2.1, 0.0, 0.0};
  params.state_variable_configuration.position_error_direction_2_in_task = {
      2.2, 3.3, 0.0};
  params.state_variable_configuration.position_error_direction_3_in_task = {
      2.3, 3.4, 4.5};
  params.state_variable_configuration.orientation_error_axis_1_in_task = {
      3.1, 0.0, 0.0};
  params.state_variable_configuration.orientation_error_axis_2_in_task = {
      3.2, 4.3, 0.0};
  params.state_variable_configuration.orientation_error_axis_3_in_task = {
      3.3, 4.4, 5.5};

  params.reference_generators.cartesian_reference_generator =
      cartesian_impedance::
          CartesianPositionReflexxesImpedanceGeneratorParameters();

  params.reference_generators.nullspace_reference_generator =
      cartesian_impedance::ReflexxesNullspaceReferenceGeneratorParameters();

  return params;
}

TEST(CartesianImpedanceParametersTest,
     ConversionWorksWithNoOptionalParametersSet) {
  auto params = CreateMinimalTestParameters();

  intrinsic_proto::icon::actions::proto::CartesianImpedanceParameters
      proto_params = ToProto(params);

  ASSERT_OK_AND_ASSIGN(auto reconstructed_params, FromProto(proto_params));

  // 'robot_tip_t_robot_tool' offset should default to Identity.
  EXPECT_THAT(reconstructed_params.cartesian_target.robot_tip_t_robot_tool,
              IsApprox(Pose3d::Identity()));

  // 'robot_base_t_task' should default to Identity.
  EXPECT_THAT(reconstructed_params.cartesian_target.robot_base_t_task,
              IsApprox(Pose3d::Identity()));

  EXPECT_THAT(reconstructed_params.cartesian_target.cartesian_stiffness,
              IsApprox(params.cartesian_target.cartesian_stiffness));

  EXPECT_THAT(reconstructed_params.cartesian_target.virtual_cartesian_inertia,
              IsApprox(params.cartesian_target.virtual_cartesian_inertia));

  EXPECT_FALSE(reconstructed_params.cartesian_target.task_t_tool_reference_pose
                   .has_value());
  EXPECT_THAT(reconstructed_params.cartesian_target.tool_reference_twist,
              IsApprox(Twist::ZERO));
  EXPECT_THAT(reconstructed_params.cartesian_target.tool_reference_acceleration,
              IsApprox(Acceleration::ZERO));
  EXPECT_THAT(reconstructed_params.cartesian_target.tool_reference_wrench,
              IsApprox(Wrench::ZERO));
  EXPECT_FALSE(
      reconstructed_params.cartesian_target.cartesian_damping.has_value());

  EXPECT_FALSE(reconstructed_params.nullspace_target.joint_position_nullspace
                   .has_value());
  EXPECT_FALSE(
      reconstructed_params.nullspace_target.nullspace_damping.has_value());
  EXPECT_FALSE(
      reconstructed_params.nullspace_target.nullspace_stiffness.has_value());

  EXPECT_DOUBLE_EQ(
      reconstructed_params.algorithm_configuration.jacobian_pinv_damping,
      cartesian_impedance::kDefaultJacobianPseudoInverseDamping);

  EXPECT_THAT(
      reconstructed_params.algorithm_configuration.sensed_wrench_deadband,
      IsApprox(Wrench::ZERO));

  EXPECT_THAT(
      reconstructed_params.algorithm_configuration.pose_error_integrator_gain,
      IsApprox(eigenmath::Vector6d::Zero()));

  EXPECT_THAT(
      reconstructed_params.algorithm_configuration.pose_error_integrator_bound,
      IsApprox(eigenmath::Vector6d::Zero()));

  EXPECT_DOUBLE_EQ(reconstructed_params.algorithm_configuration
                       .post_sensor_dynamic_load_multiplier,
                   0.0);

  EXPECT_FALSE(
      reconstructed_params.constraints.enforce_cartesian_and_joint_limits);
  EXPECT_FALSE(reconstructed_params.constraints.joint_limits.has_value());
  EXPECT_FALSE(reconstructed_params.constraints.cartesian_limits.has_value());

  // Expect default motion generators to be set.
  EXPECT_TRUE(std::holds_alternative<
              cartesian_impedance::SimpleImpedanceGeneratorParameters>(
      reconstructed_params.reference_generators.cartesian_reference_generator));
  EXPECT_TRUE(std::holds_alternative<
              cartesian_impedance::SimpleNullspaceReferenceGeneratorParameters>(
      reconstructed_params.reference_generators.nullspace_reference_generator));

  EXPECT_THAT(reconstructed_params.cartesian_target.wrench_selection_matrix,
              IsApprox(eigenmath::Matrix6d::Identity()));

  EXPECT_FALSE(reconstructed_params.state_variable_configuration
                   .translational_distance_along.has_value());
  EXPECT_DOUBLE_EQ(
      reconstructed_params.state_variable_configuration
          .time_window_for_maximum_displacement,
      cartesian_impedance::kDefaultTimeWindowForMaximumDisplacement);
  EXPECT_EQ(reconstructed_params.state_variable_configuration
                .force_direction_1_in_task,
            eigenmath::Vector3d::UnitX());
  EXPECT_EQ(reconstructed_params.state_variable_configuration
                .force_direction_2_in_task,
            eigenmath::Vector3d::UnitY());
  EXPECT_EQ(reconstructed_params.state_variable_configuration
                .force_direction_3_in_task,
            eigenmath::Vector3d::UnitZ());
  EXPECT_EQ(
      reconstructed_params.state_variable_configuration.torque_axis_1_in_task,
      eigenmath::Vector3d::UnitX());
  EXPECT_EQ(
      reconstructed_params.state_variable_configuration.torque_axis_2_in_task,
      eigenmath::Vector3d::UnitY());
  EXPECT_EQ(
      reconstructed_params.state_variable_configuration.torque_axis_3_in_task,
      eigenmath::Vector3d::UnitZ());
  EXPECT_EQ(reconstructed_params.state_variable_configuration
                .position_error_direction_1_in_task,
            eigenmath::Vector3d::UnitX());
  EXPECT_EQ(reconstructed_params.state_variable_configuration
                .position_error_direction_2_in_task,
            eigenmath::Vector3d::UnitY());
  EXPECT_EQ(reconstructed_params.state_variable_configuration
                .position_error_direction_3_in_task,
            eigenmath::Vector3d::UnitZ());
  EXPECT_EQ(reconstructed_params.state_variable_configuration
                .orientation_error_axis_1_in_task,
            eigenmath::Vector3d::UnitX());
  EXPECT_EQ(reconstructed_params.state_variable_configuration
                .orientation_error_axis_2_in_task,
            eigenmath::Vector3d::UnitY());
  EXPECT_EQ(reconstructed_params.state_variable_configuration
                .orientation_error_axis_3_in_task,
            eigenmath::Vector3d::UnitZ());
}

TEST(CartesianImpedanceParametersTest,
     ConversionWorksWithAllOptionalParametersSet) {
  auto params = CreateCompleteTestParameters();
  intrinsic_proto::icon::actions::proto::CartesianImpedanceParameters
      proto_params = ToProto(params);

  ASSERT_OK_AND_ASSIGN(auto reconstructed_params, FromProto(proto_params));

  EXPECT_THAT(reconstructed_params.cartesian_target.robot_tip_t_robot_tool,
              IsApprox(params.cartesian_target.robot_tip_t_robot_tool));

  EXPECT_THAT(reconstructed_params.cartesian_target.robot_base_t_task,
              IsApprox(params.cartesian_target.robot_base_t_task));

  EXPECT_THAT(reconstructed_params.cartesian_target.cartesian_stiffness,
              IsApprox(params.cartesian_target.cartesian_stiffness));

  EXPECT_THAT(reconstructed_params.cartesian_target.virtual_cartesian_inertia,
              IsApprox(params.cartesian_target.virtual_cartesian_inertia));

  EXPECT_THAT(
      reconstructed_params.cartesian_target.task_t_tool_reference_pose,
      Optional(IsApprox(*params.cartesian_target.task_t_tool_reference_pose)));

  EXPECT_THAT(reconstructed_params.cartesian_target.tool_reference_twist,
              IsApprox(params.cartesian_target.tool_reference_twist));

  EXPECT_THAT(reconstructed_params.cartesian_target.tool_reference_acceleration,
              IsApprox(params.cartesian_target.tool_reference_acceleration));

  EXPECT_THAT(reconstructed_params.cartesian_target.tool_reference_wrench,
              IsApprox(params.cartesian_target.tool_reference_wrench));

  EXPECT_THAT(reconstructed_params.cartesian_target.cartesian_damping,
              Optional(IsApprox(*params.cartesian_target.cartesian_damping)));

  EXPECT_THAT(reconstructed_params.cartesian_target.wrench_selection_matrix,
              IsApprox(params.cartesian_target.wrench_selection_matrix));

  EXPECT_THAT(reconstructed_params.nullspace_target.nullspace_damping,
              Optional(Eq(*params.nullspace_target.nullspace_damping)));

  EXPECT_THAT(reconstructed_params.nullspace_target.nullspace_stiffness,
              Optional(Eq(*params.nullspace_target.nullspace_stiffness)));

  EXPECT_THAT(
      reconstructed_params.nullspace_target.joint_position_nullspace,
      Optional(IsApprox(*params.nullspace_target.joint_position_nullspace)));

  EXPECT_THAT(
      reconstructed_params.algorithm_configuration.jacobian_pinv_damping,
      Eq(params.algorithm_configuration.jacobian_pinv_damping));

  EXPECT_THAT(
      reconstructed_params.algorithm_configuration.sensed_wrench_deadband,
      IsApprox(params.algorithm_configuration.sensed_wrench_deadband));

  EXPECT_THAT(
      reconstructed_params.algorithm_configuration.pose_error_integrator_gain,
      IsApprox(params.algorithm_configuration.pose_error_integrator_gain));

  EXPECT_THAT(
      reconstructed_params.algorithm_configuration.pose_error_integrator_bound,
      IsApprox(params.algorithm_configuration.pose_error_integrator_bound));

  EXPECT_THAT(
      reconstructed_params.algorithm_configuration
          .post_sensor_dynamic_load_multiplier,
      Eq(params.algorithm_configuration.post_sensor_dynamic_load_multiplier));

  EXPECT_THAT(reconstructed_params.state_variable_configuration
                  .position_error_threshold,
              Eq(params.state_variable_configuration.position_error_threshold));

  EXPECT_DOUBLE_EQ(
      reconstructed_params.state_variable_configuration
          .orientation_error_threshold,
      params.state_variable_configuration.orientation_error_threshold);

  EXPECT_DOUBLE_EQ(reconstructed_params.state_variable_configuration
                       .translational_velocity_error_threshold,
                   params.state_variable_configuration
                       .translational_velocity_error_threshold);

  EXPECT_DOUBLE_EQ(
      reconstructed_params.state_variable_configuration
          .angular_velocity_error_threshold,
      params.state_variable_configuration.angular_velocity_error_threshold);

  EXPECT_DOUBLE_EQ(
      reconstructed_params.state_variable_configuration
          .joint_velocity_threshold,
      params.state_variable_configuration.joint_velocity_threshold);

  ASSERT_TRUE(params.state_variable_configuration.translational_distance_along
                  .has_value());
  EXPECT_THAT(
      reconstructed_params.state_variable_configuration
          .translational_distance_along,
      Optional(IsApprox(
          *params.state_variable_configuration.translational_distance_along)));

  EXPECT_DOUBLE_EQ(
      reconstructed_params.state_variable_configuration
          .time_window_for_maximum_displacement,
      params.state_variable_configuration.time_window_for_maximum_displacement);

  EXPECT_DOUBLE_EQ(
      reconstructed_params.state_variable_configuration.force_settled_band,
      params.state_variable_configuration.force_settled_band);

  EXPECT_THAT(reconstructed_params.state_variable_configuration
                  .force_direction_1_in_task,
              ElementsAre(1.1, 0.0, 0.0));
  EXPECT_THAT(reconstructed_params.state_variable_configuration
                  .force_direction_2_in_task,
              ElementsAre(1.1, 2.2, 0.0));
  EXPECT_THAT(reconstructed_params.state_variable_configuration
                  .force_direction_3_in_task,
              ElementsAre(1.1, 2.2, 3.3));
  EXPECT_THAT(
      reconstructed_params.state_variable_configuration.torque_axis_1_in_task,
      ElementsAre(1.2, 0.0, 0.0));
  EXPECT_THAT(
      reconstructed_params.state_variable_configuration.torque_axis_2_in_task,
      ElementsAre(1.3, 2.4, 0.0));
  EXPECT_THAT(
      reconstructed_params.state_variable_configuration.torque_axis_3_in_task,
      ElementsAre(1.5, 2.6, 3.7));
  EXPECT_THAT(reconstructed_params.state_variable_configuration
                  .position_error_direction_1_in_task,
              ElementsAre(2.1, 0.0, 0.0));
  EXPECT_THAT(reconstructed_params.state_variable_configuration
                  .position_error_direction_2_in_task,
              ElementsAre(2.2, 3.3, 0.0));
  EXPECT_THAT(reconstructed_params.state_variable_configuration
                  .position_error_direction_3_in_task,
              ElementsAre(2.3, 3.4, 4.5));
  EXPECT_THAT(reconstructed_params.state_variable_configuration
                  .orientation_error_axis_1_in_task,
              ElementsAre(3.1, 0.0, 0.0));
  EXPECT_THAT(reconstructed_params.state_variable_configuration
                  .orientation_error_axis_2_in_task,
              ElementsAre(3.2, 4.3, 0.0));
  EXPECT_THAT(reconstructed_params.state_variable_configuration
                  .orientation_error_axis_3_in_task,
              ElementsAre(3.3, 4.4, 5.5));

  // TODO(giftthaler): implement better comparison for limits.
  EXPECT_TRUE(reconstructed_params.constraints.cartesian_limits.has_value());
  EXPECT_EQ(
      reconstructed_params.constraints.cartesian_limits.value()
          .max_translational_position,
      params.constraints.cartesian_limits.value().max_translational_position);
  EXPECT_EQ(
      reconstructed_params.constraints.cartesian_limits.value()
          .min_translational_position,
      params.constraints.cartesian_limits.value().min_translational_position);
  EXPECT_EQ(
      reconstructed_params.constraints.cartesian_limits.value()
          .max_translational_velocity,
      params.constraints.cartesian_limits.value().max_translational_velocity);
  EXPECT_EQ(
      reconstructed_params.constraints.cartesian_limits.value()
          .min_translational_velocity,
      params.constraints.cartesian_limits.value().min_translational_velocity);
  EXPECT_EQ(reconstructed_params.constraints.cartesian_limits.value()
                .min_translational_acceleration,
            params.constraints.cartesian_limits.value()
                .min_translational_acceleration);
  EXPECT_EQ(reconstructed_params.constraints.cartesian_limits.value()
                .max_translational_acceleration,
            params.constraints.cartesian_limits.value()
                .max_translational_acceleration);
  EXPECT_EQ(
      reconstructed_params.constraints.cartesian_limits.value()
          .max_rotational_velocity,
      params.constraints.cartesian_limits.value().max_rotational_velocity);
  EXPECT_EQ(
      reconstructed_params.constraints.cartesian_limits.value()
          .max_rotational_acceleration,
      params.constraints.cartesian_limits.value().max_rotational_acceleration);
  EXPECT_EQ(reconstructed_params.constraints.cartesian_limits.value()
                .max_rotational_jerk,
            params.constraints.cartesian_limits.value().max_rotational_jerk);

  EXPECT_TRUE(reconstructed_params.constraints.joint_limits.has_value());
  EXPECT_EQ(reconstructed_params.constraints.joint_limits.value().min_position,
            params.constraints.joint_limits.value().min_position);
  EXPECT_EQ(reconstructed_params.constraints.joint_limits.value().max_position,
            params.constraints.joint_limits.value().max_position);
  EXPECT_EQ(reconstructed_params.constraints.joint_limits.value().max_velocity,
            params.constraints.joint_limits.value().max_velocity);
  EXPECT_EQ(
      reconstructed_params.constraints.joint_limits.value().max_acceleration,
      params.constraints.joint_limits.value().max_acceleration);
  EXPECT_EQ(reconstructed_params.constraints.joint_limits.value().max_jerk,
            params.constraints.joint_limits.value().max_jerk);

  EXPECT_TRUE(
      reconstructed_params.constraints.enforce_cartesian_and_joint_limits);

  // Expect Reflexxes motion generators to be set.
  EXPECT_TRUE(std::holds_alternative<
              cartesian_impedance::
                  CartesianPositionReflexxesImpedanceGeneratorParameters>(
      reconstructed_params.reference_generators.cartesian_reference_generator));
  EXPECT_TRUE(
      std::holds_alternative<
          cartesian_impedance::ReflexxesNullspaceReferenceGeneratorParameters>(
          reconstructed_params.reference_generators
              .nullspace_reference_generator));
}

TEST(CartesianImpedanceParametersTest,
     TranslationalDistanceAlongNormalizationWorks) {
  auto params = CreateMinimalTestParameters();
  params.state_variable_configuration.translational_distance_along =
      eigenmath::Vector3d(10., 0., 0.);

  intrinsic_proto::icon::actions::proto::CartesianImpedanceParameters
      proto_params = ToProto(params);
  ASSERT_OK_AND_ASSIGN(auto reconstructed_params, FromProto(proto_params));
  EXPECT_THAT(reconstructed_params.state_variable_configuration
                  .translational_distance_along,
              Optional(IsApprox(eigenmath::Vector3d(1., 0., 0.))));
}

TEST(CartesianImpedanceParametersTest,
     TranslationalDistanceAlongValidationWorks) {
  auto params = CreateMinimalTestParameters();
  params.state_variable_configuration.translational_distance_along =
      eigenmath::Vector3d(0., 0., 0.);

  intrinsic_proto::icon::actions::proto::CartesianImpedanceParameters
      proto_params = ToProto(params);
  EXPECT_THAT(FromProto(proto_params),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(CartesianImpedanceParametersTest,
     ProtoConversionForQuinticSplineCartesianReferenceGeneratorWorks) {
  intrinsic_proto::icon::actions::proto::
      QuinticSplineCartesianReferenceGenerator params;
  params.set_movement_duration_seconds(1.0);

  auto reconstructed_params = ToProto(FromProto(params));
  EXPECT_THAT(reconstructed_params, EqualsProto(params));
}

TEST(CartesianImpedanceParametersTest,
     ProtoConversionForQuinticSplineNullspaceReferenceGeneratorWorks) {
  intrinsic_proto::icon::actions::proto::
      QuinticSplineNullspaceReferenceGenerator params;
  params.set_movement_duration_seconds(1.0);

  auto reconstructed_params = ToProto(FromProto(params));
  EXPECT_THAT(reconstructed_params, EqualsProto(params));
}

TEST(CartesianImpedanceParametersTest,
     ProtoConversionForSimpleCartesianReferenceGeneratorWorks) {
  intrinsic_proto::icon::actions::proto::SimpleImpedanceReferenceGenerator
      params;
  params.set_lowpass_filter_constant(.01);

  auto reconstructed_params = ToProto(FromProto(params));
  EXPECT_THAT(reconstructed_params, EqualsProto(params));
}

TEST(CartesianImpedanceParametersTest,
     ProtoConversionForSimpleNullspaceReferenceGeneratorWorks) {
  intrinsic_proto::icon::actions::proto::SimpleNullspaceReferenceGenerator
      params;
  params.set_lowpass_filter_constant(.01);

  auto reconstructed_params = ToProto(FromProto(params));
  EXPECT_THAT(reconstructed_params, EqualsProto(params));
}

TEST(CartesianImpedanceParametersTest,
     CartesianPositionReflexxesImpedanceReferenceGeneratorConversionsWork) {
  auto params = CreateMinimalTestParameters();

  params.reference_generators.cartesian_reference_generator =
      cartesian_impedance::
          CartesianPositionReflexxesImpedanceGeneratorParameters();

  intrinsic_proto::icon::actions::proto::CartesianImpedanceParameters
      proto_params = ToProto(params);
  ASSERT_OK_AND_ASSIGN(auto reconstructed_params, FromProto(proto_params));
  EXPECT_TRUE(std::holds_alternative<
              cartesian_impedance::
                  CartesianPositionReflexxesImpedanceGeneratorParameters>(
      reconstructed_params.reference_generators.cartesian_reference_generator));
}

TEST(CartesianImpedanceParametersTest,
     CartesianVelocityReflexxesImpedanceReferenceGeneratorConversionsWork) {
  auto params = CreateMinimalTestParameters();

  params.reference_generators.cartesian_reference_generator =
      cartesian_impedance::
          CartesianVelocityReflexxesImpedanceGeneratorParameters();

  intrinsic_proto::icon::actions::proto::CartesianImpedanceParameters
      proto_params = ToProto(params);
  ASSERT_OK_AND_ASSIGN(auto reconstructed_params, FromProto(proto_params));

  EXPECT_TRUE(std::holds_alternative<
              cartesian_impedance::
                  CartesianVelocityReflexxesImpedanceGeneratorParameters>(
      reconstructed_params.reference_generators.cartesian_reference_generator));
}

TEST(CartesianImpedanceParametersTest,
     CartesianSimpleImpedanceReferenceGeneratorConversionsWork) {
  auto params = CreateMinimalTestParameters();

  cartesian_impedance::SimpleImpedanceGeneratorParameters gen;
  gen.lowpass_filter_constant = 0.025;
  params.reference_generators.cartesian_reference_generator = gen;

  intrinsic_proto::icon::actions::proto::CartesianImpedanceParameters
      proto_params = ToProto(params);
  ASSERT_OK_AND_ASSIGN(auto reconstructed_params, FromProto(proto_params));

  EXPECT_TRUE(std::holds_alternative<
              cartesian_impedance::SimpleImpedanceGeneratorParameters>(
      reconstructed_params.reference_generators.cartesian_reference_generator));

  auto generator =
      std::get<cartesian_impedance::SimpleImpedanceGeneratorParameters>(
          reconstructed_params.reference_generators
              .cartesian_reference_generator);

  EXPECT_EQ(generator.lowpass_filter_constant, 0.025);
}

TEST(CartesianImpedanceParametersTest,
     CartesianQuinticImpedanceReferenceGeneratorConversionsWork) {
  auto params = CreateMinimalTestParameters();

  cartesian_impedance::QuinticSplineCartesianGeneratorParameters gen;
  gen.movement_duration_seconds = 100.0;
  params.reference_generators.cartesian_reference_generator = gen;

  intrinsic_proto::icon::actions::proto::CartesianImpedanceParameters
      proto_params = ToProto(params);
  ASSERT_OK_AND_ASSIGN(auto reconstructed_params, FromProto(proto_params));

  EXPECT_TRUE(std::holds_alternative<
              cartesian_impedance::QuinticSplineCartesianGeneratorParameters>(
      reconstructed_params.reference_generators.cartesian_reference_generator));

  auto generator =
      std::get<cartesian_impedance::QuinticSplineCartesianGeneratorParameters>(
          reconstructed_params.reference_generators
              .cartesian_reference_generator);

  EXPECT_EQ(generator.movement_duration_seconds, 100);
}

TEST(CartesianImpedanceParametersTest,
     ReflexxesNullspaceReferenceGeneratorConversionsWork) {
  auto params = CreateMinimalTestParameters();

  params.reference_generators.nullspace_reference_generator =
      cartesian_impedance::ReflexxesNullspaceReferenceGeneratorParameters();

  intrinsic_proto::icon::actions::proto::CartesianImpedanceParameters
      proto_params = ToProto(params);
  ASSERT_OK_AND_ASSIGN(auto reconstructed_params, FromProto(proto_params));

  EXPECT_TRUE(
      std::holds_alternative<
          cartesian_impedance::ReflexxesNullspaceReferenceGeneratorParameters>(
          reconstructed_params.reference_generators
              .nullspace_reference_generator));
}

TEST(CartesianImpedanceParametersTest,
     SimpleNullspaceReferenceGeneratorConversionsWork) {
  auto params = CreateMinimalTestParameters();

  params.reference_generators.nullspace_reference_generator =
      cartesian_impedance::SimpleNullspaceReferenceGeneratorParameters();

  intrinsic_proto::icon::actions::proto::CartesianImpedanceParameters
      proto_params = ToProto(params);
  ASSERT_OK_AND_ASSIGN(auto reconstructed_params, FromProto(proto_params));

  EXPECT_TRUE(std::holds_alternative<
              cartesian_impedance::SimpleNullspaceReferenceGeneratorParameters>(
      reconstructed_params.reference_generators.nullspace_reference_generator));
}

TEST(CartesianImpedanceParametersTest,
     QuinticNullspaceReferenceGeneratorConversionsWork) {
  auto params = CreateMinimalTestParameters();

  params.reference_generators.nullspace_reference_generator =
      cartesian_impedance::QuinticSplineNullspaceReferenceGeneratorParameters();

  intrinsic_proto::icon::actions::proto::CartesianImpedanceParameters
      proto_params = ToProto(params);
  ASSERT_OK_AND_ASSIGN(auto reconstructed_params, FromProto(proto_params));

  EXPECT_TRUE(std::holds_alternative<
              cartesian_impedance::
                  QuinticSplineNullspaceReferenceGeneratorParameters>(
      reconstructed_params.reference_generators.nullspace_reference_generator));
}

}  // namespace
}  // namespace intrinsic::icon
