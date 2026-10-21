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

#include "intrinsic/util/eigen.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "boost/regex.hpp"  // NOLINT
#include "intrinsic/eigenmath/pose2.h"
#include "intrinsic/eigenmath/rotation_utils.h"
#include "intrinsic/eigenmath/so3.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

namespace {
std::vector<double> toVector(const std::string& pose_string) {
  // ([-.0-9e]+)[,\s] would be better, but \s is not well supported in current
  // version of boost or std at the moment
  boost::regex regex("([-e.0-9]+)");

  std::string str = pose_string;

  std::vector<double> pose_vector;
  boost::sregex_token_iterator rend;
  boost::sregex_token_iterator regex_iter(str.begin(), str.end(), regex);
  while (regex_iter != rend) {
    pose_vector.emplace_back(std::stod(*regex_iter++));
  }

  return pose_vector;
}
}  // namespace

eigenmath::Pose2d toPose2d(const std::string& pose_string) {
  return toPose2(toVector(pose_string));
}

Pose3d toPose3d(const std::string& pose_string) {
  return toPose3(toVector(pose_string));
}

Pose3d toPose3dXYZXYZW(const std::vector<double>& pose) {
  CHECK_EQ(pose.size(), 7)
      << "Size of pose should be 7 and representing a pose [x,y,z,qx,qy,qz,qw]";
  return Pose3d(eigenmath::Quaterniond(pose[6], pose[3], pose[4], pose[5]),
                {pose[0], pose[1], pose[2]});
}

Pose3d toPose3dFromXYZRPY(const std::string& xyzrpy_string) {
  auto values = toVector(xyzrpy_string);

  CHECK_EQ(values.size(), 6) << "Size of value string should be 6 and "
                                "representing a pose [x,y,z,r,p,y]";

  Pose3d result;
  result.setTranslation({values[0], values[1], values[2]});
  result.setQuaternion(
      eigenmath::QuaternionFromRPY(values[3], values[4], values[5]));
  return result;
}

eigenmath::Matrix4d toScaleMatrix(const eigenmath::Vector3d& scale) {
  return eigenmath::Matrix4d({{scale(0), 0, 0, 0},
                              {0, scale(1), 0, 0},
                              {0, 0, scale(2), 0},
                              {0, 0, 0, 1}});
}

absl::StatusOr<std::pair<Pose3d, eigenmath::Vector3d>> matrixToPoseAndScale(
    const eigenmath::Matrix4d& affine_transform) {
  // A valid rotation is first extracted from `affine_transform`. This
  // prevents a possible crash if the Pose3 object is directly constructed from
  // a bad `affine_transform`.
  INTR_ASSIGN_OR_RETURN(eigenmath::SO3d rotation,
                        eigenmath::SO3d::FromMatrix(
                            affine_transform.template topLeftCorner<3, 3>()));

  const Pose3d pose(std::move(rotation),
                    affine_transform.topRightCorner<3, 1>());
  const eigenmath::Matrix4d maybe_scale_matrix =
      pose.inverse().matrix() * affine_transform;

  const bool only_scale = maybe_scale_matrix.isDiagonal();
  if (!only_scale) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Matrix\n$0\ncan't be decomposed into equivalent pose3d + scale",
        toString(affine_transform)));
  }
  eigenmath::Vector3d scale({maybe_scale_matrix(0, 0), maybe_scale_matrix(1, 1),
                             maybe_scale_matrix(2, 2)});
  return std::make_pair(pose, scale);
}

eigenmath::Quaterniond quatLog(const eigenmath::Quaterniond& quat_in) {
  double r = sqrt(quat_in.x() * quat_in.x() + quat_in.y() * quat_in.y() +
                  quat_in.z() * quat_in.z());

  double t = 0.0;
  if (r > std::numeric_limits<double>::epsilon()) {
    t = atan2(r, quat_in.w()) / r;
  }

  eigenmath::Quaterniond quat_out(quat_in);
  quat_out.w() = 0.5 * log(quat_in.squaredNorm());
  quat_out.x() *= t;
  quat_out.y() *= t;
  quat_out.z() *= t;

  return quat_out;
}

eigenmath::Quaterniond quatExp(const eigenmath::Quaterniond& quat_in) {
  double r = sqrt(quat_in.x() * quat_in.x() + quat_in.y() * quat_in.y() +
                  quat_in.z() * quat_in.z());
  double et = exp(quat_in.w());

  double s = 0.0;
  if (r > std::numeric_limits<double>::epsilon()) {
    s = et * sin(r) / r;
  }

  eigenmath::Quaterniond quat_out(quat_in);

  quat_out.w() = et * cos(r);
  quat_out.x() *= s;
  quat_out.y() *= s;
  quat_out.z() *= s;

  return quat_out;
}

