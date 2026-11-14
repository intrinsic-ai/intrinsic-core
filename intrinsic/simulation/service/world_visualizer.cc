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

#include "intrinsic/simulation/service/world_visualizer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/simulation/world/world_interpolation.h"
#include "intrinsic/simulation/world/world_updater.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/object_entity_filter.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/proto/entity_search.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"

namespace intrinsic {
namespace simulation {

namespace {

// We will sample the visualization at a rate of 30Hz.
constexpr absl::Duration kMinVisualizationStepSize = absl::Milliseconds(33);

absl::Duration GetMaxDofUpdateDuration(
    const eigenmath::VectorXd& current_values,
    const eigenmath::VectorXd& current_velocity_limits,
    const eigenmath::VectorXd& new_values) {
  absl::Duration max_duration = absl::ZeroDuration();
  for (int i = 0; i < current_values.size(); ++i) {
    auto current_dof_value = current_values[i];
    auto updated_dof_value = new_values[i];

    // Our joint velocity will be 3/4 of the max for any movement.
    auto max_dof_velocity = 3.f * current_velocity_limits[i] / 4.f;

    auto duration = absl::Seconds(
        std::abs(updated_dof_value - current_dof_value) / max_dof_velocity);
    max_duration = std::max(max_duration, duration);
  }
  VLOG(1) << "Updating joint values from ["
          << absl::StrJoin(current_values, ", ") << "] to ["
          << absl::StrJoin(new_values, ", ") << "] within limits ["
          << absl::StrJoin(current_velocity_limits, ", ") << "] over "
          << max_duration;
  return max_duration;
}

absl::StatusOr<absl::Duration> EstimateTimeToVisualizeUpdateObjectJoints(
    const world::ObjectWorldClient& world,
    const intrinsic_proto::world::UpdateObjectJointsRequest&
        update_object_joints) {
  if (update_object_joints.has_joint_application_limits()) {
    return absl::InvalidArgumentError(
        "Unsupported update: "
        "UpdateObjectJointsRequest::joint_application_limits.");
  }
  if (update_object_joints.has_joint_system_limits()) {
    return absl::InvalidArgumentError(
        "Unsupported update: UpdateObjectJointsRequest::joint_system_limits.");
  }

  if (update_object_joints.joint_positions_size() == 0) {
    return absl::ZeroDuration();
  }

  // For joint updates, use the max of the joint velocity limits...
  INTR_ASSIGN_OR_RETURN(const auto kin_obj, world.GetKinematicObject(
                                                update_object_joints.object()));
  const eigenmath::VectorXd current_values = kin_obj.JointPositions();

  const auto& joint_limits = kin_obj.JointApplicationLimits();

  const eigenmath::VectorXd current_velocity_limits = joint_limits.max_velocity;

  if (current_values.size() != update_object_joints.joint_positions_size() ||
      current_values.size() != current_velocity_limits.size()) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "Attempting to interpolate robot with " << current_values.size()
           << " dofs when update contains "
           << update_object_joints.joint_positions_size() << " dofs.";
  }

