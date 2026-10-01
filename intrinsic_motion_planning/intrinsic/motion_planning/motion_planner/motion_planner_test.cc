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

#include "intrinsic/motion_planning/motion_planner/motion_planner.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <tuple>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/so3.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/check_joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/numopt/constraint_interface.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_base_test_fixture.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_flags.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_test_util.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_topp_test_fixture.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy_util.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"
#include "intrinsic/motion_planning/proto/motion_specification_proto_utils.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_blending_parameter.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_config.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/testing/constrained_motion_test_utils.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.h"
#include "intrinsic/skills/apps/testing/test_world.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/dof_kinematic_view.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/objects/frame_internal.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/object_world_proto_utils.h"
#include "intrinsic/world/proto/collision_settings.pb.h"
#include "intrinsic/world/world.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic {
namespace {

using ::absl_testing::StatusIs;
using ::intrinsic::ParseTextProtoOrDie;
using ::intrinsic::eigenmath::testing::IsApprox;
using ::protobuf_matchers::EqualsProto;
using ::testing::AllOf;
using ::testing::Combine;
using ::testing::DoubleNear;
using ::testing::ElementsAreArray;
using ::testing::Field;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::Pointwise;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;
using ::testing::Values;
using ::testing::ValuesIn;

using intrinsic_proto::motion_planning::v1::MotionPlannerConfiguration;
using intrinsic_proto::motion_planning::v1::MotionSpecification;

constexpr double kTargetJointConfigErrorTolerance = 1e-8;
struct MotionPlannerParams {
  std::string test_name;
};

// Testing params used to control whether to force strict fallback trajectory
// generation.
struct FallbackParams {
  std::string test_name;
  bool force_strict_fallback = false;
  friend void PrintTo(const FallbackParams& params, std::ostream* os) {
    *os << params.test_name
        << " (enable_strict_fallback=" << params.force_strict_fallback << ")";
  }
};

intrinsic_proto::motion_planning::v1::MotionSpecification
CreateDefaultMotionSpecification(
    const MotionPlannerParams& motion_planner_params) {
  intrinsic_proto::motion_planning::v1::MotionSpecification motion_spec;
  return motion_spec;
}

// Parameterized test fixture for `MotionPlanner::PlanTrajectory` across
// TOPP solver variations.
class MotionPlannerTest
    : public MotionPlannerToppBaseTest,
      public ::testing::TestWithParam<
          std::tuple<MotionPlannerParams, ToppTestParams, FallbackParams>> {
 protected:
  MotionPlannerTest() = default;
  ~MotionPlannerTest() override = default;

  void SetUp() override {
    motion_planner_params_ = std::get<0>(GetParam());
    SetUpTopp(std::get<1>(GetParam()));
    fallback_params_ = std::get<2>(GetParam());

    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);

    if (fallback_params_.force_strict_fallback) {
      motion_planner_flags_.enable_fallback_trajectory = true;
      motion_planner_flags_.enable_path_refinement_validation_step = true;
      motion_planner_flags_.enable_fallback_trajectory_local_adjust = true;
      run_time_flags_.force_strict_fallback_for_testing = true;
    }
  }

  MotionPlannerParams motion_planner_params_;
  FallbackParams fallback_params_;
  RunTimeMotionPlannerFlags run_time_flags_;
  intrinsic_proto::motion_planning::v1::MotionSpecification motion_spec_;
};

// Pointwise matcher for comparing a specified member field of an object to the
// expected value.
MATCHER_P(FieldPointwise, member_ptr, "") {
  return (std::get<0>(arg).*member_ptr) == std::get<1>(arg);
}

// Clones the given world and sets the specified `robot`'s joint positions to
// `joint_values`.
absl::StatusOr<World> CloneAndSetJointValues(
    const World& world, const WorldObjectName& robot,
    const eigenmath::VectorXd joint_values) {
  World copy_world = world.Clone();
  INTR_ASSIGN_OR_RETURN(auto copy_object_world,
                        object_world::ObjectWorld::CreateView(copy_world));
  INTR_ASSIGN_OR_RETURN(auto* copy_robot,
                        copy_object_world->GetKinematicObject(robot));
  INTR_RETURN_IF_ERROR(copy_robot->SetJointPositions(joint_values));
  return copy_world;
}

absl::StatusOr<JointLimitsXd> GetApplicationLimits(
    const World& world, const RobotCollectionsEntityId& robot_id) {
  INTR_ASSIGN_OR_RETURN(auto dof_view, world.GetDofKinematicView(robot_id));
  int ndof = dof_view->GetDofCount();
  CHECK(ndof <= eigenmath::VectorNd::MaxSizeAtCompileTime);
  return dof_view->GetDofApplicationLimits();
}

// Creates a `JointLimitsUpdate` proto with the given `min_position` and
// `max_position` joint position limits.
intrinsic_proto::motion_planning::v1::JointLimitsUpdate
CreateJointLimitsUpdateProtoFromPositionLimits(
    const eigenmath::VectorNd& min_position,
    const eigenmath::VectorNd& max_position) {
  intrinsic_proto::motion_planning::v1::JointLimitsUpdate
      joint_limits_update_proto;
  VectorNdToRepeatedDouble(
      min_position,
      joint_limits_update_proto.mutable_min_position()->mutable_values());
  VectorNdToRepeatedDouble(
      max_position,
      joint_limits_update_proto.mutable_max_position()->mutable_values());
  return joint_limits_update_proto;
}

// Validates that each path sample position in the planning result matches its
// corresponding trajectory joint state position.
void ValidatePathSamplesFromPlanTrajectoryResult(
    const MotionPlanner::PlanTrajectoryResult& plan_trajectory_result) {
  ASSERT_EQ(plan_trajectory_result.path_samples.size(),
            plan_trajectory_result.trajectory.size())
      << absl::StrCat("Path samples size ",
                      plan_trajectory_result.path_samples.size(),
                      " does not match trajectory size ",
                      plan_trajectory_result.trajectory.size());
  for (int i = 0; i < plan_trajectory_result.path_samples.size(); ++i) {
    const topp::PathSample& path_sample =
        plan_trajectory_result.path_samples[i];
    const JointStatePVA& joint_state =
        plan_trajectory_result.trajectory.data()[i];
    EXPECT_THAT(path_sample.q, IsApprox(joint_state.position));
  }

  // If this trajectory was produced using strict fallback, then the
  // trajectory's geometry should very closely match the geometry of the
  // `path_segments`.
  if (plan_trajectory_result.fallback_stage ==
      MotionPlannerInterface::FallbackStage::kStrictlyFollowPathGlobal) {
    EXPECT_OK(testing::CheckThatPathSamplesStrictlyFollowPathSegments(
        plan_trajectory_result.path_samples,
        plan_trajectory_result.path_segments));
  }
}

// Validates that duration breakdowns in motion planning statistics are
// consistent and smaller than total planning times.
void ValidateMotionPlanningStatisticsFromPlanTrajectoryResult(
    const MotionPlanner::MotionPlanningStatistics& motion_planning_statistics) {
  if (motion_planning_statistics.trajectory_generation_statistics
              .path_refinement_and_trajectory_generation_duration !=
          absl::ZeroDuration() ||
      motion_planning_statistics.trajectory_generation_statistics
              .path_refinement_validation_duration != absl::ZeroDuration()) {
    EXPECT_LT(absl::ToDoubleSeconds(
                  motion_planning_statistics.trajectory_generation_statistics
                      .path_refinement_validation_duration),
              absl::ToDoubleSeconds(
                  motion_planning_statistics.trajectory_generation_statistics
                      .path_refinement_and_trajectory_generation_duration));
  }
  EXPECT_LT(absl::ToDoubleSeconds(
                motion_planning_statistics.path_planning_statistics
                    .path_planning_duration +
                motion_planning_statistics.trajectory_generation_statistics
                    .path_refinement_and_trajectory_generation_duration),
            absl::ToDoubleSeconds(
                motion_planning_statistics.motion_planning_duration));
}

// Combines the validation logic of
// `ValidatePathSamplesFromPlanTrajectoryResult` and
// `ValidateMotionPlanningStatisticsFromPlanTrajectoryResult`.
void ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
    const MotionPlanner::PlanTrajectoryResult& plan_trajectory_result) {
  ValidatePathSamplesFromPlanTrajectoryResult(plan_trajectory_result);
  ValidateMotionPlanningStatisticsFromPlanTrajectoryResult(
      plan_trajectory_result.motion_planning_statistics);
}
TEST_P(MotionPlannerTest, CheckPathConstraintSatisfaction) {
  eigenmath::Vector6d joint_configuration = {
      -1.5830168625113954, -1.570702106934764, -1.5706219444277991,
      -1.5707054917593837, 1.5706998190287802, 1.5707031008962957};

  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  MotionPlannerConfiguration motion_planner_config;
  Pose3d root_t_waypoint = toPose3d(
      "0.243,0.230,0.372,-0.00006538532782465,0.71138140385464799,0."
      "70280612999809222,0."
      "00019393724206537,");

  intrinsic_proto::motion_planning::v1::MotionSegment* motion_segment =
      motion_spec_.add_motion_segments();
  *motion_segment->mutable_target()->mutable_cartesian_pose() =
      CreatePoseEqualityGeometricConstraint(
          robot_->GetName(), tip_frame_->GetName(), root_t_waypoint);

  *motion_segment->mutable_path_constraints()->mutable_rotation_cone() =
      CreateRotationConeGeometricConstraint(
          robot_->GetName(), tip_frame_->GetName(), 0.4, root_t_waypoint);
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  // Create the constraint from proto
  ASSERT_OK_AND_ASSIGN(
      std::vector<std::unique_ptr<ConstraintInterface>> constraints,
      GetUniformGeometricConstraintsFromProto(
          *object_world_, motion_segment->path_constraints()));

  for (const auto& state : planning_result.trajectory.data()) {
    ASSERT_TRUE(constraints.front()->IsSatisfied(state.position).value());
  }
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, CheckPlanWithPathConstraints) {
  eigenmath::Vector6d joint_configuration = {
      -1.5830168625113954, -1.570702106934764, -1.5706219444277991,
      -1.5707054917593837, 1.5706998190287802, 1.5707031008962957};
  MotionPlannerConfiguration motion_planner_config;

  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  ASSERT_OK_AND_ASSIGN(
      JointLimitsXd planning_limits,
      GetApplicationLimits(world_, robot_->GetRobotEntityId()));
  eigenmath::VectorXd restricted_max = planning_limits.max_position * 0.6;
  eigenmath::VectorXd restricted_min = planning_limits.min_position * 0.9;

  intrinsic_proto::motion_planning::v1::ConstraintIntersection
      constraint_intersection;
  *constraint_intersection.add_constraints()->mutable_joint_position_limits() =
      CreateJointPositionLimitsProto(restricted_min, restricted_max);

  Pose3d root_t_waypoint = toPose3d(
      "0.243,0.230,0.372,-0.00006538532782465,0.71138140385464799,0."
      "70280612999809222,0."
      "00019393724206537,");
  *constraint_intersection.add_constraints()->mutable_cartesian_pose() =
      CreatePoseEqualityGeometricConstraint(
          robot_->GetName(), tip_frame_->GetName(), root_t_waypoint);
  intrinsic_proto::motion_planning::v1::MotionSegment* motion_segment =
      motion_spec_.add_motion_segments();
  *motion_segment->mutable_target()->mutable_constraint_intersection() =
      constraint_intersection;
  *motion_segment->mutable_path_constraints()->mutable_rotation_cone() =
      CreateRotationConeGeometricConstraint(
          robot_->GetName(), tip_frame_->GetName(), 0.8, root_t_waypoint);
  intrinsic_proto::motion_planning::v1::MotionSegment* second_motion_segment =
      motion_spec_.add_motion_segments();
  intrinsic_proto::motion_planning::v1::ConstraintIntersection
      constraint_intersection_2;
  Pose3d root_t_waypoint_2 = toPose3d(
      "0.243,0.130,0.572,-0.00006538532782465,0.71138140385464799,0."
      "70280612999809222,0."
      "00019393724206537,");
  *constraint_intersection_2.add_constraints()->mutable_cartesian_pose() =
      CreatePoseEqualityGeometricConstraint(
          robot_->GetName(), tip_frame_->GetName(), root_t_waypoint_2);

  *second_motion_segment->mutable_target()->mutable_constraint_intersection() =
      constraint_intersection_2;

  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));
  // Check every state in the planned trajectory is within the
  // most restricted constraint.
  for (const auto& state : planning_result.trajectory.data()) {
    for (int i = 0; i < state.position.size(); ++i) {
      ASSERT_TRUE(state.position[i] < restricted_max[i] &&
                  state.position[i] > restricted_min[i]);
    }
  }
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, ReturnsInvalidArgumentForEmptyMotionSegments) {
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits));

  // Empty motion specification
  MotionPlannerConfiguration motion_planner_config;

  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  EXPECT_THAT(
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Motion Specification does not contain any motion segments.")));
}

TEST_P(MotionPlannerTest, FailsForLinearMoveSegmentWithoutTargetSet) {
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits));

  auto* motion_segment = motion_spec_.add_motion_segments();
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  EXPECT_THAT(motion_planner->PlanTrajectory(
                  *object_world_, robot_specification_, motion_spec_,
                  motion_planner_config, run_time_flags_),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("motion segments need to specify a target")));
}

TEST_P(MotionPlannerTest, WorksForLinearMoveSegmentWithJointTarget) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  auto* motion_segment = motion_spec_.add_motion_segments();
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  eigenmath::VectorNd goal_configuration = eigenmath::VectorNd{
      {1.19161, -1.47558, 1.94577, -1.78063, -1.02406, -0.45048}};

  VectorNdToRepeatedDouble(goal_configuration, motion_segment->mutable_target()
                                                   ->mutable_joint_position()
                                                   ->mutable_joints());
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));
  ASSERT_GE(planning_result.trajectory.size(), 2);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}
TEST_P(MotionPlannerTest, LinearMoveWorksForSinglePoseTarget) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  Pose3d root_t_waypoint =
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065");

  auto* motion_segment = motion_spec_.add_motion_segments();
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);

  *motion_segment->mutable_target()->mutable_cartesian_pose() =
      CreatePoseEqualityGeometricConstraint(
          robot_->GetName(), tip_frame_->GetName(), root_t_waypoint);

  *motion_segment->mutable_cartesian_limits() =
      CreateDynamicCartesianLimitsProto(
          /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
          /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));
  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());
  EXPECT_THAT(root_t_waypoint,
              IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-4));
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, LinearMoveWorksForMultiplePoseTarget) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    *motion_segment->mutable_cartesian_limits() =
        CreateDynamicCartesianLimitsProto(
            /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
            /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
  }
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));
  ASSERT_OK_AND_ASSIGN(auto tmp_jont, robot_->GetJointPositions());
  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));
  ASSERT_OK_AND_ASSIGN(tmp_jont, robot_->GetJointPositions());
  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());
  EXPECT_THAT(waypoints.back(),
              IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-3));
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}
TEST_P(MotionPlannerTest, LinearMoveFailsForDifferentDynamicLimitSettings) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);

    *motion_segment->mutable_cartesian_limits() =
        CreateDynamicCartesianLimitsProto(
            /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0 + i,
            /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
  }
  MotionPlannerConfiguration motion_planner_config;

  EXPECT_THAT(
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Motion segment 1 contains different Cartesian limits "
                         "than the previous segment. We currently do not "
                         "support neighboring segments of type 'LINEAR' that "
                         "have different Cartesian limits.")));
}

TEST_P(MotionPlannerTest, LinearMoveFailsForDifferentCollisionSettings) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
  }
  *motion_spec_.mutable_motion_segments(0)->mutable_collision_settings() =
      ParseTextProtoOrDie(R"pb(
        minimum_margin: 0.1
      )pb");
  MotionPlannerConfiguration motion_planner_config;

  EXPECT_THAT(
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Motion segment 1 defines different collision settings than "
              "the previous segment. We currently do not support segments with "
              "different collision settings for blended Cartesian motions.")));
}

TEST_P(MotionPlannerTest, LinearMoveWorksForCustomedJointLimits) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    *motion_segment->mutable_joint_limits() = CreateJointLimitsUpdateProto(
        planning_limits_.max_velocity, planning_limits_.max_acceleration,
        planning_limits_.max_jerk);
    *motion_segment->mutable_cartesian_limits() =
        CreateDynamicCartesianLimitsProto(
            /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
            /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
  }
  MotionPlannerConfiguration motion_planner_config;

  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));
  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());
  EXPECT_THAT(waypoints.back(),
              IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-3));

  for (const auto& state : planning_result.trajectory.data()) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(const LimitCheckResult limit_check_result,
                                      IsWithinLimits(state, planning_limits_));
    EXPECT_TRUE(limit_check_result.a_ok);
    EXPECT_TRUE(limit_check_result.p_ok);
    EXPECT_TRUE(limit_check_result.v_ok);
  }
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, LinearMoveFailsWithDifferentJointLimits) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    *motion_segment->mutable_joint_limits() = CreateJointLimitsUpdateProto(
        planning_limits_.max_velocity,
        (1 + i) * planning_limits_.max_acceleration, planning_limits_.max_jerk);
    *motion_segment->mutable_cartesian_limits() =
        CreateDynamicCartesianLimitsProto(
            /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
            /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
  }
  MotionPlannerConfiguration motion_planner_config;

  EXPECT_THAT(
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Motion segment 1 contains different joint limits than the "
              "previous segment. We currently do not support neighboring "
              "segments of type 'LINEAR' that have different joint limits.")));
}

