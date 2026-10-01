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

#ifndef INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_MOTION_PLANNER_BASE_TEST_FIXTURE_H_
#define INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_MOTION_PLANNER_BASE_TEST_FIXTURE_H_

#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning_limits.pb.h"
#include "intrinsic/motion_planning/proto/v1/robot_specification.pb.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/objects/frame_internal.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

// Sets the robot configuration of the robot identified by `robot_id` to the
// desired configuration specified in `joint_values` and saves the result in
// `world`. Method will return error if no dof_view of the desired robot could
// be generated.
absl::Status SetRobotConfig(RobotCollectionsEntityId robot_id,
                            const eigenmath::VectorXd& joint_values,
                            World* world);

inline constexpr char kUr5eTestWorldGzfPath[] =
    "intrinsic/skills/apps/test_data/ur5e_world.gzf";

// Struct holding per-robot-type configuration for test fixtures.
struct RobotTestParams {
  std::string world_gzf_path;
  std::string robot_name;

  bool operator==(const RobotTestParams& other) const = default;
};

inline constexpr char k4RobotsCubeWorldGzfPath[] =
    "intrinsic/world/test_data/4_robots_cube_world.gzf";

inline const RobotTestParams kUrTestParams = {
    .world_gzf_path = kUr5eTestWorldGzfPath,
    .robot_name = "my_robot",
};

inline const RobotTestParams kAgilusTestParams = {
    .world_gzf_path = k4RobotsCubeWorldGzfPath,
    .robot_name = "agilus-04",
};

// Creates a `DynamicCartesianLimits` proto with the specified maximum
// rotational and translational velocities and accelerations.
intrinsic_proto::motion_planning::v1::DynamicCartesianLimits
CreateDynamicCartesianLimitsProto(double max_rotational_velocity,
                                  double max_rotational_acceleration,
                                  double max_translational_velocity,
                                  double max_translational_acceleration);

// Creates a `JointLimitsUpdate` proto containing `max_velocity`,
// `max_acceleration`, and `max_jerk` as limits, with optional `min_position`
// and `max_position`.
intrinsic_proto::motion_planning::v1::JointLimitsUpdate
CreateJointLimitsUpdateProto(
    const eigenmath::VectorNd& max_velocity,
    const eigenmath::VectorNd& max_acceleration,
    const eigenmath::VectorNd& max_jerk,
    std::optional<eigenmath::VectorNd> min_position = std::nullopt,
    std::optional<eigenmath::VectorNd> max_position = std::nullopt);

// Base test fixture providing shared state, world initialization, and proto
// creation utilities for motion planner tests.
class MotionPlannerBaseTest {
 public:
  MotionPlannerBaseTest();
  explicit MotionPlannerBaseTest(RobotTestParams robot_params);
  virtual ~MotionPlannerBaseTest() = default;

 protected:
  // Loads the test world and initializes kinematic object references, frames,
  // planning/system joint limits, and default cartesian limits for the given
  // `robot_params`. If `set_infinite_jerk_limits` is true, jerk limits are set
  // to infinity. If `start_configuration` is provided, sets the robot's initial
  // joint configuration (returning an error if its size does not match the
  // robot's degrees of freedom).
  absl::Status InitializeWorld(
      const RobotTestParams& robot_params, bool set_infinite_jerk_limits,
      std::optional<eigenmath::VectorNd> start_configuration = std::nullopt);

  // Overload using `robot_params_` configured on the fixture.
  absl::Status InitializeWorld(
      bool set_infinite_jerk_limits,
      std::optional<eigenmath::VectorNd> start_configuration = std::nullopt);

  // Creates a `JointPositionLimits` proto with `min_position` and
  // `max_position` limits targeting `robot_name_`.
  intrinsic_proto::motion_planning::v1::JointPositionLimits
  CreateJointPositionLimitsProto(const eigenmath::VectorNd& min_position,
                                 const eigenmath::VectorNd& max_position);

  RobotTestParams robot_params_;
  World world_;
  std::unique_ptr<object_world::ObjectWorld> object_world_;
  object_world::KinematicObject* robot_ = nullptr;
  std::string robot_name_;
  intrinsic_proto::motion_planning::v1::RobotSpecification robot_specification_;
  object_world::Frame* tip_frame_ = nullptr;
  JointLimits planning_limits_;
  CartesianLimits default_cartesian_limits_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_MOTION_PLANNER_BASE_TEST_FIXTURE_H_
