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

#ifndef INTRINSIC_MOTION_PLANNING_PATH_PLANNING_PATH_SEGMENT_H_
#define INTRINSIC_MOTION_PLANNING_PATH_PLANNING_PATH_SEGMENT_H_

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/motion_planning/path_planning/path_segment.pb.h"

#include "intrinsic/motion_planning/proto/v1/motion_events.pb.h"

#include "intrinsic/util/unique_id.h"
#include "intrinsic/world/collision/collision_context.pb.h"
// Need to include this to avoid build errors for
// std::multiset<intrinsic_proto::Rule>
// as this header defines the necessary comparison operator for
// intrinsic_proto::Rule.
#include "intrinsic/world/collision/rule_matching.h"

namespace intrinsic {


// A motion event and the index of the particular joint configuration within
// `PathSegment.joint_configurations` that the motion event is associated with.
// This is tracked so that the event location can be correctly calculated with
// respect to the path and trajectory.
struct MotionEventAndJointConfigurationIndex {
  intrinsic_proto::motion_planning::v1::MotionEvent motion_event;
  int joint_configuration_index;
};


struct PathSegment {
  enum class Type {
    kAny,     // Segment/sub-segment which was originated from
              // intrinsic_proto.motion_planning.v1::MotionSegment::ANY.
    kLinear,  // Segment/sub-segment which was originated from
              // intrinsic_proto.motion_planning.v1::MotionSegment::LINEAR.
    kJoint,   // Segment/sub-segment which was originated from
              // intrinsic_proto.motion_planning.v1::MotionSegment::JOINT.
    kSinglePoint,  // For a single point path segment, for example, a
                   // `PathSegment` with two identical joint configurations,
                   // such as `{start_configuration, start_configuration}`.
    kUndefined,
  };

  absl::string_view GetUniqueId() const { return unique_id; }

  // Ordered list of joint configurations (a joint path).
  std::vector<eigenmath::VectorNd> joint_configurations;

  // Joint blending parameters to apply to every point in
  // `joint_configurations`. Must be the same size as `joint_configurations`.
  // This implies that even the boundaries of the PathSegment have a blending
  // parameter, which is required for blending between PathSegments.
  std::vector<double> joint_blending_parameter_rad;

  // Joint limits to apply to the segment.
  JointLimits joint_limits;

  // Cartesian limits to apply to the Cartesian target frame to the segment.
  CartesianLimits cartesian_limits;

  // Collision rule set to apply to the segment.
  // If not defined, disable_collision_checking will be set to true when
  // creating the KinematicsSystemProxy.
  std::optional<std::multiset<intrinsic_proto::Rule>> collision_rule_set;

  // Offset between robot tip and target frame, to which above Cartesian Limits
  // are applied.
  Pose3d tip_t_target = Pose3d::Identity();


  // The motion events associated with the path segment.
  std::vector<MotionEventAndJointConfigurationIndex> indexed_motion_events;


  // The type of this specific path segment.
  Type type = Type::kUndefined;

  // A unique id for the identification of the segment.
  const std::string unique_id = UniqueId();
};

intrinsic_proto::motion_planning::PathSegment ToProto(
    const PathSegment& path_segment);

absl::StatusOr<PathSegment> FromProto(
    const intrinsic_proto::motion_planning::PathSegment& path_segment_proto);


intrinsic_proto::motion_planning::MotionEventAndJointConfigurationIndex ToProto(
    const MotionEventAndJointConfigurationIndex&
        motion_event_and_joint_configuration_index_proto);

MotionEventAndJointConfigurationIndex FromProto(
    const intrinsic_proto::motion_planning::
        MotionEventAndJointConfigurationIndex&
            motion_event_and_joint_configuration_index_proto);


intrinsic_proto::motion_planning::PathSegment::Type ToProto(
    const PathSegment::Type type);

PathSegment::Type FromProto(
    intrinsic_proto::motion_planning::PathSegment::Type type);

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_PATH_PLANNING_PATH_SEGMENT_H_
