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

#ifndef INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_TRAJECTORY_SEGMENT_H_
#define INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_TRAJECTORY_SEGMENT_H_

#include <optional>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "google/protobuf/duration.pb.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/motion_planning/motion_planner/robot_specification.h"
#include "intrinsic/motion_planning/path_planning/planners/linear_cartesian_motion_path_planner_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/pipeline_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/rrt_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/shortcutter_configs.pb.h"
#include "intrinsic/motion_planning/proto/motion_target.pb.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_blending_parameter.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/objects/object_world.h"

namespace intrinsic {
struct TrajectorySegment {
 public:
  enum class Type { kUndefined, kBlendedCartesian, kBlendedJoint };
  struct PlanningFrames {
    AttachmentEntityId reference_id;
    AttachmentEntityId moving_id;
  };

  std::vector<intrinsic_proto::motion_planning::v1::MotionSegment>
      motion_segments;
  // The joint samples for the motion targets of the motion segments.
  std::vector<std::vector<eigenmath::VectorXd>> joint_samples;
  const CartesianLimits cart_limits;
  const JointLimits joint_limits;
  const std::optional<intrinsic_proto::motion_planning::v1::BlendingParameters>
      blending_parameters;
  const Type trajectory_type;
  const PlanningFrames planning_frames;
  const Pose3d tip_t_moving_frame;
  // Cartesian limits for additional monitored frames (e.g. elbow frame). If
  // empty, no additional per-frame Cartesian limits are enforced (only
  // `cart_limits` and `joint_limits` apply).
  const std::vector<FrameCartesianPathLimits> frame_cartesian_limits;

  static absl::StatusOr<TrajectorySegment> Create(
      const object_world::ObjectWorld& object_world,
      const RobotSpecification& robot_specification,
      const intrinsic_proto::motion_planning::v1::MotionSegment& motion_segment,
      const std::optional<
          intrinsic_proto::motion_planning::v1::BlendingParameters>&
          blending_parameters);

  // Checks if the path constraints in the motion segments are valid, i.e., if
  // no contradicting constraints are defined and returns the corresponding
  // trajectory type. kBlendedCartesian if a linear cartesian path constraint is
  // defined and kBlendedJoint otherwise. Returns an InvalidArgumentError if
  // both are set.
  static absl::StatusOr<Type> ValidatePathConstraintsAndReturnTrajectoryType(
      const intrinsic_proto::motion_planning::v1::MotionSegment&
          motion_segment);

 private:
  TrajectorySegment(
      const std::vector<intrinsic_proto::motion_planning::v1::MotionSegment>&
          motion_segments,
      const std::vector<std::vector<eigenmath::VectorXd>>& joint_samples,
      const CartesianLimits& cart_limits, const JointLimits& joint_limits,
      const std::optional<
          intrinsic_proto::motion_planning::v1::BlendingParameters>&
          blending_parameters,
      const Type& trajectory_type, const PlanningFrames& planning_frames,
      const Pose3d& tip_t_moving_frame,
      std::vector<FrameCartesianPathLimits> frame_cartesian_limits = {})
      : motion_segments(motion_segments),
        joint_samples(joint_samples),
        cart_limits(cart_limits),
        joint_limits(joint_limits),
        blending_parameters(blending_parameters),
        trajectory_type(trajectory_type),
        planning_frames(planning_frames),
        tip_t_moving_frame(tip_t_moving_frame),
        frame_cartesian_limits(std::move(frame_cartesian_limits)) {}
};
}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_TRAJECTORY_SEGMENT_H_
