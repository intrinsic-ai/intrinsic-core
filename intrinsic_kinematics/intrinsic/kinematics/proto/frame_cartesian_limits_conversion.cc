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

#include "intrinsic/kinematics/proto/frame_cartesian_limits_conversion.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/proto/cart_space_conversion.h"
#include "intrinsic/kinematics/proto/frame_cartesian_limits.pb.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

intrinsic_proto::FrameCartesianPathLimits ToProto(
    const FrameCartesianPathLimits& frame_cartesian_path_limits) {
  intrinsic_proto::FrameCartesianPathLimits proto;
  proto.set_frame_name(
      absl::string_view(frame_cartesian_path_limits.frame_name));
  proto.set_parent_link_name(
      absl::string_view(frame_cartesian_path_limits.parent_link_name));
  *proto.mutable_parent_link_t_frame() =
      intrinsic::ToProto(frame_cartesian_path_limits.parent_link_t_frame);
  *proto.mutable_limits() =
      intrinsic::icon::ToProto(frame_cartesian_path_limits.limits);
  return proto;
}

absl::StatusOr<FrameCartesianPathLimits> FromProto(
    const intrinsic_proto::FrameCartesianPathLimits&
        frame_cartesian_path_limits_proto) {
  if (frame_cartesian_path_limits_proto.frame_name().empty() ||
      frame_cartesian_path_limits_proto.frame_name().size() >
          FrameCartesianPathLimits::kMaxFrameNameLength) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Frame name '%s' must be non-empty and at most %d characters, got %d.",
        frame_cartesian_path_limits_proto.frame_name(),
        FrameCartesianPathLimits::kMaxFrameNameLength,
        frame_cartesian_path_limits_proto.frame_name().size()));
  }
  if (frame_cartesian_path_limits_proto.parent_link_name().empty() ||
      frame_cartesian_path_limits_proto.parent_link_name().size() >
          FrameCartesianPathLimits::kMaxFrameNameLength) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Parent link name '%s' must be non-empty and at most %d characters, "
        "got %d.",
        frame_cartesian_path_limits_proto.parent_link_name(),
        FrameCartesianPathLimits::kMaxFrameNameLength,
        frame_cartesian_path_limits_proto.parent_link_name().size()));
  }
  FrameCartesianPathLimits frame_limits;
  frame_limits.frame_name =
      absl::string_view(frame_cartesian_path_limits_proto.frame_name());
  frame_limits.parent_link_name =
      absl::string_view(frame_cartesian_path_limits_proto.parent_link_name());
  INTR_ASSIGN_OR_RETURN(
      frame_limits.parent_link_t_frame,
      intrinsic_proto::FromProto(
          frame_cartesian_path_limits_proto.parent_link_t_frame()));
  INTR_ASSIGN_OR_RETURN(
      frame_limits.limits,
      intrinsic::icon::FromProto(frame_cartesian_path_limits_proto.limits()));
  return frame_limits;
}

}  // namespace intrinsic