// Test that nothing crashes when setting limits as path constraint and we are
// still able to compute a linear trajectory for joint limits that are not
// restricting the trajectory and that we fail with the correct error message
// for joint limit that mess up the path or initial and final configuration.
// This tests for uniformly setting the position joint limits for linear move
// segments.
TEST_P(MotionPlannerTest, LinearMoveWorksForCustomedPositionLimits) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));

  eigenmath::VectorNd max_position(6), min_position(6);
  max_position << 4.0, 0.8, 2.0, 4.0, 4.0, 4.0;
  min_position << -3.0, -3.0, -2.0, -3.0, -3.0, -3.0;

  // Case 1: Restrict joint limits but not such that it would interfere with the
  // resulting trajectory. Expect success.
  {
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
      *motion_segment->mutable_target()->mutable_cartesian_pose() =
          CreatePoseEqualityGeometricConstraint(
              robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
      *motion_segment->mutable_joint_limits() = CreateJointLimitsUpdateProto(
          planning_limits_.max_velocity, planning_limits_.max_acceleration,
          planning_limits_.max_jerk, min_position, max_position);
      *motion_segment->mutable_cartesian_limits() =
          CreateDynamicCartesianLimitsProto(
              /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
              /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
    }
    MotionPlannerConfiguration motion_planner_config;

    ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                         motion_planner->PlanTrajectory(
                             *object_world_, robot_specification_, motion_spec_,
                             motion_planner_config, run_time_flags_));

    // Assert that the last configuration brings the robot to the expected pose
    ASSERT_GE(planning_result.trajectory.size(), 1);
    ASSERT_OK(robot_->SetJointPositions(
        planning_result.trajectory.data().back().position));
    ASSERT_OK_AND_ASSIGN(const auto tip_id,
                         tip_frame_->GetTransformOriginEntityId());
    EXPECT_THAT(waypoints.back(),
                IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-3));

    JointLimits applied_planning_limits = planning_limits_;
    applied_planning_limits.max_position = max_position;
    applied_planning_limits.min_position = min_position;
    for (const auto& state : planning_result.trajectory.data()) {
      INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
          const LimitCheckResult limit_check_result,
          IsWithinLimits(state, applied_planning_limits));
      EXPECT_TRUE(limit_check_result.p_ok);
    }
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 2: Restrict joint values such that it interferes with the path, but
  // not the initial and final configuration. Note limit in joint 3 has been
  // specifically chosen for the waypoints selected.
  {
    max_position << 4.0, 0.8, 1.85, 4.0, 4.0, 4.0;
    min_position << -3.0, -3.0, -2.0, -3.0, -3.0, -3.0;
    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
      *motion_segment->mutable_target()->mutable_cartesian_pose() =
          CreatePoseEqualityGeometricConstraint(
              robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
      *motion_segment->mutable_joint_limits() = CreateJointLimitsUpdateProto(
          planning_limits_.max_velocity, planning_limits_.max_acceleration,
          planning_limits_.max_jerk, min_position, max_position);
      *motion_segment->mutable_cartesian_limits() =
          CreateDynamicCartesianLimitsProto(
              /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
              /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
    }
    MotionPlannerConfiguration motion_planner_config;

      // There can be multiple
      // `MotionSegment`s per one `TrajectorySegment`, so we expect the error to
      // be reported w.r.t. to the multiple segments.
      EXPECT_THAT(
          motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                         motion_spec_, motion_planner_config,
                                         run_time_flags_),
          StatusIs(
              absl::StatusCode::kNotFound,
              AllOf(
                  HasSubstr("Could not solve FinePathIK without "
                            "excessive change in joint config."),
                  HasSubstr(
                      "Error occurred during planning of segments 0 to 1."))));
  }

  // Case 3: Restrict joint values such that it interferes with the starting
  // configuration.
  {
    max_position << 1.0, 0.8, 1.85, 4.0, 4.0, 4.0;
    min_position << -3.0, -3.0, -2.0, -3.0, -3.0, -3.0;
    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
      *motion_segment->mutable_target()->mutable_cartesian_pose() =
          CreatePoseEqualityGeometricConstraint(
              robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
      *motion_segment->mutable_joint_limits() = CreateJointLimitsUpdateProto(
          planning_limits_.max_velocity, planning_limits_.max_acceleration,
          planning_limits_.max_jerk, min_position, max_position);
      *motion_segment->mutable_cartesian_limits() =
          CreateDynamicCartesianLimitsProto(
              /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
              /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
    }
    MotionPlannerConfiguration motion_planner_config;

    // Restriction of segment limits should cause an error stating 'is not
    // within joint limits'.
    EXPECT_THAT(motion_planner->PlanTrajectory(
                    *object_world_, robot_specification_, motion_spec_,
                    motion_planner_config, run_time_flags_),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("is not within joint limits")));
  }

  // Case 4: Restrict joint values such that it interferes with the end
  // configuration.
  {
    max_position << 4.0, 0.8, 2.0, 4.0, 4.0, 4.0;
    min_position << -3.0, -3.0, 1.1, -3.0, -3.0, -3.0;
    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
      *motion_segment->mutable_target()->mutable_cartesian_pose() =
          CreatePoseEqualityGeometricConstraint(
              robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
      *motion_segment->mutable_joint_limits() = CreateJointLimitsUpdateProto(
          planning_limits_.max_velocity, planning_limits_.max_acceleration,
          planning_limits_.max_jerk, min_position, max_position);
      *motion_segment->mutable_cartesian_limits() =
          CreateDynamicCartesianLimitsProto(
              /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
              /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
    }
    MotionPlannerConfiguration motion_planner_config;

    // Restriction of limit for segment should cause an IK error for the motion
    // target.
    EXPECT_THAT(
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_),
        StatusIs(absl::StatusCode::kNotFound,
                 HasSubstr("IK solver couldn't find any solutions for the "
                           "given constraints.")));
  }
}

// Test detection that joint limits set as segment path constraint are outside
// the application limits of the robot.
TEST_P(MotionPlannerTest,
       LinearMoveFailsForSegmentJointPositionLimitsAreOutsideSystemLimits) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));

  eigenmath::VectorNd max_position(6), min_position(6);
  max_position << 8.0, 4.0, 4.0, 4.0, 4.0, 4.0;
  min_position << -3.0, -3.0, -3.0, -3.0, -3.0, -3.0;

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    *motion_segment->mutable_joint_limits() =
        CreateJointLimitsUpdateProtoFromPositionLimits(min_position,
                                                       max_position);
  }
  MotionPlannerConfiguration motion_planner_config;

  EXPECT_THAT(
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Provided joint limits violate the application limits.")));
}

// Test detection of invalid joint limits set as segment path limits. Invalid
// because min and max are switched.
TEST_P(MotionPlannerTest,
       LinearMoveFailsForInvalidSegmentJointLimitDefinition) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));

  eigenmath::VectorNd max_position(6), min_position(6);
  max_position << 4.0, 4.0, 4.0, 4.0, 4.0, 4.0;
  min_position << -3.0, -3.0, -3.0, -3.0, -3.0, -3.0;

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    *motion_segment->mutable_joint_limits() = CreateJointLimitsUpdateProto(
        planning_limits_.max_velocity, planning_limits_.max_acceleration,
        planning_limits_.max_jerk, max_position, min_position);
    *motion_segment->mutable_cartesian_limits() =
        CreateDynamicCartesianLimitsProto(
            /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
            /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
  }
  MotionPlannerConfiguration motion_planner_config;

  EXPECT_THAT(
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr(
                   "Minimum position limits exceed maximum position limits.")));
}

// For linear move, we do not support joint position limits that differ for the
// different segment. Test checks that if different position  limits are
// provided for different segments that the motion planner fails.
TEST_P(MotionPlannerTest, LinearMoveFailsForDifferrentPositionLimits) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));

  eigenmath::VectorNd max_position(6), min_position(6);
  max_position << 4.0, 0.8, 2.0, 4.0, 4.0, 4.0;
  min_position << -3.0, -3.0, -2.0, -3.0, -3.0, -3.0;

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    *motion_segment->mutable_joint_limits() = CreateJointLimitsUpdateProto(
        planning_limits_.max_velocity, planning_limits_.max_acceleration,
        planning_limits_.max_jerk, min_position, max_position);
    *motion_segment->mutable_cartesian_limits() =
        CreateDynamicCartesianLimitsProto(
            /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
            /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
    min_position[3] += 0.2;
  }
  MotionPlannerConfiguration motion_planner_config;

  EXPECT_THAT(
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Motion segment 1 contains joint position limits that are "
                    "not identical to the limits in the segment before. We "
                    "currently do not support neighboring segments of type "
                    "'LINEAR' that have different joint position limits.")));
}

TEST_P(MotionPlannerTest, LinearMoveFailsForWrongPositionLimitsDimension) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));

  eigenmath::VectorNd max_position(5), min_position(6);
  max_position << 4.0, 4.0, 4.0, 4.0, 4.0;
  min_position << -3.0, -3.0, -3.0, -3.0, -3.0, -3.0;

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    *motion_segment->mutable_joint_limits() = CreateJointLimitsUpdateProto(
        planning_limits_.max_velocity, planning_limits_.max_acceleration,
        planning_limits_.max_jerk, min_position, max_position);
    *motion_segment->mutable_cartesian_limits() =
        CreateDynamicCartesianLimitsProto(
            /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
            /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
  }
  MotionPlannerConfiguration motion_planner_config;

  EXPECT_THAT(
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("position limits in motion segment are of different "
                         "size then expected")));
}

TEST_P(MotionPlannerTest, LinearMoveWorksForCartesianCurveParameters) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {0.0374165, -1.97835,  2.08278,
                                             0.344806,  -0.110898, -0.34285};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  Pose3d root_t_waypoint = toPose3d(
      "0.175127, 0.533182, 0.822006, 0.00243315, -0.707967, 0.00293097, "
      "0.706235");

  auto* motion_segment = motion_spec_.add_motion_segments();
  intrinsic_proto::motion_planning::v1::CartesianBlendingParameters curve_proto;
  curve_proto.set_rotational_corner_rounding(0.1);
  curve_proto.set_translation_corner_rounding(0.1);
  *motion_spec_.mutable_curve_parameters()->mutable_cartesian_blending() =
      curve_proto;

  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  *motion_segment->mutable_target()->mutable_cartesian_pose() =
      CreatePoseEqualityGeometricConstraint(
          robot_->GetName(), tip_frame_->GetName(), root_t_waypoint);
  *motion_segment->mutable_cartesian_limits() =
      CreateDynamicCartesianLimitsProto(
          /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
          /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
  MotionPlannerConfiguration motion_planner_config;

  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));
  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());
  EXPECT_THAT(root_t_waypoint,
              IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-4));
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, JointMoveWorksForSinglePoseTarget) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  Pose3d root_t_waypoint =
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065");

  auto* motion_segment = motion_spec_.add_motion_segments();
  *motion_segment->mutable_target()->mutable_cartesian_pose() =
      CreatePoseEqualityGeometricConstraint(
          robot_->GetName(), tip_frame_->GetName(), root_t_waypoint);
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  EXPECT_LT(
      (planning_result.trajectory.data().front().position - joint_configuration)
          .norm(),
      1e-8);
  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));
  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());
  EXPECT_THAT(root_t_waypoint,
              IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-3));
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);

  // Check path segments are returned correctly.
  EXPECT_EQ(planning_result.path_segments.size(),
            motion_spec_.motion_segments_size());
  EXPECT_EQ(planning_result.path_segments.front().joint_configurations.size(),
            2);
  EXPECT_LT(
      (planning_result.path_segments.front().joint_configurations.front() -
       joint_configuration)
          .norm(),
      1e-8);
  EXPECT_LT((planning_result.path_segments.front().joint_configurations.back() -
             planning_result.trajectory.data().back().position)
                .norm(),
            1e-8);
}

TEST_P(MotionPlannerTest, JointMoveWorksForMultiplePoseTarget) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
  }
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));
  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());
  EXPECT_LT(
      (planning_result.trajectory.data().front().position - joint_configuration)
          .norm(),
      1e-8);
  EXPECT_THAT(waypoints.back(),
              IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-3));
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, JointMoveWorksForSingleJointTarget) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());

    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  }
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  EXPECT_LT(
      (planning_result.trajectory.data().front().position - joint_configuration)
          .norm(),
      1e-8);
  EXPECT_LT(
      (planning_result.trajectory.data().back().position - waypoints.back())
          .norm(),
      1e-8);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, JointMoveWorksForMultipleJointTarget) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});
  waypoints.push_back(eigenmath::VectorNd{
      {1.13851, -1.06023, 1.04918, -0.658946, -1.29353, -0.322911}});

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());

    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  }
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  EXPECT_LT(
      (planning_result.trajectory.data().front().position - joint_configuration)
          .norm(),
      1e-8);
  EXPECT_LT(
      (planning_result.trajectory.data().back().position - waypoints.back())
          .norm(),
      1e-8);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, JointMoveWorksForMixedTargets) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  // First motion segment with a Cartesian pose as waypoint.
  auto* motion_segment = motion_spec_.add_motion_segments();
  *motion_segment->mutable_target()->mutable_cartesian_pose() =
      CreatePoseEqualityGeometricConstraint(
          robot_->GetName(), tip_frame_->GetName(),
          toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);

  // Second motion segment with a joint position target.
  motion_segment = motion_spec_.add_motion_segments();
  eigenmath::VectorNd configuration = eigenmath::VectorNd{
      {1.13851, -1.06023, 1.04918, -0.658946, -1.29353, -0.322911}};
  VectorNdToRepeatedDouble(configuration, motion_segment->mutable_target()
                                              ->mutable_joint_position()
                                              ->mutable_joints());
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);

  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  EXPECT_LT(
      (planning_result.trajectory.data().front().position - joint_configuration)
          .norm(),
      1e-8);
  EXPECT_LT((planning_result.trajectory.data().back().position - configuration)
                .norm(),
            1e-8);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, JointMoveWorksForCustomedJointLimits) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});
  waypoints.push_back(eigenmath::VectorNd{
      {1.13851, -1.06023, 1.04918, -0.658946, -1.29353, -0.322911}});

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());

    *motion_segment->mutable_joint_limits() = CreateJointLimitsUpdateProto(
        planning_limits_.max_velocity, planning_limits_.max_acceleration,
        planning_limits_.max_jerk);
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  }
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  EXPECT_LT(
      (planning_result.trajectory.data().front().position - joint_configuration)
          .norm(),
      1e-8);
  EXPECT_LT(
      (planning_result.trajectory.data().back().position - waypoints.back())
          .norm(),
      1e-8);
  // Check that we stay in the set limits.
  for (const auto& state : planning_result.trajectory.data()) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(const LimitCheckResult limit_check_result,
                                      IsWithinLimits(state, planning_limits_));
    EXPECT_TRUE(limit_check_result.a_ok);
    EXPECT_TRUE(limit_check_result.p_ok);
    EXPECT_TRUE(limit_check_result.v_ok);
  }
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

// Test is covering the following things: (1) Setting path limits that are
// identical for different segments that do not change the solution works and
// will not cause issues. (2) Changing the limit such that one of the waypoints
// can not generate a solution will cause a ComputeIk error. (3) Setting the
// path segment limits such that the initial configuration is out of limits will
// not cause a crash and result in a initial configuration is out of limits
// error.
TEST_P(MotionPlannerTest, JointMoveWorksForCustomedPositionLimits) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});
  waypoints.push_back(eigenmath::VectorNd{
      {1.13851, -1.06023, 1.04918, -0.658946, -1.29353, -0.322911}});

  // Working limit
  {
    ASSERT_OK_AND_ASSIGN(
        JointLimitsXd planning_limits,
        GetApplicationLimits(world_, robot_->GetRobotEntityId()));
    planning_limits.max_position << 4.0, 0.8, 2.0, 4.0, 4.0, 4.0;
    planning_limits.min_position << -3.0, -3.0, -2.0, -3.0, -3.0, -3.0;

    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      *motion_segment->mutable_joint_limits() =
          CreateJointLimitsUpdateProtoFromPositionLimits(
              planning_limits.min_position, planning_limits.max_position);
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
    }
    MotionPlannerConfiguration motion_planner_config;

    ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                         motion_planner->PlanTrajectory(
                             *object_world_, robot_specification_, motion_spec_,
                             motion_planner_config, run_time_flags_));

    // Assert that the last configuration brings the robot to the expected
    // pose
    ASSERT_GE(planning_result.trajectory.size(), 1);
    EXPECT_LT(
        (planning_result.trajectory.data().back().position - waypoints.back())
            .norm(),
        1e-8);

    for (const auto& state : planning_result.trajectory.data()) {
      INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
          const LimitCheckResult limit_check_result,
          IsWithinLimits(state, planning_limits_));
      EXPECT_TRUE(limit_check_result.p_ok);
    }
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 2: Waypoint pose Ik solutions are outside the segment limits. Setting
  // path segment limits will result in an 'outside of application limits'
  // error.
  {
    ASSERT_OK_AND_ASSIGN(
        JointLimitsXd planning_limits,
        GetApplicationLimits(world_, robot_->GetRobotEntityId()));
    planning_limits.max_position << 4.0, 0.8, 1.9, 4.0, 4.0, 4.0;
    planning_limits.min_position << -3.0, -3.0, -2.0, -3.0, -3.0, -3.0;

    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());

      *motion_segment->mutable_joint_limits() =
          CreateJointLimitsUpdateProtoFromPositionLimits(
              planning_limits.min_position, planning_limits.max_position);
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
    }
    MotionPlannerConfiguration motion_planner_config;

    EXPECT_THAT(
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_),
        StatusIs(absl::StatusCode::kInvalidArgument,
                 HasSubstr("is outside the joint configuration limits")));
  }

  // Case 3: Start configuration is not within limits. Make sure this is
  // resulting in the right error message.
  {
    ASSERT_OK_AND_ASSIGN(
        JointLimitsXd planning_limits,
        GetApplicationLimits(world_, robot_->GetRobotEntityId()));
    planning_limits.max_position << 1.2, 0.8, 2.0, 4.0, 4.0, 4.0;
    planning_limits.min_position << -3.0, -3.0, -2.0, -3.0, -3.0, -3.0;

    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      *motion_segment->mutable_joint_limits() =
          CreateJointLimitsUpdateProtoFromPositionLimits(
              planning_limits.min_position, planning_limits.max_position);
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
    }
    MotionPlannerConfiguration motion_planner_config;

    EXPECT_THAT(motion_planner->PlanTrajectory(
                    *object_world_, robot_specification_, motion_spec_,
                    motion_planner_config, run_time_flags_),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("is not within joint limits")));
  }
}

