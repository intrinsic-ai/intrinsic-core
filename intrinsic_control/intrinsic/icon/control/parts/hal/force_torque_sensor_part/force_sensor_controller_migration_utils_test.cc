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

#include "intrinsic/icon/control/parts/hal/force_torque_sensor_part/force_sensor_controller_migration_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>

#include "absl/status/statusor.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/pose3_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/kinematics/state.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/transform_utils.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/build_tree_skeleton_from_world.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/test/world_test_utils.h"
#include "intrinsic/world/world.h"

namespace intrinsic::icon {
namespace {

using intrinsic::eigenmath::testing::IsApprox;

// A simple 3 DoF robot with elbow-wrist configuration.
absl::StatusOr<std::unique_ptr<kinematics::Skeleton>> TestRobotSkeleton() {
  constexpr char kRobotCollectionName[] = "robot";
  World test_world = World::CreateEmptyWorld();
  INTR_ASSIGN_OR_RETURN(
      const LinearChainRobotData robot_data,
      CreateLinearChainRobot(
          &test_world,
          LinearChainRobotParams{
              .robot_name = kRobotCollectionName,
              .robot_alias = kRobotCollectionName,
              .link_params =
                  {
                      {.name = "base_link"},
                      {.name = "link_mast_pan"},
                      {.name = "link_mast_tilt"},
                      {.name = "link_mast_wrist_tip"},
                  },
              .joint_params =
                  {
                      {
                          .name = "jaco_joint_mast_pan",
                          .parent_t_inboard =
                              Pose3d(eigenmath::Vector3d{0, 0, 1.0}),
                          .axis = eigenmath::Vector3d::UnitZ(),
                          .lower_value_limit = -3.7,
                          .upper_value_limit = 2.15,
                      },
                      {
                          .name = "jaco_joint_mast_tilt",
                          .parent_t_inboard =
                              Pose3d(eigenmath::Vector3d{0, 0, 1.0}),
                          .axis = eigenmath::Vector3d::UnitY(),
                          .lower_value_limit = -1.55,
                          .upper_value_limit = 1.55,
                      },
                      {
                          .name = "jaco_joint_mast_wrist",
                          .parent_t_inboard =
                              Pose3d(eigenmath::Vector3d{1.0, 0, 0}),
                          .axis = eigenmath::Vector3d::UnitX(),
                      },
                  },
          }));
  auto entity_ids = test_world.ValidateByLabel<AttachmentEntityId>(
      LabelId(kRobotCollectionName));
  INTR_ASSIGN_OR_RETURN(
      const AttachmentEntityId root_id,
      test_world.GetRootEntity({entity_ids.begin(), entity_ids.end()}));
  return BuildTreeSkeletonFromWorld(test_world, root_id);
}

TEST(PostSensorPayloadOperationTest, PayloadCombinationMallocTest) {
  // Expect no malloc
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  icon::PostSensorPayload m1{.ft_t_cog = {1.0, 2.0, 3.0}, .mass_kg = -40.0};
  icon::PostSensorPayload m2{.ft_t_cog = {1.0, 2.0, 3.0}, .mass_kg = 42.0};
  EXPECT_EQ((m1 + m2).mass_kg, 2.0);  // Actual comparison is not relevant here,
                                      // but should not be optimized out.
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST(ConstSensorReadingsCounter, Works) {
  eigenmath::Vector6d some_reading{1, 2, 3, 4, 5, 6};
  ConstSensorReadingsCounter<eigenmath::Vector6d> counter;

  EXPECT_EQ(counter.num_constant_readings(), 0);

  counter.AddReading(some_reading, some_reading);
  EXPECT_EQ(counter.num_constant_readings(), 1);

  counter.AddReading(some_reading, some_reading);
  EXPECT_EQ(counter.num_constant_readings(), 2);

  eigenmath::Vector6d different_reading{2, 3, 4, 5, 6, 7};
  counter.AddReading(some_reading, different_reading);
  EXPECT_EQ(counter.num_constant_readings(), 0);
}

TEST(ComputePostSensorDynamicLoadTest, CentrifugalLoad) {
  // Set an FT sensor at the tip of the robot, with both translational and
  // rotational offset between tip and sensor.
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<kinematics::Skeleton> skeleton,
                       TestRobotSkeleton());
  ASSERT_OK_AND_ASSIGN(const kinematics::Chain chain,
                       kinematics::CreateChainFromModel(*skeleton));
  kinematics::State kinematics_state(&chain);
  const Pose3d tip_t_ft{eigenmath::Quaterniond(0.7071, 0.7071, 0.0, 0.0),
                        eigenmath::Vector3d(0.1, 0.2, 0.3)};

  // Set the post-sensor payload.
  constexpr double kPayloadMassKg = 1.5;
  const eigenmath::Vector3d ft_p_cog{1.0, 0.5, 0.5};
  const PostSensorPayload payload{
      .ft_t_cog = ft_p_cog,
      .mass_kg = kPayloadMassKg,
  };

  // Oscillating motion: set a sinusoid on the first joint, keep the others in
  // the zero position.
  constexpr double kOmegaRadSec = 2.0;
  constexpr double kTimeHorizonSec = 40;
  constexpr double kAmplitudeRad = 0.5;
  constexpr double kControlFrequencyHz = 250;
  eigenmath::VectorNd joint_position = eigenmath::VectorNd::Zero(3);
  eigenmath::VectorNd joint_velocity = eigenmath::VectorNd::Zero(3);
  eigenmath::VectorNd joint_acceleration = eigenmath::VectorNd::Zero(3);
  for (size_t i = 0; i < kTimeHorizonSec * kControlFrequencyHz; i++) {
    const double phi = i * kOmegaRadSec / kControlFrequencyHz;
    joint_position[0] = kAmplitudeRad * cos(phi);
    joint_velocity[0] = -kAmplitudeRad * kOmegaRadSec * sin(phi);
    joint_acceleration[0] =
        -kAmplitudeRad * kOmegaRadSec * kOmegaRadSec * cos(phi);

    if (fabs(std::fmod(phi + M_PI / 2, M_PI)) < 0.005) {
      // Test-region with analytic solution: pass-through through the
      // configuration with maximum joint velocity (joint position[0] at 0,
      // joint acceleration is zero). In this region the post sensor dynamic
      // load is mainly centrifugal.
      INTRINSIC_RT_ASSERT_OK(
          kinematics_state.SetDofPositions(joint_position, false));
      INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
          const Pose3d base_t_tip,
          kinematics_state.GetTransform(chain.GetTipId()));
      const eigenmath::Vector3d expected_base_p_tip(
          cos(joint_position[0]), sin(joint_position[0]), 2.0);
      const eigenmath::Matrix3d expected_base_R_tip =
          eigenmath::RotationZ(joint_position[0]).rotationMatrix();
      ASSERT_THAT(expected_base_p_tip, IsApprox(base_t_tip.translation()));
      ASSERT_THAT(expected_base_R_tip, IsApprox(base_t_tip.rotationMatrix()));

      IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
      INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
          const ComputePostSensorDynamicsResult post_sensor_dynamic_load,
          ComputePostSensorDynamicLoad(joint_velocity, joint_acceleration,
                                       chain.GetTipId(), base_t_tip, tip_t_ft,
                                       payload, &kinematics_state));
      IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

      const eigenmath::Matrix3d ft_R_base =
          (base_t_tip * tip_t_ft).rotationMatrix().transpose();
      const eigenmath::Vector3d base_p_cog =
          (base_t_tip * tip_t_ft * Pose3d(ft_p_cog)).translation();

      const eigenmath::Vector3d expected_base_angular_vel_cog(
          0, 0, joint_velocity[0]);
      const eigenmath::Vector3d expected_base_centrifugal_acc_cog =
          expected_base_angular_vel_cog.cross(
              expected_base_angular_vel_cog.cross(base_p_cog));
      Wrench expected_post_sensor_dynamic_load_at_ft = Wrench::ZERO;
      expected_post_sensor_dynamic_load_at_ft.head<3>() =
          ft_R_base * kPayloadMassKg * expected_base_centrifugal_acc_cog;
      expected_post_sensor_dynamic_load_at_ft.tail<3>() =
          ft_p_cog.cross(expected_post_sensor_dynamic_load_at_ft.head<3>());

      const Wrench expected_post_sensor_dynamic_load_at_tip =
          TransformWrench(tip_t_ft, expected_post_sensor_dynamic_load_at_ft);

      // We can only check with a liberal tolerance, since there are still small
      // translational and angular components not accounted for.
      constexpr double kEstimationTolerance = 5.0e-2;
      EXPECT_THAT(post_sensor_dynamic_load.dynamic_load_at_ft,
                  IsApprox(expected_post_sensor_dynamic_load_at_ft,
                           kEstimationTolerance));
      EXPECT_THAT(expected_post_sensor_dynamic_load_at_tip,
                  IsApprox(post_sensor_dynamic_load.dynamic_load_at_target,
                           kEstimationTolerance));
    }
  }
}

}  // namespace
}  // namespace intrinsic::icon
