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

#include "intrinsic/icon/control/algorithms/cartesian_impedance_trajectory.h"

#include <memory>

#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/pose3_utils.h"
#include "intrinsic/icon/actions/cartesian_impedance.pb.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

/*static*/
absl::StatusOr<std::unique_ptr<CartesianImpedanceTrajectoryInterface>>
ConstantCartesianImpedance::Create(
    const intrinsic_proto::icon::actions::proto::CartesianTarget&
        proto_params) {
  INTR_ASSIGN_OR_RETURN(cartesian_impedance::CartesianTarget params,
                        FromProto(proto_params));

  // Perform non-realtime pre-processing steps on the received parameters,
  // such as definiteness checking and auto-derivation of missing elements.
  INTR_RETURN_IF_ERROR(cartesian_impedance::Preprocess(params));

  return std::unique_ptr<CartesianImpedanceTrajectoryInterface>(
      new ConstantCartesianImpedance(params));
}

RealtimeStatus ConstantCartesianImpedance::Initialize(
    const CartStatePVA& base_t_target_last_commanded) {
  // If a Cartesian target pose has been provided, check that the error
  // between current and target pose is not too large. Otherwise, an
  // admittance controller might result in extreme motion.
  if (non_rt_cartesian_target_.task_t_tool_reference_pose.has_value()) {
    auto pose_error = eigenmath::PoseErrorBetween(
        base_t_target_last_commanded.pose,
        non_rt_cartesian_target_.robot_base_t_task *
            non_rt_cartesian_target_.task_t_tool_reference_pose.value());
    if (pose_error.translation > kMaxTranslationErrorAtInitTime) {
      return icon::FailedPreconditionError(RealtimeStatus::StrCat(
          "Difference between current and target position exceeds ",
          kMaxTranslationErrorAtInitTime));
    } else if (pose_error.rotation > kMaxRotationErrorAtInitTime) {
      return icon::FailedPreconditionError(RealtimeStatus::StrCat(
          "Difference between current and target orientation exceeds ",
          kMaxRotationErrorAtInitTime));
    }
  }
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      rt_cartesian_target_,
      cartesian_impedance::ToRealTimeCartesianTarget(
          base_t_target_last_commanded.pose *
              non_rt_cartesian_target_.robot_tip_t_robot_tool.inverse(),
          non_rt_cartesian_target_));

  return icon::OkStatus();
}

RealtimeStatusOr<cartesian_impedance::RealTimeCartesianTarget>
ConstantCartesianImpedance::Step(double speed_override) {
  return cartesian_impedance::RealTimeCartesianTarget(rt_cartesian_target_);
}

/*static*/
absl::StatusOr<std::unique_ptr<NullspaceTrajectoryInterface>>
ConstantNullspaceTarget::Create(
    const intrinsic_proto::icon::actions::proto::NullspaceTarget&
        proto_params) {
  INTR_ASSIGN_OR_RETURN(cartesian_impedance::NullspaceTarget params,
                        FromProto(proto_params));

  // Perform non-realtime pre-processing steps on the received parameters,
  // auto-derivation of missing elements.
  INTR_RETURN_IF_ERROR(cartesian_impedance::Preprocess(params));

  return std::unique_ptr<NullspaceTrajectoryInterface>(
      new ConstantNullspaceTarget(params));
}

RealtimeStatus ConstantNullspaceTarget::Initialize(
    const JointStatePVA& joint_state_last_commanded) {
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      rt_nullspace_target_,
      cartesian_impedance::ToRealTimeNullspaceTarget(
          non_rt_nullspace_target_, joint_state_last_commanded));
  return icon::OkStatus();
}

RealtimeStatusOr<cartesian_impedance::RealTimeNullspaceTarget>
ConstantNullspaceTarget::Step(double speed_override) {
  return cartesian_impedance::RealTimeNullspaceTarget(rt_nullspace_target_);
}

}  // namespace intrinsic::icon
