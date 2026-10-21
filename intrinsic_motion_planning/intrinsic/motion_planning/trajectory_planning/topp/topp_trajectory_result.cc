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

#include "intrinsic/motion_planning/trajectory_planning/topp/topp_trajectory_result.h"

#include "absl/status/statusor.h"
#include "intrinsic/icon/proto/joint_trajectory_conversion.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/bspline_squared_path_velocity.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/piecewise_linear_squared_path_velocity.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/topp_trajectory_result.pb.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::topp {

absl::StatusOr<intrinsic_proto::topp::ToppTrajectoryResult> ToProto(
    const ToppTrajectoryResult& topp_trajectory_result) {
  intrinsic_proto::topp::ToppTrajectoryResult proto;
  INTR_ASSIGN_OR_RETURN(*proto.mutable_trajectory(),
                        ToProto(topp_trajectory_result.trajectory));
  if (topp_trajectory_result.squared_path_velocity != nullptr) {
    INTR_RETURN_IF_ERROR(
        topp_trajectory_result.squared_path_velocity->PopulateProto(&proto));
  }

  *proto.mutable_motion_events() = {
      topp_trajectory_result.motion_events.begin(),
      topp_trajectory_result.motion_events.end()};

  return proto;
}

absl::StatusOr<ToppTrajectoryResult> FromProto(
    const intrinsic_proto::topp::ToppTrajectoryResult&
        topp_trajectory_result_proto) {
  ToppTrajectoryResult result;
  INTR_ASSIGN_OR_RETURN(
      result.trajectory,
      intrinsic::FromProto(topp_trajectory_result_proto.trajectory()));
  switch (topp_trajectory_result_proto.squared_path_velocity_type_case()) {
    case intrinsic_proto::topp::ToppTrajectoryResult::kSquaredPathVelocity: {
      INTR_ASSIGN_OR_RETURN(
          result.squared_path_velocity,
          FromProto(topp_trajectory_result_proto.squared_path_velocity()));
      break;
    }
    case intrinsic_proto::topp::ToppTrajectoryResult::
        kPiecewiseLinearSquaredPathVelocity: {
      INTR_ASSIGN_OR_RETURN(
          result.squared_path_velocity,
          FromProto(topp_trajectory_result_proto
                        .piecewise_linear_squared_path_velocity()));
      break;
    }
    case intrinsic_proto::topp::ToppTrajectoryResult::
        SQUARED_PATH_VELOCITY_TYPE_NOT_SET:
      result.squared_path_velocity = nullptr;
      break;
  }

  result.motion_events = {topp_trajectory_result_proto.motion_events().begin(),
                          topp_trajectory_result_proto.motion_events().end()};

  return result;
}

}  // namespace intrinsic::topp