// Tests that we can set differential joint limits (velocity and acceleration)
// for different motion segments in the request. Those are differently handled
// from the position limits and therefore require a different test.
TEST_P(MotionPlannerTest, JointMoveWorksWithDifferentJointLimits) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});
  waypoints.push_back(eigenmath::VectorNd{
      {1.13851, -1.06023, 1.04918, -0.658946, -1.29353, -0.322911}});

  JointLimits planning_limits = planning_limits_;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
    planning_limits.max_acceleration =
        (1 + i) * planning_limits.max_acceleration;
    *motion_segment->mutable_joint_limits() = CreateJointLimitsUpdateProto(
        planning_limits.max_velocity, planning_limits.max_acceleration,
        planning_limits.max_jerk);
  }
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected
  // pose
  ASSERT_GE(planning_result.trajectory.size(), 2);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest,
       JointMoveFailsForPathPositionLimitsOutsideSystemLimits) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});

  ASSERT_OK_AND_ASSIGN(
      JointLimitsXd planning_limits,
      GetApplicationLimits(world_, robot_->GetRobotEntityId()));
  planning_limits.max_position << 4.0, 9.0, 4.0, 4.0, 4.0, 4.0;
  planning_limits.min_position << -3.0, -3.0, -3.0, -3.0, -3.0, -3.0;

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());

    *motion_segment->mutable_joint_limits() =
        CreateJointLimitsUpdateProtoFromPositionLimits(
            planning_limits.min_position, planning_limits.max_position);
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  }
  MotionPlannerConfiguration motion_planner_config;

  EXPECT_THAT(
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Provided joint limits violate the application limits")));
}

TEST_P(MotionPlannerTest, JointMoveWorksForDifferentPositionLimits) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});
  waypoints.push_back(eigenmath::VectorNd{
      {1.13851, -1.06023, 1.04918, -0.658946, -1.29353, -0.322911}});

  ASSERT_OK_AND_ASSIGN(
      JointLimitsXd planning_limits,
      GetApplicationLimits(world_, robot_->GetRobotEntityId()));

  // Case 1: Setting them differently in the different segments works and
  // results in a valid trajectory.
  {
    planning_limits.max_position << 4.0, 0.9, 2.8, 4.0, 4.0, 4.0;
    planning_limits.min_position << -4.0, -3.0, -2.8, -4.0, -4.0, -4.0;

    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      planning_limits.max_position = 0.9 * planning_limits.max_position;
      *motion_segment->mutable_joint_limits() =
          CreateJointLimitsUpdateProtoFromPositionLimits(
              planning_limits.min_position, planning_limits.max_position);
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
    }
    MotionPlannerConfiguration motion_planner_config;

    ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                         motion_planner->PlanTrajectory(
                             *object_world_, robot_specification_, motion_spec_,
                             motion_planner_config, run_time_flags_));

    // Assert that the last configuration brings the robot to the expected
    // pose
    ASSERT_GE(planning_result.trajectory.size(), 1);
    EXPECT_LT(
        (planning_result.trajectory.data().back().position - waypoints.back())
            .norm(),
        1e-8);
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 2: Setting the limits in the second segment such that it not allows to
  // find an Ik solution for the pose will lead to the correct error message.
  {
    planning_limits.max_position << 4.0, 0.9, 2.8, 4.0, 4.0, 4.0;
    planning_limits.min_position << -4.0, -3.0, -2.8, -4.0, -4.0, -4.0;

    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      *motion_segment->mutable_joint_limits() =
          CreateJointLimitsUpdateProtoFromPositionLimits(
              planning_limits.min_position, planning_limits.max_position);
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
      planning_limits.max_position[2] = 1.9;
    }
    MotionPlannerConfiguration motion_planner_config;

    EXPECT_THAT(
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_),
        StatusIs(absl::StatusCode::kInvalidArgument,
                 HasSubstr("is outside the joint configuration limits")));
  }

  // Case 3: Setting the limits in the second segment such that it would limit
  // the first path of the motion part but not the second still produces a valid
  // trajectory.
  {
    planning_limits.max_position << 4.0, 0.9, 2.8, 4.0, 4.0, 4.0;
    planning_limits.min_position << -4.0, -3.0, -2.8, -4.0, -4.0, -4.0;

    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      *motion_segment->mutable_joint_limits() =
          CreateJointLimitsUpdateProtoFromPositionLimits(
              planning_limits.min_position, planning_limits.max_position);
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
      planning_limits.max_position[0] = 1.2;
    }
    MotionPlannerConfiguration motion_planner_config;

    ASSERT_OK_AND_ASSIGN(
        const MotionPlanner::PlanTrajectoryResult planning_result,
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_));

    // Assert that the last configuration brings the robot to the expected
    // pose
    ASSERT_GE(planning_result.trajectory.size(), 1);
    EXPECT_LT(
        (planning_result.trajectory.data().back().position - waypoints.back())
            .norm(),
        1e-8);
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }
}

// Checking that we get the correct error message when switching max and min
// position limits in the path segment constraints.
TEST_P(MotionPlannerTest, JointMoveFailsForInvalidPositionLimits) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});
  waypoints.push_back(eigenmath::VectorNd{
      {1.13851, -1.06023, 1.04918, -0.658946, -1.29353, -0.322911}});

  ASSERT_OK_AND_ASSIGN(
      JointLimitsXd planning_limits,
      GetApplicationLimits(world_, robot_->GetRobotEntityId()));

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());
    *motion_segment->mutable_joint_limits() =
        CreateJointLimitsUpdateProtoFromPositionLimits(
            planning_limits.max_position, planning_limits.min_position);
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  }
  MotionPlannerConfiguration motion_planner_config;

  EXPECT_THAT(
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr(
                   "Minimum position limits exceed maximum position limits.")));
}

TEST_P(MotionPlannerTest, JointMoveFailsForInvalidPositionLimitsDimensions) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});

  eigenmath::VectorNd max_position(5), min_position(6);
  max_position << 4.0, 4.0, 4.0, 4.0, 4.0;
  min_position << -3.0, -3.0, -3.0, -3.0, -3.0, -3.0;

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());
    *motion_segment->mutable_joint_limits() =
        CreateJointLimitsUpdateProtoFromPositionLimits(min_position,
                                                       max_position);
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  }
  MotionPlannerConfiguration motion_planner_config;

  EXPECT_THAT(
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("position limits in motion segment are of different "
                         "size then expected")));
}

TEST_P(MotionPlannerTest, JointMoveWorksForCartesianLimits) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});
  waypoints.push_back(eigenmath::VectorNd{
      {1.13851, -1.06023, 1.04918, -0.658946, -1.29353, -0.322911}});

  // Case 1: Setting reasonable limits, the motion planner returns a valid
  // trajectory.
  {
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      *motion_segment->mutable_joint_limits() = CreateJointLimitsUpdateProto(
          planning_limits_.max_velocity, planning_limits_.max_acceleration,
          planning_limits_.max_jerk);
      if (i == 1) {
        *motion_segment->mutable_cartesian_limits() =
            CreateDynamicCartesianLimitsProto(
                /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
                /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
      }
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
    }
    MotionPlannerConfiguration motion_planner_config;
    ASSERT_OK_AND_ASSIGN(
        const MotionPlanner::PlanTrajectoryResult planning_result,
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_));

    // Assert that the last configuration brings the robot to the expected
    // pose
    ASSERT_GE(planning_result.trajectory.size(), 1);
    EXPECT_LT(
        (planning_result.trajectory.data().back().position - waypoints.back())
            .norm(),
        1e-8);
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 2: Setting extremely low (==0), but valid, Cartesian limits leads to
  // failed attempt to compute the trajectory.
  {
    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      *motion_segment->mutable_joint_limits() = CreateJointLimitsUpdateProto(
          planning_limits_.max_velocity, planning_limits_.max_acceleration,
          planning_limits_.max_jerk);
      if (i == 1) {
        *motion_segment->mutable_cartesian_limits() =
            CreateDynamicCartesianLimitsProto(
                /*max_rot_vel*/ 0, /*max_rot_accel*/ 5.0,
                /*max_trans_vel*/ 0, /*max_trans_accel*/ 2.0);
      }
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
    }
    MotionPlannerConfiguration motion_planner_config;
    EXPECT_THAT(
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_),
        StatusIs(
            Not(absl::StatusCode::kOk),
            AllOf(HasSubstr("Not able to compute"),
                  HasSubstr("trajectory for formulated planning problem"))));
  }
}

TEST_P(MotionPlannerTest, MotionPlannerFailsForInvalidCartesianLimits) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});
  waypoints.push_back(eigenmath::VectorNd{
      {1.13851, -1.06023, 1.04918, -0.658946, -1.29353, -0.322911}});

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());
    *motion_segment->mutable_joint_limits() = CreateJointLimitsUpdateProto(
        planning_limits_.max_velocity, planning_limits_.max_acceleration,
        planning_limits_.max_jerk);
    if (i == 1) {
      *motion_segment->mutable_cartesian_limits() =
          CreateDynamicCartesianLimitsProto(
              /*max_rot_vel*/ -1.0, /*max_rot_accel*/ 5.0,
              /*max_trans_vel*/ -0.6, /*max_trans_accel*/ 2.0);
    }
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  }
  MotionPlannerConfiguration motion_planner_config;
  EXPECT_THAT(motion_planner->PlanTrajectory(
                  *object_world_, robot_specification_, motion_spec_,
                  motion_planner_config, run_time_flags_),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Cartesian limits are invalid")));
}

// Tests that no error is thrown when setting Cartesian curve parameter during
// joint move.
TEST_P(MotionPlannerTest, JointMoveWorksWhenSettingCartesianCurveParameters) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});

  intrinsic_proto::motion_planning::v1::CartesianBlendingParameters curve_proto;
  curve_proto.set_rotational_corner_rounding(0.1);
  curve_proto.set_translation_corner_rounding(0.1);

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
    *motion_spec_.mutable_curve_parameters()->mutable_cartesian_blending() =
        curve_proto;
  }
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, JointMoveWorksForJointCurveParameters) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});

  intrinsic_proto::motion_planning::v1::JointBlendingParameters curve_proto;
  curve_proto.set_desired_tightness_rad(0.1);

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
    *motion_spec_.mutable_curve_parameters()->mutable_joint_blending() =
        curve_proto;
  }
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  EXPECT_LT(
      (planning_result.trajectory.data().back().position - waypoints.back())
          .norm(),
      1e-8);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

// The following test executes a collision free trajectory between the initial
// and desired joint configuration. The path is designed to be collision free.
// Collision testing is performed.
TEST_P(MotionPlannerTest, JointMoveWorksForSmallCollisionMargins) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {-0.10, 0.48, -1.36,
                                             -0.77, 0.83, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {-0.107039, 0.481256, -1.3636, -0.775345, 0.834874, -1.03907}});

  // Impose a small non-zero margin for collision checking. Planning should
  // still succeed.
  intrinsic_proto::world::CollisionSettings small_margin_settings;
  small_margin_settings.set_disable_collision_checking(false);
  constexpr double kMinimumMargin = 0.01;  // 1 cm.
  small_margin_settings.set_minimum_margin(kMinimumMargin);

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());
    *motion_segment->mutable_collision_settings() = small_margin_settings;
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  }
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  EXPECT_LT(
      (planning_result.trajectory.data().back().position - waypoints.back())
          .norm(),
      1e-8);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

// The following test executes a collision free trajectory between the
// initial and desired joint configuration. The path is designed to be
// collision free. Collision testing is performed.
TEST_P(MotionPlannerTest, JointMoveWorksForLargeCollisionMargins) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {-0.10, 0.48, -1.36,
                                             -0.77, 0.83, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {-0.107039, 0.481256, -1.3636, -0.775345, 0.834874, -1.03907}});

  // Impose a huge minimum margin for collision checking. Planning should
  // fail.
  intrinsic_proto::world::CollisionSettings large_margin_settings;
  large_margin_settings.set_disable_collision_checking(false);
  constexpr double kMinimumMargin = 100.0;  // m. Huge margin.
  large_margin_settings.set_minimum_margin(kMinimumMargin);

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());
    *motion_segment->mutable_collision_settings() = large_margin_settings;
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  }
  MotionPlannerConfiguration motion_planner_config;
  EXPECT_THAT(
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_),
      StatusIs(absl::StatusCode::kNotFound,
               HasSubstr("IK could not find a collision free configuration.")));
}

// The following test attempts to plan a movement to a collision free
// configuration where the pathway between initial and desired is not
// collision free. By default, collision checking should be enabled and the
// request should fail.
TEST_P(MotionPlannerTest, CollisionCheckingIsEnabledByDefault) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {-0.10, 0.48, -1.36,
                                             -0.77, 0.83, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  eigenmath::VectorNd configuration(6);
  configuration << -0.900000, 0.4, -0.1194, -0.705302, 0.673298, -1.554707;

  auto* motion_segment = motion_spec_.add_motion_segments();
  VectorNdToRepeatedDouble(configuration, motion_segment->mutable_target()
                                              ->mutable_joint_position()
                                              ->mutable_joints());
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);

  MotionPlannerConfiguration motion_planner_config;
  EXPECT_THAT(motion_planner->PlanTrajectory(
                  *object_world_, robot_specification_, motion_spec_,
                  motion_planner_config, run_time_flags_),
              StatusIs(absl::StatusCode::kNotFound,
                       HasSubstr("Path is not fulfilling path constraints.")));
}

// The following test attempts to plan a movement to a collision free
// configuration where the pathway between initial and desired is not
// collision free. With collision checking disabled this will succeed.
TEST_P(MotionPlannerTest, DisablingCollisionCheckingLocallyWorksForJointMove) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {-0.10, 0.48, -1.36,
                                             -0.77, 0.83, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  eigenmath::VectorNd configuration(6);
  configuration << -0.900000, 0.4, -0.1194, -0.705302, 0.673298, -1.554707;

  auto* motion_segment = motion_spec_.add_motion_segments();
  VectorNdToRepeatedDouble(configuration, motion_segment->mutable_target()
                                              ->mutable_joint_position()
                                              ->mutable_joints());
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);

  MotionPlannerConfiguration motion_planner_config;
  // Motion planning fails because collision checking is enabled by default.
  EXPECT_THAT(motion_planner->PlanTrajectory(
                  *object_world_, robot_specification_, motion_spec_,
                  motion_planner_config, run_time_flags_),
              StatusIs(absl::StatusCode::kNotFound,
                       HasSubstr("Path is not fulfilling path constraints.")));

  // Disabling collision checking will lead to a successful motion plan.
  intrinsic_proto::world::CollisionSettings collision_settings;
  collision_settings.set_disable_collision_checking(true);
  *motion_segment->mutable_collision_settings() = collision_settings;
  ASSERT_OK_AND_ASSIGN(
      const MotionPlanner::PlanTrajectoryResult planning_result,
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  EXPECT_LT((planning_result.trajectory.data().back().position - configuration)
                .norm(),
            1e-8);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

// Regression test for b/555408975:
// When collecting collision statistics (e.g.,
// `enable_distance_check_statistics` = true, which is set by default in
// `PlanTrajectoryWithDebugData()`), if a motion segment has collision checking
// disabled, the active kinematic proxy does not create an underlying
// `CollisionChecker` (`GetCollisionChecker()` returns nullptr). Verifies that
// `MotionPlanner::PlanTrajectory()` does not dereference nullptr and
// successfully plans the trajectory both when enabled via `MotionPlannerFlags`
// and when enabled via `RunTimeMotionPlannerFlags`.
TEST_P(MotionPlannerTest,
       DisablingCollisionCheckingLocallyWithDistanceCheckStatisticsSucceeds) {
  motion_planner_flags_.enable_distance_check_statistics = true;
  ASSERT_OK_AND_ASSIGN(const std::unique_ptr<MotionPlanner> motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  const eigenmath::Vector6d init_joint_config = {-0.10, 0.48, -1.36,
                                                 -0.77, 0.83, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            init_joint_config));

  const eigenmath::VectorNd target_joint_config = []() {
    eigenmath::VectorNd joint_config(6);
    joint_config << -0.9, 0.4, -0.1194, -0.705302, 0.673298, -1.554707;
    return joint_config;
  }();

  intrinsic_proto::motion_planning::v1::MotionSegment* motion_segment =
      motion_spec_.add_motion_segments();
  VectorNdToRepeatedDouble(target_joint_config, motion_segment->mutable_target()
                                                    ->mutable_joint_position()
                                                    ->mutable_joints());
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);

  // Disable collision checking locally on the segment.
  intrinsic_proto::world::CollisionSettings collision_settings;
  collision_settings.set_disable_collision_checking(true);
  *motion_segment->mutable_collision_settings() = collision_settings;

  const MotionPlannerConfiguration motion_planner_config;
  // Test 1: Statistics enabled via `MotionPlannerFlags`.
  ASSERT_OK_AND_ASSIGN(
      const MotionPlanner::PlanTrajectoryResult planning_result_flags,
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_));
  ASSERT_GE(planning_result_flags.trajectory.size(), 1);
  EXPECT_LT((planning_result_flags.trajectory.data().back().position -
             target_joint_config)
                .norm(),
            kTargetJointConfigErrorTolerance);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result_flags);

  // Test 2: Statistics enabled at runtime (matching
  // `PlanTrajectoryWithDebugData()`).
  MotionPlannerFlags default_flags = motion_planner_flags_;
  default_flags.enable_distance_check_statistics = false;
  ASSERT_OK_AND_ASSIGN(
      const std::unique_ptr<MotionPlanner> motion_planner_default,
      MotionPlanner::Create(default_flags));
  RunTimeMotionPlannerFlags runtime_flags = run_time_flags_;
  runtime_flags.enable_distance_check_statistics = true;
  ASSERT_OK_AND_ASSIGN(
      const MotionPlanner::PlanTrajectoryResult planning_result_runtime,
      motion_planner_default->PlanTrajectory(
          *object_world_, robot_specification_, motion_spec_,
          motion_planner_config, runtime_flags));
  ASSERT_GE(planning_result_runtime.trajectory.size(), 1);
  EXPECT_LT((planning_result_runtime.trajectory.data().back().position -
             target_joint_config)
                .norm(),
            kTargetJointConfigErrorTolerance);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result_runtime);
}

