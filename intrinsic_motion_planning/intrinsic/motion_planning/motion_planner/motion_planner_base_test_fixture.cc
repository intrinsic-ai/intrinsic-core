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

#include "intrinsic/motion_planning/motion_planner/motion_planner_base_test_fixture.h"

#include <limits>
#include <memory>
#include <optional>
#include <string>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/motion_planning/proto/motion_planner_service_proto_utils.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning_limits.pb.h"
#include "intrinsic/motion_planning/proto/v1/robot_specification.pb.h"
#include "intrinsic/skills/apps/testing/test_world.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/dof_kinematic_view.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

absl::Status SetRobotConfig(RobotCollectionsEntityId robot_id,
                            const eigenmath::VectorXd& joint_values,
                            World* world) {
  if (world == nullptr) {
    return absl::FailedPreconditionError(
        "Cannot call SetRobotConfig with a null world");
  }

  INTR_ASSIGN_OR_RETURN(const auto dof_view,
                        world->GetDofKinematicView(robot_id));
  if (!dof_view) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to get dof view for robot: ",
                     world->GetLocalNameForEntityById(robot_id)));
  }
  CHECK(dof_view);
  INTR_RETURN_IF_ERROR(
      dof_view->SetDofValues(joint_values, /*enforce_limits=*/true));
  return absl::OkStatus();
}

namespace {

// Creates a new `JointLimits` object from the given `dof_view`.
// Preserves the application limits on joint position and torque from the view
// while overriding velocity, acceleration, and jerk limits with the provided
// vectors. Returns an error if any override vector size does not match the
// degree of freedom count.
absl::StatusOr<JointLimits> CreateJointLimits(
    const DofKinematicView& dof_view,
    const eigenmath::VectorXd& max_velocity_override,
    const eigenmath::VectorXd& max_acceleration_override,
    const eigenmath::VectorXd& max_jerk_override) {
  int ndof = dof_view.GetDofCount();
  CHECK(ndof <= eigenmath::VectorNd::MaxSizeAtCompileTime);
  JointLimitsXd current_limits = dof_view.GetDofApplicationLimits();
  JointLimits new_limits;
  CHECK(current_limits.IsSizeConsistent() &&
        current_limits.max_position.size() <=
            eigenmath::VectorNd::MaxSizeAtCompileTime);
  new_limits.max_position = current_limits.max_position;
  new_limits.min_position = current_limits.min_position;

  if (max_velocity_override.size() != ndof) {
    return absl::InvalidArgumentError(
        absl::StrCat("The input joint velocity size does not match the robot "
                     "number of DOF: ",
                     max_velocity_override.size(), " != ", ndof, "."));
  }
  if (max_acceleration_override.size() != ndof) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The input joint acceleration size does not match the robot "
        "number of DOF: ",
        max_acceleration_override.size(), " != ", ndof, "."));
  }
  if (max_jerk_override.size() != ndof) {
    return absl::InvalidArgumentError(
        absl::StrCat("The input joint jerk size does not match the robot "
                     "number of DOF: ",
                     max_jerk_override.size(), " != ", ndof, "."));
  }
  new_limits.max_velocity = max_velocity_override;
  new_limits.max_acceleration = max_acceleration_override;
  new_limits.max_jerk = max_jerk_override;
  new_limits.max_torque = current_limits.max_torque;
  return new_limits;
}

}  // namespace

MotionPlannerBaseTest::MotionPlannerBaseTest()
    : world_(World::CreateEmptyWorld()) {}

MotionPlannerBaseTest::MotionPlannerBaseTest(RobotTestParams robot_params)
    : robot_params_(std::move(robot_params)),
      world_(World::CreateEmptyWorld()) {}

absl::Status MotionPlannerBaseTest::InitializeWorld(
    const bool set_infinite_jerk_limits,
    const std::optional<eigenmath::VectorNd> start_configuration) {
  return InitializeWorld(robot_params_, set_infinite_jerk_limits,
                         start_configuration);
}

