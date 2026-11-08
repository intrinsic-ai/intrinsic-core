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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_FINITE_DIFFERENCE_TEST_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_FINITE_DIFFERENCE_TEST_H_

#include <cmath>
#include <string>

#include "intrinsic/eigenmath/types.h"

namespace intrinsic {
namespace control {
namespace test {
// Utility classes to compute time derivatives by finite differences.

// template function to convert a DiffType (finite differences)
// to a ValueType. This is for angular velocity calculations
// via finite differences.
template <typename ValueType, typename DiffType>
DiffType toDiffType(const ValueType& fd, const ValueType& val);

/// template function to compute the norm of a DiffType
template <typename DiffType>
double norm(const DiffType& val);

// vector case: just return finite difference approximation
template <>
inline eigenmath::Vector3d toDiffType(const eigenmath::Vector3d& fd,
                                      const eigenmath::Vector3d& val) {
  return fd;
}

// vector case: just return finite difference approximation
template <>
inline eigenmath::VectorNd toDiffType(const eigenmath::VectorNd& fd,
                                      const eigenmath::VectorNd& val) {
  return fd;
}

// orientation case: calculate spin tensor and extract angular velocity
template <>
inline eigenmath::Vector3d toDiffType(const eigenmath::Matrix3d& fd,
                                      const eigenmath::Matrix3d& val) {
  // spin tensor
  eigenmath::Matrix3d omega_tilde = fd * val.transpose();
  // extract vector from spin tensor
  eigenmath::Vector3d omega;
  omega(0) = 0.5 * (omega_tilde(2, 1) - omega_tilde(1, 2));
  omega(1) = 0.5 * (omega_tilde(0, 2) - omega_tilde(2, 0));
  omega(2) = 0.5 * (omega_tilde(1, 0) - omega_tilde(0, 1));
  return omega;
}

// scalar case
template <>
inline double toDiffType(const double& fd, const double& val) {
  return fd;
}

/// implementation for Vector3d
template <>
inline double norm(const eigenmath::Vector3d& val) {
  return val.norm();
}
/// implementation for VectorNd
template <>
inline double norm(const eigenmath::VectorNd& val) {
  return val.norm();
}
/// implementation for double
template <>
inline double norm(const double& val) {
  return std::fabs(val);
}

/// Class for calculating finite difference approximation
/// of time derivatives and comparing it to an analytical solution
/// DiffType and ValueType can be different, to allow comparison
/// of angular velocity vectors and orientations given as transform matrices.
template <typename ValueType, typename DiffType>
class FiniteDifferenceTest {
 public:
  /// constructor
  FiniteDifferenceTest()
      : dt_(0.0),
        num_updates_(0),
        max_error_(0.0),
        max_value_(0.0),
        valid_fd_(false) {}
  /// initialize.
  /// @param name string to identify object in messages.
  /// @param dt timestep at which update() is called.
  void init(std::string name, double dt) {
    name_ = name;
    dt_ = dt;
    num_updates_ = 0;
    max_error_ = 0.0;
    max_value_ = 0.0;
    avg_error_ = 0.0;
    valid_fd_ = false;
  }

  /// add new data and update finite difference approximations
  /// @param val new value
  /// @param true_diff new (analytical) derivative wrt. time. This is what the
  /// finite differences will be compared to.
  void update(const ValueType& val, const DiffType& true_diff) {
    val_ = val;
    if (num_updates_ > 2) {
      // 2nd order finite difference approximation for d(value)/dt
      ValueType diff_value_fd = (val - older_val_) / (2.0 * dt_);
      // convert to analytical diff type. This is for angular velocities
      diff_fd_ = toDiffType<ValueType, DiffType>(diff_value_fd, old_val_);
      // now, calculate the error
      double error = norm<DiffType>(diff_fd_ - old_true_diff_);
      if (error > max_error_) {
        max_error_ = error;
      }

      double value = norm<DiffType>(old_true_diff_);
      if (value > max_value_) {
        max_value_ = value;
      }

      valid_fd_ = true;

      avg_error_ =
          (avg_error_ * (num_updates_ - 3) + error) / (num_updates_ - 2);
    }
    older_val_ = old_val_;
    old_val_ = val_;

    // printCurrent();
    old_true_diff_ = true_diff;
    num_updates_++;
    time_ += dt_;
  }
  /// return maximum error (difference between finite difference and reference
  /// value)
  double getMaxError() const { return max_error_; }

 private:
  double dt_;
  ValueType val_;
  ValueType old_val_;
  ValueType older_val_;
  DiffType old_true_diff_;
  DiffType diff_fd_;
  int num_updates_;
  double max_error_;
  double avg_error_;
  double max_value_;
  double time_;
  std::string name_;
  bool valid_fd_;
};
}  // namespace test
}  // namespace control
}  // namespace intrinsic
#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_FINITE_DIFFERENCE_TEST_H_