// The following test attempts to plan a movement to a collision free
// configuration where the pathway between initial and desired is not
// collision free. With collision checking disabled this will succeed.
TEST_P(MotionPlannerTest,
       LocalPathPlanningOverridesGlobalPathPlanningForSegments) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {-0.10, 0.48, -1.36,
                                             -0.77, 0.83, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  eigenmath::VectorNd configuration(6);
  configuration << -0.900000, 0.4, -0.1194, -0.705302, 0.673298, -1.554707;

  auto* motion_segment = motion_spec_.add_motion_segments();
  VectorNdToRepeatedDouble(configuration, motion_segment->mutable_target()
                                              ->mutable_joint_position()
                                              ->mutable_joints());
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);

  MotionPlannerConfiguration motion_planner_config;
  intrinsic_proto::world::CollisionSettings collision_settings_disabled;
  collision_settings_disabled.set_disable_collision_checking(true);
  *motion_segment->mutable_collision_settings() = collision_settings_disabled;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  EXPECT_LT(
      (planning_result.trajectory.data().front().position - joint_configuration)
          .norm(),
      1e-8);
  EXPECT_LT((planning_result.trajectory.data().back().position - configuration)
                .norm(),
            1e-8);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

// The following test attempts to plan a movement to a collision free
// configuration where the pathway between initial and desired is not
// collision free. With collision checking disabled this will succeed.
TEST_P(MotionPlannerTest, DisablingCollisionCheckingWorks) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {-0.10, 0.48, -1.36,
                                             -0.77, 0.83, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  // Target in collision.
  eigenmath::VectorNd configuration(6);
  configuration << -0.700000, 0.4, -0.096120, -0.705302, 0.673298, -1.554707;

  // Case 1: Collision checking is not disabled. No segment path constraint
  // defined. The planning request should fail due to collision.
  {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(configuration, motion_segment->mutable_target()
                                                ->mutable_joint_position()
                                                ->mutable_joints());
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);

    MotionPlannerConfiguration motion_planner_config;
    EXPECT_THAT(motion_planner->PlanTrajectory(
                    *object_world_, robot_specification_, motion_spec_,
                    motion_planner_config, run_time_flags_),
                StatusIs(absl::StatusCode::kNotFound, HasSubstr("collision")));
  }

  // Case 2: Collision checking switched off. Planning is
  // expected to succeed.
  {
    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(configuration, motion_segment->mutable_target()
                                                ->mutable_joint_position()
                                                ->mutable_joints());
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);

    MotionPlannerConfiguration motion_planner_config;
    intrinsic_proto::world::CollisionSettings collision_settings_disabled;
    collision_settings_disabled.set_disable_collision_checking(true);
    *motion_segment->mutable_collision_settings() = collision_settings_disabled;
    ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                         motion_planner->PlanTrajectory(
                             *object_world_, robot_specification_, motion_spec_,
                             motion_planner_config, run_time_flags_));

    // Assert that the last configuration brings the robot to the expected
    // pose
    ASSERT_GE(planning_result.trajectory.size(), 2);
    EXPECT_LT((planning_result.trajectory.data().front().position -
               joint_configuration)
                  .norm(),
              1e-8);
    EXPECT_LT(
        (planning_result.trajectory.data().back().position - configuration)
            .norm(),
        1e-8);
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }
}

TEST_P(MotionPlannerTest, PlannedMoveWorksIfStraightLinePossible) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});
  waypoints.push_back(eigenmath::VectorNd{
      {1.13851, -1.06023, 1.04918, -0.658946, -1.29353, -0.322911}});

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());
  }
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  EXPECT_LT(
      (planning_result.trajectory.data().front().position - joint_configuration)
          .norm(),
      1e-8);
  EXPECT_LT(
      (planning_result.trajectory.data().back().position - waypoints.back())
          .norm(),
      1e-8);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, PlannedMoveWorksIfStraightLineNotPossible) {
  // This test requires fallback trajectory generation to pass.
  motion_planner_flags_.enable_fallback_trajectory = true;
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {-0.10, 0.48, -1.36,
                                             -0.77, 0.83, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  eigenmath::VectorNd configuration(6);
  configuration << -0.900000, 0.4, -0.1194, -0.705302, 0.673298, -1.554707;

  auto* motion_segment = motion_spec_.add_motion_segments();
  VectorNdToRepeatedDouble(configuration, motion_segment->mutable_target()
                                              ->mutable_joint_position()
                                              ->mutable_joints());
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(
      const MotionPlanner::PlanTrajectoryResult planning_result,
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  EXPECT_LT(
      (planning_result.trajectory.data().front().position - joint_configuration)
          .norm(),
      1e-8);
  EXPECT_LT((planning_result.trajectory.data().back().position - configuration)
                .norm(),
            1e-8);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest,
       PlannedMoveWorksForSettingDifferentPathPositionConstraints) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});
  waypoints.push_back(eigenmath::VectorNd{
      {-0.900000, 0.4, -0.096120, -0.705302, 0.673298, -1.554707}});

  ASSERT_OK_AND_ASSIGN(
      JointLimitsXd planning_limits,
      GetApplicationLimits(world_, robot_->GetRobotEntityId()));

  // Case 1: Setting limits uniformly in the segments does not cause an failure.
  {
    planning_limits.max_position << 4.0, 0.8, 2.0, 4.0, 4.0, 4.0;
    planning_limits.min_position << -3.0, -3.0, -2.0, -3.0, -3.0, -3.0;

    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      *motion_segment->mutable_joint_limits() =
          CreateJointLimitsUpdateProtoFromPositionLimits(
              planning_limits.min_position, planning_limits.max_position);
    }
    MotionPlannerConfiguration motion_planner_config;
    ASSERT_OK_AND_ASSIGN(
        const MotionPlanner::PlanTrajectoryResult planning_result,
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_));

    // Assert that the last configuration brings the robot to the expected pose
    ASSERT_GE(planning_result.trajectory.size(), 1);
    EXPECT_LT((planning_result.trajectory.data().front().position -
               joint_configuration)
                  .norm(),
              1e-8);
    EXPECT_LT(
        (planning_result.trajectory.data().back().position - waypoints.back())
            .norm(),
        1e-8);
    for (const auto& state : planning_result.trajectory.data()) {
      INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
          const LimitCheckResult limit_check_result,
          IsWithinLimits(state, planning_limits));
      EXPECT_TRUE(limit_check_result.p_ok);
    }
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 2: Setting different limits in the two different segments does not
  // cause an failure if limits do not limit path. Choose second segment limits
  // such that they would interfere with first segment.
  {
    planning_limits.max_position << 4.0, 0.8, 2.5, 4.0, 4.0, 4.0;
    planning_limits.min_position << -3.0, -3.0, -2.0, -3.0, -3.0, -3.0;

    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      *motion_segment->mutable_joint_limits() =
          CreateJointLimitsUpdateProtoFromPositionLimits(
              planning_limits.min_position, planning_limits.max_position);
      planning_limits.max_position *= 0.9;
    }
    MotionPlannerConfiguration motion_planner_config;
    ASSERT_OK_AND_ASSIGN(
        const MotionPlanner::PlanTrajectoryResult planning_result,
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_));

    // Assert that the last configuration brings the robot to the expected pose
    ASSERT_GE(planning_result.trajectory.size(), 1);
    EXPECT_LT((planning_result.trajectory.data().front().position -
               joint_configuration)
                  .norm(),
              1e-8);
    EXPECT_LT(
        (planning_result.trajectory.data().back().position - waypoints.back())
            .norm(),
        1e-8);
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 3: Restrict limits in second segment such that they interfere with
  // first segment. As a result the motion should be feasible
  {
    planning_limits.max_position << 4.0, 0.8, 2.5, 4.0, 4.0, 4.0;
    planning_limits.min_position << -3.0, -3.0, -2.0, -3.0, -3.0, -3.0;

    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      *motion_segment->mutable_joint_limits() =
          CreateJointLimitsUpdateProtoFromPositionLimits(
              planning_limits.min_position, planning_limits.max_position);
      planning_limits.max_position[0] = 1.2;
    }
    MotionPlannerConfiguration motion_planner_config;
    ASSERT_OK_AND_ASSIGN(
        const MotionPlanner::PlanTrajectoryResult planning_result,
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_));

    // Assert that the last configuration brings the robot to the expected pose
    ASSERT_GE(planning_result.trajectory.size(), 1);
    EXPECT_LT((planning_result.trajectory.data().front().position -
               joint_configuration)
                  .norm(),
              1e-8);
    EXPECT_LT(
        (planning_result.trajectory.data().back().position - waypoints.back())
            .norm(),
        1e-8);
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 4: Do the same limit change as in Case 3, but in the first segment.
  // This should cause an out of limit error.
  {
    planning_limits.max_position << 1.2, 0.8, 2.5, 4.0, 4.0, 4.0;
    planning_limits.min_position << -3.0, -3.0, -2.0, -3.0, -3.0, -3.0;

    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      *motion_segment->mutable_joint_limits() =
          CreateJointLimitsUpdateProtoFromPositionLimits(
              planning_limits.min_position, planning_limits.max_position);
      planning_limits.max_position[0] = 4.0;
    }
    MotionPlannerConfiguration motion_planner_config;
    EXPECT_THAT(motion_planner->PlanTrajectory(
                    *object_world_, robot_specification_, motion_spec_,
                    motion_planner_config, run_time_flags_),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("is not within joint limits")));
  }
}

TEST_P(MotionPlannerTest, GetEmptyTrajectoryIfStartAndGoalAreIdendical) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {-0.10, 0.48, -1.36,
                                             -0.77, 0.83, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  auto* motion_segment = motion_spec_.add_motion_segments();
  VectorNdToRepeatedDouble(joint_configuration, motion_segment->mutable_target()
                                                    ->mutable_joint_position()
                                                    ->mutable_joints());
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(
      const MotionPlanner::PlanTrajectoryResult planning_result,
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 0);
  ASSERT_EQ(planning_result.path_segments.size(),
            motion_spec_.motion_segments_size());
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, GetEmptyTrajectoryIfStartAndGoalAreApproximate) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {-0.10, 0.48, -1.36,
                                             -0.77, 0.83, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  eigenmath::VectorNd noise(6);
  noise.setConstant(1e-5);

  auto* motion_segment = motion_spec_.add_motion_segments();
  VectorNdToRepeatedDouble(joint_configuration + noise,
                           motion_segment->mutable_target()
                               ->mutable_joint_position()
                               ->mutable_joints());
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(
      const MotionPlanner::PlanTrajectoryResult planning_result,
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 0);
  ASSERT_EQ(planning_result.path_segments.size(),
            motion_spec_.motion_segments_size());
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, JointMoveWorksIfWaypointHasNoMotion) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {-0.10, 0.48, -1.36,
                                             -0.77, 0.83, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(joint_configuration);
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());
  }

  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(
      const MotionPlanner::PlanTrajectoryResult planning_result,
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 2);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, JointMoveWorksIfWaypointIsCloseToPrevious) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {-0.10, 0.48, -1.36,
                                             -0.77, 0.83, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  eigenmath::VectorNd noise(6);
  noise.setConstant(1e-3);
  eigenmath::VectorNd noisy_start = joint_configuration + noise;

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(noisy_start);
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());
  }

  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(
      const MotionPlanner::PlanTrajectoryResult planning_result,
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 2);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest,
       PlannedMoveWorksWithGeometricConstraintsJointMoveJointEquality) {
  // This test requires fallback trajectory generation to pass.
  motion_planner_flags_.enable_fallback_trajectory = true;
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {-0.10, 0.48, -1.36,
                                             -0.77, 0.83, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  eigenmath::VectorNd configuration(6);
  configuration << -0.900000, 0.4, -0.1194, -0.705302, 0.673298, -1.554707;

  auto* motion_segment = motion_spec_.add_motion_segments();
  VectorNdToRepeatedDouble(configuration, motion_segment->mutable_target()
                                              ->mutable_joint_position()
                                              ->mutable_joints());
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(
      const MotionPlanner::PlanTrajectoryResult planning_result,
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  EXPECT_LT(
      (planning_result.trajectory.data().front().position - joint_configuration)
          .norm(),
      1e-8);
  EXPECT_LT((planning_result.trajectory.data().back().position - configuration)
                .norm(),
            1e-8);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, SettingPlanningTimeOutWorksForBigLimits) {
  // This test requires fallback trajectory generation to pass.
  motion_planner_flags_.enable_fallback_trajectory = true;
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {-0.10, 0.48, -1.36,
                                             -0.77, 0.83, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  eigenmath::VectorNd configuration(6);
  configuration << -0.900000, 0.4, -0.1194, -0.705302, 0.673298, -1.554707;

  auto* motion_segment = motion_spec_.add_motion_segments();
  VectorNdToRepeatedDouble(configuration, motion_segment->mutable_target()
                                              ->mutable_joint_position()
                                              ->mutable_joints());
  MotionPlannerConfiguration motion_planner_config;
  motion_planner_config.mutable_timeout_sec()->set_seconds(120);
  motion_planner_config.mutable_timeout_sec()->set_nanos(50000000);
  ASSERT_OK_AND_ASSIGN(
      const MotionPlanner::PlanTrajectoryResult planning_result,
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  EXPECT_LT(
      (planning_result.trajectory.data().front().position - joint_configuration)
          .norm(),
      1e-8);
  EXPECT_LT((planning_result.trajectory.data().back().position - configuration)
                .norm(),
            1e-8);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, SettingPlanningTimeOutTimesOutForSmallLimits) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {-0.10, 0.48, -1.36,
                                             -0.77, 0.83, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  eigenmath::VectorNd configuration(6);
  configuration << -0.900000, 0.4, -0.1194, -0.705302, 0.673298, -1.554707;

  auto* motion_segment = motion_spec_.add_motion_segments();
  VectorNdToRepeatedDouble(configuration, motion_segment->mutable_target()
                                              ->mutable_joint_position()
                                              ->mutable_joints());
  MotionPlannerConfiguration motion_planner_config;
  motion_planner_config.mutable_timeout_sec()->set_seconds(0);
  motion_planner_config.mutable_timeout_sec()->set_nanos(50000000);
  EXPECT_THAT(
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_),
      StatusIs(
          absl::StatusCode::kDeadlineExceeded,
          AllOf(HasSubstr(
                    "Planner could not find a path in the given time of "
                    "0.050 seconds. "
                    "Most likely reason is that there is no path that "
                    "satisfies the "
                    "given constraints or that the time out is set too low"),
                HasSubstr("Error occurred during planning in segment 0."))));
}

TEST_P(MotionPlannerTest, PlannedMoveWorksWithCollisionCheckingDisabled) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {1.1916, -1.47557, 1.94578, -1.78062, -1.02402, -0.450498}});
  waypoints.push_back(eigenmath::VectorNd{
      {1.13851, -1.06023, 1.04918, -0.658946, -1.29353, -0.322911}});

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());
    motion_segment->mutable_collision_settings()
        ->set_disable_collision_checking(true);
  }
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  EXPECT_LT(
      (planning_result.trajectory.data().front().position - joint_configuration)
          .norm(),
      1e-8);
  EXPECT_LT(
      (planning_result.trajectory.data().back().position - waypoints.back())
          .norm(),
      1e-8);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

TEST_P(MotionPlannerTest, PlannedMoveWorksForMultipleIdenticalPoseTargets) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  // Case 1: All segments are (almost) identical. Should result in zero motion.
  {
    std::vector<Pose3d> waypoints;
    waypoints.push_back(toPose3d(
        "0.00934103, 0.490983, 0.398881, -0.000474858, -0.0028313, -0.999979, "
        "0.00589545"));
    waypoints.push_back(toPose3d(
        "0.00934103, 0.490983, 0.398881, -0.000474858, -0.0028313, -0.999979, "
        "0.00589545"));
    waypoints.push_back(toPose3d(
        "0.00934103, 0.490983, 0.398881, -0.000474858, -0.0028313, -0.999979, "
        "0.00589545"));

    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      *motion_segment->mutable_target()->mutable_cartesian_pose() =
          CreatePoseEqualityGeometricConstraint(
              robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    }
    MotionPlannerConfiguration motion_planner_config;
    ASSERT_OK_AND_ASSIGN(const auto planning_result,
                         motion_planner->PlanTrajectory(
                             *object_world_, robot_specification_, motion_spec_,
                             motion_planner_config, run_time_flags_));

    // Expect an empty trajectory.
    EXPECT_EQ(planning_result.trajectory.size(), 2);
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 2: the first two segments have close to or identical poses to the
  // start pose, but still a final pose leads to a successful linear move.
  {
    std::vector<Pose3d> waypoints;
    waypoints.push_back(toPose3d(
        "0.00934103, 0.490983, 0.398881, -0.000474858, -0.0028313, -0.999979, "
        "0.00589545"));
    waypoints.push_back(toPose3d(
        "0.00934103, 0.490983, 0.398881, -0.000474858, -0.0028313, -0.999979, "
        "0.00589545"));
    waypoints.push_back(
        toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));

    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      *motion_segment->mutable_target()->mutable_cartesian_pose() =
          CreatePoseEqualityGeometricConstraint(
              robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    }
    MotionPlannerConfiguration motion_planner_config;
    ASSERT_OK_AND_ASSIGN(const auto planning_result,
                         motion_planner->PlanTrajectory(
                             *object_world_, robot_specification_, motion_spec_,
                             motion_planner_config, run_time_flags_));

    // Expect a trajectory of at least two data points that ends in the final
    // waypoint.
    ASSERT_GT(planning_result.trajectory.size(), 1);
    ASSERT_OK_AND_ASSIGN(
        World test_world,
        CloneAndSetJointValues(
            world_, robot_->GetName(),
            planning_result.trajectory.data().back().position));
    ASSERT_OK_AND_ASSIGN(const auto tip_id,
                         tip_frame_->GetTransformOriginEntityId());
    EXPECT_THAT(waypoints.back(),
                IsApprox(test_world.GetTransform(kRootEntityId, tip_id), 1e-3));
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 3: Only the first segment contain a motion worthy target, subsequent
  // segments are identical. Expect successful linear motion.
  {
    std::vector<Pose3d> waypoints;
    waypoints.push_back(
        toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
    waypoints.push_back(
        toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));

    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      *motion_segment->mutable_target()->mutable_cartesian_pose() =
          CreatePoseEqualityGeometricConstraint(
              robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    }
    MotionPlannerConfiguration motion_planner_config;
    ASSERT_OK_AND_ASSIGN(const auto planning_result,
                         motion_planner->PlanTrajectory(
                             *object_world_, robot_specification_, motion_spec_,
                             motion_planner_config, run_time_flags_));

    // Expect a trajectory that ends in the final waypoint.
    ASSERT_GT(planning_result.trajectory.size(), 1);
    ASSERT_OK(robot_->SetJointPositions(
        planning_result.trajectory.data().back().position));
    ASSERT_OK_AND_ASSIGN(const auto tip_id,
                         tip_frame_->GetTransformOriginEntityId());
    EXPECT_THAT(waypoints.back(),
                IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-3));
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }
}

