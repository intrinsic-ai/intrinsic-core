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

#include "intrinsic/scene/conversion/scene_object_model_utils.h"

#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/kinematics_component.pb.h"

namespace intrinsic {
namespace scene_object {

namespace {
using ::intrinsic::eigenmath::Vector3d;
using ::intrinsic_proto::FromProto;
using ::intrinsic_proto::FromProtoNormalized;
using ::intrinsic_proto::scene_object::v1::Entity;
using ::intrinsic_proto::world::KinematicsComponent;

double DefaultJointValueFromLimits(const KinematicsComponent& kinematics) {
  std::optional<std::pair<double, double>> fixed_limits;
  if (kinematics.application_limits().has_fixed_limits()) {
    fixed_limits = {kinematics.application_limits().fixed_limits().lower(),
                    kinematics.application_limits().fixed_limits().upper()};
  } else if (kinematics.system_limits().has_fixed_limits()) {
    fixed_limits = {kinematics.system_limits().fixed_limits().lower(),
                    kinematics.system_limits().fixed_limits().upper()};
  }

  double joint_value = 0.0;
  if (fixed_limits.has_value()) {
    const auto [lower, upper] = *fixed_limits;
    if (!(joint_value <= upper && joint_value >= lower)) {
      if (std::isfinite(lower) && std::isfinite(upper)) {
        joint_value = lower + (upper - lower) * 0.5;
      } else if (std::isfinite(lower)) {
        joint_value = lower;
      } else if (std::isfinite(upper)) {
        joint_value = upper;
      }
    }
  }
  return joint_value;
}

}  // namespace

absl::flat_hash_set<std::string> GetNonFixedJointNames(
    const std::vector<Entity>& entities) {
  absl::flat_hash_set<std::string> joint_names;
  for (const auto& entity : entities) {
    if (entity.has_joint() &&
        entity.joint().kinematics_component().motion_type() !=
            intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED)
      joint_names.insert(entity.name());
  }
  return joint_names;
}

absl::StatusOr<Pose3d> ResolveJointEntityPose(const Entity& entity) {
  if (!entity.has_joint()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Entity '", entity.name(), "' does not have a joint component."));
  }

  // `entity.parent_t_this()` is ignored for joints; the pose is computed
  // solely from `kinematics_component`.
  const KinematicsComponent& kinematics = entity.joint().kinematics_component();

  Pose3d parent_t_inboard = Pose3d::Identity();
  if (kinematics.has_parent_t_inboard()) {
    INTR_ASSIGN_OR_RETURN(parent_t_inboard,
                          FromProtoNormalized(kinematics.parent_t_inboard()));
  }

  Pose3d outboard_t_child = Pose3d::Identity();
  if (kinematics.has_outboard_t_child()) {
    INTR_ASSIGN_OR_RETURN(outboard_t_child,
                          FromProtoNormalized(kinematics.outboard_t_child()));
  }

  double joint_value = 0.0;
  if (kinematics.has_raw_value()) {
    joint_value = kinematics.raw_value();
    if (!std::isfinite(joint_value)) {
      return absl::InvalidArgumentError("Joint has non-finite raw_value.");
    }
  } else {
    joint_value = DefaultJointValueFromLimits(kinematics);
  }

  Pose3d inboard_t_outboard = Pose3d::Identity();
  switch (kinematics.motion_type()) {
    case KinematicsComponent::MOTION_TYPE_FIXED:
      if (kinematics.has_raw_value() && kinematics.raw_value() != 0.0) {
        return absl::InvalidArgumentError(
            "Fixed joint cannot have a non-zero raw_value.");
      }
      break;
    case KinematicsComponent::MOTION_TYPE_REVOLUTE:
    case KinematicsComponent::MOTION_TYPE_PRISMATIC: {
      Vector3d axis = Vector3d::UnitZ();
      if (kinematics.has_axis()) {
        axis = FromProto(kinematics.axis());
        if (!axis.allFinite() || axis.squaredNorm() < 1e-12) {
          return absl::InvalidArgumentError(
              "Joint must have a finite, non-zero axis.");
        }
        axis.normalize();
      }
      if (kinematics.motion_type() ==
          KinematicsComponent::MOTION_TYPE_REVOLUTE) {
        inboard_t_outboard = CreateAngleAxisPose(joint_value, axis);
      } else {
        inboard_t_outboard = Pose3d(Vector3d(joint_value * axis));
      }
      break;
    }
    case KinematicsComponent::MOTION_TYPE_UNDEFINED:
    default:
      return absl::InvalidArgumentError(
          "Joint must have a valid motion_type defined.");
  }

  return parent_t_inboard * inboard_t_outboard * outboard_t_child;
}

}  // namespace scene_object
}  // namespace intrinsic
