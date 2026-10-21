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

#include "intrinsic/motion_planning/path_planning/path_segment.h"

#include <set>
#include <vector>

#include "absl/status/statusor.h"
#include "google/protobuf/message.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/proto/cart_space_conversion.h"
#include "intrinsic/icon/proto/eigen_conversion.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/motion_planning/path_planning/path_segment.pb.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/proto/collision_action.pb.h"

namespace intrinsic {

intrinsic_proto::motion_planning::PathSegment ToProto(
    const PathSegment& path_segment) {
  intrinsic_proto::motion_planning::PathSegment path_segment_proto;

  path_segment_proto.set_unique_id(path_segment.unique_id);

  for (const eigenmath::VectorNd& joint_configuration :
       path_segment.joint_configurations) {
    VectorXdToRepeatedDouble(
        joint_configuration,
        path_segment_proto.add_joint_configurations()->mutable_joints());
  }

  VectorDoubleToRepeatedDouble(
      path_segment.joint_blending_parameter_rad,
      path_segment_proto.mutable_joint_blending_parameter_rad());

  *path_segment_proto.mutable_joint_limits() =
      intrinsic::ToProto(path_segment.joint_limits);

  *path_segment_proto.mutable_cartesian_limits() =
      intrinsic::icon::ToProto(path_segment.cartesian_limits);

  if (path_segment.collision_rule_set.has_value()) {
    path_segment_proto.mutable_collision_rule_set()->mutable_rules()->Assign(
        path_segment.collision_rule_set.value().begin(),
        path_segment.collision_rule_set.value().end());
  }

  *path_segment_proto.mutable_tip_t_target() =
      intrinsic::ToProto(path_segment.tip_t_target);


  for (const MotionEventAndJointConfigurationIndex&
           motion_event_and_path_index : path_segment.indexed_motion_events) {
    *path_segment_proto.add_indexed_motion_events() =
        ToProto(motion_event_and_path_index);
  }


  path_segment_proto.set_type(ToProto(path_segment.type));

  return path_segment_proto;
}

absl::StatusOr<PathSegment> FromProto(
    const intrinsic_proto::motion_planning::PathSegment& path_segment_proto) {
  PathSegment path_segment = {.unique_id = path_segment_proto.unique_id()};
  for (const intrinsic_proto::icon::JointVec& joint_vec :
       path_segment_proto.joint_configurations()) {
    INTR_ASSIGN_OR_RETURN(eigenmath::VectorNd joint_configuration,
                          icon::RepeatedDoubleToVectorNd(joint_vec.joints()));
    path_segment.joint_configurations.push_back(joint_configuration);
  }

  path_segment.joint_blending_parameter_rad = RepeatedDoubleToVectorDouble(
      path_segment_proto.joint_blending_parameter_rad());

  INTR_ASSIGN_OR_RETURN(
      path_segment.joint_limits,
      intrinsic::FromProto(path_segment_proto.joint_limits()));

  INTR_ASSIGN_OR_RETURN(
      path_segment.cartesian_limits,
      intrinsic::icon::FromProto(path_segment_proto.cartesian_limits()));

  if (path_segment_proto.has_collision_rule_set()) {
    path_segment.collision_rule_set = std::multiset<intrinsic_proto::Rule>(
        path_segment_proto.collision_rule_set().rules().begin(),
        path_segment_proto.collision_rule_set().rules().end());
  }

  INTR_ASSIGN_OR_RETURN(
      path_segment.tip_t_target,
      intrinsic_proto::FromProto(path_segment_proto.tip_t_target()));


  for (const intrinsic_proto::motion_planning::
           MotionEventAndJointConfigurationIndex&
               motion_event_and_joint_configuration_index_proto :
       path_segment_proto.indexed_motion_events()) {
    path_segment.indexed_motion_events.push_back(
        FromProto(motion_event_and_joint_configuration_index_proto));
  }


  path_segment.type = FromProto(path_segment_proto.type());

  return path_segment;
}


intrinsic_proto::motion_planning::MotionEventAndJointConfigurationIndex ToProto(
    const MotionEventAndJointConfigurationIndex&
        motion_event_and_joint_configuration_index) {
  intrinsic_proto::motion_planning::MotionEventAndJointConfigurationIndex
      motion_event_and_path_index_proto;
  *motion_event_and_path_index_proto.mutable_motion_event() =
      motion_event_and_joint_configuration_index.motion_event;
  motion_event_and_path_index_proto.set_joint_configuration_index(
      motion_event_and_joint_configuration_index.joint_configuration_index);
  return motion_event_and_path_index_proto;
}

MotionEventAndJointConfigurationIndex FromProto(
    const intrinsic_proto::motion_planning::
        MotionEventAndJointConfigurationIndex&
            motion_event_and_joint_configuration_index_proto) {
  return MotionEventAndJointConfigurationIndex{
      .motion_event =
          motion_event_and_joint_configuration_index_proto.motion_event(),
      .joint_configuration_index =
          motion_event_and_joint_configuration_index_proto
              .joint_configuration_index()};
}


intrinsic_proto::motion_planning::PathSegment::Type ToProto(
    const PathSegment::Type type) {
  switch (type) {
    case PathSegment::Type::kAny:
      return intrinsic_proto::motion_planning::PathSegment::TYPE_ANY;
    case PathSegment::Type::kLinear:
      return intrinsic_proto::motion_planning::PathSegment::TYPE_LINEAR;
    case PathSegment::Type::kJoint:
      return intrinsic_proto::motion_planning::PathSegment::TYPE_JOINT;
    case PathSegment::Type::kSinglePoint:
      return intrinsic_proto::motion_planning::PathSegment::TYPE_SINGLE_POINT;
    case PathSegment::Type::kUndefined:
    default:
      return intrinsic_proto::motion_planning::PathSegment::TYPE_UNSPECIFIED;
  }
}

PathSegment::Type FromProto(
    intrinsic_proto::motion_planning::PathSegment::Type proto_type) {
  switch (proto_type) {
    case intrinsic_proto::motion_planning::PathSegment::TYPE_ANY:
      return PathSegment::Type::kAny;
    case intrinsic_proto::motion_planning::PathSegment::TYPE_LINEAR:
      return PathSegment::Type::kLinear;
    case intrinsic_proto::motion_planning::PathSegment::TYPE_JOINT:
      return PathSegment::Type::kJoint;
    case intrinsic_proto::motion_planning::PathSegment::TYPE_SINGLE_POINT:
      return PathSegment::Type::kSinglePoint;
    case intrinsic_proto::motion_planning::PathSegment::TYPE_UNSPECIFIED:
    default:
      return PathSegment::Type::kUndefined;
  }
}

}  // namespace intrinsic