TEST_P(MotionPlannerTest, LinearMoveWorksForMultipleIdenticalPoseTargets) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  // Case 1: All segments are (almost) identical. Should result in zero motion.
  {
    std::vector<Pose3d> waypoints;
    waypoints.push_back(toPose3d(
        "0.00934103, 0.490983, 0.398881, -0.000474858, -0.0028313, -0.999979, "
        "0.00589545"));
    waypoints.push_back(toPose3d(
        "0.00934103, 0.490983, 0.398881, -0.000474858, -0.0028313, -0.999979, "
        "0.00589545"));

    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
      *motion_segment->mutable_target()->mutable_cartesian_pose() =
          CreatePoseEqualityGeometricConstraint(
              robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
      *motion_segment->mutable_cartesian_limits() =
          CreateDynamicCartesianLimitsProto(
              /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
              /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
    }
    MotionPlannerConfiguration motion_planner_config;
    ASSERT_OK_AND_ASSIGN(
        const MotionPlanner::PlanTrajectoryResult planning_result,
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_));

    // Expect an empty trajectory.
    ASSERT_EQ(planning_result.trajectory.size(), 2);
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 2: the first two segments have close to or identical poses to the
  // start pose, but still a final pose leads to a successful linear move.
  {
    std::vector<Pose3d> waypoints;
    waypoints.push_back(toPose3d(
        "0.00934103, 0.490983, 0.398881, -0.000474858, -0.0028313, -0.999979, "
        "0.00589545"));
    waypoints.push_back(toPose3d(
        "0.00934103, 0.490983, 0.398881, -0.000474858, -0.0028313, -0.999979, "
        "0.00589545"));
    waypoints.push_back(
        toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));

    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
      *motion_segment->mutable_target()->mutable_cartesian_pose() =
          CreatePoseEqualityGeometricConstraint(
              robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
      *motion_segment->mutable_cartesian_limits() =
          CreateDynamicCartesianLimitsProto(
              /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
              /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
    }
    MotionPlannerConfiguration motion_planner_config;
    ASSERT_OK_AND_ASSIGN(
        const MotionPlanner::PlanTrajectoryResult planning_result,
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_));

    // Expect a trajectory of at least two data points that ends in the final
    // waypoint.
    ASSERT_GT(planning_result.trajectory.size(), 2);
    ASSERT_OK_AND_ASSIGN(
        World test_world,
        CloneAndSetJointValues(
            world_, robot_->GetName(),
            planning_result.trajectory.data().back().position));
    ASSERT_OK_AND_ASSIGN(const auto tip_id,
                         tip_frame_->GetTransformOriginEntityId());
    EXPECT_THAT(waypoints.back(),
                IsApprox(test_world.GetTransform(kRootEntityId, tip_id), 1e-3));
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 3: Only the first segment contain a motion worthy target, subsequent
  // segments are identical. Expect successful linear motion.
  {
    std::vector<Pose3d> waypoints;
    waypoints.push_back(
        toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
    waypoints.push_back(
        toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));

    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
      *motion_segment->mutable_target()->mutable_cartesian_pose() =
          CreatePoseEqualityGeometricConstraint(
              robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
      *motion_segment->mutable_cartesian_limits() =
          CreateDynamicCartesianLimitsProto(
              /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
              /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
    }
    MotionPlannerConfiguration motion_planner_config;
    ASSERT_OK_AND_ASSIGN(
        const MotionPlanner::PlanTrajectoryResult planning_result,
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_));

    // Expect a trajectory that ends in the final waypoint.
    ASSERT_GT(planning_result.trajectory.size(), 2);
    ASSERT_OK(robot_->SetJointPositions(
        planning_result.trajectory.data().back().position));
    ASSERT_OK_AND_ASSIGN(const auto tip_id,
                         tip_frame_->GetTransformOriginEntityId());
    EXPECT_THAT(waypoints.back(),
                IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-3));
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }
}

TEST_P(MotionPlannerTest, LocalCollisionSettingsWorksForJointAndPlannedMove) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {0.000000, -2.000000, 2.000000,
                                             0.000000, 0.000000,  0.000000};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {-0.220002, -1.055767, 1.9, 0.000000, 0.000000, 0.000000}});
  waypoints.push_back(eigenmath::VectorNd{
      {-0.648115, -1.179059, 1.882857, 0.0, 0.000000, 0.000000}});

  intrinsic_proto::world::CollisionSettings collision_margin_settings;
  collision_margin_settings.set_disable_collision_checking(false);
  constexpr double kMinimumMargin = 0.02;  // 2 cm.
  collision_margin_settings.set_minimum_margin(kMinimumMargin);

  intrinsic_proto::world::CollisionSettings zero_margin_settings;
  zero_margin_settings.set_disable_collision_checking(false);
  constexpr double kZeroMargin = 0.0;
  zero_margin_settings.set_minimum_margin(kZeroMargin);

  // Case 1: Default collision settings lead to successful trajectory.
  {
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
    }
    MotionPlannerConfiguration motion_planner_config;
    ASSERT_OK_AND_ASSIGN(
        const MotionPlanner::PlanTrajectoryResult planning_result,
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_));

    // Assert that the last configuration brings the robot to the expected pose
    ASSERT_GE(planning_result.trajectory.size(), 1);
    EXPECT_LT((planning_result.trajectory.data().front().position -
               joint_configuration)
                  .norm(),
              1e-8);
    EXPECT_LT(
        (planning_result.trajectory.data().back().position - waypoints.back())
            .norm(),
        1e-8);
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 2: Setting a high global collision margin will fail due to path not
  // found.
  {
    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
      *motion_segment->mutable_collision_settings() = collision_margin_settings;
    }
    MotionPlannerConfiguration motion_planner_config;
    EXPECT_THAT(
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_),
        StatusIs(absl::StatusCode::kNotFound,
                 HasSubstr("Path is not fulfilling path constraints.")));
  }

  // Case 3: Setting a high collision margin in the first and zero margin in the
  // second will succeed.
  {
    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
      if (i >= 1) {
        *motion_segment->mutable_collision_settings() = zero_margin_settings;
      } else {
        *motion_segment->mutable_collision_settings() =
            collision_margin_settings;
      }
    }
    MotionPlannerConfiguration motion_planner_config;
    ASSERT_OK_AND_ASSIGN(
        const MotionPlanner::PlanTrajectoryResult planning_result,
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_));

    // Assert that the last configuration brings the robot to the expected pose
    ASSERT_GE(planning_result.trajectory.size(), 1);
    EXPECT_LT((planning_result.trajectory.data().front().position -
               joint_configuration)
                  .norm(),
              1e-8);
    EXPECT_LT(
        (planning_result.trajectory.data().back().position - waypoints.back())
            .norm(),
        1e-8);
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }
}

TEST_P(MotionPlannerTest, PathSegmentTypesAreSetCorrectly) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));
  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));

  MotionPlannerConfiguration motion_planner_config;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    *motion_segment->mutable_cartesian_limits() =
        CreateDynamicCartesianLimitsProto(
            /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
            /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
  }

  ASSERT_EQ(motion_spec_.motion_segments_size(), 2);
  motion_spec_.mutable_motion_segments(0)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::ANY);
  motion_spec_.mutable_motion_segments(1)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);

  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));
    ASSERT_THAT(planning_result.path_segments, SizeIs(2));
    EXPECT_EQ(planning_result.path_segments.front().type,
              PathSegment::Type::kUndefined);
    EXPECT_EQ(planning_result.path_segments.back().type,
              PathSegment::Type::kUndefined);
}

TEST_P(MotionPlannerTest, LowLevelJointBlendingsAreSetCorrectly) {
  if (run_time_flags_.force_strict_fallback_for_testing) {
    GTEST_SKIP() << "This tests joint blending, so we skip this for strict "
                    "fallback which does not use joint blending.";
  }

  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));
  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));

  MotionPlannerConfiguration motion_planner_config;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    *motion_segment->mutable_cartesian_limits() =
        CreateDynamicCartesianLimitsProto(
            /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
            /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
  }

  ASSERT_EQ(motion_spec_.motion_segments_size(), 2);
  motion_spec_.mutable_motion_segments(0)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::ANY);
  motion_spec_.mutable_motion_segments(1)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);

  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));
    ASSERT_THAT(planning_result.path_segments, SizeIs(2));
}

TEST_P(MotionPlannerTest, WorksForCombiningMotionTypesSharpVMove) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));

  MotionPlannerConfiguration motion_planner_config;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    *motion_segment->mutable_cartesian_limits() =
        CreateDynamicCartesianLimitsProto(
            /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
            /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
  }

  // Case 1: Blended joint move in the first segment (no path segments
  // specified) and linear move in the second segment.
  {
    ASSERT_EQ(motion_spec_.motion_segments_size(), 2);
    motion_spec_.mutable_motion_segments(0)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::ANY);
    motion_spec_.mutable_motion_segments(1)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);

    ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                         motion_planner->PlanTrajectory(
                             *object_world_, robot_specification_, motion_spec_,
                             motion_planner_config, run_time_flags_));

    // Assert that the last configuration brings the robot to the expected pose
    ASSERT_GE(planning_result.trajectory.size(), 2);
      ASSERT_EQ(planning_result.path_segments.size(),
                motion_spec_.motion_segments_size());
    ASSERT_OK_AND_ASSIGN(
        World test_world,
        CloneAndSetJointValues(
            world_, robot_->GetName(),
            planning_result.trajectory.data().back().position));
    ASSERT_OK_AND_ASSIGN(const auto tip_id,
                         tip_frame_->GetTransformOriginEntityId());
    EXPECT_THAT(waypoints.back(),
                IsApprox(test_world.GetTransform(kRootEntityId, tip_id), 1e-4));
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 2: Blended Cartesian linear move in the first segment and blended
  // joint motion (no path segments specified) in the second segment.
  {
    ASSERT_EQ(motion_spec_.motion_segments_size(), 2);
    motion_spec_.mutable_motion_segments(0)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    motion_spec_.mutable_motion_segments(1)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::ANY);

    ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                         motion_planner->PlanTrajectory(
                             *object_world_, robot_specification_, motion_spec_,
                             motion_planner_config, run_time_flags_));

    // Assert that the last configuration brings the robot to the expected pose
    ASSERT_GE(planning_result.trajectory.size(), 2);
      ASSERT_EQ(planning_result.path_segments.size(),
                motion_spec_.motion_segments_size());
    ASSERT_OK_AND_ASSIGN(
        World test_world,
        CloneAndSetJointValues(
            world_, robot_->GetName(),
            planning_result.trajectory.data().back().position));
    ASSERT_OK_AND_ASSIGN(const auto tip_id,
                         tip_frame_->GetTransformOriginEntityId());
    EXPECT_THAT(waypoints.back(),
                IsApprox(test_world.GetTransform(kRootEntityId, tip_id), 1e-4));
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 3: Blended Cartesian linear move in the both segments.
  {
    ASSERT_EQ(motion_spec_.motion_segments_size(), 2);
    motion_spec_.mutable_motion_segments(0)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    motion_spec_.mutable_motion_segments(1)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                         motion_planner->PlanTrajectory(
                             *object_world_, robot_specification_, motion_spec_,
                             motion_planner_config, run_time_flags_));

    // Assert that the last configuration brings the robot to the expected pose
    ASSERT_GE(planning_result.trajectory.size(), 2);
      // Subsequent LINEAR `MotionSegment`s are combined into one `PathSegment`.
      ASSERT_EQ(planning_result.path_segments.size(), 1);
    ASSERT_OK_AND_ASSIGN(
        World test_world,
        CloneAndSetJointValues(
            world_, robot_->GetName(),
            planning_result.trajectory.data().back().position));
    ASSERT_OK_AND_ASSIGN(const auto tip_id,
                         tip_frame_->GetTransformOriginEntityId());
    EXPECT_THAT(waypoints.back(),
                IsApprox(test_world.GetTransform(kRootEntityId, tip_id), 1e-4));
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }
}

TEST_P(MotionPlannerTest, CollisionRuleSetSizesAreCorrectInPathSegments) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));
  waypoints.push_back(
      toPose3d("0.12, 0.62, 0.42, -0.2746, 0.1088, 0.8870, -0.3549"));

  MotionPlannerConfiguration motion_planner_config;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    *motion_segment->mutable_cartesian_limits() =
        CreateDynamicCartesianLimitsProto(
            /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
            /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);

    // Disable collision checking for the first segment.
    if (i == 0) {
      motion_segment->mutable_collision_settings()
          ->set_disable_collision_checking(true);
    } else {
      motion_segment->mutable_collision_settings()
          ->set_disable_collision_checking(false);
    }
  }

  ASSERT_EQ(motion_spec_.motion_segments_size(), 3);
  motion_spec_.mutable_motion_segments(0)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::ANY);
  motion_spec_.mutable_motion_segments(1)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  motion_spec_.mutable_motion_segments(2)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);

  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  std::vector<std::optional<int>> expected_collision_rule_set_sizes = {};
    ASSERT_EQ(planning_result.path_segments.size(), 3);
    expected_collision_rule_set_sizes = {std::nullopt, 13, 13};

  ASSERT_EQ(planning_result.path_segments.size(),
            expected_collision_rule_set_sizes.size());
  for (int i = 0; i < planning_result.path_segments.size(); ++i) {
    if (planning_result.path_segments[i].collision_rule_set.has_value()) {
      ASSERT_TRUE(expected_collision_rule_set_sizes[i].has_value());
      EXPECT_EQ(
          planning_result.path_segments[i].collision_rule_set.value().size(),
          expected_collision_rule_set_sizes[i].value());
    } else {
      EXPECT_EQ(planning_result.path_segments[i].collision_rule_set,
                std::nullopt);
    }
    CHECK_OK(CreateKinematicsProxy(*object_world_, *robot_,
                                   planning_result.path_segments[i]));
  }
}

TEST_P(MotionPlannerTest, CreateKinematicsProxyFromPathSegmentsWorks) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));
  waypoints.push_back(
      toPose3d("0.12, 0.62, 0.42, -0.2746, 0.1088, 0.8870, -0.3549"));

  MotionPlannerConfiguration motion_planner_config;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    *motion_segment->mutable_cartesian_limits() =
        CreateDynamicCartesianLimitsProto(
            /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
            /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
  }

  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  for (const auto& path_segment : planning_result.path_segments) {
    ASSERT_OK_AND_ASSIGN(
        const auto proxy,
        CreateKinematicsProxy(*object_world_, *robot_, path_segment));
    ASSERT_OK(proxy->SetJointLimits(
        JointLimitsXd::Create(path_segment.joint_limits)));

    for (const auto& joint_configuration : path_segment.joint_configurations) {
      ASSERT_OK(proxy->IsValid(joint_configuration, nullptr));
    }
  }
}

