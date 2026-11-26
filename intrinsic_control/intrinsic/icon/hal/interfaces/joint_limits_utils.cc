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

#include "intrinsic/icon/hal/interfaces/joint_limits_utils.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "flatbuffers/detached_buffer.h"
#include "flatbuffers/flatbuffer_builder.h"
#include "flatbuffers/vector.h"
#include "intrinsic/icon/hal/hardware_interface_handle.h"
#include "intrinsic/icon/hal/interfaces/joint_limits.fbs.h"
#include "intrinsic/icon/utils/fixed_str_cat.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits.pb.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic_fbs {

flatbuffers::DetachedBuffer BuildJointLimits(uint32_t num_dof) {
  flatbuffers::FlatBufferBuilder builder;
  builder.ForceDefaults(true);

  std::vector<double> zeros(num_dof, 0.0);
  auto min_pos = builder.CreateVector(zeros);
  auto max_pos = builder.CreateVector(zeros);
  auto max_vel = builder.CreateVector(zeros);
  auto max_acc = builder.CreateVector(zeros);
  auto max_jerk = builder.CreateVector(zeros);
  auto max_effort = builder.CreateVector(zeros);
  auto joint_limits =
      CreateJointLimits(builder, min_pos, max_pos, false, max_vel, false,
                        max_acc, false, max_jerk, false, max_effort);
  builder.Finish(joint_limits);
  return builder.Release();
}

}  // namespace intrinsic_fbs

namespace intrinsic::icon {

// Checks that a absl::Span<double> and a Vector<double> from a
// flatbuffer have the same size. The field name is only used for the error
// message if sizes are not equal. Returns InvalidArgumentError otherwise.
RealtimeStatus CheckSizeEqual(absl::Span<const double> field,
                              const flatbuffers::Vector<double>& fb_field,
                              absl::string_view field_name) {
  int fb_num_joints = fb_field.size();
  int num_joints = field.size();
  if (fb_num_joints != num_joints) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "JointLimits Flatbuffer expects ", fb_num_joints,
        " joints but the field '", field_name, "' contains ", num_joints,
        " values"));
  }
  return OkStatus();
}

RealtimeStatus ToFlatbufferWithSizeCheck(absl::Span<const double> field,
                                         flatbuffers::Vector<double>& fb_field,
                                         absl::string_view field_name) {
  INTRINSIC_RT_RETURN_IF_ERROR(CheckSizeEqual(field, fb_field, field_name));
  for (int i = 0; i < fb_field.size(); i++) {
    fb_field.Mutate(i, field.at(i));
  }
  return OkStatus();
}

RealtimeStatus CopyTo(const JointLimits& limits,
                      intrinsic_fbs::JointLimits& fb_limits) {
  if (fb_limits.mutable_min_position() == nullptr ||
      fb_limits.mutable_max_position() == nullptr ||
      fb_limits.mutable_max_velocity() == nullptr ||
      fb_limits.mutable_max_acceleration() == nullptr ||
      fb_limits.mutable_max_jerk() == nullptr ||
      fb_limits.mutable_max_effort() == nullptr) {
    return InvalidArgumentError(
        "JointLimits Flatbuffer is missing one or more required vectors.");
  }
  INTRINSIC_RT_RETURN_IF_ERROR(ToFlatbufferWithSizeCheck(
      limits.min_position, *fb_limits.mutable_min_position(), "min_position"));
  INTRINSIC_RT_RETURN_IF_ERROR(ToFlatbufferWithSizeCheck(
      limits.max_position, *fb_limits.mutable_max_position(), "max_position"));
  INTRINSIC_RT_RETURN_IF_ERROR(ToFlatbufferWithSizeCheck(
      limits.max_velocity, *fb_limits.mutable_max_velocity(), "max_velocity"));
  INTRINSIC_RT_RETURN_IF_ERROR(ToFlatbufferWithSizeCheck(
      limits.max_acceleration, *fb_limits.mutable_max_acceleration(),
      "max_acceleration"));
  INTRINSIC_RT_RETURN_IF_ERROR(ToFlatbufferWithSizeCheck(
      limits.max_jerk, *fb_limits.mutable_max_jerk(), "max_jerk"));
  INTRINSIC_RT_RETURN_IF_ERROR(ToFlatbufferWithSizeCheck(
      limits.max_torque, *fb_limits.mutable_max_effort(), "max_effort"));

  fb_limits.mutate_has_velocity_limits(true);
  fb_limits.mutate_has_acceleration_limits(true);
  fb_limits.mutate_has_jerk_limits(true);
  fb_limits.mutate_has_effort_limits(true);

  return OkStatus();
}