absl::Status MotionPlannerBaseTest::InitializeWorld(
    const RobotTestParams& robot_params, const bool set_infinite_jerk_limits,
    const std::optional<eigenmath::VectorNd> start_configuration) {
  robot_params_ = robot_params;
  INTR_ASSIGN_OR_RETURN(world_,
                        testing::LoadTestWorld(robot_params.world_gzf_path));

  INTR_ASSIGN_OR_RETURN(object_world_,
                        object_world::ObjectWorld::CreateView(world_));
  INTR_ASSIGN_OR_RETURN(robot_, object_world_->GetKinematicObject(
                                    WorldObjectName(robot_params.robot_name)));

  INTR_ASSIGN_OR_RETURN(const eigenmath::VectorXd& world_configuration,
                        robot_->GetJointPositions());
  int num_dof = world_configuration.size();
  if (start_configuration.has_value()) {
    if (start_configuration->size() != num_dof) {
      return absl::InvalidArgumentError(absl::StrCat(
          "The input start_configuration size does not match the robot "
          "number of DOF: ",
          start_configuration->size(), " != ", num_dof, "."));
    }
    INTR_RETURN_IF_ERROR(SetRobotConfig(robot_->GetRobotEntityId(),
                                        *start_configuration, &world_));
  }
  INTR_ASSIGN_OR_RETURN(tip_frame_, robot_->GetSingleIsoFlangeFrame());
  robot_name_ = robot_params.robot_name;

  // Create some default planning limits.
  eigenmath::VectorNd velocity(num_dof);
  const int head_size = num_dof / 2;
  const int tail_size = num_dof - head_size;
  velocity.head(head_size).setConstant(0.6);
  velocity.tail(tail_size).setConstant(0.4);
  const eigenmath::VectorNd planning_max_acc =
      eigenmath::VectorNd::Constant(num_dof, 2.0);
  const eigenmath::VectorNd system_max_acc =
      eigenmath::VectorNd::Constant(num_dof, 100.0);

  const double kInfiniteJerk = std::numeric_limits<double>::max();
  const eigenmath::VectorNd planning_max_jerk =
      set_infinite_jerk_limits
          ? eigenmath::VectorNd::Constant(num_dof, kInfiniteJerk)
          : eigenmath::VectorNd::Constant(num_dof, 200.0);
  const eigenmath::VectorNd system_max_jerk =
      set_infinite_jerk_limits
          ? eigenmath::VectorNd::Constant(num_dof, kInfiniteJerk)
          : eigenmath::VectorNd::Constant(num_dof, 1000.0);

  // Query the kinematic view once from the underlying world to read existing
  // joint limits. Build tighter `planning_limits` and wider `robot_limits` (set
  // right back onto the view so planning algorithms can explore trajectories
  // without hitting application limits).
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<DofKinematicView> dof_view,
                        world_.GetDofKinematicView(robot_->GetRobotEntityId()));
  INTR_ASSIGN_OR_RETURN(planning_limits_,
                        CreateJointLimits(*dof_view, velocity, planning_max_acc,
                                          planning_max_jerk));
  INTR_ASSIGN_OR_RETURN(
      const JointLimits robot_limits,
      CreateJointLimits(*dof_view, velocity, system_max_acc, system_max_jerk));
  INTR_RETURN_IF_ERROR(dof_view->SetDofApplicationLimits(
      JointLimitsXd::Create(robot_limits), /*enforce_limits=*/true));
  INTR_RETURN_IF_ERROR(dof_view->SetDofSystemLimits(
      JointLimitsXd::Create(robot_limits), /*enforce_limits=*/true));

  *robot_specification_.mutable_robot_reference() =
      CreateRobotReferenceByName(robot_name_);
  default_cartesian_limits_.min_translational_position.setConstant(-100);
  default_cartesian_limits_.max_translational_position.setConstant(100);
  default_cartesian_limits_.min_translational_velocity.setConstant(-1);
  default_cartesian_limits_.max_translational_velocity.setConstant(1);
  default_cartesian_limits_.min_translational_acceleration.setConstant(-10);
  default_cartesian_limits_.max_translational_acceleration.setConstant(10);
  INTR_RETURN_IF_ERROR(robot_->SetCartesianLimits(default_cartesian_limits_));

  return absl::OkStatus();
}

intrinsic_proto::motion_planning::v1::JointPositionLimits
MotionPlannerBaseTest::CreateJointPositionLimitsProto(
    const eigenmath::VectorNd& min_position,
    const eigenmath::VectorNd& max_position) {
  intrinsic_proto::motion_planning::v1::JointPositionLimits
      joint_position_proto;
  VectorNdToRepeatedDouble(min_position,
                           joint_position_proto.mutable_lower_limits());
  VectorNdToRepeatedDouble(max_position,
                           joint_position_proto.mutable_upper_limits());
  *joint_position_proto.mutable_object_id()
       ->mutable_by_name()
       ->mutable_object_name() = robot_name_;
  return joint_position_proto;
}

intrinsic_proto::motion_planning::v1::DynamicCartesianLimits
CreateDynamicCartesianLimitsProto(const double max_rotational_velocity,
                                  const double max_rotational_acceleration,
                                  const double max_translational_velocity,
                                  const double max_translational_acceleration) {
  intrinsic_proto::motion_planning::v1::DynamicCartesianLimits
      cart_limits_proto;
  cart_limits_proto.set_max_rotational_velocity(max_rotational_velocity);
  cart_limits_proto.set_max_rotational_acceleration(
      max_rotational_acceleration);
  cart_limits_proto.set_max_translational_velocity(max_translational_velocity);
  cart_limits_proto.set_max_translational_acceleration(
      max_translational_acceleration);
  return cart_limits_proto;
}

intrinsic_proto::motion_planning::v1::JointLimitsUpdate
CreateJointLimitsUpdateProto(
    const eigenmath::VectorNd& max_velocity,
    const eigenmath::VectorNd& max_acceleration,
    const eigenmath::VectorNd& max_jerk,
    const std::optional<eigenmath::VectorNd> min_position,
    const std::optional<eigenmath::VectorNd> max_position) {
  intrinsic_proto::motion_planning::v1::JointLimitsUpdate joint_limits_proto;
  VectorNdToRepeatedDouble(
      max_velocity,
      joint_limits_proto.mutable_max_velocity()->mutable_values());
  VectorNdToRepeatedDouble(
      max_acceleration,
      joint_limits_proto.mutable_max_acceleration()->mutable_values());
  VectorNdToRepeatedDouble(
      max_jerk, joint_limits_proto.mutable_max_jerk()->mutable_values());
  if (min_position.has_value()) {
    VectorNdToRepeatedDouble(
        min_position.value(),
        joint_limits_proto.mutable_min_position()->mutable_values());
  }
  if (max_position.has_value()) {
    VectorNdToRepeatedDouble(
        max_position.value(),
        joint_limits_proto.mutable_max_position()->mutable_values());
  }
  return joint_limits_proto;
}

}  // namespace intrinsic
