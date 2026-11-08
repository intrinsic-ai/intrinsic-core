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

#include "intrinsic/icon/control/algorithms/axis_angle_kinematics.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "Eigen/Core"
#include "intrinsic/eigenmath/types.h"

namespace intrinsic {
namespace control {
// This checks that converting from axis*angle to angular velocity and back is
// consistent (ie, that one transform is the inverse of the other)
// Correctness of the derivatives is checked in the unit test for
// the CartesianSetpointGenerator.
TEST(AxisAngleKinematics, TestForwardBackward) {
  double max_err_vel = 0;
  double max_err_acc = 0;

  struct TestData {
    eigenmath::Vector3d axis_angle;
    eigenmath::Vector3d dot_axis_angle;
    eigenmath::Vector3d ddot_axis_angle;
  };

  std::vector<TestData> test_data;

  // add random poses
  for (int loop = 0; loop < 1000; loop++) {
    TestData data;
    data.axis_angle = eigenmath::Vector3d::Random() * 0.99 / std::sqrt(3);
    data.dot_axis_angle = eigenmath::Vector3d::Random() * 10.0;
    data.ddot_axis_angle = eigenmath::Vector3d::Random() * 10.0;
    test_data.push_back(data);
  }
  // add specific poses for nearly singular cases
  for (int loop = 0; loop < 100; loop++) {
    TestData data;
    data.axis_angle = eigenmath::Vector3d::Random() * 1e-3;
    data.dot_axis_angle = eigenmath::Vector3d::Random() * 10.0;
    data.ddot_axis_angle = eigenmath::Vector3d::Random() * 10.0;
    test_data.push_back(data);
  }
  TestData data;
  data.axis_angle.setZero();
  data.dot_axis_angle.setZero();
  data.ddot_axis_angle.setZero();
  test_data.push_back(data);

  for (auto& data : test_data) {
    eigenmath::Vector3d omega;
    eigenmath::Vector3d dot_omega;

    AxisAngleKinematics k1;

    k1.ToAngularVelocityAcceleration(data.axis_angle, data.dot_axis_angle,
                                     data.ddot_axis_angle, &omega, &dot_omega);

    eigenmath::Vector3d dot_axis_angle_back;
    eigenmath::Vector3d ddot_axis_angle_back;
    AxisAngleKinematics k2;
    k2.ToAxisAngleDerivatives(data.axis_angle, omega, dot_omega,
                              &dot_axis_angle_back, &ddot_axis_angle_back);

    double err_vel = (data.dot_axis_angle - dot_axis_angle_back).norm();
    double err_acc = (data.ddot_axis_angle - ddot_axis_angle_back).norm();
    max_err_vel = err_vel > max_err_vel ? err_vel : max_err_vel;
    max_err_acc = err_acc > max_err_acc ? err_acc : max_err_acc;
    EXPECT_LE(err_vel, 1e-10);
    EXPECT_LE(err_acc, 1e-10);
  }
}
}  // namespace control
}  // namespace intrinsic