eigenmath::Quaterniond quatPower(const eigenmath::Quaterniond& quat,
                                 const double power) {
  eigenmath::Quaterniond quat_in(quat);

  if (fabs(quat_in.norm() - 1) > std::numeric_limits<double>::epsilon()) {
    LOG(WARNING) << "Normalizing quaternion before executing log function.";
    quat_in.normalize();
  }

  // q^p = exp(p * log(q))
  eigenmath::Quaterniond quat_out = quatLog(quat_in);

  quat_out.w() *= power;
  quat_out.x() *= power;
  quat_out.y() *= power;
  quat_out.z() *= power;

  quat_out = quatExp(quat_out);

  return quat_out;
}

Pose3d interpolate(const Pose3d& a_t_b, const Pose3d& a_t_c, const double pct) {
  CHECK_GE(pct, 0) << "Percentage outside [0,1]";
  CHECK_LE(pct, 1) << "Percentage outside [0,1]";

  Pose3d b_t_c = a_t_b.inverse() * a_t_c;

  return Pose3d(a_t_b.quaternion().slerp(pct, a_t_c.quaternion()),
                a_t_b * (b_t_c.translation() * pct));
}

absl::StatusOr<double> ParseDouble(absl::string_view s) {
  double value;
  if (!absl::SimpleAtod(s, &value)) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Failed to parse \"%s\" as an double.", s));
  }
  return value;
}

absl::StatusOr<std::vector<double>> ParseCsvDoubles(absl::string_view ss) {
  std::vector<double> out;
  if (ss.empty()) {
    return out;
  }
  std::vector<std::string> split = absl::StrSplit(ss, ',');
  out.reserve(split.size());
  for (const auto& s : split) {
    INTR_ASSIGN_OR_RETURN(double value, ParseDouble(s));
    out.push_back(value);
  }
  return out;
}

absl::StatusOr<Pose3d> ParsePose(absl::string_view ss) {
  INTR_ASSIGN_OR_RETURN(auto values, ParseCsvDoubles(ss));
  if (values.size() != 7) {
    return absl::InvalidArgumentError("Pose must have 7 args (xyz_wxyz).");
  }
  Pose3d pose;
  pose.setTranslation(
      eigenmath::Vector3d(values.at(0), values.at(1), values.at(2)));
  pose.setQuaternion(eigenmath::Quaterniond(values.at(3), values.at(4),
                                            values.at(5), values.at(6)));
  return pose;
}

absl::StatusOr<Pose3d> ParsePoseXYZXYZW(absl::string_view ss, bool normalize) {
  INTR_ASSIGN_OR_RETURN(auto values, ParseCsvDoubles(ss));
  if (values.size() != 7) {
    return absl::InvalidArgumentError("Pose must have 7 args (xyzxyzw).");
  }
  return Pose3d(
      eigenmath::Quaterniond(values[6], values[3], values[4], values[5]),
      eigenmath::Vector3d(values[0], values[1], values[2]),
      normalize ? eigenmath::kNormalize : eigenmath::kDoNotNormalize);
}

Pose3d GetPoseFromArrays(const std::array<double, 3>& base_t_tip_pos,
                         const std::array<double, 4>& base_t_tip_ori) {
  eigenmath::Vector3d position;
  for (size_t i = 0; i < base_t_tip_pos.size(); ++i) {
    position[i] = base_t_tip_pos[i];
  }

  // Constructing w,x,y,z. Constructing quaternion using .data() needs x,y,z,w.
  eigenmath::Quaterniond quaternion(base_t_tip_ori[0], base_t_tip_ori[1],
                                    base_t_tip_ori[2], base_t_tip_ori[3]);

  return Pose3d(eigenmath::SO3d(quaternion), position);
}

eigenmath::VectorXd Saturate(const eigenmath::VectorXd& v,
                             const eigenmath::VectorXd& v_min,
                             const eigenmath::VectorXd& v_max) {
  eigenmath::VectorXd result(v.size());
  for (size_t i = 0; i < v.size(); ++i) {
    CHECK_LE(v_min[i], v_max[i])
        << "Lower bound should be smaller or equal than upper bound";
    result[i] = std::clamp(v[i], v_min[i], v_max[i]);
  }
  return result;
}

void CopyDoublesToRepeatedDoubles(
    absl::Span<const double> doubles,
    google::protobuf::RepeatedField<double>* repeated_doubles) {
  repeated_doubles->Clear();
  repeated_doubles->Reserve(doubles.size());
  repeated_doubles->Assign(doubles.begin(), doubles.end());
}

bool CopyRepeatedDoublesToDoubles(
    const google::protobuf::RepeatedField<double>& src,
    absl::Span<double> dst) {
  if (dst.size() < src.size()) {
    return false;
  }
  std::copy(src.begin(), src.end(), dst.begin());
  return true;
}

}  // namespace intrinsic
