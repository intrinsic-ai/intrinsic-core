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

#ifndef INTRINSIC_UTIL_EIGEN_H_
#define INTRINSIC_UTIL_EIGEN_H_

#include <array>
#include <cstddef>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"  
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "google/protobuf/repeated_field.h"
#include "intrinsic/eigenmath/pose2.h"  
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic {

namespace eigen_details {
template <typename T>
void ToRepeatedDouble(const T& values,
                      google::protobuf::RepeatedField<double>* output) {
  output->Clear();
  output->Reserve(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    output->Add(values[i]);
  }
}

template <typename T>
T FromRepeatedDouble(const google::protobuf::RepeatedField<double>& values) {
  T result(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    result[i] = values[i];
  }

  return result;
}

template <typename S, typename T>
S ConvertVector(const T& values) {
  S result(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    result[i] = values[i];
  }

  return result;
}

}  // namespace eigen_details

// Convert a std::vector to a vectorNd
inline eigenmath::VectorNd VectorToVectorNd(const std::vector<double>& values) {
  return eigen_details::ConvertVector<eigenmath::VectorNd>(values);
}

// Convert a vectorNd to a std::vector
inline std::vector<double> VectorNdToVector(const eigenmath::VectorNd& values) {
  return eigen_details::ConvertVector<std::vector<double>>(values);
}

// Convert a vectorNd to a proto field of repeated doubles
inline void VectorNdToRepeatedDouble(
    const eigenmath::VectorNd& values,
    google::protobuf::RepeatedField<double>* output) {
  eigen_details::ToRepeatedDouble(values, output);
}

// Convert a std vector to vectorXd
inline eigenmath::VectorXd VectorToVectorXd(const std::vector<double>& values) {
  return eigen_details::ConvertVector<eigenmath::VectorXd>(values);
}

// Convert an std::vector of VectorXd to a MatrixXd.
inline eigenmath::MatrixXd VectorOfVectorXdToMatrixXd(
    const std::vector<eigenmath::VectorXd>& vectors) {
  CHECK_GT(vectors.size(), 0) << absl::StrFormat(
      "The number of VectorXd's should be greater than 0, but got %lu instead.",
      vectors.size());
  eigenmath::MatrixXd matrixxd(vectors[0].size(), vectors.size());
  for (size_t i = 0; i < vectors.size(); ++i) {
    matrixxd.col(i) = vectors[i];
  }
  return matrixxd;
}

inline std::vector<double> VectorXdToVector(const eigenmath::VectorXd& values) {
  return eigen_details::ConvertVector<std::vector<double>>(values);
}

template <size_t T>
inline std::array<double, T> VectorXdToArray(
    const eigenmath::VectorXd& values) {
  CHECK_EQ(values.size(), T) << absl::StrFormat(
      "The size of the input VectorXd[%lu] should be equal to T[%lu]",
      values.size(), T);
  std::array<double, T> out_array;
  for (size_t i = 0; i < T; ++i) {
    out_array[i] = values[i];
  }
  return out_array;
}

template <size_t T>
inline eigenmath::VectorXd ArrayToVectorXd(std::array<double, T>& values) {
  return Eigen::Map<eigenmath::VectorXd>(values.data(), values.size());
}

// Convert a vectorXd to a proto repeated field double
inline void VectorXdToRepeatedDouble(
    const eigenmath::VectorXd& values,
    google::protobuf::RepeatedField<double>* output) {
  eigen_details::ToRepeatedDouble(values, output);
}

// Convert a proto double repeated field to a VectorXd
inline eigenmath::VectorXd RepeatedDoubleToVectorXd(
    const google::protobuf::RepeatedField<double>& values) {
  return eigen_details::FromRepeatedDouble<eigenmath::VectorXd>(values);
}

// Convert a repeated double to vector double
inline std::vector<double> RepeatedDoubleToVectorDouble(
    const google::protobuf::RepeatedField<double>& values) {
  return eigen_details::FromRepeatedDouble<std::vector<double>>(values);
}

// Convert a vector double to repeated double
inline void VectorDoubleToRepeatedDouble(
    const std::vector<double>& values,
    google::protobuf::RepeatedField<double>* output) {
  eigen_details::ToRepeatedDouble(values, output);
}

// Validates the shape of matrix and converts the input MatrixXd to a
// affine transform Matrix4d type
// Returns an error if matrix's size is not valid for an affine transform
inline absl::StatusOr<eigenmath::Matrix4d> toAffineMatrix4d(
    const eigenmath::MatrixXd& matrix) {
  if (matrix.cols() != 4) {
    return absl::InvalidArgumentError(
        absl::Substitute("3D affine transform matrix must have 4 columns but "
                         "input has $0 columns",
                         matrix.cols()));
  }
  if (matrix.rows() != 4) {
    return absl::InvalidArgumentError(absl::Substitute(
        "3D affine transform matrix must have 4 rows but input has $0 rows",
        matrix.rows()));
  }
  eigenmath::Matrix4d affine_matrix(matrix);
  affine_matrix.row(3) << 0, 0, 0, 1;
  return affine_matrix;
}


// Converts a list of 7 values (x, y, z, qw, qx, qy, qz) to a Pose3d
template <typename T>
Pose3<T, Eigen::AutoAlign> toPose3(const std::vector<T>& pose) {
  CHECK_EQ(pose.size(), 7)
      << "Size of pose should be 7 and representing a pose [x,y,z,qw,qx,qy,qz]";
  return Pose3<T, Eigen::AutoAlign>(eigenmath::Quaternion<T, Eigen::AutoAlign>(
                                        pose[3], pose[4], pose[5], pose[6]),
                                    {pose[0], pose[1], pose[2]});
}

// Converts a list of 7 values (x, y, z, qx, qy, qz, qw) to a Pose3d
Pose3d toPose3dXYZXYZW(const std::vector<double>& pose);

// Converts a list of 3 values (x, y, rz) to a Pose2d
template <typename T>
eigenmath::Pose2<T, Eigen::AutoAlign> toPose2(const std::vector<T>& pose) {
  CHECK_EQ(pose.size(), 3)
      << "Size of pose should be 3 and representing a pose [x,y,rz]";
  return eigenmath::Pose2<T, Eigen::AutoAlign>(
      pose[2], eigenmath::Vector2<T>(pose[0], pose[1]));
}

// Converts a double string vector of format "<x> <y> <rz>" to a Pose2d
eigenmath::Pose2d toPose2d(const std::string& pose_string);

// Converts a double string vector of format "<x> <y> <z> <qw> <qx> <qy> <qz>"
// to Pose3d
Pose3d toPose3d(const std::string& pose_string);

// Converts a double string vector of format "<x> <y> <z> <roll> <pitch> <yaw>"
// to Pose3d
// Note: Euler angles are expressed as radians around XYZ in a static reference
// frame.
Pose3d toPose3dFromXYZRPY(const std::string& xyzrpy_string);
// Converts a 3d scale vector to an equivalent 4x4 transform matrix
eigenmath::Matrix4d toScaleMatrix(const eigenmath::Vector3d& scale);

// Attempts to decompose a affine_transform into equivalent pose and scale.
// Returns an error if the matrix can't be decomposed into an equivalent pose
// and scale
absl::StatusOr<std::pair<Pose3d, eigenmath::Vector3d>> matrixToPoseAndScale(
    const eigenmath::Matrix4d& affine_transform);

template <typename T, int Options>
std::string toString(const Pose3<T, Options>& pose) {
  return absl::StrCat(pose.translation().x(), ",", pose.translation().y(), ",",
                      pose.translation().z(), ",", pose.quaternion().w(), ",",
                      pose.quaternion().x(), ",", pose.quaternion().y(), ",",
                      pose.quaternion().z());
}

template <typename T, int Options>
std::string toStringXYZXYZW(const Pose3<T, Options>& pose) {
  return absl::StrCat(pose.translation().x(), ",", pose.translation().y(), ",",
                      pose.translation().z(), ",", pose.quaternion().x(), ",",
                      pose.quaternion().y(), ",", pose.quaternion().z(), ",",
                      pose.quaternion().w());
}

// Like toString but ensure that the quaternion scalar w is positive. This help
// to ensure that quaternions are display consistently displayed as q == -q.
template <typename T, int Options>
std::string toStringPretty(const Pose3<T, Options>& pose) {
  eigenmath::Quaternion<T, Options> q = pose.quaternion();
  if (q.w() < 0) {
    q = eigenmath::Quaternion<T, Options>(-1 * q.coeffs());
  }
  return absl::StrCat(pose.translation().x(), ",", pose.translation().y(), ",",
                      pose.translation().z(), ",", q.w(), ",", q.x(), ",",
                      q.y(), ",", q.z());
}

template <typename T, int Options>
std::string toStringPrettyXYZXYZW(const Pose3<T, Options>& pose) {
  eigenmath::Quaternion<T, Options> q = pose.quaternion();
  if (q.w() < 0) {
    q = eigenmath::Quaternion<T, Options>(-1 * q.coeffs());
  }
  return absl::StrCat(pose.translation().x(), ",", pose.translation().y(), ",",
                      pose.translation().z(), ",", q.x(), ",", q.y(), ",",
                      q.z(), ",", q.w());
}

template <typename T, int Options>
std::string toString(const eigenmath::Quaternion<T, Options>& quaternion) {
  return absl::StrCat(quaternion.w(), ",", quaternion.x(), ",", quaternion.y(),
                      ",", quaternion.z());
}
template <typename T, int Options>
std::string toStringXYZW(const eigenmath::Quaternion<T, Options>& quaternion) {
  return absl::StrCat(quaternion.x(), ",", quaternion.y(), ",", quaternion.z(),
                      ",", quaternion.w());
}

template <typename T>
std::string toString(const eigenmath::Vector3<T>& vec) {
  return absl::StrCat(vec.x(), ",", vec.y(), ",", vec.z());
}

namespace eigen_details {

// Helper function for stringifying a vector
template <typename T>
std::string VectorToString(const T& vec) {
  if (vec.size() == 0) {
    return "";
  }

  std::stringstream ss;
  ss << vec[0];

  for (size_t i = 1; i < vec.size(); ++i) {
    ss << "," << vec[i];
  }

  return ss.str();
}
}  // namespace eigen_details

template <class Scalar, int Options>
std::string toString(const eigenmath::Matrix4<Scalar, Options>& m) {
  return absl::StrCat(eigen_details::VectorToString(m.row(0)), "\n",
                      eigen_details::VectorToString(m.row(1)), "\n",
                      eigen_details::VectorToString(m.row(2)), "\n",
                      eigen_details::VectorToString(m.row(3)), "\n");
}

inline std::string toString(const eigenmath::VectorNd& vec) {
  return eigen_details::VectorToString(vec);
}

template <typename T>
std::string toString(const eigenmath::VectorX<T>& vec) {
  return eigen_details::VectorToString(vec);
}

template <typename T>
std::string toString(const std::vector<T>& vec) {
  return eigen_details::VectorToString(vec);
}

inline std::string toString(const std::vector<eigenmath::VectorXd>& vec) {
  if (vec.empty()) {
    return "";
  }
  std::stringstream ss;
  ss << toString(vec[0]);
  for (size_t i = 1; i < vec.size(); ++i) {
    ss << "\n" << toString(vec[i]);
  }
  return ss.str();
}

// Log, exp and power functions for quaternions; based on theory from
// http://web.mit.edu/2.998/www/QuaternionReport1.pdf
//
// TODO(stoyang): consider moving these to eigenmath folder
eigenmath::Quaterniond quatLog(const eigenmath::Quaterniond& quat_in);
eigenmath::Quaterniond quatExp(const eigenmath::Quaterniond& quat_in);
eigenmath::Quaterniond quatPower(const eigenmath::Quaterniond& quat,
                                 double power);

Pose3d interpolate(const Pose3d& a_t_b, const Pose3d& a_t_c, double pct);

// Additional parsing functions. These tend to be more convenient for user input
// since they format appropriate error messages.
absl::StatusOr<double> ParseDouble(absl::string_view s);
absl::StatusOr<std::vector<double>> ParseCsvDoubles(absl::string_view ss);
// In "x,y,z,qw,qx,qy,qz" form.
absl::StatusOr<Pose3d> ParsePose(absl::string_view ss);
// In "x,y,z,qx,qy,qz,qw" form.
// If normalize is true, then the quaternion will be normalized.
absl::StatusOr<Pose3d> ParsePoseXYZXYZW(absl::string_view ss,
                                        bool normalize = true);

// Get a pose from a {x,y,z},{qw,qx,qy,qz} form.
Pose3d GetPoseFromArrays(const std::array<double, 3>& base_t_tip_pos,
                         const std::array<double, 4>& base_t_tip_ori);

// For every element of v, adjust to v_min[i] if v[i] is smaller than v_min[i]
// or to v_max[i] if v[i] is greater than v_max[i].
eigenmath::VectorXd Saturate(const eigenmath::VectorXd& v,
                             const eigenmath::VectorXd& v_min,
                             const eigenmath::VectorXd& v_max);

// Copies all values from the span to the repeated field.
//
// The repeated field will be cleared so that the two match exactly.
void CopyDoublesToRepeatedDoubles(
    absl::Span<const double> doubles,
    google::protobuf::RepeatedField<double>* repeated_doubles);

// Copies all values from the repeated field to the span.
//
// Fails if there is not enough space to copy the data.
//
// This works well with fixed size arrays as the Span construction understands
// the size.
bool CopyRepeatedDoublesToDoubles(
    const google::protobuf::RepeatedField<double>& src, absl::Span<double> dst);


}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_EIGEN_H_
