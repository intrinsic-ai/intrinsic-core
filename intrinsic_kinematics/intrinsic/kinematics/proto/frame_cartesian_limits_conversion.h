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

#ifndef INTRINSIC_KINEMATICS_PROTO_FRAME_CARTESIAN_LIMITS_CONVERSION_H_
#define INTRINSIC_KINEMATICS_PROTO_FRAME_CARTESIAN_LIMITS_CONVERSION_H_

#include "absl/status/statusor.h"
#include "intrinsic/kinematics/proto/frame_cartesian_limits.pb.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"

namespace intrinsic {

// Converts `FrameCartesianPathLimits` to a `FrameCartesianPathLimits` proto.
intrinsic_proto::FrameCartesianPathLimits ToProto(
    const FrameCartesianPathLimits& frame_cartesian_path_limits);

// Converts a `FrameCartesianPathLimits` proto to `FrameCartesianPathLimits`.
// Returns `absl::InvalidArgumentError` if `frame_name` or `parent_link_name` is
// empty or exceeds `FrameCartesianPathLimits::kMaxFrameNameLength`, or if
// `parent_link_t_frame` or `limits` is invalid.
absl::StatusOr<FrameCartesianPathLimits> FromProto(
    const intrinsic_proto::FrameCartesianPathLimits&
        frame_cartesian_path_limits_proto);

}  // namespace intrinsic

#endif  // INTRINSIC_KINEMATICS_PROTO_FRAME_CARTESIAN_LIMITS_CONVERSION_H_
