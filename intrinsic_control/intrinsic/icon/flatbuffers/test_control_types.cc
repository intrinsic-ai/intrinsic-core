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

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/flatbuffers/control_types_copy.h"
#include "intrinsic/icon/flatbuffers/control_types_view.h"
#include "intrinsic/icon/flatbuffers/transform_types.fbs.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"

namespace intrinsic {
namespace {

using ::testing::Test;

// Tests const Wrench
TEST(TypesTest, ChecksConstBlueWrench) {
  const intrinsic_fbs::Wrench wrench(1, 2, 3, 11, 22, 33);

  EXPECT_EQ(wrench.x(), 1);
  EXPECT_EQ(wrench.y(), 2);
  EXPECT_EQ(wrench.z(), 3);
  EXPECT_EQ(wrench.rx(), 11);
  EXPECT_EQ(wrench.ry(), 22);
  EXPECT_EQ(wrench.rz(), 33);

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();

  const Wrench* intrinsic_wrench = intrinsic_fbs::FromSchema(wrench);

  EXPECT_EQ(intrinsic_wrench->x(), 1);
  EXPECT_EQ(intrinsic_wrench->y(), 2);
  EXPECT_EQ(intrinsic_wrench->z(), 3);
  EXPECT_EQ(intrinsic_wrench->RX(), 11);
  EXPECT_EQ(intrinsic_wrench->RY(), 22);
  EXPECT_EQ(intrinsic_wrench->RZ(), 33);

  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_ALLOCATIONS_EQ(0);
}

// Tests Wrench
TEST(TypesTest, ChecksBlueWrench) {
  intrinsic_fbs::Wrench wrench(1, 2, 3, 11, 22, 33);

  EXPECT_EQ(wrench.x(), 1);
  EXPECT_EQ(wrench.y(), 2);
  EXPECT_EQ(wrench.z(), 3);
  EXPECT_EQ(wrench.rx(), 11);
  EXPECT_EQ(wrench.ry(), 22);
  EXPECT_EQ(wrench.rz(), 33);

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();

  Wrench* intrinsic_wrench = intrinsic_fbs::FromSchema(&wrench);

  EXPECT_EQ(intrinsic_wrench->x(), 1);
  EXPECT_EQ(intrinsic_wrench->y(), 2);
  EXPECT_EQ(intrinsic_wrench->z(), 3);
  EXPECT_EQ(intrinsic_wrench->RX(), 11);
  EXPECT_EQ(intrinsic_wrench->RY(), 22);
  EXPECT_EQ(intrinsic_wrench->RZ(), 33);

  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_ALLOCATIONS_EQ(0);

  intrinsic_wrench->x() = 66;
  intrinsic_wrench->y() = 77;
  intrinsic_wrench->z() = 88;
  intrinsic_wrench->RX() = 91;
  intrinsic_wrench->RY() = 92;
  intrinsic_wrench->RZ() = 93;

  EXPECT_EQ(wrench.x(), 66);
  EXPECT_EQ(wrench.y(), 77);
  EXPECT_EQ(wrench.z(), 88);
  EXPECT_EQ(wrench.rx(), 91);
  EXPECT_EQ(wrench.ry(), 92);
  EXPECT_EQ(wrench.rz(), 93);
}

// Tests CopyToTransform
TEST(CopyTest, ChecksCopyToTransform) {
  intrinsic_fbs::Transform transform;

  eigenmath::Vector3d position(1, 2, 3);
  Eigen::Quaterniond rotation(6, 7, 8, 9);

  Pose3d pose(rotation, position);

  intrinsic_fbs::CopyTo(&transform, pose);

  EXPECT_EQ(transform.position().x(), 1);
  EXPECT_EQ(transform.position().y(), 2);
  EXPECT_EQ(transform.position().z(), 3);

  // Normalize rotation since that is what Pose3d does during initialization.
  rotation.normalize();

  EXPECT_EQ(transform.rotation().qx(), rotation.coeffs()[0]);
  EXPECT_EQ(transform.rotation().qy(), rotation.coeffs()[1]);
  EXPECT_EQ(transform.rotation().qz(), rotation.coeffs()[2]);
  EXPECT_EQ(transform.rotation().qw(), rotation.coeffs()[3]);
}

// Tests CopyToBlueEigenmathPose3d
TEST(CopyTest, ChecksCopyToBlueEigenmathPose3d) {
  intrinsic_fbs::Transform transform;

  intrinsic_fbs::Point position(1, 2, 3);
  intrinsic_fbs::Rotation rotation(6, 7, 8, 9);

  transform.mutable_position() = position;
  transform.mutable_rotation() = rotation;

  Pose3d pose;

  intrinsic_fbs::CopyTo(&pose, transform);

  EXPECT_EQ(pose.translation()[0], 1);
  EXPECT_EQ(pose.translation()[1], 2);
  EXPECT_EQ(pose.translation()[2], 3);

  EXPECT_EQ(pose.quaternion().coeffs()[0], rotation.qx());
  EXPECT_EQ(pose.quaternion().coeffs()[1], rotation.qy());
  EXPECT_EQ(pose.quaternion().coeffs()[2], rotation.qz());
  EXPECT_EQ(pose.quaternion().coeffs()[3], rotation.qw());
}

}  // namespace
}  // namespace intrinsic
