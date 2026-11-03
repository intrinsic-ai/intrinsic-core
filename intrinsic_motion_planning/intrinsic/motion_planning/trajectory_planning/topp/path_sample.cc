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

#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.h"

#include <cstddef>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/proto/cart_space_conversion.h"
#include "intrinsic/icon/proto/eigen_conversion.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/kinematics/proto/frame_cartesian_limits_conversion.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.pb.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace topp {

/*static*/
absl::StatusOr<PathSample> PathSample::Unlimited(int size) {
  PathSample sample;
  INTR_RETURN_IF_ERROR(sample.SetSize(size));
  sample.SetUnlimited();
  return sample;
}

absl::Status PathSample::SetSize(int size) {
  s = 0;
  s_c = 0;
  q = eigenmath::VectorNd::Zero(size);
  qp = eigenmath::VectorNd::Zero(size);
  qpp = eigenmath::VectorNd::Zero(size);
  qppp = eigenmath::VectorNd::Zero(size);
  qppp_in = std::nullopt;
  return joint_limits.SetSize(size);
}

void PathSample::SetUnlimited() {
  joint_limits.SetUnlimited();
  cart_limits.SetUnlimited();
  for (auto& frame_limit : frame_cartesian_limits) {
    frame_limit.limits.SetUnlimited();
  }
}

void PathSample::SetZero() {
  s = 0;
  s_c = 0;
  q.setZero();
  qp.setZero();
  qpp.setZero();
  qppp.setZero();
  qppp_in = std::nullopt;
  tip_t_target = Pose3d::Identity();
}

size_t PathSample::Size() const { return q.size(); }

bool PathSample::IsSizeConsistent() const {
  return ((qp.rows() == Size()) && (qpp.rows() == Size()) &&
          (qppp.rows() == Size()) &&
          (!qppp_in.has_value() || qppp_in->rows() == Size()) &&
          (joint_limits.size() == Size()) && joint_limits.IsSizeConsistent());
}

bool PathSample::IsValid() const {
  if (Size() == 0) return true;
  if (!IsSizeConsistent()) return false;
  if (s < 0) return false;
  if (s_c < 0) return false;
  if (!cart_limits.IsValid()) return false;
  for (const auto& frame_limit : frame_cartesian_limits) {
    if (!frame_limit.limits.IsValid()) return false;
  }
  return joint_limits.IsValid();
}

intrinsic_proto::topp::PathSample ToProto(const PathSample& path_sample) {
  intrinsic_proto::topp::PathSample path_sample_proto;
  path_sample_proto.set_s(path_sample.s);
  path_sample_proto.set_s_c(path_sample.s_c);
  VectorXdToRepeatedDouble(path_sample.q, path_sample_proto.mutable_q());
  VectorXdToRepeatedDouble(path_sample.qp, path_sample_proto.mutable_qp());
  VectorXdToRepeatedDouble(path_sample.qpp, path_sample_proto.mutable_qpp());
  VectorXdToRepeatedDouble(path_sample.qppp, path_sample_proto.mutable_qppp());
  if (path_sample.qppp_in.has_value()) {
    VectorXdToRepeatedDouble(*path_sample.qppp_in,
                             path_sample_proto.mutable_qppp_in());
  }
  *path_sample_proto.mutable_joint_limits() =
      intrinsic::ToProto(path_sample.joint_limits);
  *path_sample_proto.mutable_cart_limits() =
      intrinsic::icon::ToProto(path_sample.cart_limits);
  *path_sample_proto.mutable_tip_t_target() =
      intrinsic::ToProto(path_sample.tip_t_target);
  for (const FrameCartesianPathLimits& frame_limits :
       path_sample.frame_cartesian_limits) {
    *path_sample_proto.add_frame_cartesian_limits() =
        intrinsic::ToProto(frame_limits);
  }
  path_sample_proto.set_segment_id(path_sample.segment_id);
  return path_sample_proto;
}

absl::StatusOr<PathSample> FromProto(
    const intrinsic_proto::topp::PathSample& path_sample_proto) {
  PathSample path_sample;
  path_sample.s = path_sample_proto.s();
  path_sample.s_c = path_sample_proto.s_c();
  INTR_ASSIGN_OR_RETURN(path_sample.q,
                        icon::RepeatedDoubleToVectorNd(path_sample_proto.q()));
  INTR_ASSIGN_OR_RETURN(path_sample.qp,
                        icon::RepeatedDoubleToVectorNd(path_sample_proto.qp()));
  INTR_ASSIGN_OR_RETURN(
      path_sample.qpp, icon::RepeatedDoubleToVectorNd(path_sample_proto.qpp()));
  INTR_ASSIGN_OR_RETURN(path_sample.qppp, icon::RepeatedDoubleToVectorNd(
                                              path_sample_proto.qppp()));
  if (!path_sample_proto.qppp_in().empty()) {
    INTR_ASSIGN_OR_RETURN(
        path_sample.qppp_in,
        icon::RepeatedDoubleToVectorNd(path_sample_proto.qppp_in()));
  }

  INTR_ASSIGN_OR_RETURN(path_sample.joint_limits,
                        intrinsic::FromProto(path_sample_proto.joint_limits()));
  INTR_ASSIGN_OR_RETURN(
      path_sample.cart_limits,
      intrinsic::icon::FromProto(path_sample_proto.cart_limits()));
  INTR_ASSIGN_OR_RETURN(
      path_sample.tip_t_target,
      intrinsic_proto::FromProto(path_sample_proto.tip_t_target()));
  for (const intrinsic_proto::FrameCartesianPathLimits& frame_limits_proto :
       path_sample_proto.frame_cartesian_limits()) {
    INTR_ASSIGN_OR_RETURN(FrameCartesianPathLimits frame_limits,
                          intrinsic::FromProto(frame_limits_proto));
    path_sample.frame_cartesian_limits.push_back(std::move(frame_limits));
  }
  path_sample.segment_id = path_sample_proto.segment_id();

  return path_sample;
}

}  // namespace topp
}  // namespace intrinsic
