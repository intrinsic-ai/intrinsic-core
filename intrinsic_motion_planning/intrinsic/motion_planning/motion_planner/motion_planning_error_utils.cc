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

#include "intrinsic/motion_planning/motion_planner/motion_planning_error_utils.h"

#include <optional>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/proto/eigen_conversion.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/proto/motion_target.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning_error.pb.h"
#include "intrinsic/util/status/annotate.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/collision_checker.h"

namespace intrinsic {

using intrinsic_proto::motion_planning::v1::ErrorContext;
using intrinsic_proto::motion_planning::v1::GeometricConstraint;
using intrinsic_proto::motion_planning::v1::MotionPlanningError;

namespace {

MotionPlanningError CreateMotionPlanningError(
    intrinsic_proto::motion_planning::v1::CollisionError collision_error) {
  MotionPlanningError motion_planning_error;
  *motion_planning_error.mutable_collision_error() = collision_error;
  return motion_planning_error;
}

MotionPlanningError CreateMotionPlanningError(
    const intrinsic_proto::motion_planning::v1::IKError& ik_error) {
  MotionPlanningError motion_planning_error;
  *motion_planning_error.mutable_ik_error() = ik_error;
  return motion_planning_error;
}

MotionPlanningError CreateMotionPlanningError(
    const intrinsic_proto::motion_planning::v1::FinePathIKError&
        fine_path_ik_error) {
  MotionPlanningError motion_planning_error;
  *motion_planning_error.mutable_fine_path_ik_error() = fine_path_ik_error;
  return motion_planning_error;
}

MotionPlanningError CreateMotionPlanningError(
    const intrinsic_proto::motion_planning::v1::
        LinearCartesianMotionPathPlannerError&
            linear_cartesian_path_planning_error) {
  MotionPlanningError motion_planning_error;
  *motion_planning_error.mutable_linear_cartesian_path_planning_error() =
      linear_cartesian_path_planning_error;
  return motion_planning_error;
}

MotionPlanningError CreateMotionPlanningError(
    const intrinsic_proto::motion_planning::v1::JointLimitError&
        joint_limit_error) {
  MotionPlanningError motion_planning_error;
  *motion_planning_error.mutable_joint_limit_error() = joint_limit_error;
  return motion_planning_error;
}
}  // namespace

bool HasMotionPlanningErrorPayload(const absl::Status& status) {
  bool has_payload = false;
  status.ForEachPayload(
      [&](absl::string_view type_url, const absl::Cord& payload) {
        if (absl::StrContains(type_url, "MotionPlanningError")) {
          has_payload = true;
        }
      });
  return has_payload;
}

absl::StatusOr<intrinsic_proto::motion_planning::v1::CollisionError>
CreateCollisionError(
    absl::string_view error_message, const CollisionCheckingDebug& debug,
    const eigenmath::VectorXd& configuration,
    const KinematicsSystemProxy& proxy, ErrorContext::Type error_context,
    std::optional<intrinsic_proto::motion_planning::v1::CollisionError>
        collision_error_proto) {
  if (debug.collisions.empty()) {
    LOG(INFO) << "No collisions found, no collision error can be created.";
    return absl::InvalidArgumentError(
        "Collision checking debug cannot be empty.");
  }
  INTR_ASSIGN_OR_RETURN(auto collision_debug_proto,
                        proxy.GetCollisionDebugMessage(debug));

  // Assign values to the collision debug proto
  *collision_debug_proto.mutable_joint_positions() =
      icon::ToJointVecProto(configuration);
  *collision_debug_proto.mutable_error_message() = error_message;
  intrinsic_proto::motion_planning::v1::CollisionError collision_error;
  if (collision_error_proto.has_value()) {
    collision_error = collision_error_proto.value();
  }
  *collision_error.mutable_collision_debug()->Add() = collision_debug_proto;
  collision_error.set_error_context(error_context);
  return collision_error;
}

absl::Status CreateStatusWithCollisionError(
    absl::string_view error_message, const CollisionCheckingDebug& debug,
    const eigenmath::VectorXd& configuration,
    const KinematicsSystemProxy& proxy, absl::string_view message_url_suffix,
    absl::StatusCode error_type, ErrorContext::Type error_context) {
  if (debug.collisions.empty()) {
    LOG(INFO) << "No collisions found, no collision error will be returned "
                 "with the status.";
    return absl::Status(error_type, error_message);
  }
  INTR_ASSIGN_OR_RETURN(
      auto collision_error,
      CreateCollisionError(error_message, debug, configuration, proxy,
                           error_context));

  MotionPlanningError motion_planning_error =
      CreateMotionPlanningError(collision_error);
  absl::Status status = absl::Status(error_type, error_message);
  status.SetPayload(
      absl::StrCat(kMotionPlanningCollisionErrorPrefix, message_url_suffix),
      absl::Cord(motion_planning_error.SerializeAsString()));
  return status;
}

absl::Status CreateStatusWithCollisionError(
    absl::string_view error_message, absl::string_view message_url_suffix,
    const intrinsic_proto::motion_planning::v1::CollisionError&
        collision_error_proto,
    absl::StatusCode error_type) {
  MotionPlanningError motion_planning_error =
      CreateMotionPlanningError(collision_error_proto);
  absl::Status status = absl::Status(error_type, error_message);
  status.SetPayload(
      absl::StrCat(kMotionPlanningCollisionErrorPrefix, message_url_suffix),
      absl::Cord(motion_planning_error.SerializeAsString()));
  return status;
}

absl::Status CreateStatusWithIKError(
    absl::string_view error_message, absl::string_view message_url_suffix,
    absl::StatusCode error_type, const ErrorContext::Type& error_context,
    std::optional<intrinsic_proto::motion_planning::v1::GeometricConstraint>
        geometric_constraint) {
  intrinsic_proto::motion_planning::v1::IKError ik_error_proto;
  *ik_error_proto.mutable_error_message() = error_message;
  ik_error_proto.set_error_context(error_context);
  if (geometric_constraint.has_value()) {
    *ik_error_proto.mutable_constraint() = geometric_constraint.value();
  }
  const MotionPlanningError motion_planning_error =
      CreateMotionPlanningError(ik_error_proto);
  absl::Status status = absl::Status(error_type, error_message);
  status.SetPayload(
      absl::StrCat(kMotionPlanningIKErrorPrefix, message_url_suffix),
      absl::Cord(motion_planning_error.SerializeAsString()));
  return status;
}

// Update the constraint for a Motion planning error in a status payload.
absl::Status AssignConstraintForIKError(
    absl::Status status, const GeometricConstraint geometric_constraint,
    absl::string_view message_url_suffix) {
  if (HasMotionPlanningErrorPayload(status)) {
    // Access the existing MotionPlanningError
    status.ForEachPayload(
        [&](absl::string_view type_url, const absl::Cord& payload) {
          if (absl::StrContains(type_url, "MotionPlanningError")) {
            MotionPlanningError motion_planning_error;
            if (absl::StrContains(type_url, "ik_error")) {
              static_cast<void>(motion_planning_error.ParseFromString(
                  payload));  // TODO: Handle return value
            }
            *motion_planning_error.mutable_ik_error()->mutable_constraint() =
                geometric_constraint;
            absl::string_view new_type_url = type_url;
            // Erase the original payload
            status.ErasePayload(type_url);
            // Set the updated payload
            status.SetPayload(new_type_url,
                              motion_planning_error.SerializeAsCord());
          }
        });
  } else {
    // Create a Motion planning error
    MotionPlanningError motion_planning_error;
    intrinsic_proto::motion_planning::v1::IKError ik_error_proto;
    *ik_error_proto.mutable_error_message() = status.message();
    *ik_error_proto.mutable_constraint() = geometric_constraint;
    *motion_planning_error.mutable_ik_error() = ik_error_proto;
    status.SetPayload(
        absl::StrCat(kMotionPlanningIKErrorPrefix, message_url_suffix),
        motion_planning_error.SerializeAsCord());
  }
  return status;
};
intrinsic_proto::motion_planning::v1::FinePathIKError CreateFinePathIKError(
    absl::string_view error_message,
    const eigenmath::VectorNd& prev_ik_solution,
    const eigenmath::VectorNd& curr_ik_hint,
    const eigenmath::VectorNd& curr_ik_solution,
    const Pose3d& base_t_tip_desired, const JointLimits& joint_limits,
    const ErrorContext::Type& error_context) {
  intrinsic_proto::motion_planning::v1::FinePathIKError fine_path_ik_error;
  *fine_path_ik_error.mutable_error_message() = error_message;
  *fine_path_ik_error.mutable_previous_ik_solution() =
      icon::ToJointVecProto(prev_ik_solution);
  *fine_path_ik_error.mutable_current_ik_hint() =
      icon::ToJointVecProto(curr_ik_hint);
  *fine_path_ik_error.mutable_current_ik_solution() =
      icon::ToJointVecProto(curr_ik_solution);
  *fine_path_ik_error.mutable_base_t_tip_desired() =
      ToProto(base_t_tip_desired);
  *fine_path_ik_error.mutable_applied_joint_limits() = ToProto(joint_limits);
  fine_path_ik_error.set_error_context(error_context);

  return fine_path_ik_error;
}

intrinsic_proto::motion_planning::v1::LinearCartesianMotionPathPlannerError
CreateLinearCartesianMotionPathPlannerError(
    absl::string_view error_message,
    const eigenmath::VectorNd& target_joint_positions,
    const eigenmath::VectorNd& final_joint_positions,
    const ErrorContext::Type& error_context) {
  intrinsic_proto::motion_planning::v1::LinearCartesianMotionPathPlannerError
      linear_cartesian_path_planning_error;
  *linear_cartesian_path_planning_error.mutable_error_message() = error_message;
  *linear_cartesian_path_planning_error.mutable_target_joint_positions() =
      icon::ToJointVecProto(target_joint_positions);
  *linear_cartesian_path_planning_error.mutable_final_joint_positions() =
      icon::ToJointVecProto(final_joint_positions);
  linear_cartesian_path_planning_error.set_error_context(error_context);
  return linear_cartesian_path_planning_error;
}

absl::Status CreateStatusWithFinePathIKError(
    absl::string_view error_message,
    const eigenmath::VectorNd& prev_ik_solution,
    const eigenmath::VectorNd& curr_ik_hint,
    const eigenmath::VectorNd& curr_ik_solution,
    const Pose3d& base_t_tip_desired, const JointLimits& joint_limits,
    absl::string_view message_url_suffix,
    const ErrorContext::Type& error_context,
    const absl::StatusCode error_type) {
  intrinsic_proto::motion_planning::v1::FinePathIKError path_planning_error =
      CreateFinePathIKError(error_message, prev_ik_solution, curr_ik_hint,
                            curr_ik_solution, base_t_tip_desired, joint_limits,
                            error_context);
  MotionPlanningError motion_planning_error =
      CreateMotionPlanningError(path_planning_error);
  absl::Status status = absl::Status(error_type, error_message);
  status.SetPayload(
      absl::StrCat(kMotionPlanningFinePathIKErrorPrefix, message_url_suffix),
      absl::Cord(motion_planning_error.SerializeAsCord()));
  return status;
}

absl::Status CreateStatusWithLinCartPathPlanningError(
    absl::string_view error_message,
    const eigenmath::VectorNd& target_joint_positions,
    const eigenmath::VectorNd& final_joint_positions,
    absl::string_view message_url_suffix, const absl::StatusCode error_type,
    const ErrorContext::Type& error_context) {
  intrinsic_proto::motion_planning::v1::LinearCartesianMotionPathPlannerError
      linear_cartesian_path_planning_error =
          CreateLinearCartesianMotionPathPlannerError(
              error_message, target_joint_positions, final_joint_positions,
              error_context);
  MotionPlanningError motion_planning_error =
      CreateMotionPlanningError(linear_cartesian_path_planning_error);
  absl::Status status = absl::Status(error_type, error_message);
  status.SetPayload(
      absl::StrCat(kMotionPlanningLinearCartesianPathPlanningErrorPrefix,
                   message_url_suffix),
      absl::Cord(motion_planning_error.SerializeAsCord()));
  return status;
}

intrinsic_proto::motion_planning::v1::JointLimitError CreateJointLimitError(
    absl::string_view error_message, const eigenmath::VectorNd& joint_positions,
    ErrorContext::Type error_context, std::optional<JointLimits> joint_limits) {
  intrinsic_proto::motion_planning::v1::JointLimitError joint_limit_error;
  *joint_limit_error.mutable_error_message() = error_message;
  *joint_limit_error.mutable_joint_positions() =
      icon::ToJointVecProto(joint_positions);
  if (joint_limits.has_value()) {
    *joint_limit_error.mutable_joint_limits() = ToProto(joint_limits.value());
  }
  joint_limit_error.set_error_context(error_context);
  return joint_limit_error;
}

absl::Status CreateStatusWithJointLimitError(
    absl::string_view error_message, const eigenmath::VectorNd& joint_positions,
    absl::string_view message_url_suffix, ErrorContext::Type error_context,
    absl::StatusCode error_type, std::optional<JointLimits> joint_limits) {
  intrinsic_proto::motion_planning::v1::JointLimitError joint_limit_error =
      CreateJointLimitError(
          error_message, joint_positions, error_context,
          joint_limits.has_value() ? joint_limits : std::nullopt);
  MotionPlanningError motion_planning_error =
      CreateMotionPlanningError(joint_limit_error);
  absl::Status status = absl::Status(error_type, error_message);
  status.SetPayload(
      absl::StrCat(kMotionPlanningJointLimitErrorPrefix, message_url_suffix),
      absl::Cord(motion_planning_error.SerializeAsCord()));
  return status;
}

absl::Status UpdateStatusErrorContext(
    const absl::Status& status, ErrorContext::Type error_context,
    std::optional<absl::StatusCode> status_code) {
  absl::Status updated_status =
      status_code.has_value()
          ? absl::Status(status_code.value(), status.message())
          : status;

  status.ForEachPayload([&](absl::string_view type_url,
                            const absl::Cord& payload) {
    MotionPlanningError motion_planning_error;
    if (absl::StrContains(type_url, "MotionPipelineError")) {
      intrinsic_proto::motion_planning::v1::MotionPipelineError
          motion_pipeline_error;
      static_cast<void>(motion_pipeline_error.ParseFromString(
          payload));  // TODO: Handle return value
      if (!motion_pipeline_error.motion_planning_error().empty()) {
        motion_planning_error =
            motion_pipeline_error.motion_planning_error().Get(0);
      }
    } else if (absl::StrContains(type_url, "MotionPlanningError")) {
      static_cast<void>(motion_planning_error.ParseFromString(
          payload));  // TODO: Handle return value
    };
    switch (motion_planning_error.error_case()) {
      case MotionPlanningError::kCollisionError:
        motion_planning_error.mutable_collision_error()->set_error_context(
            error_context);
        break;
      case MotionPlanningError::kIkError:
        motion_planning_error.mutable_ik_error()->set_error_context(
            error_context);
        break;
      case MotionPlanningError::kFinePathIkError:
        motion_planning_error.mutable_fine_path_ik_error()->set_error_context(
            error_context);
        break;
      case MotionPlanningError::kLinearCartesianPathPlanningError:
        motion_planning_error.mutable_linear_cartesian_path_planning_error()
            ->set_error_context(error_context);
        break;
      case MotionPlanningError::kJointLimitError:
        motion_planning_error.mutable_joint_limit_error()->set_error_context(
            error_context);
        break;
      default:
        break;
    }
    updated_status.SetPayload(type_url,
                              motion_planning_error.SerializeAsCord());
  });

  return updated_status;
}

absl::Status UpdateStatusWithPipelineError(
    const absl::Status& status, absl::Span<const int> segment_indices,
    absl::string_view append_to_error_message) {
  absl::Status updated_status = status;
  if (!append_to_error_message.empty()) {
    updated_status = AnnotateError(updated_status, append_to_error_message);
  }
  // `absl::Status` forbids mutating (erasing or setting) payloads during
  // `ForEachPayload` visitation. We collect payload keys and new payloads into
  // local vectors first and apply the mutations after iteration completes.
  std::vector<std::string> payloads_to_erase;
  std::vector<std::pair<std::string, absl::Cord>> payloads_to_add;

  updated_status.ForEachPayload(
      [&](absl::string_view type_url, const absl::Cord& payload) {
        // Only convert unwrapped `MotionPlanningError` payloads. Skip payloads
        // already wrapped in a `MotionPipelineError` to prevent double-wrapping
        // and proto parsing corruption.
        if (absl::StrContains(type_url, "MotionPlanningError") &&
            !absl::StrContains(type_url, "MotionPipelineError")) {
          intrinsic_proto::motion_planning::v1::MotionPlanningError
              motion_planning_error;
          intrinsic_proto::motion_planning::v1::MotionPipelineError
              motion_pipeline_error;
          if (!motion_planning_error.ParseFromCord(payload)) {
            LOG(ERROR) << "Failed to parse MotionPlanningError from payload: "
                       << type_url;
            return;
          }
          for (const int index : segment_indices) {
            *motion_planning_error.mutable_segment_id()->Add() = index;
          }
          *motion_pipeline_error.mutable_motion_planning_error()->Add() =
              motion_planning_error;
          const std::string new_type_url = absl::StrCat(
              "type.googleapis.com/", "MotionPipelineError:", type_url);
          payloads_to_erase.push_back(std::string(type_url));
          payloads_to_add.push_back(
              {new_type_url, motion_pipeline_error.SerializeAsCord()});
        }
      });

  // Mutate payloads outside `ForEachPayload` visitation.
  for (const std::string& url : payloads_to_erase) {
    updated_status.ErasePayload(url);
  }
  for (const auto& [url, cord] : payloads_to_add) {
    updated_status.SetPayload(url, cord);
  }
  return updated_status;
}

}  // namespace intrinsic
