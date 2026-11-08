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


#include <gtest/gtest.h>

#include "flatbuffers/buffer.h"
#include "flatbuffers/detached_buffer.h"
#include "flatbuffers/vector.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/flatbuffers/transform_copy.h"
#include "intrinsic/icon/flatbuffers/transform_types.fbs.h"
#include "intrinsic/icon/flatbuffers/transform_types.h"
#include "intrinsic/icon/flatbuffers/transform_view.h"

namespace intrinsic_fbs {
namespace {

using ::testing::Test;

// Tests Point viewed as Eigen
TEST(TransformsTest, ChecksPointToEigen) {
  intrinsic_fbs::Point point(1, 2, 3);

  auto vec3 =
      intrinsic_fbs::ViewAs<Eigen::Vector3d>(&point);  // NOLINT eigen-auto

  EXPECT_EQ(vec3.x(), 1);
  EXPECT_EQ(vec3.y(), 2);
  EXPECT_EQ(vec3.z(), 3);

  vec3 << 7, 8, 9;

  EXPECT_EQ(point.x(), 7);
  EXPECT_EQ(point.y(), 8);
  EXPECT_EQ(point.z(), 9);
}

// Test Eigen viewed as Point
TEST(TransformsTest, ChecksEigenViewAsPoint) {
  Eigen::Vector3d vec3(1, 2, 3);

  auto point = intrinsic_fbs::ViewAs<intrinsic_fbs::Point>(&vec3);

  EXPECT_EQ(point->x(), 1);
  EXPECT_EQ(point->y(), 2);
  EXPECT_EQ(point->z(), 3);

  vec3 << 7, 8, 9;

  EXPECT_EQ(point->x(), 7);
  EXPECT_EQ(point->y(), 8);
  EXPECT_EQ(point->z(), 9);

  point->mutate_x(1);
  point->mutate_y(2);
  point->mutate_z(3);

  EXPECT_EQ(vec3.x(), 1);
  EXPECT_EQ(vec3.y(), 2);
  EXPECT_EQ(vec3.z(), 3);
}

// Tests Eigen copy to Point
TEST(TransformsTest, ChecksEigenCopyToPoint) {
  Eigen::Vector3d vec3(1, 2, 3);
  intrinsic_fbs::Point point;

  CopyTo(vec3, &point);

  EXPECT_EQ(point.x(), 1);
  EXPECT_EQ(point.y(), 2);
  EXPECT_EQ(point.z(), 3);

  vec3.x() = 7;
  vec3.y() = 8;
  vec3.z() = 9;

  EXPECT_EQ(point.x(), 1);
  EXPECT_EQ(point.y(), 2);
  EXPECT_EQ(point.z(), 3);
}

// Tests Rotation viewed as Eigen
TEST(TransformsTest, ChecksRotationToEigen) {
  intrinsic_fbs::Rotation rotation(1, 2, 3, 4);  // initialized as x,y,z,w

  auto quat = intrinsic_fbs::ViewAs<Eigen::Quaterniond>(&rotation);  // NOLINT

  EXPECT_EQ(quat.x(), 1);
  EXPECT_EQ(quat.y(), 2);
  EXPECT_EQ(quat.z(), 3);
  EXPECT_EQ(quat.w(), 4);

  quat.w() = 6;
  quat.x() = 7;
  quat.y() = 8;
  quat.z() = 9;

  EXPECT_EQ(rotation.qw(), 6);
  EXPECT_EQ(rotation.qx(), 7);
  EXPECT_EQ(rotation.qy(), 8);
  EXPECT_EQ(rotation.qz(), 9);
}
// Test Eigen viewed as Quaternion
TEST(TransformsTest, ChecksEigenViewAsRotation) {
  Eigen::Quaterniond quat(1, 2, 3, 4);  // initialized as w,x,y,z

  auto rotation = intrinsic_fbs::ViewAs<intrinsic_fbs::Rotation>(&quat);

  EXPECT_EQ(rotation->qw(), 1);
  EXPECT_EQ(rotation->qx(), 2);
  EXPECT_EQ(rotation->qy(), 3);
  EXPECT_EQ(rotation->qz(), 4);

  quat.w() = 6;
  quat.x() = 7;
  quat.y() = 8;
  quat.z() = 9;

  EXPECT_EQ(rotation->qw(), 6);
  EXPECT_EQ(rotation->qx(), 7);
  EXPECT_EQ(rotation->qy(), 8);
  EXPECT_EQ(rotation->qz(), 9);

  rotation->mutate_qw(1);
  rotation->mutate_qx(2);
  rotation->mutate_qy(3);
  rotation->mutate_qz(4);

  EXPECT_EQ(quat.w(), 1);
  EXPECT_EQ(quat.x(), 2);
  EXPECT_EQ(quat.y(), 3);
  EXPECT_EQ(quat.z(), 4);
}

// Tests Eigen copy to Quaternion
TEST(TransformsTest, ChecksEigenCopyToQuaternion) {
  Eigen::Quaterniond quat(1, 2, 3, 4);  // initialized as w,x,y,z
  intrinsic_fbs::Rotation rotation;

  CopyTo(quat, &rotation);

  EXPECT_EQ(rotation.qw(), 1);
  EXPECT_EQ(rotation.qx(), 2);
  EXPECT_EQ(rotation.qy(), 3);
  EXPECT_EQ(rotation.qz(), 4);

  quat.w() = 6;
  quat.x() = 7;
  quat.y() = 8;
  quat.z() = 9;

  EXPECT_EQ(rotation.qw(), 1);
  EXPECT_EQ(rotation.qx(), 2);
  EXPECT_EQ(rotation.qy(), 3);
  EXPECT_EQ(rotation.qz(), 4);
}

// Tests Twist viewed as Eigen
TEST(TransformsTest, ChecksTwistToEigen) {
  intrinsic_fbs::Twist twist(1, 2, 3, 11, 22, 33);

  auto vec6 =
      intrinsic_fbs::ViewAs<intrinsic::eigenmath::Vector6d>(&twist);  // NOLINT

  EXPECT_EQ(vec6.data()[0], 1);
  EXPECT_EQ(vec6.data()[1], 2);
  EXPECT_EQ(vec6.data()[2], 3);
  EXPECT_EQ(vec6.data()[3], 11);
  EXPECT_EQ(vec6.data()[4], 22);
  EXPECT_EQ(vec6.data()[5], 33);

  vec6 << 7, 8, 9, 77, 88, 99;

  EXPECT_EQ(twist.x(), 7);
  EXPECT_EQ(twist.y(), 8);
  EXPECT_EQ(twist.z(), 9);
  EXPECT_EQ(twist.rx(), 77);
  EXPECT_EQ(twist.ry(), 88);
  EXPECT_EQ(twist.rz(), 99);
}

// Tests Twist viewed as Eigen
TEST(TransformsTest, ChecksConstTwistToEigen) {
  const intrinsic_fbs::Twist twist(1, 2, 3, 11, 22, 33);

  auto& vec6 = intrinsic_fbs::ViewAs<intrinsic::eigenmath::Vector6d>(twist);

  EXPECT_EQ(vec6.data()[0], 1);
  EXPECT_EQ(vec6.data()[1], 2);
  EXPECT_EQ(vec6.data()[2], 3);
  EXPECT_EQ(vec6.data()[3], 11);
  EXPECT_EQ(vec6.data()[4], 22);
  EXPECT_EQ(vec6.data()[5], 33);
}

// Test Eigen viewed as Twist
TEST(TransformsTest, ChecksEigenViewAsTwist) {
  intrinsic::eigenmath::Vector6d vec6;
  vec6 << 1, 2, 3, 11, 22, 33;

  auto twist = intrinsic_fbs::ViewAs<intrinsic_fbs::Twist>(&vec6);

  EXPECT_EQ(twist->x(), 1);
  EXPECT_EQ(twist->y(), 2);
  EXPECT_EQ(twist->z(), 3);
  EXPECT_EQ(twist->rx(), 11);
  EXPECT_EQ(twist->ry(), 22);
  EXPECT_EQ(twist->rz(), 33);

  vec6 << 7, 8, 9, 77, 88, 99;

  EXPECT_EQ(twist->x(), 7);
  EXPECT_EQ(twist->y(), 8);
  EXPECT_EQ(twist->z(), 9);
  EXPECT_EQ(twist->rx(), 77);
  EXPECT_EQ(twist->ry(), 88);
  EXPECT_EQ(twist->rz(), 99);

  twist->mutate_x(1);
  twist->mutate_y(2);
  twist->mutate_z(3);
  twist->mutate_rx(11);
  twist->mutate_ry(22);
  twist->mutate_rz(33);

  EXPECT_EQ(vec6.data()[0], 1);
  EXPECT_EQ(vec6.data()[1], 2);
  EXPECT_EQ(vec6.data()[2], 3);
  EXPECT_EQ(vec6.data()[3], 11);
  EXPECT_EQ(vec6.data()[4], 22);
  EXPECT_EQ(vec6.data()[5], 33);
}

// Tests Eigen copy to Twist
TEST(TransformsTest, ChecksEigenCopyToTwist) {
  intrinsic::eigenmath::Vector6d vec6;
  vec6 << 1, 2, 3, 11, 22, 33;
  intrinsic_fbs::Twist twist;

  CopyTo(vec6, &twist);

  EXPECT_EQ(twist.x(), 1);
  EXPECT_EQ(twist.y(), 2);
  EXPECT_EQ(twist.z(), 3);
  EXPECT_EQ(twist.rx(), 11);
  EXPECT_EQ(twist.ry(), 22);
  EXPECT_EQ(twist.rz(), 33);

  vec6 << 7, 8, 9, 77, 88, 99;

  EXPECT_EQ(twist.x(), 1);
  EXPECT_EQ(twist.y(), 2);
  EXPECT_EQ(twist.z(), 3);
  EXPECT_EQ(twist.rx(), 11);
  EXPECT_EQ(twist.ry(), 22);
  EXPECT_EQ(twist.rz(), 33);
}

// Tests Acceleration viewed as Eigen
TEST(TransformsTest, ChecksAccelerationToEigen) {
  intrinsic_fbs::Acceleration acceleration(1, 2, 3, 11, 22, 33);

  auto vec6 =  // NOLINT
      intrinsic_fbs::ViewAs<intrinsic::eigenmath::Vector6d>(&acceleration);

  EXPECT_EQ(vec6.data()[0], 1);
  EXPECT_EQ(vec6.data()[1], 2);
  EXPECT_EQ(vec6.data()[2], 3);
  EXPECT_EQ(vec6.data()[3], 11);
  EXPECT_EQ(vec6.data()[4], 22);
  EXPECT_EQ(vec6.data()[5], 33);

  vec6 << 7, 8, 9, 77, 88, 99;

  EXPECT_EQ(acceleration.x(), 7);
  EXPECT_EQ(acceleration.y(), 8);
  EXPECT_EQ(acceleration.z(), 9);
  EXPECT_EQ(acceleration.rx(), 77);
  EXPECT_EQ(acceleration.ry(), 88);
  EXPECT_EQ(acceleration.rz(), 99);
}

// Tests Acceleration viewed as Eigen
TEST(TransformsTest, ChecksConstAccelerationToEigen) {
  const intrinsic_fbs::Acceleration acceleration(1, 2, 3, 11, 22, 33);

  auto& vec6 =
      intrinsic_fbs::ViewAs<intrinsic::eigenmath::Vector6d>(acceleration);

  EXPECT_EQ(vec6.data()[0], 1);
  EXPECT_EQ(vec6.data()[1], 2);
  EXPECT_EQ(vec6.data()[2], 3);
  EXPECT_EQ(vec6.data()[3], 11);
  EXPECT_EQ(vec6.data()[4], 22);
  EXPECT_EQ(vec6.data()[5], 33);
}

// Test Eigen viewed as Acceleration
TEST(TransformsTest, ChecksEigenViewAsAcceleration) {
  intrinsic::eigenmath::Vector6d vec6;
  vec6 << 1, 2, 3, 11, 22, 33;

  auto acceleration = intrinsic_fbs::ViewAs<intrinsic_fbs::Acceleration>(&vec6);

  EXPECT_EQ(acceleration->x(), 1);
  EXPECT_EQ(acceleration->y(), 2);
  EXPECT_EQ(acceleration->z(), 3);
  EXPECT_EQ(acceleration->rx(), 11);
  EXPECT_EQ(acceleration->ry(), 22);
  EXPECT_EQ(acceleration->rz(), 33);

  vec6 << 7, 8, 9, 77, 88, 99;

  EXPECT_EQ(acceleration->x(), 7);
  EXPECT_EQ(acceleration->y(), 8);
  EXPECT_EQ(acceleration->z(), 9);
  EXPECT_EQ(acceleration->rx(), 77);
  EXPECT_EQ(acceleration->ry(), 88);
  EXPECT_EQ(acceleration->rz(), 99);

  acceleration->mutate_x(1);
  acceleration->mutate_y(2);
  acceleration->mutate_z(3);
  acceleration->mutate_rx(11);
  acceleration->mutate_ry(22);
  acceleration->mutate_rz(33);

  EXPECT_EQ(vec6.data()[0], 1);
  EXPECT_EQ(vec6.data()[1], 2);
  EXPECT_EQ(vec6.data()[2], 3);
  EXPECT_EQ(vec6.data()[3], 11);
  EXPECT_EQ(vec6.data()[4], 22);
  EXPECT_EQ(vec6.data()[5], 33);
}

// Tests Eigen copy to Acceleration
TEST(TransformsTest, ChecksEigenCopyToAcceleration) {
  intrinsic::eigenmath::Vector6d vec6;
  vec6 << 1, 2, 3, 11, 22, 33;
  intrinsic_fbs::Acceleration acceleration;

  CopyTo(vec6, &acceleration);

  EXPECT_EQ(acceleration.x(), 1);
  EXPECT_EQ(acceleration.y(), 2);
  EXPECT_EQ(acceleration.z(), 3);
  EXPECT_EQ(acceleration.rx(), 11);
  EXPECT_EQ(acceleration.ry(), 22);
  EXPECT_EQ(acceleration.rz(), 33);

  vec6 << 7, 8, 9, 77, 88, 99;

  EXPECT_EQ(acceleration.x(), 1);
  EXPECT_EQ(acceleration.y(), 2);
  EXPECT_EQ(acceleration.z(), 3);
  EXPECT_EQ(acceleration.rx(), 11);
  EXPECT_EQ(acceleration.ry(), 22);
  EXPECT_EQ(acceleration.rz(), 33);
}

// Tests Jerk viewed as Eigen
TEST(TransformsTest, ChecksJerkToEigen) {
  intrinsic_fbs::Jerk jerk(1, 2, 3, 11, 22, 33);

  auto vec6 =
      intrinsic_fbs::ViewAs<intrinsic::eigenmath::Vector6d>(&jerk);  // NOLINT

  EXPECT_EQ(vec6.data()[0], 1);
  EXPECT_EQ(vec6.data()[1], 2);
  EXPECT_EQ(vec6.data()[2], 3);
  EXPECT_EQ(vec6.data()[3], 11);
  EXPECT_EQ(vec6.data()[4], 22);
  EXPECT_EQ(vec6.data()[5], 33);

  vec6 << 7, 8, 9, 77, 88, 99;

  EXPECT_EQ(jerk.x(), 7);
  EXPECT_EQ(jerk.y(), 8);
  EXPECT_EQ(jerk.z(), 9);
  EXPECT_EQ(jerk.rx(), 77);
  EXPECT_EQ(jerk.ry(), 88);
  EXPECT_EQ(jerk.rz(), 99);
}

// Test Eigen viewed as Jerk
TEST(TransformsTest, ChecksEigenViewAsJerk) {
  intrinsic::eigenmath::Vector6d vec6;
  vec6 << 1, 2, 3, 11, 22, 33;

  auto jerk = intrinsic_fbs::ViewAs<intrinsic_fbs::Jerk>(&vec6);

  EXPECT_EQ(jerk->x(), 1);
  EXPECT_EQ(jerk->y(), 2);
  EXPECT_EQ(jerk->z(), 3);
  EXPECT_EQ(jerk->rx(), 11);
  EXPECT_EQ(jerk->ry(), 22);
  EXPECT_EQ(jerk->rz(), 33);

  vec6 << 7, 8, 9, 77, 88, 99;

  EXPECT_EQ(jerk->x(), 7);
  EXPECT_EQ(jerk->y(), 8);
  EXPECT_EQ(jerk->z(), 9);
  EXPECT_EQ(jerk->rx(), 77);
  EXPECT_EQ(jerk->ry(), 88);
  EXPECT_EQ(jerk->rz(), 99);

  jerk->mutate_x(1);
  jerk->mutate_y(2);
  jerk->mutate_z(3);
  jerk->mutate_rx(11);
  jerk->mutate_ry(22);
  jerk->mutate_rz(33);

  EXPECT_EQ(vec6.data()[0], 1);
  EXPECT_EQ(vec6.data()[1], 2);
  EXPECT_EQ(vec6.data()[2], 3);
  EXPECT_EQ(vec6.data()[3], 11);
  EXPECT_EQ(vec6.data()[4], 22);
  EXPECT_EQ(vec6.data()[5], 33);
}

// Tests Eigen copy to Jerk
TEST(TransformsTest, ChecksEigenCopyToJerk) {
  intrinsic::eigenmath::Vector6d vec6;
  vec6 << 1, 2, 3, 11, 22, 33;
  intrinsic_fbs::Jerk jerk;

  CopyTo(vec6, &jerk);

  EXPECT_EQ(jerk.x(), 1);
  EXPECT_EQ(jerk.y(), 2);
  EXPECT_EQ(jerk.z(), 3);
  EXPECT_EQ(jerk.rx(), 11);
  EXPECT_EQ(jerk.ry(), 22);
  EXPECT_EQ(jerk.rz(), 33);

  vec6 << 7, 8, 9, 77, 88, 99;

  EXPECT_EQ(jerk.x(), 1);
  EXPECT_EQ(jerk.y(), 2);
  EXPECT_EQ(jerk.z(), 3);
  EXPECT_EQ(jerk.rx(), 11);
  EXPECT_EQ(jerk.ry(), 22);
  EXPECT_EQ(jerk.rz(), 33);
}

// Tests Wrench viewed as Eigen
TEST(TransformsTest, ChecksWrenchToEigen) {
  intrinsic_fbs::Wrench wrench(1, 2, 3, 11, 22, 33);

  auto vec6 =
      intrinsic_fbs::ViewAs<intrinsic::eigenmath::Vector6d>(&wrench);  // NOLINT

  EXPECT_EQ(vec6.data()[0], 1);
  EXPECT_EQ(vec6.data()[1], 2);
  EXPECT_EQ(vec6.data()[2], 3);
  EXPECT_EQ(vec6.data()[3], 11);
  EXPECT_EQ(vec6.data()[4], 22);
  EXPECT_EQ(vec6.data()[5], 33);

  vec6 << 7, 8, 9, 77, 88, 99;

  EXPECT_EQ(wrench.x(), 7);
  EXPECT_EQ(wrench.y(), 8);
  EXPECT_EQ(wrench.z(), 9);
  EXPECT_EQ(wrench.rx(), 77);
  EXPECT_EQ(wrench.ry(), 88);
  EXPECT_EQ(wrench.rz(), 99);
}

// Test Eigen viewed as Wrench
TEST(TransformsTest, ChecksEigenViewAsWrench) {
  intrinsic::eigenmath::Vector6d vec6;
  vec6 << 1, 2, 3, 11, 22, 33;

  auto wrench = intrinsic_fbs::ViewAs<intrinsic_fbs::Wrench>(&vec6);

  EXPECT_EQ(wrench->x(), 1);
  EXPECT_EQ(wrench->y(), 2);
  EXPECT_EQ(wrench->z(), 3);
  EXPECT_EQ(wrench->rx(), 11);
  EXPECT_EQ(wrench->ry(), 22);
  EXPECT_EQ(wrench->rz(), 33);

  vec6 << 7, 8, 9, 77, 88, 99;

  EXPECT_EQ(wrench->x(), 7);
  EXPECT_EQ(wrench->y(), 8);
  EXPECT_EQ(wrench->z(), 9);
  EXPECT_EQ(wrench->rx(), 77);
  EXPECT_EQ(wrench->ry(), 88);
  EXPECT_EQ(wrench->rz(), 99);

  wrench->mutate_x(1);
  wrench->mutate_y(2);
  wrench->mutate_z(3);
  wrench->mutate_rx(11);
  wrench->mutate_ry(22);
  wrench->mutate_rz(33);

  EXPECT_EQ(vec6.data()[0], 1);
  EXPECT_EQ(vec6.data()[1], 2);
  EXPECT_EQ(vec6.data()[2], 3);
  EXPECT_EQ(vec6.data()[3], 11);
  EXPECT_EQ(vec6.data()[4], 22);
  EXPECT_EQ(vec6.data()[5], 33);
}

// Tests Eigen copy to Wrench
TEST(TransformsTest, ChecksEigenCopyToWrench) {
  intrinsic::eigenmath::Vector6d vec6;
  vec6 << 1, 2, 3, 11, 22, 33;
  intrinsic_fbs::Wrench wrench;

  CopyTo(vec6, &wrench);

  EXPECT_EQ(wrench.x(), 1);
  EXPECT_EQ(wrench.y(), 2);
  EXPECT_EQ(wrench.z(), 3);
  EXPECT_EQ(wrench.rx(), 11);
  EXPECT_EQ(wrench.ry(), 22);
  EXPECT_EQ(wrench.rz(), 33);

  vec6 << 7, 8, 9, 77, 88, 99;

  EXPECT_EQ(wrench.x(), 1);
  EXPECT_EQ(wrench.y(), 2);
  EXPECT_EQ(wrench.z(), 3);
  EXPECT_EQ(wrench.rx(), 11);
  EXPECT_EQ(wrench.ry(), 22);
  EXPECT_EQ(wrench.rz(), 33);
}

// Tests VectorNd viewed as Eigen
TEST(TransformsTest, ChecksVectorNdToEigen) {
  flatbuffers::DetachedBuffer buffer = CreateVectorNdBuffer({1, 2, 3});

  intrinsic_fbs::VectorNd* vector_n =
      flatbuffers::GetMutableRoot<intrinsic_fbs::VectorNd>(buffer.data());

  auto vector_x = intrinsic_fbs::ViewAs<Eigen::VectorXd>(vector_n);  // NOLINT

  EXPECT_EQ(vector_x.data()[0], 1);
  EXPECT_EQ(vector_x.data()[1], 2);
  EXPECT_EQ(vector_x.data()[2], 3);

  vector_x << 7, 8, 9;

  EXPECT_EQ(vector_n->data()->Get(0), 7);
  EXPECT_EQ(vector_n->data()->Get(1), 8);
  EXPECT_EQ(vector_n->data()->Get(2), 9);
}

// Test Eigen viewed as DoubleArray
TEST(TransformsTest, ChecksEigenViewAsDoubleArray) {
  Eigen::VectorXd vector_x(4);
  vector_x << 1, 2, 3, 4;

  auto vector = intrinsic_fbs::ViewAs<double>(&vector_x);

  EXPECT_EQ(vector[0], 1);
  EXPECT_EQ(vector[1], 2);
  EXPECT_EQ(vector[2], 3);
  EXPECT_EQ(vector[3], 4);

  vector_x << 6, 7, 8, 9;

  EXPECT_EQ(vector[0], 6);
  EXPECT_EQ(vector[1], 7);
  EXPECT_EQ(vector[2], 8);
  EXPECT_EQ(vector[3], 9);
}

// Test Buffer viewed as Eigen
TEST(TransformsTest, ChecksBufferViewAsEigen) {
  flatbuffers::DetachedBuffer buffer = CreateVectorNdBuffer({1, 2, 3, 4});

  intrinsic_fbs::VectorNd* vector_n =
      flatbuffers::GetMutableRoot<intrinsic_fbs::VectorNd>(buffer.data());

  auto vector_x = intrinsic_fbs::ViewAs<Eigen::VectorXd>(&buffer);  // NOLINT

  EXPECT_EQ(vector_x.data()[0], vector_n->data()->Get(0));
  EXPECT_EQ(vector_x.data()[1], vector_n->data()->Get(1));
  EXPECT_EQ(vector_x.data()[2], vector_n->data()->Get(2));
  EXPECT_EQ(vector_x.data()[3], vector_n->data()->Get(3));

  vector_x << 6, 7, 8, 9;

  EXPECT_EQ(vector_n->data()->Get(0), 6);
  EXPECT_EQ(vector_n->data()->Get(1), 7);
  EXPECT_EQ(vector_n->data()->Get(2), 8);
  EXPECT_EQ(vector_n->data()->Get(3), 9);
}

// Tests Eigen copy to VectorNd
TEST(TransformsTest, ChecksEigenCopyToVectorNd) {
  Eigen::VectorXd vector_x(4);
  vector_x << 1, 2, 3, 4;

  flatbuffers::DetachedBuffer buffer = CreateVectorNdBuffer(4);
  intrinsic_fbs::VectorNd* vector_n =
      flatbuffers::GetMutableRoot<intrinsic_fbs::VectorNd>(buffer.data());

  CopyTo(vector_x, vector_n);

  EXPECT_EQ(vector_n->data()->Get(0), 1);
  EXPECT_EQ(vector_n->data()->Get(1), 2);
  EXPECT_EQ(vector_n->data()->Get(2), 3);
  EXPECT_EQ(vector_n->data()->Get(3), 4);

  vector_x << 6, 7, 8, 9;

  EXPECT_EQ(vector_n->data()->Get(0), 1);
  EXPECT_EQ(vector_n->data()->Get(1), 2);
  EXPECT_EQ(vector_n->data()->Get(2), 3);
  EXPECT_EQ(vector_n->data()->Get(3), 4);
}

// Tests VectorNd viewed as Eigen
TEST(TransformsTest, ChecksVectorNdToEigenVectorNd) {
  flatbuffers::DetachedBuffer buffer = CreateVectorNdBuffer({1, 2, 3});

  intrinsic_fbs::VectorNd* vector_n =
      flatbuffers::GetMutableRoot<intrinsic_fbs::VectorNd>(buffer.data());

  auto vector_x =  // NOLINT
      intrinsic_fbs::ViewAs<intrinsic::eigenmath::VectorNd>(vector_n);

  EXPECT_EQ(vector_x.data()[0], 1);
  EXPECT_EQ(vector_x.data()[1], 2);
  EXPECT_EQ(vector_x.data()[2], 3);

  vector_x << 7, 8, 9;

  EXPECT_EQ(vector_n->data()->Get(0), 7);
  EXPECT_EQ(vector_n->data()->Get(1), 8);
  EXPECT_EQ(vector_n->data()->Get(2), 9);
}

// Test Buffer viewed as Eigen
TEST(TransformsTest, ChecksBufferViewAsEigenVectorNd) {
  flatbuffers::DetachedBuffer buffer = CreateVectorNdBuffer({1, 2, 3, 4});

  intrinsic_fbs::VectorNd* vector_n =
      flatbuffers::GetMutableRoot<intrinsic_fbs::VectorNd>(buffer.data());

  auto vector_x =
      intrinsic_fbs::ViewAs<intrinsic::eigenmath::VectorNd>(&buffer);  // NOLINT
  EXPECT_EQ(vector_x[0], vector_n->data()->Get(0));
  EXPECT_EQ(vector_x.data()[1], vector_n->data()->Get(1));
  EXPECT_EQ(vector_x.data()[2], vector_n->data()->Get(2));
  EXPECT_EQ(vector_x.data()[3], vector_n->data()->Get(3));

  vector_x << 6, 7, 8, 9;

  EXPECT_EQ(vector_n->data()->Get(0), 6);
  EXPECT_EQ(vector_n->data()->Get(1), 7);
  EXPECT_EQ(vector_n->data()->Get(2), 8);
  EXPECT_EQ(vector_n->data()->Get(3), 9);
}

// Tests Eigen copy to VectorNd
TEST(TransformsTest, ChecksEigenVectorNdCopyToVectorNd) {
  intrinsic::eigenmath::VectorNd vector_x(4);
  vector_x << 1, 2, 3, 4;

  flatbuffers::DetachedBuffer buffer = CreateVectorNdBuffer(4);
  intrinsic_fbs::VectorNd* vector_n =
      flatbuffers::GetMutableRoot<intrinsic_fbs::VectorNd>(buffer.data());

  CopyTo(vector_x, vector_n);

  EXPECT_EQ(vector_n->data()->Get(0), 1);
  EXPECT_EQ(vector_n->data()->Get(1), 2);
  EXPECT_EQ(vector_n->data()->Get(2), 3);
  EXPECT_EQ(vector_n->data()->Get(3), 4);

  vector_x << 6, 7, 8, 9;

  EXPECT_EQ(vector_n->data()->Get(0), 1);
  EXPECT_EQ(vector_n->data()->Get(1), 2);
  EXPECT_EQ(vector_n->data()->Get(2), 3);
  EXPECT_EQ(vector_n->data()->Get(3), 4);
}

// Test Vector viewed as Eigen
TEST(TransformsTest, ChecksVectorViewAsEigenVectorNd) {
  flatbuffers::DetachedBuffer buffer = CreateVectorNdBuffer({1, 22, 333});

  ::flatbuffers::Vector<double>* vector_n =
      flatbuffers::GetMutableRoot<intrinsic_fbs::VectorNd>(buffer.data())
          ->mutable_data();

  Eigen::Map<intrinsic::eigenmath::VectorNd> vector_x =
      intrinsic_fbs::ViewAs<intrinsic::eigenmath::VectorNd>(vector_n);

  EXPECT_EQ(vector_x[0], 1);
  EXPECT_EQ(vector_x[1], 22);
  EXPECT_EQ(vector_x[2], 333);

  vector_x << 6, 77, 888;

  EXPECT_EQ(vector_n->Get(0), 6);
  EXPECT_EQ(vector_n->Get(1), 77);
  EXPECT_EQ(vector_n->Get(2), 888);
}

TEST(TransformsTest, ChecksEmptyVectorViewAsEigenVectorNd) {
  flatbuffers::DetachedBuffer buffer = CreateVectorNdBuffer({});

  ::flatbuffers::Vector<double>* vector_n =
      flatbuffers::GetMutableRoot<intrinsic_fbs::VectorNd>(buffer.data())
          ->mutable_data();

  Eigen::Map<intrinsic::eigenmath::VectorNd> vector_x =
      intrinsic_fbs::ViewAs<intrinsic::eigenmath::VectorNd>(vector_n);

  EXPECT_EQ(vector_x.size(), 0);
}

}  // namespace
}  // namespace intrinsic_fbs
