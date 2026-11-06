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

#ifndef INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_MOTION_PLANNING_ERROR_UTILS_H_
#define INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_MOTION_PLANNING_ERROR_UTILS_H_

#include <optional>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/proto/motion_target.pb.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning_error.pb.h"
#include "intrinsic/world/collision/collision_checker.h"

namespace intrinsic {

using intrinsic_proto::motion_planning::v1::ErrorContext;

constexpr absl::string_view kMotionPlanningCollisionErrorPrefix =
    "MotionPlanningError/collision_error:";
constexpr absl::string_view kMotionPlanningIKErrorPrefix =
    "MotionPlanningError/ik_error:";
constexpr absl::string_view kMotionPlanningFinePathIKErrorPrefix =
    "MotionPlanningError/fine_path_ik_error:";
constexpr absl::string_view
    kMotionPlanningLinearCartesianPathPlanningErrorPrefix =
        "MotionPlanningError/linear_cartesian_path_planning_error:";
constexpr absl::string_view kMotionPlanningJointLimitErrorPrefix =
    "MotionPlanningError/joint_limit_error:";

// Creates a status with a `CollisionError` payload.
// This function creates a status with a `CollisionError` payload. It takes the
// following arguments:
// - error_message: The string error message to be included in the status.
// - debug: The collision checking debug information. Please see
//   `CollisionCheckingDebug` for more details.
// - configuration: The configuration of the robot.
// - proxy: The kinematics system proxy.
// - message_url_suffix: The suffix of the URL of the message. This will be
//   appended to `kMotionPlanningCollisionErrorPrefix` to form the
//   message URL.
// - error_type: The type of error.
// - error_context: The context of the error.
// It returns a status with a `CollisionError` payload.
absl::Status CreateStatusWithCollisionError(
    absl::string_view error_message, const CollisionCheckingDebug& debug,
    const eigenmath::VectorXd& configuration,
    const KinematicsSystemProxy& proxy, absl::string_view message_url_suffix,
    absl::StatusCode error_type,
    ErrorContext::Type error_context = ErrorContext::UNKNOWN);

// Creates a status with a `CollisionError` payload.
// This function creates a status with a `CollisionError` payload. It takes the
// following arguments:
// - error_message: The string error message to be included in the status.
// - message_url_suffix: The suffix of the URL of the message. This will be
//   appended to `kMotionPlanningCollisionErrorPrefix` to form the
//   message URL.
// - collision_error_proto: The `CollisionError` proto.
// - error_type: The type of error.
// It returns a status with a `CollisionError` payload.
absl::Status CreateStatusWithCollisionError(
    absl::string_view error_message, absl::string_view message_url_suffix,
    const intrinsic_proto::motion_planning::v1::CollisionError&
        collision_error_proto,
    absl::StatusCode error_type);

// Creates a `CollisionError` proto.
// This function creates a `CollisionError` proto. It takes the
// following arguments:
// - error_message: The string error message to be included in the proto.
// - debug: The collision checking debug information. Please see
//   `CollisionCheckingDebug` for more details.
// - configuration: The configuration of the robot.
// - proxy: The kinematics system proxy.
// - error_context: The context of the error.
// - collision_error_proto: An optional `CollisionError` proto to be updated.
// A collision error proto is created if `collision_error_proto` is not provided
// (i.e. supplied with nullopt).
// It returns a `CollisionError` proto.
absl::StatusOr<intrinsic_proto::motion_planning::v1::CollisionError>
CreateCollisionError(
    absl::string_view error_message, const CollisionCheckingDebug& debug,
    const eigenmath::VectorXd& configuration,
    const KinematicsSystemProxy& proxy,
    ErrorContext::Type error_context = ErrorContext::UNKNOWN,
    std::optional<intrinsic_proto::motion_planning::v1::CollisionError>
        collision_error_proto = std::nullopt);

// Creates a status with an `IKError` payload.
// This function creates a status with an `IKError` payload. It takes the
// following arguments:
// - error_message: The string error message to be included in the status.
// - message_url_suffix: The suffix of the URL of the message. This will be
//   appended to `kMotionPlanningIKErrorPrefix` to form the
//   message URL.
// - error_type: The type of error.
// - error_context: The context of the error.
// - geometric_constraint: The geometric constraint that caused the error.
// It returns a status with an `IKError` payload.
absl::Status CreateStatusWithIKError(
    absl::string_view error_message, absl::string_view message_url_suffix,
    absl::StatusCode error_type,
    const ErrorContext::Type& error_context = ErrorContext::UNKNOWN,
    std::optional<intrinsic_proto::motion_planning::v1::GeometricConstraint>
        geometric_constraint = std::nullopt);

// Assigns a geometric constraint to an `IKError` status. `message_url_suffix`
// is the suffix of the URL of the message. This will be appended to
// `kMotionPlanningIKErrorPrefix` to form the message URL.
absl::Status AssignConstraintForIKError(
    absl::Status status,
    intrinsic_proto::motion_planning::v1::GeometricConstraint
        geometric_constraint,
    absl::string_view message_url_suffix);

// Create a `FinePathIKError` and update the status payload. This function
// creates a status with a `FinePathIKError` payload. It takes the following
// arguments:
// - error_message: The string error message to be included in the status.
// - prev_ik_solution: The previous IK solution.
// - curr_ik_hint: The hint for the current IK solution.
// - curr_ik_solution: The current IK solution.
// - base_t_tip_desired: The desired base to tip transform.
// - joint_limits: The applied joint limits.
// - message_url_suffix: The suffix of the URL of the message. This will be
//   appended to `kMotionPlanningFinePathIKErrorPrefix` to form the
//   message URL.
// - error_type: The type of error.
// - error_context: The context of the error.
// It returns a status with a `FinePathIKError` payload.
absl::Status CreateStatusWithFinePathIKError(
    absl::string_view error_message,
    const eigenmath::VectorNd& prev_ik_solution,
    const eigenmath::VectorNd& curr_ik_hint,
    const eigenmath::VectorNd& curr_ik_solution,
    const Pose3d& base_t_tip_desired, const JointLimits& joint_limits,
    absl::string_view message_url_suffix,
    const ErrorContext::Type& error_context, absl::StatusCode error_type);
// Returns true if the status has a payload that contains a MotionPlanningError
bool HasMotionPlanningErrorPayload(const absl::Status& s);

// Updates the error context in a status that contains a
// MotionPlanningError payload. StatusCode is optional and allows user to update
// type of error we want to surface.
absl::Status UpdateStatusErrorContext(
    const absl::Status& status, ErrorContext::Type error_context,
    std::optional<absl::StatusCode> status_code);

// Annotates `status` with an optional error message (`append_to_error_message`)
// and converts any `MotionPlanningError` `status` payload into a
// `MotionPipelineError` `status` payload containing the given
// `segment_indices`.
absl::Status UpdateStatusWithPipelineError(
    const absl::Status& status, absl::Span<const int> segment_indices = {},
    absl::string_view append_to_error_message = "");

// Create a `LinearCartesianMotionPathPlannerError` and update the status
// payload. This function creates a status with a
// `LinearCartesianMotionPathPlannerError` payload. It takes the following
// arguments:
// - error_message: The string error message to be included in the status.
// - target_joint_positions: The target joint positions.
// - final_joint_positions: The final joint positions.
// - message_url_suffix: The suffix of the URL of the message. This will be
//   appended to
//   `kMotionPlanningLinearCartesianPathPlanningErrorPrefix` to form
//   the message URL.
// - error_type: The type of error.
// - error_context: The context of the error.
// It returns a status with a `LinearCartesianMotionPathPlannerError` payload.
absl::Status CreateStatusWithLinCartPathPlanningError(
    absl::string_view error_message,
    const eigenmath::VectorNd& target_joint_positions,
    const eigenmath::VectorNd& final_joint_positions,
    absl::string_view message_url_suffix, absl::StatusCode error_type,
    const ErrorContext::Type& error_context);

// Create a `JointLimitError`. Joint positions represents the joint
// configuration that violates the joint limits. This function creates a
// `JointLimitError`. It takes the following arguments:
// - error_message: The string error message to be included in the proto.
// - joint_positions: The joint positions that violate the joint limits.
// - error_context: The context of the error.
// - joint_limits: The joint limits.
// It returns a `JointLimitError` proto.
intrinsic_proto::motion_planning::v1::JointLimitError CreateJointLimitError(
    absl::string_view error_message, const eigenmath::VectorNd& joint_positions,
    ErrorContext::Type error_context, std::optional<JointLimits> joint_limits);

// Create a `JointLimitError` and update the status payload with the error.
// This function creates a status with a `JointLimitError` payload. It takes the
// following arguments:
// - error_message: The string error message to be included in the status.
// - joint_positions: The joint positions that violate the joint limits.
// - message_url_suffix: The suffix of the URL of the message. This will be
//   appended to `kMotionPlanningJointLimitErrorPrefix` to form the
//   message URL.
// - error_context: The context of the error.
// - error_type: The type of error.
// - joint_limits: The joint limits.
// It returns a status with a `JointLimitError` payload.
absl::Status CreateStatusWithJointLimitError(
    absl::string_view error_message, const eigenmath::VectorNd& joint_positions,
    absl::string_view message_url_suffix, ErrorContext::Type error_context,
    absl::StatusCode error_type,
    std::optional<JointLimits> joint_limits = std::nullopt);

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_MOTION_PLANNING_ERROR_UTILS_H_
