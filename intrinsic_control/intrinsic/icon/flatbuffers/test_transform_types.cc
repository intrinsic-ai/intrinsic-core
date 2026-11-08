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

#include <vector>

#include "flatbuffers/buffer.h"
#include "flatbuffers/detached_buffer.h"
#include "flatbuffers/flatbuffer_builder.h"
#include "intrinsic/icon/flatbuffers/transform_types.fbs.h"
#include "intrinsic/icon/flatbuffers/transform_types.h"

namespace intrinsic_fbs {
namespace {

using ::testing::Test;

// Tests Point
TEST(TypesTest, ChecksPoint) {
  Point point(1, 2, 3);

  EXPECT_EQ(point.x(), 1);
  EXPECT_EQ(point.y(), 2);
  EXPECT_EQ(point.z(), 3);
}

// Tests Rotation
TEST(TypesTest, ChecksRotation) {
  intrinsic_fbs::Rotation rotation(1, 2, 3, 4);

  EXPECT_EQ(rotation.qx(), 1);
  EXPECT_EQ(rotation.qy(), 2);
  EXPECT_EQ(rotation.qz(), 3);
  EXPECT_EQ(rotation.qw(), 4);
}

// Tests Twist
TEST(TypesTest, ChecksTwist) {
  intrinsic_fbs::Twist twist(1, 2, 3, 11, 22, 33);

  EXPECT_EQ(twist.x(), 1);
  EXPECT_EQ(twist.y(), 2);
  EXPECT_EQ(twist.z(), 3);
  EXPECT_EQ(twist.rx(), 11);
  EXPECT_EQ(twist.ry(), 22);
  EXPECT_EQ(twist.rz(), 33);
}

// Tests Acceleration
TEST(TypesTest, ChecksAcceleration) {
  intrinsic_fbs::Acceleration acceleration(1, 2, 3, 11, 22, 33);

  EXPECT_EQ(acceleration.x(), 1);
  EXPECT_EQ(acceleration.y(), 2);
  EXPECT_EQ(acceleration.z(), 3);
  EXPECT_EQ(acceleration.rx(), 11);
  EXPECT_EQ(acceleration.ry(), 22);
  EXPECT_EQ(acceleration.rz(), 33);
}

// Tests Jerk
TEST(TypesTest, ChecksJerk) {
  intrinsic_fbs::Jerk jerk(1, 2, 3, 11, 22, 33);

  EXPECT_EQ(jerk.x(), 1);
  EXPECT_EQ(jerk.y(), 2);
  EXPECT_EQ(jerk.z(), 3);
  EXPECT_EQ(jerk.rx(), 11);
  EXPECT_EQ(jerk.ry(), 22);
  EXPECT_EQ(jerk.rz(), 33);
}

// Tests Wrench
TEST(TypesTest, ChecksWrench) {
  intrinsic_fbs::Wrench wrench(1, 2, 3, 11, 22, 33);

  EXPECT_EQ(wrench.x(), 1);
  EXPECT_EQ(wrench.y(), 2);
  EXPECT_EQ(wrench.z(), 3);
  EXPECT_EQ(wrench.rx(), 11);
  EXPECT_EQ(wrench.ry(), 22);
  EXPECT_EQ(wrench.rz(), 33);
}

// Tests Transform
TEST(TypesTest, ChecksTransform) {
  intrinsic_fbs::Transform transform({1, 2, 3}, {5, 6, 7, 8});

  EXPECT_EQ(transform.position().x(), 1);
  EXPECT_EQ(transform.position().y(), 2);
  EXPECT_EQ(transform.position().z(), 3);

  EXPECT_EQ(transform.rotation().qx(), 5);
  EXPECT_EQ(transform.rotation().qy(), 6);
  EXPECT_EQ(transform.rotation().qz(), 7);
  EXPECT_EQ(transform.rotation().qw(), 8);
}

// Tests VectorNd
TEST(TypesTest, ChecksVectorNd) {
  std::vector<double> std_vector{1, 2, 3};
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(intrinsic_fbs::CreateVectorNdDirect(builder, &std_vector));

  intrinsic_fbs::VectorNd* vector_fbs =
      flatbuffers::GetMutableRoot<intrinsic_fbs::VectorNd>(
          builder.GetBufferPointer());

  EXPECT_EQ(vector_fbs->data()->Length(), std_vector.size());
  EXPECT_EQ(vector_fbs->data()->Get(0), std_vector[0]);
  EXPECT_EQ(vector_fbs->data()->Get(1), std_vector[1]);
  EXPECT_EQ(vector_fbs->data()->Get(2), std_vector[2]);
}

// Tests Utiltiy CreateVectorNdBuffer(std::vector&)
TEST(TypesTest, ChecksCreateVectorNdBufferFromStdVector) {
  std::vector<double> std_vector{1, 2, 3};

  flatbuffers::DetachedBuffer buffer = CreateVectorNdBuffer(std_vector);

  intrinsic_fbs::VectorNd* vector_fbs =
      flatbuffers::GetMutableRoot<intrinsic_fbs::VectorNd>(buffer.data());

  EXPECT_EQ(vector_fbs->data()->Length(), std_vector.size());
  EXPECT_EQ(vector_fbs->data()->Get(0), std_vector[0]);
  EXPECT_EQ(vector_fbs->data()->Get(1), std_vector[1]);
  EXPECT_EQ(vector_fbs->data()->Get(2), std_vector[2]);
}

// Tests utility CreateVectorNdBuffer(size_t)
TEST(TypesTest, ChecksCreateVectorNdBufferOfAGivenSize) {
  flatbuffers::DetachedBuffer buffer = CreateVectorNdBuffer(4);

  intrinsic_fbs::VectorNd* vector_fbs =
      flatbuffers::GetMutableRoot<intrinsic_fbs::VectorNd>(buffer.data());

  EXPECT_EQ(vector_fbs->data()->Length(), 4);
  EXPECT_EQ(vector_fbs->data()->Get(0), 0);
  EXPECT_EQ(vector_fbs->data()->Get(1), 0);
  EXPECT_EQ(vector_fbs->data()->Get(2), 0);
  EXPECT_EQ(vector_fbs->data()->Get(3), 0);
}

// Tests utility CopyToVectorNdBuffer
TEST(TypesTest, ChecksCopyToVectorNdBuffer) {
  flatbuffers::DetachedBuffer buffer = CreateVectorNdBuffer(3);

  std::vector<double> std_vector{1, 2, 3};
  intrinsic_fbs::VectorNd* vector_fbs =
      CopyToVectorNdBuffer(std_vector, buffer.data(), buffer.size());

  EXPECT_EQ(vector_fbs->data()->Length(), std_vector.size());
  EXPECT_EQ(vector_fbs->data()->Get(0), std_vector[0]);
  EXPECT_EQ(vector_fbs->data()->Get(1), std_vector[1]);
  EXPECT_EQ(vector_fbs->data()->Get(2), std_vector[2]);
}

}  // namespace
}  // namespace intrinsic_fbs