  eigenmath::VectorXd new_values = current_values;
  for (int i = 0; i < update_object_joints.joint_positions_size(); ++i) {
    new_values[i] = update_object_joints.joint_positions(i);
  }
  return GetMaxDofUpdateDuration(current_values, current_velocity_limits,
                                 new_values);
}

absl::StatusOr<absl::Duration> EstimateTimeToVisualizeUpdateObjectJoint(
    const world::ObjectWorldClient& world,
    const intrinsic_proto::world::UpdateObjectJointRequest&
        update_object_joint) {
  // For joint updates, use the max of the joint velocity limits...
  INTR_ASSIGN_OR_RETURN(const auto kin_obj,
                        world.GetKinematicObject(update_object_joint.object()));
  const eigenmath::VectorXd current_values = kin_obj.JointPositions();

  const auto& joint_limits = kin_obj.JointApplicationLimits();

  const eigenmath::VectorXd current_velocity_limits = joint_limits.max_velocity;

  auto joint_entity_ids =
      kin_obj.Proto().kinematic_object_component().joint_entity_ids();
  std::optional<size_t> target_joint_idx;
  for (size_t i = 0; i < joint_entity_ids.size(); ++i) {
    const auto& joint_entity_id = joint_entity_ids[i];
    if (!kin_obj.Proto().entities().contains(joint_entity_id)) {
      return absl::InternalError("Joint entity is not found in object!");
    }

    absl::string_view joint_local_name =
        kin_obj.Proto().entities().at(joint_entity_id).name();

    if (joint_local_name == update_object_joint.joint_name()) {
      target_joint_idx = i;
      break;
    }
  }
  if (!target_joint_idx.has_value()) {
    return absl::NotFoundError(absl::StrCat("Joint with name `",
                                            update_object_joint.joint_name(),
                                            "` was not found."));
  }

  eigenmath::VectorXd new_values = current_values;
  new_values[target_joint_idx.value()] = update_object_joint.joint_position();
  return GetMaxDofUpdateDuration(current_values, current_velocity_limits,
                                 new_values);
}

absl::StatusOr<absl::Duration> EstimateTimeToVisualizeUpdateTransform(
    const world::ObjectWorldClient& world,
    const intrinsic_proto::world::UpdateTransformRequest& update_transform) {
  // Calculate the distance in displacement and base it off of a fixed speed
  constexpr float kSecondsPerMeter = 2.0f;

  INTR_ASSIGN_OR_RETURN(auto node_a,
                        world.GetTransformNode(update_transform.node_a()));
  INTR_ASSIGN_OR_RETURN(auto node_b,
                        world.GetTransformNode(update_transform.node_b()));

  std::optional<world::ObjectEntityFilter> node_a_filter = std::nullopt;
  if (update_transform.has_node_a_filter()) {
    node_a_filter =
        world::ObjectEntityFilter::FromProto(update_transform.node_a_filter());
  }
  std::optional<world::ObjectEntityFilter> node_b_filter = std::nullopt;
  if (update_transform.has_node_b_filter()) {
    node_b_filter =
        world::ObjectEntityFilter::FromProto(update_transform.node_b_filter());
  }

  INTR_ASSIGN_OR_RETURN(
      const Pose original_pose,
      world.GetTransform(node_a, node_a_filter, node_b, node_b_filter));
  INTR_ASSIGN_OR_RETURN(const Pose new_pose,
                        FromProto(update_transform.a_t_b()));

  Pose pose_delta = new_pose * original_pose.inverse();
  float distance = pose_delta.translation().norm();
  return absl::Seconds(distance * kSecondsPerMeter);
}

absl::StatusOr<absl::Duration> EstimateTimeToVisualize(
    const world::ObjectWorldClient& world,
    const ObjectWorldUpdate& next_update) {
  switch (next_update.update_case()) {
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateObjectJoints: {
      return EstimateTimeToVisualizeUpdateObjectJoints(
          world, next_update.update_object_joints());
    }
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateTransform: {
      return EstimateTimeToVisualizeUpdateTransform(
          world, next_update.update_transform());
    }
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateObjectJoint:
      return EstimateTimeToVisualizeUpdateObjectJoint(
          world, next_update.update_object_joint());
    case intrinsic_proto::world::ObjectWorldUpdate::kToggleCollisions:
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateCollisionSettings:
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateObjectName:
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateFrameName:
    case intrinsic_proto::world::ObjectWorldUpdate::
        kUpdateKinematicObjectProperties:
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateObjectProperties:
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateEntityProperties:
    case intrinsic_proto::world::ObjectWorldUpdate::kCreateObject:
    case intrinsic_proto::world::ObjectWorldUpdate::kDeleteObject:
    case intrinsic_proto::world::ObjectWorldUpdate::kCreateFrame:
    case intrinsic_proto::world::ObjectWorldUpdate::kDeleteFrame:
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateFrameProperties:
    case intrinsic_proto::world::ObjectWorldUpdate::kReparentObject:
    case intrinsic_proto::world::ObjectWorldUpdate::kReparentFrame:
    case intrinsic_proto::world::ObjectWorldUpdate::UPDATE_NOT_SET: {
      return absl::ZeroDuration();  // These are instant.
    }
  }
}

std::vector<absl::Duration> EstimateTimesToVisualize(
    const world::ObjectWorldClient& world,
    const ObjectWorldUpdates& next_updates) {
  std::vector<absl::Duration> result;

  // Loop over the object world updates
  for (const auto& update : next_updates.updates()) {
    auto estimate = EstimateTimeToVisualize(world, update);
    if (!estimate.ok()) {
      LOG(WARNING) << "Unable to estimate time for update: "
                   << estimate.status();
      result.push_back(absl::ZeroDuration());
    } else {
      result.push_back(*estimate);
    }
  }

  return result;
}

std::vector<double> GetNormalizedInterpolatedTimes(absl::Duration t) {
  const absl::Duration dt = kMinVisualizationStepSize;
  const int num_steps = t / dt;
  if (num_steps == 0) {
    return {};
  }

  const int64_t t_micro = t / absl::Microseconds(1);
  CHECK(t_micro > 0)
      << "Shouldn't be here. Please ensure kMinVisualizationStepSize > 1us.";

  std::vector<double> interpolated_time(num_steps);
  int n = 0;
  std::generate(interpolated_time.begin(), interpolated_time.end(),
                [&n, &dt, &t_micro]() {
                  absl::Duration interim_t = n++ * dt;
                  int64_t interim_t_micro = interim_t / absl::Microseconds(1);
                  return std::fmin(1.0, static_cast<double>(interim_t_micro) /
                                            static_cast<double>(t_micro));
                });
  return interpolated_time;
}

}  // namespace

absl::StatusOr<std::unique_ptr<WorldVisualizer>> WorldVisualizer::Create() {
  return absl::WrapUnique(new WorldVisualizer);
}

absl::Status WorldVisualizer::Visualize(
    const world::ObjectWorldClient& world, const ObjectWorldUpdates& updates,
    std::optional<absl::Duration> animation_time) {
  const int num_updates = updates.updates_size();

  // Visualize the result
  std::vector<absl::Duration> times_to_visualize;
  if (animation_time) {
    // TODO(b/259744285) Fix the warning.
    LOG_IF(WARNING, num_updates > 1)
        << "Multiple updates to visualize with a single animation time. This "
        << "could look weird (see: b/259744285).";
    times_to_visualize.resize(num_updates, animation_time.value());
  } else {
    times_to_visualize = EstimateTimesToVisualize(world, updates);
    INTR_RET_CHECK(times_to_visualize.size() == num_updates);
  }
  const absl::Duration max_time_to_visualize =
      *std::max_element(times_to_visualize.begin(), times_to_visualize.end());
  LOG(INFO) << "Visualizing " << num_updates << " update"
            << ((num_updates == 1) ? "" : "s") << " over "
            << max_time_to_visualize;

  std::vector<std::vector<ObjectWorldUpdate>> interpolated_updates;
  for (int i = 0; i < num_updates; ++i) {
    std::vector<double> interpolated_time =
        GetNormalizedInterpolatedTimes(times_to_visualize[i]);
    INTR_ASSIGN_OR_RETURN(std::vector<ObjectWorldUpdate> interpolated_update,
                          InterpolateObjectWorldUpdate(
                              world, updates.updates(i), interpolated_time));
    interpolated_updates.push_back(std::move(interpolated_update));
  }

  const absl::Duration dt = kMinVisualizationStepSize;
  const absl::Time vis_start_time = absl::Now();
  for (int t_ind = 0; t_ind < (max_time_to_visualize / dt); ++t_ind) {
    const absl::Duration elapsed_time = absl::Now() - vis_start_time;
    if ((elapsed_time / dt) > t_ind) {
      LOG_EVERY_N_SEC(WARNING, 1)
          << "Skipping step to catch animation up to wall clock.";
      continue;
    }

    const absl::Time step_start_time = absl::Now();

    for (int i = 0; i < num_updates; ++i) {
      if (t_ind >= interpolated_updates[i].size()) {
        continue;
      }

      const ObjectWorldUpdate& next_update = interpolated_updates[i][t_ind];
      // Don't publish empty updates
      if (next_update.update_case() !=
          intrinsic_proto::world::ObjectWorldUpdate::UPDATE_NOT_SET) {
        PublishUpdate(next_update);
      }
    }

    // Sleep for the rest of the time it takes for this update.
    absl::SleepFor(dt - (absl::Now() - step_start_time));
  }

  // Final updates to match final world
  INTR_ASSIGN_OR_RETURN(auto final_updates,
                        InterpolateObjectWorldUpdates(world, updates, 1.0));
  for (const auto& world_update : final_updates.updates()) {
    PublishUpdate(world_update);
  }

  LOG(INFO) << "Finished visualizing!";

  return absl::OkStatus();
}

}  // namespace simulation
}  // namespace intrinsic