TEST_P(MotionPlannerTest, MotionPlanningWorksForPoseInversionPointEdgeCase) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  // Inversion point edge-case (in Cartesian space): P1 => P2 => P1:
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));  // P1
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.2746, 0.1088, 0.8870, -0.3549"));  // P2
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));  // P1 (again)

  MotionPlannerConfiguration motion_planner_config;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    *motion_segment->mutable_cartesian_limits() =
        CreateDynamicCartesianLimitsProto(
            /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
            /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
  }

  ASSERT_EQ(motion_spec_.motion_segments_size(), 3);
  motion_spec_.mutable_motion_segments(0)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  motion_spec_.mutable_motion_segments(1)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  motion_spec_.mutable_motion_segments(2)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 2);
    // Subsequent LINEAR `MotionSegment`s are combined into one `PathSegment`.
    ASSERT_EQ(planning_result.path_segments.size(), 1);
  ASSERT_OK_AND_ASSIGN(World test_world,
                       CloneAndSetJointValues(
                           world_, robot_->GetName(),
                           planning_result.trajectory.data().back().position));
  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());
  EXPECT_THAT(waypoints.back(),
              IsApprox(test_world.GetTransform(kRootEntityId, tip_id), 1e-4));
}
TEST_P(MotionPlannerTest, MotionPlanningWorksForJointInversionPointEdgeCase) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());

  std::vector<Pose3d> waypoints;
  // Inversion point edge-case (in joint space): P1 => P2 => P1:
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.000474858, -0.0028313, -0.999979, "
               "-0.00589545"));  // P1
  waypoints.push_back(
      toPose3d("0.0, 0.5, 0.4, -0.000474858, -0.0028313, -0.999979, "
               "-0.00589545"));  // P2
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.4, -0.000474858, -0.0028313, -0.999979, "
               "-0.00589545"));  // P1 (again)

  const MotionPlannerConfiguration motion_planner_config;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
  }

  ASSERT_EQ(motion_spec_.motion_segments_size(), 3);
  motion_spec_.mutable_motion_segments(0)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  motion_spec_.mutable_motion_segments(1)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  motion_spec_.mutable_motion_segments(2)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  ASSERT_GE(planning_result.trajectory.size(), 2);
    ASSERT_EQ(planning_result.path_segments.size(),
              motion_spec_.motion_segments_size());
  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));
  EXPECT_THAT(waypoints.back(),
              IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-4));
}
TEST_P(MotionPlannerTest,
       MotionPlanningWorksForCombiningMotionTypesStraightMove) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(toPose3d(
      "0, 0.5, 0.3, -0.000474858, -0.0028313, -0.999979, -0.00589545"));
  waypoints.push_back(toPose3d(
      "0, 0.5, 0.2, -0.000474858, -0.0028313, -0.999979, -0.00589545"));

  MotionPlannerConfiguration motion_planner_config;
  intrinsic_proto::motion_planning::v1::CartesianBlendingParameters curve_proto;
  curve_proto.set_rotational_corner_rounding(0.1);
  curve_proto.set_translation_corner_rounding(0.1);
  *motion_spec_.mutable_curve_parameters()->mutable_cartesian_blending() =
      curve_proto;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    *motion_segment->mutable_cartesian_limits() =
        CreateDynamicCartesianLimitsProto(
            /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
            /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
  }

  // Case 1: Blended joint move in the first segment (no path segments
  // specified) and linear move in the second segment.
  {
    ASSERT_EQ(motion_spec_.motion_segments_size(), 2);
    motion_spec_.mutable_motion_segments(0)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::ANY);
    motion_spec_.mutable_motion_segments(1)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);

    ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                         motion_planner->PlanTrajectory(
                             *object_world_, robot_specification_, motion_spec_,
                             motion_planner_config, run_time_flags_));

    // Assert that the last configuration brings the robot to the expected pose
    ASSERT_GE(planning_result.trajectory.size(), 2);
      ASSERT_EQ(planning_result.path_segments.size(),
                motion_spec_.motion_segments_size());
    ASSERT_OK_AND_ASSIGN(
        World test_world,
        CloneAndSetJointValues(
            world_, robot_->GetName(),
            planning_result.trajectory.data().back().position));
    ASSERT_OK_AND_ASSIGN(const auto tip_id,
                         tip_frame_->GetTransformOriginEntityId());
    EXPECT_THAT(waypoints.back(),
                IsApprox(test_world.GetTransform(kRootEntityId, tip_id), 1e-4));
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 2: Blended Cartesian linear move in the first segment and blended
  // joint motion (no path segments specified) in the second segment.
  {
    ASSERT_EQ(motion_spec_.motion_segments_size(), 2);
    motion_spec_.mutable_motion_segments(0)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    motion_spec_.mutable_motion_segments(1)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::ANY);
    ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                         motion_planner->PlanTrajectory(
                             *object_world_, robot_specification_, motion_spec_,
                             motion_planner_config, run_time_flags_));

    // Assert that the last configuration brings the robot to the expected pose
    ASSERT_GE(planning_result.trajectory.size(), 2);
      ASSERT_EQ(planning_result.path_segments.size(),
                motion_spec_.motion_segments_size());
    ASSERT_OK_AND_ASSIGN(
        World test_world,
        CloneAndSetJointValues(
            world_, robot_->GetName(),
            planning_result.trajectory.data().back().position));
    ASSERT_OK_AND_ASSIGN(const auto tip_id,
                         tip_frame_->GetTransformOriginEntityId());
    EXPECT_THAT(waypoints.back(),
                IsApprox(test_world.GetTransform(kRootEntityId, tip_id), 1e-4));
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 3: Blended Cartesian linear move in the both segments.
  {
    ASSERT_EQ(motion_spec_.motion_segments_size(), 2);
    motion_spec_.mutable_motion_segments(0)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    motion_spec_.mutable_motion_segments(1)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                         motion_planner->PlanTrajectory(
                             *object_world_, robot_specification_, motion_spec_,
                             motion_planner_config, run_time_flags_));

    // Assert that the last configuration brings the robot to the expected pose
    ASSERT_GE(planning_result.trajectory.size(), 2);
      // Subsequent LINEAR `MotionSegment`s are combined into one `PathSegment`.
      ASSERT_EQ(planning_result.path_segments.size(), 1);
    ASSERT_OK_AND_ASSIGN(
        World test_world,
        CloneAndSetJointValues(
            world_, robot_->GetName(),
            planning_result.trajectory.data().back().position));
    ASSERT_OK_AND_ASSIGN(const auto tip_id,
                         tip_frame_->GetTransformOriginEntityId());
    EXPECT_THAT(waypoints.back(),
                IsApprox(test_world.GetTransform(kRootEntityId, tip_id), 1e-4));
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }
}

TEST_P(MotionPlannerTest, MotionPlanningWorksForCombiningMotionTypesWideV) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(toPose3d(
      "0.1, 0.5, 0.35, -0.000474858, -0.0028313, -0.999979, -0.00589545"));
  waypoints.push_back(toPose3d(
      "0.2, 0.5, 0.4, -0.000474858, -0.0028313, -0.999979, -0.00589545"));

  MotionPlannerConfiguration motion_planner_config;
  intrinsic_proto::motion_planning::v1::CartesianBlendingParameters curve_proto;
  curve_proto.set_rotational_corner_rounding(0.1);
  curve_proto.set_translation_corner_rounding(0.1);
  *motion_spec_.mutable_curve_parameters()->mutable_cartesian_blending() =
      curve_proto;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    *motion_segment->mutable_cartesian_limits() =
        CreateDynamicCartesianLimitsProto(
            /*max_rot_vel*/ 1.0, /*max_rot_accel*/ 5.0,
            /*max_trans_vel*/ 0.6, /*max_trans_accel*/ 2.0);
  }

  // Case 1: Blended joint move in the first segment (no path segments
  // specified) and linear move in the second segment.
  {
    ASSERT_EQ(motion_spec_.motion_segments_size(), 2);
    motion_spec_.mutable_motion_segments(0)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::ANY);
    motion_spec_.mutable_motion_segments(1)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);

    ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                         motion_planner->PlanTrajectory(
                             *object_world_, robot_specification_, motion_spec_,
                             motion_planner_config, run_time_flags_));

    // Assert that the last configuration brings the robot to the expected pose
    ASSERT_GE(planning_result.trajectory.size(), 2);
      ASSERT_EQ(planning_result.path_segments.size(),
                motion_spec_.motion_segments_size());
    ASSERT_OK_AND_ASSIGN(
        World test_world,
        CloneAndSetJointValues(
            world_, robot_->GetName(),
            planning_result.trajectory.data().back().position));
    ASSERT_OK_AND_ASSIGN(const auto tip_id,
                         tip_frame_->GetTransformOriginEntityId());
    EXPECT_THAT(waypoints.back(),
                IsApprox(test_world.GetTransform(kRootEntityId, tip_id), 1e-4));
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 2: Blended Cartesian linear move in the first segment and blended
  // joint motion (no path segments specified) in the second segment.
  {
    ASSERT_EQ(motion_spec_.motion_segments_size(), 2);
    motion_spec_.mutable_motion_segments(0)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    motion_spec_.mutable_motion_segments(1)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::ANY);
    ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                         motion_planner->PlanTrajectory(
                             *object_world_, robot_specification_, motion_spec_,
                             motion_planner_config, run_time_flags_));

    // Assert that the last configuration brings the robot to the expected pose
    ASSERT_GE(planning_result.trajectory.size(), 2);
      ASSERT_EQ(planning_result.path_segments.size(),
                motion_spec_.motion_segments_size());
    ASSERT_OK_AND_ASSIGN(
        World test_world,
        CloneAndSetJointValues(
            world_, robot_->GetName(),
            planning_result.trajectory.data().back().position));
    ASSERT_OK_AND_ASSIGN(const auto tip_id,
                         tip_frame_->GetTransformOriginEntityId());
    EXPECT_THAT(waypoints.back(),
                IsApprox(test_world.GetTransform(kRootEntityId, tip_id), 1e-4));
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }

  // Case 3: Blended Cartesian linear move in the both segments.
  {
    ASSERT_EQ(motion_spec_.motion_segments_size(), 2);
    motion_spec_.mutable_motion_segments(0)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    motion_spec_.mutable_motion_segments(1)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                         motion_planner->PlanTrajectory(
                             *object_world_, robot_specification_, motion_spec_,
                             motion_planner_config, run_time_flags_));

    // Assert that the last configuration brings the robot to the expected pose
    ASSERT_GE(planning_result.trajectory.size(), 2);
      // Subsequent LINEAR `MotionSegment`s are combined into one `PathSegment`.
      ASSERT_EQ(planning_result.path_segments.size(), 1);
    ASSERT_OK_AND_ASSIGN(
        World test_world,
        CloneAndSetJointValues(
            world_, robot_->GetName(),
            planning_result.trajectory.data().back().position));
    ASSERT_OK_AND_ASSIGN(const auto tip_id,
                         tip_frame_->GetTransformOriginEntityId());
    EXPECT_THAT(waypoints.back(),
                IsApprox(test_world.GetTransform(kRootEntityId, tip_id), 1e-4));
    ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
        planning_result);
  }
}

// Test more complex set up with multi-segment linear move position and
// orientation change.
TEST_P(MotionPlannerTest,
       MotionPlanningWorksForCombiningMotionTypesSquareMove) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(toPose3d(
      "0.1, 0.6, 0.4, -0.000474858, -0.0028313, -0.999979, -0.00589545"));
  waypoints.push_back(toPose3d(
      "0.0, 0.5, 0.4, -0.000474858, -0.0028313, -0.999979, -0.00589545"));

  MotionPlannerConfiguration motion_planner_config;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
  }

  ASSERT_EQ(motion_spec_.motion_segments_size(), 4);
  motion_spec_.mutable_motion_segments(0)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::ANY);
  motion_spec_.mutable_motion_segments(1)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  motion_spec_.mutable_motion_segments(2)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  motion_spec_.mutable_motion_segments(3)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::ANY);

  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  ASSERT_GE(planning_result.trajectory.size(), 2);
    // Subsequent LINEAR `MotionSegment`s are combined into one `PathSegment`.
    ASSERT_EQ(planning_result.path_segments.size(),
              motion_spec_.motion_segments_size() - 1);
  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));
  EXPECT_THAT(waypoints.back(),
              IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-4));
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

  TEST_P(MotionPlannerTest,
  WorksForCombiningMotionTypes) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(toPose3d(
      "0.1, 0.6, 0.4, -0.000474858, -0.0028313, -0.999979, -0.00589545"));
  waypoints.push_back(toPose3d(
      "0.0, 0.5, 0.4, -0.000474858, -0.0028313, -0.999979, -0.00589545"));
  waypoints.push_back(toPose3d(
      "0.1, 0.6, 0.4, -0.000474858, -0.0028313, -0.999979, -0.00589545"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));

  const MotionPlannerConfiguration motion_planner_config;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
  }

  ASSERT_EQ(motion_spec_.motion_segments_size(), 7);
  motion_spec_.mutable_motion_segments(0)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::ANY);
  motion_spec_.mutable_motion_segments(1)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  motion_spec_.mutable_motion_segments(2)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  motion_spec_.mutable_motion_segments(3)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  motion_spec_.mutable_motion_segments(4)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  motion_spec_.mutable_motion_segments(5)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  motion_spec_.mutable_motion_segments(6)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::ANY);

  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  ASSERT_GE(planning_result.trajectory.size(), 2);
    // Subsequent LINEAR `MotionSegment`s are combined into one `PathSegment`.
    ASSERT_EQ(planning_result.path_segments.size(),
              motion_spec_.motion_segments_size() - 1);
  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));
  EXPECT_THAT(waypoints.back(),
              IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-4));
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

  TEST_P(MotionPlannerTest,
  WorksForCombiningMotionTypesV2)
  {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(toPose3d(
      "0.1, 0.6, 0.4, -0.000474858, -0.0028313, -0.999979, -0.00589545"));
  waypoints.push_back(toPose3d(
      "0.0, 0.5, 0.4, -0.000474858, -0.0028313, -0.999979, -0.00589545"));
  waypoints.push_back(toPose3d(
      "0.1, 0.6, 0.35, -0.000474858, -0.0028313, -0.999979, -0.00589545"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));

  const MotionPlannerConfiguration motion_planner_config;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
  }

  ASSERT_EQ(motion_spec_.motion_segments_size(), 7);
  motion_spec_.mutable_motion_segments(0)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::ANY);
  motion_spec_.mutable_motion_segments(1)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  motion_spec_.mutable_motion_segments(2)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  motion_spec_.mutable_motion_segments(3)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  motion_spec_.mutable_motion_segments(4)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  motion_spec_.mutable_motion_segments(5)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  motion_spec_.mutable_motion_segments(6)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::ANY);

  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  ASSERT_GE(planning_result.trajectory.size(), 2);
    // Subsequent LINEAR `MotionSegment`s are combined into one `PathSegment`.
    ASSERT_EQ(planning_result.path_segments.size(),
              motion_spec_.motion_segments_size() - 1);
  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));
  EXPECT_THAT(waypoints.back(),
              IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-4));
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}
  TEST_P(MotionPlannerTest,
  WorksForCombiningMotionTypesV3)
  {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(toPose3d(
      "0.1, 0.6, 0.4, -0.000474858, -0.0028313, -0.999979, -0.00589545"));
  waypoints.push_back(toPose3d(
      "0.0, 0.5, 0.4, -0.000474858, -0.0028313, -0.999979, -0.00589545"));
  waypoints.push_back(toPose3d(
      "0.1, 0.6, 0.4, -0.000474858, -0.0028313, -0.999979, -0.00589545"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));

  const MotionPlannerConfiguration motion_planner_config;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
  }

  ASSERT_EQ(motion_spec_.motion_segments_size(), 7);
  motion_spec_.mutable_motion_segments(0)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::ANY);
  motion_spec_.mutable_motion_segments(1)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  motion_spec_.mutable_motion_segments(2)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  motion_spec_.mutable_motion_segments(3)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  motion_spec_.mutable_motion_segments(4)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  motion_spec_.mutable_motion_segments(5)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  motion_spec_.mutable_motion_segments(6)->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::ANY);

  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  ASSERT_GE(planning_result.trajectory.size(), 2);
    ASSERT_EQ(planning_result.path_segments.size(),
              motion_spec_.motion_segments_size());
  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));
  EXPECT_THAT(waypoints.back(),
              IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-4));
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

// Test more complex set up with multi-segment linear move: Start and goal are
// the same pose. Additionally, the start config is the same config as start and
// goal.
TEST_P(MotionPlannerTest, MotionPlanningWorksLinearSquareMove) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  // Start configuration chosen to match desired cartesian pose of initial and
  // final waypoint.
  eigenmath::Vector6d joint_configuration = {1.1916,   -1.47557, 1.94578,
                                             -1.78062, -1.02402, -0.450497};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(toPose3d(
      "0.1, 0.6, 0.4, -0.000474858, -0.0028313, -0.999979, -0.00589545"));
  // If you adapt this waypoint, please also adapt the start config to match the
  // corresponding joint config.
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));

  MotionPlannerConfiguration motion_planner_config;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  }

  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 2);
    // Subsequent LINEAR `MotionSegment`s are combined into one `PathSegment`.
    ASSERT_EQ(planning_result.path_segments.size(), 1);
  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));
  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());
  EXPECT_THAT(waypoints.back(),
              IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-4));
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

// Test more complex set up with multi-segment joint move: Start and goal are
// the same pose. Additionally, the start config is the same config as start and
// goal.
TEST_P(MotionPlannerTest, WorksJointSquareMove) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  // Start configuration chosen to match desired cartesian pose of initial and
  // final waypoint.
  eigenmath::Vector6d joint_configuration = {1.1916,   -1.47557, 1.94578,
                                             -1.78062, -1.02402, -0.450497};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(toPose3d(
      "0.1, 0.6, 0.4, -0.000474858, -0.0028313, -0.999979, -0.00589545"));
  // If you adapt this waypoint, please also adapt the start config to match the
  // corresponding joint config.
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));

  MotionPlannerConfiguration motion_planner_config;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
  }

  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 2);
  // We configured four `MotionSegment`s, but expect 3 `PathSegment`s to be
  // generated as the first should be filtered out.
  ASSERT_EQ(planning_result.path_segments.size(),
            motion_spec_.motion_segments_size() - 1);
  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));
  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());
  EXPECT_THAT(waypoints.back(),
              IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-4));
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

// This test creates a time out error in one of the planners to check that
// planning errors state the segment in which it occurred. Note that this gets
// added central for all path planning requests and only requires testing of one
// unspecific planning problem.
TEST_P(MotionPlannerTest, PlanningErrorsStateMotionSegment) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {-0.10, 0.48, -1.36,
                                             -0.77, 0.83, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  // Test Case 1: Motion planning fails in the first segment. Note we currently
  // start numbering in the front-end at index 0.
  {
    eigenmath::VectorNd configuration(6);
    configuration << -0.900000, 0.4, -0.1194, -0.705302, 0.673298, -1.554707;

    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(configuration, motion_segment->mutable_target()
                                                ->mutable_joint_position()
                                                ->mutable_joints());
    MotionPlannerConfiguration motion_planner_config;
    motion_planner_config.mutable_timeout_sec()->set_seconds(0);
    motion_planner_config.mutable_timeout_sec()->set_nanos(50000000);
    EXPECT_THAT(
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_),
        StatusIs(absl::StatusCode::kDeadlineExceeded,
                 HasSubstr("Error occurred during planning in segment 0.")));
  }

  // Test Case 2: Motion planning fails in the third segment. This makes
  // currently use of the fact that linear move does not time out.
  {
    std::vector<eigenmath::VectorNd> waypoints;
    waypoints.push_back(
        eigenmath::VectorNd{{-0.12, 0.47, -1.34, -0.80, 0.82, -1.1}});
    waypoints.push_back(
        eigenmath::VectorNd{{-0.20, 0.45, -1.32, -0.80, 0.82, -1.1}});
    waypoints.push_back(eigenmath::VectorNd{
        {-0.900000, 0.4, -0.1194, -0.705302, 0.673298, -1.554707}});

    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (const auto& waypoint : waypoints) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoint, motion_segment->mutable_target()
                                             ->mutable_joint_position()
                                             ->mutable_joints());
    }
    motion_spec_.mutable_motion_segments(0)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    motion_spec_.mutable_motion_segments(1)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    MotionPlannerConfiguration motion_planner_config;
    motion_planner_config.mutable_timeout_sec()->set_seconds(0);
    motion_planner_config.mutable_timeout_sec()->set_nanos(50000000);
    EXPECT_THAT(
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_),
        StatusIs(absl::StatusCode::kDeadlineExceeded,
                 HasSubstr("Error occurred during planning in segment 2.")));
  }

  // Test Case 3: Motion planning fails during linear move in segment 2-3.
  {
    std::vector<eigenmath::VectorNd> waypoints;
    waypoints.push_back(
        eigenmath::VectorNd{{-0.12, 0.47, -1.34, -0.80, 0.82, -1.1}});
    waypoints.push_back(
        eigenmath::VectorNd{{-0.20, 0.45, -1.32, -0.80, 0.82, -1.1}});
    waypoints.push_back(eigenmath::VectorNd{
        {-0.900000, 0.4, -0.1194, -0.705302, 0.673298, -1.554707}});

    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (const auto& waypoint : waypoints) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoint, motion_segment->mutable_target()
                                             ->mutable_joint_position()
                                             ->mutable_joints());
    }
    motion_spec_.mutable_motion_segments(1)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    motion_spec_.mutable_motion_segments(2)->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
    MotionPlannerConfiguration motion_planner_config;
    motion_planner_config.mutable_timeout_sec()->set_seconds(0);
    motion_planner_config.mutable_timeout_sec()->set_nanos(50000000);
      EXPECT_THAT(
          motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                         motion_spec_, motion_planner_config,
                                         run_time_flags_),
          StatusIs(
              absl::StatusCode::kInternal,
              HasSubstr("Error occurred during planning of segments 1 to 2")));
  }
}