RealtimeStatus CopyTo(const intrinsic_fbs::JointLimits& fb_limits,
                      JointLimits& limits) {
  if (fb_limits.min_position() == nullptr ||
      fb_limits.max_position() == nullptr) {
    return InvalidArgumentError(
        "JointLimits Flatbuffer is missing min_position or max_position.");
  }
  const size_t num_joints = fb_limits.min_position()->size();
  if (fb_limits.max_position()->size() != num_joints) {
    return InvalidArgumentError(
        "JointLimits Flatbuffer max_position size does not match "
        "min_position.");
  }
  if (fb_limits.has_velocity_limits() &&
      (fb_limits.max_velocity() == nullptr ||
       fb_limits.max_velocity()->size() != num_joints)) {
    return InvalidArgumentError(
        "JointLimits Flatbuffer max_velocity is missing or has mismatched "
        "size.");
  }
  if (fb_limits.has_acceleration_limits() &&
      (fb_limits.max_acceleration() == nullptr ||
       fb_limits.max_acceleration()->size() != num_joints)) {
    return InvalidArgumentError(
        "JointLimits Flatbuffer max_acceleration is missing or has mismatched "
        "size.");
  }
  if (fb_limits.has_jerk_limits() &&
      (fb_limits.max_jerk() == nullptr ||
       fb_limits.max_jerk()->size() != num_joints)) {
    return InvalidArgumentError(
        "JointLimits Flatbuffer max_jerk is missing or has mismatched size.");
  }
  if (fb_limits.has_effort_limits() &&
      (fb_limits.max_effort() == nullptr ||
       fb_limits.max_effort()->size() != num_joints)) {
    return InvalidArgumentError(
        "JointLimits Flatbuffer max_effort is missing or has mismatched size.");
  }
  INTRINSIC_RT_RETURN_IF_ERROR(limits.SetSize(num_joints));
  for (size_t i = 0; i < num_joints; ++i) {
    limits.min_position[i] = fb_limits.min_position()->Get(i);
    limits.max_position[i] = fb_limits.max_position()->Get(i);
    if (fb_limits.has_velocity_limits()) {
      limits.max_velocity[i] = fb_limits.max_velocity()->Get(i);
    }
    if (fb_limits.has_acceleration_limits()) {
      limits.max_acceleration[i] = fb_limits.max_acceleration()->Get(i);
    }
    if (fb_limits.has_jerk_limits()) {
      limits.max_jerk[i] = fb_limits.max_jerk()->Get(i);
    }
    if (fb_limits.has_effort_limits()) {
      limits.max_torque[i] = fb_limits.max_effort()->Get(i);
    }
  }
  return OkStatus();
}

absl::Status ParseProtoJointLimits(
    const intrinsic_proto::JointLimits& pb_limits,
    icon::MutableHardwareInterfaceHandle<intrinsic_fbs::JointLimits>&
        fb_limits) {
  fb_limits->mutate_has_velocity_limits(pb_limits.has_max_velocity());
  fb_limits->mutate_has_acceleration_limits(pb_limits.has_max_acceleration());
  fb_limits->mutate_has_jerk_limits(pb_limits.has_max_jerk());
  fb_limits->mutate_has_effort_limits(pb_limits.has_max_effort());

  if (pb_limits.min_position().values_size() !=
          pb_limits.max_position().values_size() ||
      (pb_limits.has_max_velocity() &&
       pb_limits.min_position().values_size() !=
           pb_limits.max_velocity().values_size()) ||
      (pb_limits.has_max_acceleration() &&
       pb_limits.min_position().values_size() !=
           pb_limits.max_acceleration().values_size()) ||
      (pb_limits.has_max_jerk() && pb_limits.min_position().values_size() !=
                                       pb_limits.max_jerk().values_size()) ||
      (pb_limits.has_max_effort() &&
       pb_limits.min_position().values_size() !=
           pb_limits.max_effort().values_size())) {
    return absl::InvalidArgumentError(
        absl::StrFormat("All non-empty fields in JointLimits proto must have "
                        "the same size. Sizes are: "
                        "min_position:%d, max_position:%d, max_velocity:%d, "
                        "max_acceleration:%d, max_jerk:%d, max_effort:%d. ",
                        pb_limits.min_position().values_size(),
                        pb_limits.max_position().values_size(),
                        pb_limits.max_velocity().values_size(),
                        pb_limits.max_acceleration().values_size(),
                        pb_limits.max_jerk().values_size(),
                        pb_limits.max_effort().values_size()));
  }

  INTR_RETURN_IF_ERROR(ToFlatbufferWithSizeCheck(
      pb_limits.min_position().values(), *fb_limits->mutable_min_position(),
      "min_position"));
  INTR_RETURN_IF_ERROR(ToFlatbufferWithSizeCheck(
      pb_limits.max_position().values(), *fb_limits->mutable_max_position(),
      "max_position"));
  if (pb_limits.has_max_velocity()) {
    INTR_RETURN_IF_ERROR(ToFlatbufferWithSizeCheck(
        pb_limits.max_velocity().values(), *fb_limits->mutable_max_velocity(),
        "max_velocity"));
  }
  if (pb_limits.has_max_acceleration()) {
    INTR_RETURN_IF_ERROR(ToFlatbufferWithSizeCheck(
        pb_limits.max_acceleration().values(),
        *fb_limits->mutable_max_acceleration(), "max_acceleration"));
  }
  if (pb_limits.has_max_jerk()) {
    INTR_RETURN_IF_ERROR(
        ToFlatbufferWithSizeCheck(pb_limits.max_jerk().values(),
                                  *fb_limits->mutable_max_jerk(), "max_jerk"));
  }
  if (pb_limits.has_max_effort()) {
    INTR_RETURN_IF_ERROR(ToFlatbufferWithSizeCheck(
        pb_limits.max_effort().values(), *fb_limits->mutable_max_effort(),
        "max_effort"));
  }

  return absl::OkStatus();
}

}  // namespace intrinsic::icon