// Check if the Payload status is updated accurately.
TEST_P(MotionPlannerTest, StatusWithErrorProtoForSegmentId) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {-0.1,    0.4787, -1.3599,
                                             -0.7566, 0.8293, -1.0};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {-0.1092, 0.47934, -1.345, -0.7879, 0.8402, -1.03897}});

  intrinsic_proto::world::CollisionSettings large_margin_settings;
  large_margin_settings.set_disable_collision_checking(false);
  constexpr double kMinimumMargin = 150.0;  // Set huge margin
  large_margin_settings.set_minimum_margin(kMinimumMargin);
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());
    *motion_segment->mutable_collision_settings() = large_margin_settings;
  }
  MotionPlannerConfiguration motion_planner_config;
  auto result = motion_planner->PlanTrajectory(
      *object_world_, robot_specification_, motion_spec_, motion_planner_config,
      run_time_flags_);
  EXPECT_THAT(
      result,
      StatusIs(absl::StatusCode::kNotFound,
               HasSubstr("Cannot resolve motion target for segment 0")));

  result.status().ForEachPayload([&](absl::string_view type_url,
                                     const absl::Cord& payload) {
    intrinsic_proto::motion_planning::v1::MotionPipelineError
        motion_pipeline_error;
    motion_pipeline_error.ParseFromString(payload);
    ASSERT_TRUE(!motion_pipeline_error.motion_planning_error().empty());
    ASSERT_EQ(
        motion_pipeline_error.motion_planning_error().at(0).segment_id().size(),
        1);
    // Assert segment id is assigned accurately.
    ASSERT_EQ(
        motion_pipeline_error.motion_planning_error().at(0).segment_id().at(0),
        0);
    ASSERT_TRUE(motion_pipeline_error.motion_planning_error()
                    .at(0)
                    .has_collision_error());
  });
}

// Test explicitly that the collision settings per segment are considered when
// resolving the motion target of the requested motion.
TEST_P(MotionPlannerTest, SegmentSpecificCollisionSettingsWorkForMotionTarget) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {0.0374165, -1.97835,  2.08278,
                                             0.344806,  -0.110898, -0.34285};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0.175127, 0.533182, 0.822006, 0.00243315, -0.707967, "
               "0.00293097, 0.706235"));
  waypoints.push_back(toPose3d(
      "0.15, 0.50, 0.822006, 0.00243315, -0.707967, 0.00293097, 0.706235"));
  waypoints.push_back(toPose3d(
      "0.14, 0.50, 0.822006, 0.00243315, -0.707967, 0.00293097, 0.706235"));

  MotionPlannerConfiguration motion_planner_config;
  // Case 1: Without any local settings the Ik target sampling and path
  // planning succeeds.
  {
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      *motion_segment->mutable_target()->mutable_cartesian_pose() =
          CreatePoseEqualityGeometricConstraint(
              robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    }
    EXPECT_OK(motion_planner->PlanTrajectory(
        *object_world_, robot_specification_, motion_spec_,
        motion_planner_config, run_time_flags_));
  }

  // Case 2: Increasing the margin in the last segment will lead to an error in
  // the target sampling step.
  {
    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      *motion_segment->mutable_target()->mutable_cartesian_pose() =
          CreatePoseEqualityGeometricConstraint(
              robot_->GetName(), tip_frame_->GetName(), waypoints[i]);

      if (i == waypoints.size() - 1) {
        motion_segment->mutable_collision_settings()->set_minimum_margin(0.05);
      }
    }

    EXPECT_THAT(
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_),
        StatusIs(
            absl::StatusCode::kNotFound,
            AllOf(HasSubstr("IK could not find a collision free "
                            "configuration."),
                  HasSubstr(" Cannot resolve motion target for segment 2"))));
  }
}

// Test explicitly that the collision settings per segment is correctly
// disabled.
TEST_P(MotionPlannerTest,
       SegmentSpecificCollisionDisablingWorkForMotionTarget) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {0.0374165, -1.97835,  2.08278,
                                             0.344806,  -0.110898, -0.34285};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0.175127, 0.533182, 0.822006, 0.00243315, -0.707967, "
               "0.00293097, 0.706235"));
  waypoints.push_back(toPose3d(
      "0.10, 0.30, 0.322006, 0.00243315, -0.707967, 0.00293097, 0.706235"));

  MotionPlannerConfiguration motion_planner_config;
  // Case 1: Without any local settings the Ik target sampling returns an error
  // for the second segment due to collision.
  {
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      *motion_segment->mutable_target()->mutable_cartesian_pose() =
          CreatePoseEqualityGeometricConstraint(
              robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    }
    EXPECT_THAT(
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_),
        StatusIs(
            absl::StatusCode::kNotFound,
            AllOf(HasSubstr("IK could not find a collision free "
                            "configuration."),
                  HasSubstr(" Cannot resolve motion target for segment 1"))));
  }

  // Case 2: Increasing the margin in the last segment will lead to an error in
  // the target sampling step.
  {
    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      *motion_segment->mutable_target()->mutable_cartesian_pose() =
          CreatePoseEqualityGeometricConstraint(
              robot_->GetName(), tip_frame_->GetName(), waypoints[i]);

      if (i == waypoints.size() - 1) {
        motion_segment->mutable_collision_settings()
            ->set_disable_collision_checking(true);
      }
    }

    EXPECT_OK(motion_planner->PlanTrajectory(
        *object_world_, robot_specification_, motion_spec_,
        motion_planner_config, run_time_flags_));
  }
}

// Test that we can set a planner step size. Note, we cannot directly test that
// the step size is set correctly, only that it is not failing path planning and
// that wrong parameter settings are getting flagged.
TEST_P(MotionPlannerTest, SettingPlannerStepSizeWorks) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {0.000000, -2.000000, 2.000000,
                                             0.000000, 0.000000,  0.000000};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {-0.220002, -1.055767, 1.9, 0.000000, 0.000000, 0.000000}});
  waypoints.push_back(eigenmath::VectorNd{
      {-0.648115, -1.179059, 1.882857, 0.0, 0.000000, 0.000000}});

  intrinsic_proto::world::CollisionSettings collision_margin_settings;
  collision_margin_settings.set_disable_collision_checking(false);
  constexpr double kMinimumMargin = 0.02;  // 2 cm.
  collision_margin_settings.set_minimum_margin(kMinimumMargin);

  intrinsic_proto::world::CollisionSettings zero_margin_settings;
  zero_margin_settings.set_disable_collision_checking(false);
  constexpr double kZeroMargin = 0.0;
  zero_margin_settings.set_minimum_margin(kZeroMargin);

  // Ensure planning is required: Setting a high global collision margin will
  // fail due to path not found.
  {
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
      *motion_segment->mutable_collision_settings() = collision_margin_settings;
    }
    MotionPlannerConfiguration motion_planner_config;
    EXPECT_THAT(
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_),
        StatusIs(absl::StatusCode::kNotFound,
                 HasSubstr("Path is not fulfilling path constraints.")));
  }

  // Test case: with path planning enabled and rrt spacing set, we are able to
  // plan a trajectory.
  {
    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::ANY);
      *motion_segment->mutable_collision_settings() = collision_margin_settings;
    }
    MotionPlannerConfiguration motion_planner_config;
    motion_planner_config.set_path_planning_step_size(0.5);
    ASSERT_OK(motion_planner->PlanTrajectory(
        *object_world_, robot_specification_, motion_spec_,
        motion_planner_config, run_time_flags_));
  }

  // Test case 2: setting a wrong value leads to an error message. Test negative
  // value leads to Invalid Argument Error.
  {
    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::ANY);
      *motion_segment->mutable_collision_settings() = collision_margin_settings;
    }
    MotionPlannerConfiguration motion_planner_config;
    motion_planner_config.set_path_planning_step_size(-0.5);
    EXPECT_THAT(
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_),
        StatusIs(absl::StatusCode::kInvalidArgument,
                 HasSubstr("Planning option 'path planning step size' is zero "
                           "or negative.")));
  }

  // Test case 3: setting a wrong value leads to an error message. Test zero
  // value leads to Invalid Argument Error.
  {
    motion_spec_ = CreateDefaultMotionSpecification(motion_planner_params_);
    for (int i = 0; i < waypoints.size(); ++i) {
      auto* motion_segment = motion_spec_.add_motion_segments();
      VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                                 ->mutable_joint_position()
                                                 ->mutable_joints());
      motion_segment->set_motion_type(
          intrinsic_proto::motion_planning::v1::MotionSegment::ANY);
      *motion_segment->mutable_collision_settings() = collision_margin_settings;
    }
    MotionPlannerConfiguration motion_planner_config;
    motion_planner_config.set_path_planning_step_size(0);
    EXPECT_THAT(
        motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       run_time_flags_),
        StatusIs(absl::StatusCode::kInvalidArgument,
                 HasSubstr("Planning option 'path planning step size' is zero "
                           "or negative.")));
  }
}

TEST_P(MotionPlannerTest, RelativeMoveWorks) {
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  // First motion segment with a relative motion with respect to the start
  // configuration.
  auto* motion_segment = motion_spec_.add_motion_segments();
  *motion_segment->mutable_target()->mutable_cartesian_pose() =
      CreatePoseEqualityGeometricConstraint(
          robot_->GetName(), tip_frame_->GetName(), robot_->GetName(),
          tip_frame_->GetName(), toPose3d("0, 0, 0.05, 1, 0, 0, 0"));
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);

  // Second motion segment with a relative motion that moves the end-effector 5
  // cm in the opposite direction of the last motion. This should result in the
  // robot being again in the start configuration.
  motion_segment = motion_spec_.add_motion_segments();
  *motion_segment->mutable_target()->mutable_cartesian_pose() =
      CreatePoseEqualityGeometricConstraint(
          robot_->GetName(), tip_frame_->GetName(), robot_->GetName(),
          tip_frame_->GetName(), toPose3d("0, 0, -0.05, 1, 0, 0, 0"));
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);

  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(MotionPlanner::PlanTrajectoryResult planning_result,
                       motion_planner->PlanTrajectory(
                           *object_world_, robot_specification_, motion_spec_,
                           motion_planner_config, run_time_flags_));

  // Assert that the last configuration brings the robot to the starting pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  EXPECT_LT(
      (planning_result.trajectory.data().front().position - joint_configuration)
          .norm(),
      1e-8);
  EXPECT_LT(
      (planning_result.trajectory.data().back().position - joint_configuration)
          .norm(),
      1e-8);
  ValidatePathSamplesAndMotionPlanningStatisticsFromPlanTrajectoryResult(
      planning_result);
}

INSTANTIATE_TEST_SUITE_P(
    MotionPlannerTests, MotionPlannerTest,
    Combine(Values(
                MotionPlannerParams{
                    .test_name = "DefaultMotionPlanner",
                }),
            ValuesIn(GetToppTestParams()),
            Values(FallbackParams{.test_name = "Default",
                                  .force_strict_fallback = false},
                   FallbackParams{.test_name = "ForceStrictFallback",
                                  .force_strict_fallback = true})),
    [](const ::testing::TestParamInfo<MotionPlannerTest::ParamType>& info) {
      return std::get<0>(info.param).test_name +
             std::get<1>(info.param).test_name +
             std::get<2>(info.param).test_name;
    });

namespace {
constexpr double kDefaultTranslationalTolerance = 1e-3;
constexpr double kDefaultAngleTolerance =
    1e-4;  // ~1.15 degree tolerance using SO3's IsApprox().
}  // namespace

TEST_P(MotionPlannerTest, RelativePoseEqualityWrtFixedFrame) {
  const eigenmath::Vector6d joint_configuration = {
      0.0374165, -1.97835, 2.08278, 0.344806, -0.110898, -0.34285};

  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  MotionPlannerConfiguration motion_planner_config;

  ASSERT_OK_AND_ASSIGN(const object_world::TransformNode* root_object,
                       object_world_->GetObject(RootObjectId()));

  // For this test, the table's frame will be used as a fixed reference for the
  // relative motion. The table's frame begins at the identity orientation so we
  // transform it to meaningfully test relative motion w.r.t. a fixed frame.

  // Set table's transform so that local +z axis points toward world -y
  // axis. Also, move table 10 meters along world -z so it doesn't collide with
  // robot.
  intrinsic_proto::world::TransformNodeReference table_ref;
  table_ref.mutable_by_name()->mutable_object()->set_object_name("table");
  ASSERT_OK_AND_ASSIGN(
      object_world::TransformNode * table_transform_node,
      object_world::GetTransformNodeByReference(*object_world_, table_ref));
  ASSERT_OK(table_transform_node->SetTransform(
      root_object, table_transform_node,
      toPose3d("0, 0, -10, 0.7071068, 0.7071068, 0, 0")));

  // Specify relative cartesian motion with translation of 0.05 along table's +z
  // axis and rotation of 90deg about table's +z axis.
  intrinsic_proto::motion_planning::v1::MotionSegment* motion_segment =
      motion_spec_.add_motion_segments();
  *motion_segment->mutable_target()->mutable_relative_cartesian_pose() =
      CreateRelativePoseEqualityGeometricConstraint(
          robot_->GetName(), tip_frame_->GetName(), WorldObjectName("table"),
          toPose3d("0, 0, 0.05, 0.7071068, 0, 0, 0.7071068"));

  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());
  const Pose3d root_t_tip_before = world_.GetTransform(kRootEntityId, tip_id);

  // Initial tip orientation should be, with respect to root:
  // x := root -z
  // y := root -y
  // z := root -x
  const eigenmath::SO3d expected_root_r_tip_before(
      eigenmath::Quaterniond(0, -0.7071068, 0, 0.7071068));
  EXPECT_THAT(root_t_tip_before.so3(),
              IsApprox(expected_root_r_tip_before, kDefaultAngleTolerance));

  ASSERT_OK_AND_ASSIGN(
      const MotionPlanner::PlanTrajectoryResult planning_result,
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);

  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));

  const Pose3d root_t_tip_after = world_.GetTransform(kRootEntityId, tip_id);

  // Relative rotation is 90deg about table's +z axis. This is equivalent to a
  // rotation of 90deg about the tip's local +y axis. This results in the
  // following tip frame:
  // x: root +x
  // y: root -y
  // z: root -z
  //
  // Relative translation is along table's +z axis, which is equivalent to
  // translation along root's -y axis.
  Pose3d expected_root_t_tip_after;
  expected_root_t_tip_after.setQuaternion(eigenmath::Quaterniond(0, 1, 0, 0));
  expected_root_t_tip_after.translation() =
      root_t_tip_before.translation() + eigenmath::Vector3d(0, -0.05, 0);

  EXPECT_THAT(
      root_t_tip_after,
      IsApprox(expected_root_t_tip_after,
               /*threshold_norm_translation=*/kDefaultTranslationalTolerance,
               /*threshold_angle=*/kDefaultAngleTolerance));
}

TEST_P(MotionPlannerTest, RelativePositionEqualityWrtFixedFrame) {
  const eigenmath::Vector6d joint_configuration = {
      0.0374165, -1.97835, 2.08278, 0.344806, -0.110898, -0.34285};

  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  MotionPlannerConfiguration motion_planner_config;

  ASSERT_OK_AND_ASSIGN(const object_world::TransformNode* root_object,
                       object_world_->GetObject(RootObjectId()));

  // For this test, the table's frame will be used as a fixed reference for the
  // relative motion. The table's frame begins at the identity orientation so we
  // transform it to meaningfully test relative motion w.r.t. a fixed frame.

  // Set table's transform so that local +z axis points toward world -y
  // axis. Also, move table 10 meters along world -z so it doesn't collide with
  // robot.
  intrinsic_proto::world::TransformNodeReference table_ref;
  table_ref.mutable_by_name()->mutable_object()->set_object_name("table");
  ASSERT_OK_AND_ASSIGN(
      object_world::TransformNode * table_transform_node,
      object_world::GetTransformNodeByReference(*object_world_, table_ref));
  ASSERT_OK(table_transform_node->SetTransform(
      root_object, table_transform_node,
      toPose3d("0, 0, -10, 0.7071068, 0.7071068, 0, 0")));

  // Attach a moving_frame_offset of (0.05, 0, 0). Then specify relative
  // position to move 0.05 in +z (relative to table's frame).
  intrinsic_proto::motion_planning::v1::MotionSegment* motion_segment =
      motion_spec_.add_motion_segments();
  const eigenmath::Vector3d moving_frame_offset(0.05, 0, 0);
  const eigenmath::Vector3d reference_frame_offset(0, 0, 0.05);
  *motion_segment->mutable_target()->mutable_relative_position_equality() =
      CreateRelativePositionEqualityGeometricConstraint(
          robot_->GetName(), tip_frame_->GetName(), WorldObjectName("table"),
          reference_frame_offset, moving_frame_offset);

  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());
  const Pose3d root_t_tip_before = world_.GetTransform(kRootEntityId, tip_id);
  const eigenmath::Vector3d root_p_point_before =
      root_t_tip_before * moving_frame_offset;

  ASSERT_OK_AND_ASSIGN(
      const MotionPlanner::PlanTrajectoryResult planning_result,
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);

  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));

  const Pose3d root_t_tip_after = world_.GetTransform(kRootEntityId, tip_id);
  const eigenmath::Vector3d root_p_point_after =
      root_t_tip_after * moving_frame_offset;

  // Relative translation is along table's +z axis, which is equivalent to
  // translation along root's -y axis.
  const eigenmath::Vector3d expected_root_p_point_after =
      root_p_point_before + eigenmath::Vector3d(0, -0.05, 0);

  EXPECT_THAT(root_p_point_after, IsApprox(expected_root_p_point_after,
                                           kDefaultTranslationalTolerance));
}

TEST_P(MotionPlannerTest, RelativeRotationEqualityWrtFixedFrame) {
  const eigenmath::Vector6d joint_configuration = {
      0.0374165, -1.97835, 2.08278, 0.344806, -0.110898, -0.34285};

  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  MotionPlannerConfiguration motion_planner_config;

  ASSERT_OK_AND_ASSIGN(const object_world::TransformNode* root_object,
                       object_world_->GetObject(RootObjectId()));

  // For this test, the table's frame will be used as a fixed reference for the
  // relative motion. The table's frame begins at the identity orientation so we
  // transform it to meaningfully test relative motion w.r.t. a fixed frame.

  // Set table's transform so that local +z axis points toward world -y
  // axis. Also, move table 10 meters along world -z so it doesn't collide with
  // robot.
  intrinsic_proto::world::TransformNodeReference table_ref;
  table_ref.mutable_by_name()->mutable_object()->set_object_name("table");
  ASSERT_OK_AND_ASSIGN(
      object_world::TransformNode * table_transform_node,
      object_world::GetTransformNodeByReference(*object_world_, table_ref));
  ASSERT_OK(table_transform_node->SetTransform(
      root_object, table_transform_node,
      toPose3d("0, 0, -10, 0.7071068, 0.7071068, 0, 0")));

  // Specify relative rotation of 90deg about table frame's +z axis.
  intrinsic_proto::motion_planning::v1::MotionSegment* motion_segment =
      motion_spec_.add_motion_segments();
  *motion_segment->mutable_target()->mutable_relative_rotation_equality() =
      CreateRelativeRotationEqualityGeometricConstraint(
          robot_->GetName(), tip_frame_->GetName(), WorldObjectName("table"),
          eigenmath::Quaterniond(0.7071068, 0, 0, 0.7071068));

  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());
  const Pose3d root_t_tip_before = world_.GetTransform(kRootEntityId, tip_id);

  // Initial tip orientation should be, with respect to root:
  // x := root -z
  // y := root -y
  // z := root -x
  const eigenmath::SO3d expected_root_r_tip_before(
      eigenmath::Quaterniond(0, -0.7071068, 0, 0.7071068));
  EXPECT_THAT(root_t_tip_before.so3(),
              IsApprox(expected_root_r_tip_before, kDefaultAngleTolerance));

  ASSERT_OK_AND_ASSIGN(
      const MotionPlanner::PlanTrajectoryResult planning_result,
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);

  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));

  const Pose3d root_t_tip_after = world_.GetTransform(kRootEntityId, tip_id);

  // Relative rotation is 90deg about table's +z axis. This is equivalent to a
  // rotation of 90deg about the tip's local +y axis. This results in the
  // following tip frame:
  // x: root +x
  // y: root -y
  // z: root -z
  const eigenmath::SO3d expected_root_r_tip_after(
      eigenmath::Quaterniond(0, 1, 0, 0));

  EXPECT_THAT(root_t_tip_after.so3(),
              IsApprox(expected_root_r_tip_after, kDefaultAngleTolerance));
}

TEST_P(MotionPlannerTest, SmallRelativeMotionWorks) {
  const eigenmath::Vector6d joint_configuration = {
      0.0374165, -1.97835, 2.08278, 0.344806, -0.110898, -0.34285};

  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  MotionPlannerConfiguration motion_planner_config;

  const double kOffset = 0.005;
  Pose3d relative_motion;
  relative_motion.translation() = eigenmath::Vector3d(0, 0, kOffset);

  intrinsic_proto::motion_planning::v1::MotionSegment* motion_segment =
      motion_spec_.add_motion_segments();
  *motion_segment->mutable_target()->mutable_relative_cartesian_pose() =
      CreateRelativePoseEqualityGeometricConstraint(
          robot_->GetName(), tip_frame_->GetName(), WorldObjectName(),
          relative_motion);

  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());
  const Pose3d root_t_tip_before = world_.GetTransform(kRootEntityId, tip_id);

  ASSERT_OK_AND_ASSIGN(
      const MotionPlanner::PlanTrajectoryResult planning_result,
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     run_time_flags_));

  // Assert that the last configuration brings the robot to the expected pose.
  ASSERT_GE(planning_result.trajectory.size(), 1);

  ASSERT_OK(robot_->SetJointPositions(
      planning_result.trajectory.data().back().position));

  const Pose3d root_t_tip_after = world_.GetTransform(kRootEntityId, tip_id);

  eigenmath::Vector3d expected_world_translation =
      root_t_tip_before.rotationMatrix() * relative_motion.translation();
  Pose3d expected_root_t_tip_after = root_t_tip_before;
  expected_root_t_tip_after.translation() += expected_world_translation;

  EXPECT_THAT(root_t_tip_after,
              IsApprox(expected_root_t_tip_after,
                       kDefaultTranslationalTolerance, kDefaultAngleTolerance));
}

class MotionPlannerPathTest : public MotionPlannerBaseTest,
                              public ::testing::Test {
 public:
  void SetUp() override {
    ASSERT_OK_AND_ASSIGN(world_, testing::LoadTestWorld(kUr5eTestWorldGzfPath));

    motion_planner_flags_.parameterization_service_address = "";
  }

 protected:
  std::unique_ptr<MotionPlanner> motion_planner_;
  ToppTestParams topp_test_params_;
  MotionPlannerFlags motion_planner_flags_;

  MotionSpecification motion_spec_;
};

TEST_F(MotionPlannerPathTest, ReturnsInvalidArgumentForEmptyMotionSegments) {
  topp_test_params_.set_infinite_jerk_limits = true;
  topp_test_params_.test_name = "InvalidArgumentForEmptyMotionSegments";
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits));

  // Empty motion specification
  MotionPlannerConfiguration motion_planner_config;

  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  EXPECT_THAT(
      motion_planner->PlanPath(*object_world_, robot_specification_,
                               motion_spec_, motion_planner_config,
                               /*run_time_flags=*/std::nullopt),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Motion Specification does not contain any motion segments.")));
}

TEST_F(MotionPlannerPathTest, FailsForLinearMoveSegmentWithoutTargetSet) {
  topp_test_params_.set_infinite_jerk_limits = true;
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits));

  auto* motion_segment = motion_spec_.add_motion_segments();
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  EXPECT_THAT(motion_planner->PlanPath(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       /*run_time_flags=*/std::nullopt),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("motion segments need to specify a target")));
}

// This test covers if everything gets piped through correctly for the PathPlan
// function. We do not test all planning options here, as that is already done
// in the PlanTrajectory tests.
TEST_F(MotionPlannerPathTest, WorksForLinearMoveSegmentWithJointTarget) {
  topp_test_params_.set_infinite_jerk_limits = true;
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  auto* motion_segment = motion_spec_.add_motion_segments();
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  eigenmath::VectorNd goal_configuration = eigenmath::VectorNd{
      {1.19161, -1.47558, 1.94577, -1.78063, -1.02406, -0.45048}};

  VectorNdToRepeatedDouble(goal_configuration, motion_segment->mutable_target()
                                                   ->mutable_joint_position()
                                                   ->mutable_joints());
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(
      MotionPlanner::PlanPathResult planning_result,
      motion_planner->PlanPath(*object_world_, robot_specification_,
                               motion_spec_, motion_planner_config,
                               /*run_time_flags=*/std::nullopt));
  // Validate that the path was populated.
  ASSERT_GE(planning_result.path.size(), 2);
  // Validate first and last joint configurations.
  EXPECT_THAT(planning_result.path.front(),
              IsApprox(joint_configuration, 1e-6));
  EXPECT_THAT(planning_result.path.back(), IsApprox(goal_configuration, 1e-6));

  // Validate that the path segments are populated.
  EXPECT_EQ(planning_result.path_segments.size(), 1);

  EXPECT_THAT(planning_result.path_segments,
              Each(Field(&PathSegment::type, PathSegment::Type::kUndefined)));

  // Validate that there are no collision statistics.
  EXPECT_EQ(planning_result.distance_check_statistics.num_checks, 0);
}

// This test covers if everything gets piped through correctly for the PathPlan
// function when local proxies are used.
TEST_F(MotionPlannerPathTest,
       WorksForLocalPathPlanningWithDistanceCheckStatistics) {
  auto motion_planner_flags = motion_planner_flags_;
  motion_planner_flags.enable_distance_check_statistics = true;
  topp_test_params_.set_infinite_jerk_limits = true;
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags));
  eigenmath::Vector6d joint_configuration = {0.000000, -2.000000, 2.000000,
                                             0.000000, 0.000000,  0.000000};
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<eigenmath::VectorNd> waypoints;
  waypoints.push_back(eigenmath::VectorNd{
      {-0.220002, -1.055767, 1.9, 0.000000, 0.000000, 0.000000}});
  waypoints.push_back(eigenmath::VectorNd{
      {-0.648115, -1.179059, 1.882857, 0.0, 0.000000, 0.000000}});

  intrinsic_proto::world::CollisionSettings collision_margin_settings;
  collision_margin_settings.set_disable_collision_checking(false);
  constexpr double kMinimumMargin = 0.02;  // 2 cm.
  collision_margin_settings.set_minimum_margin(kMinimumMargin);

  intrinsic_proto::world::CollisionSettings zero_margin_settings;
  zero_margin_settings.set_disable_collision_checking(false);
  constexpr double kZeroMargin = 0.0;
  zero_margin_settings.set_minimum_margin(kZeroMargin);

  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    VectorNdToRepeatedDouble(waypoints[i], motion_segment->mutable_target()
                                               ->mutable_joint_position()
                                               ->mutable_joints());
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
    // KR6 R900-2 geometry requires a margin <= 0.0057 here to avoid collisions
    // during joint interpolation path planning.
    motion_segment->mutable_collision_settings()->set_minimum_margin(i *
                                                                     0.0057);
  }
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(
      const MotionPlanner::PlanTrajectoryResult planning_result,
      motion_planner->PlanTrajectory(*object_world_, robot_specification_,
                                     motion_spec_, motion_planner_config,
                                     /*run_time_flags=*/std::nullopt));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.trajectory.size(), 1);
  // If local collision stats are not checked, this test will fail.
  EXPECT_GT(planning_result.distance_check_statistics.num_checks, 0);
}

TEST_F(MotionPlannerPathTest,
       WorksForGlobalPathPlanningWithDistanceCheckStatistics) {
  auto motion_planner_flags = motion_planner_flags_;
  motion_planner_flags.enable_distance_check_statistics = true;
  topp_test_params_.set_infinite_jerk_limits = true;
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags));
  eigenmath::Vector6d joint_configuration = {1.27778,  -1.58980, 1.81571,
                                             -1.78568, -1.57508, -0.28733};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  auto* motion_segment = motion_spec_.add_motion_segments();
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  eigenmath::VectorNd goal_configuration = eigenmath::VectorNd{
      {1.19161, -1.47558, 1.94577, -1.78063, -1.02406, -0.45048}};

  VectorNdToRepeatedDouble(goal_configuration, motion_segment->mutable_target()
                                                   ->mutable_joint_position()
                                                   ->mutable_joints());
  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(
      MotionPlanner::PlanPathResult planning_result,
      motion_planner->PlanPath(*object_world_, robot_specification_,
                               motion_spec_, motion_planner_config,
                               /*run_time_flags=*/std::nullopt));
  // Validate that a path was generated.
  ASSERT_GE(planning_result.path.size(), 2);

  // Validate that there are collision statistics.
  EXPECT_GT(planning_result.distance_check_statistics.num_checks, 0);
}
// We have a similar test for PlanTrajectory, but we want to test PlanPath as
// well. Objective is to ensure that by default we do not use high level
// blending and linear move gets merged to a single path segment.
TEST_F(MotionPlannerPathTest, MotionPathPlanningDefaultsToNoBlending) {
  topp_test_params_.set_infinite_jerk_limits = true;
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  // Start configuration chosen to match desired cartesian pose of initial and
  // final waypoint.
  eigenmath::Vector6d joint_configuration = {1.1916,   -1.47557, 1.94578,
                                             -1.78062, -1.02402, -0.450497};
  ASSERT_OK(InitializeWorld(kUrTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            joint_configuration));

  std::vector<Pose3d> waypoints;
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(
      toPose3d("0.1, 0.6, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));
  waypoints.push_back(toPose3d(
      "0.1, 0.6, 0.4, -0.000474858, -0.0028313, -0.999979, -0.00589545"));
  // If you adapt this waypoint, please also adapt the start config to match the
  // corresponding joint config.
  waypoints.push_back(
      toPose3d("0, 0.5, 0.3, -0.2954, 0.0010, 0.9554, -0.0065"));

  MotionPlannerConfiguration motion_planner_config;
  for (int i = 0; i < waypoints.size(); ++i) {
    auto* motion_segment = motion_spec_.add_motion_segments();
    *motion_segment->mutable_target()->mutable_cartesian_pose() =
        CreatePoseEqualityGeometricConstraint(
            robot_->GetName(), tip_frame_->GetName(), waypoints[i]);
    motion_segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  }

  ASSERT_OK_AND_ASSIGN(
      MotionPlanner::PlanPathResult planning_result,
      motion_planner->PlanPath(*object_world_, robot_specification_,
                               motion_spec_, motion_planner_config,
                               /*run_time_flags=*/std::nullopt));

  // Assert that the last configuration brings the robot to the expected pose
  ASSERT_GE(planning_result.path.size(), 2);
  ASSERT_OK(robot_->SetJointPositions(planning_result.path.back()));
  ASSERT_OK_AND_ASSIGN(const auto tip_id,
                       tip_frame_->GetTransformOriginEntityId());
  EXPECT_THAT(waypoints.back(),
              IsApprox(world_.GetTransform(kRootEntityId, tip_id), 1e-4));

  // Subsequent LINEAR `MotionSegment`s are combined into one `PathSegment`.
  ASSERT_EQ(planning_result.path_segments.size(), 1);
}

TEST_F(MotionPlannerPathTest, CollisionCheckSpacingAffectsAnyPlanning) {
  // Set a very high default collision check spacing. This will result in the
  // straight-line path being considered collision-free.
  motion_planner_flags_.default_collision_check_spacing = 100.0;
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  // Set the start and goal configurations such that the endpoints are
  // collision-free, but there exists a collision in the straight-line motion
  // between them. We will not detect the collision with an extremely high
  // collision check spacing.
  eigenmath::Vector6d start_configuration(6);
  start_configuration << 1.27778, -1.58980, 1.81571, -1.78568, -1.57508,
      -0.28733;
  eigenmath::VectorNd goal_configuration(6);
  goal_configuration << -0.900000, 0.4, -0.1194, -0.705302, 0.673298, -1.554707;
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            start_configuration));

  auto* motion_segment = motion_spec_.add_motion_segments();
  VectorNdToRepeatedDouble(goal_configuration, motion_segment->mutable_target()
                                                   ->mutable_joint_position()
                                                   ->mutable_joints());
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::ANY);

  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(
      MotionPlanner::PlanPathResult result,
      motion_planner->PlanPath(*object_world_, robot_specification_,
                               motion_spec_, motion_planner_config,
                               /*run_time_flags=*/std::nullopt));
  // A path size of 2 means that the straight-line path was considered
  // collision-free as expected.
  ASSERT_EQ(result.path.size(), 2);

  // Set the collision-check-spacing to a more practical value that will result
  // in detecting the collision in the straight-line path between start and
  // goal.
  motion_planner_config.set_collision_check_spacing_override(0.01);

  ASSERT_OK_AND_ASSIGN(
      result, motion_planner->PlanPath(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       /*run_time_flags=*/std::nullopt));
  // If the path size > 2, that means that the straight-line path between start
  // and goal had a collision. This means that the collision-check-spacing
  // change to MotionPlannerFlags is correctly affecting path planning.
  ASSERT_GT(result.path.size(), 2);
}

TEST_F(MotionPlannerPathTest, CollisionCheckSpacingAffectsJointPlanning) {
  // Set a very high default collision check spacing. This will result in the
  // straight-line path being considered collision-free.
  motion_planner_flags_.default_collision_check_spacing = 100.0;
  ASSERT_OK_AND_ASSIGN(auto motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));
  // Set the start and goal configurations such that the endpoints are
  // collision-free, but there exists a collision in the straight-line motion
  // between them. We will not detect the collision with an extremely high
  // collision check spacing.
  eigenmath::Vector6d start_configuration(6);
  start_configuration << 1.27778, -1.58980, 1.81571, -1.78568, -1.57508,
      -0.28733;
  eigenmath::VectorNd goal_configuration(6);
  goal_configuration << -0.900000, 0.4, -0.1194, -0.705302, 0.673298, -1.554707;
  ASSERT_OK(InitializeWorld(kAgilusTestParams,
                            topp_test_params_.set_infinite_jerk_limits,
                            start_configuration));

  auto* motion_segment = motion_spec_.add_motion_segments();
  VectorNdToRepeatedDouble(goal_configuration, motion_segment->mutable_target()
                                                   ->mutable_joint_position()
                                                   ->mutable_joints());
  motion_segment->set_motion_type(
      intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);

  MotionPlannerConfiguration motion_planner_config;
  ASSERT_OK_AND_ASSIGN(
      MotionPlanner::PlanPathResult result,
      motion_planner->PlanPath(*object_world_, robot_specification_,
                               motion_spec_, motion_planner_config,
                               /*run_time_flags=*/std::nullopt));
  // A path size of 2 means that the straight-line path was considered
  // collision-free as expected.
  ASSERT_EQ(result.path.size(), 2);

  // Set the collision-check-spacing to a more practical value that will result
  // in detecting the collision in the straight-line path between start and
  // goal.
  motion_planner_flags_.default_collision_check_spacing = 0.01;
  ASSERT_OK_AND_ASSIGN(motion_planner,
                       MotionPlanner::Create(motion_planner_flags_));

  // Now that there is a collision in the joint interpolation path, PlanPath()
  // should return an error.
  EXPECT_THAT(motion_planner->PlanPath(*object_world_, robot_specification_,
                                       motion_spec_, motion_planner_config,
                                       /*run_time_flags=*/std::nullopt),
              ::testing::Not(::absl_testing::IsOk()));
}

}  // namespace
}  // namespace intrinsic
