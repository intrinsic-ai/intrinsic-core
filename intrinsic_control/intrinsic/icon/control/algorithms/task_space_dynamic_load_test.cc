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

#include "intrinsic/icon/control/algorithms/task_space_dynamic_load.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {
namespace {

using eigenmath::testing::IsApprox;
using ::testing::Not;

constexpr double kSmallTolerance = 1e-5;

TEST(ComputeDynamicLoad, DoesNotMalloc) {
  const double m_kg = 1.;
  eigenmath::Vector3d some_offset(.1, .2, .3);
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  ComputeDynamicLoad(m_kg, some_offset, Twist::ZERO, Acceleration::ZERO);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST(ComputeDynamicLoad, UnitPointmassWithoutOffset) {
  const double m_kg = 1.;
  eigenmath::Vector3d zero_offset = eigenmath::Vector3d::Zero();

  // Dynamic load is zero if twist and acceleration are zero.
  EXPECT_THAT(
      ComputeDynamicLoad(m_kg, zero_offset, Twist::ZERO, Acceleration::ZERO),
      IsApprox(Wrench::ZERO, kSmallTolerance));

  // Torque is zero, even if acceleration and twist are non-zero, since all mass
  // located at origin of frame.
  Twist some_twist;
  some_twist << 0, 1, 2, 3, 4, 5;
  Acceleration some_acceleration;
  some_acceleration << 1, 2, 3, 4, 5, 6;
  EXPECT_THAT(
      ComputeDynamicLoad(m_kg, zero_offset, some_twist, some_acceleration)
          .tail<3>(),
      IsApprox(eigenmath::Vector3d::Zero(), kSmallTolerance));
}

TEST(ComputeDynamicLoad, AcceleratingAUnitPointmassWithUnitOffset) {
  const double m_kg = 1.;
  eigenmath::Vector3d unit_z_offset = eigenmath::Vector3d::UnitZ();

  {
    // Dynamic load is zero if twist and acceleration are zero.
    EXPECT_THAT(ComputeDynamicLoad(m_kg, unit_z_offset, Twist::ZERO,
                                   Acceleration::ZERO),
                IsApprox(Wrench::ZERO, kSmallTolerance));
  }

  {
    // Torque is not zero, if acceleration and twist are non-zero.
    Twist some_twist;
    some_twist << 0, 1, 2, 3, 4, 5;
    Acceleration some_acceleration;
    some_acceleration << 1, 2, 3, 4, 5, 6;
    EXPECT_THAT(
        ComputeDynamicLoad(m_kg, unit_z_offset, some_twist, some_acceleration)
            .tail<3>(),
        Not(IsApprox(eigenmath::Vector3d::Zero(), kSmallTolerance)));
  }

  {
    // Accelerating in positive translational z-direction only.
    Acceleration acceleration = Acceleration::ZERO;
    acceleration.z() = 1.0;
    Wrench expected_wrench;
    expected_wrench << 0, 0, 1, 0, 0, 0;
    EXPECT_THAT(
        ComputeDynamicLoad(m_kg, unit_z_offset, Twist::ZERO, acceleration),
        IsApprox(expected_wrench, kSmallTolerance));
  }

  {
    // Accelerating in positive translational x-direction only will require
    // positive force in x-direction and positive torque around the y axis.
    Acceleration acceleration = Acceleration::ZERO;
    acceleration.x() = 1.0;
    Wrench expected_wrench;
    expected_wrench << 1, 0, 0, 0, 1, 0;
    EXPECT_THAT(
        ComputeDynamicLoad(m_kg, unit_z_offset, Twist::ZERO, acceleration),
        IsApprox(expected_wrench, kSmallTolerance));
  }

  {
    // Accelerating in positive translational y-direction only will require
    // positive force in y-direction and negative torque around the x axis.
    Acceleration acceleration = Acceleration::ZERO;
    acceleration.y() = 1.0;
    Wrench expected_wrench;
    expected_wrench << 0, 1, 0, -1, 0, 0;
    EXPECT_THAT(
        ComputeDynamicLoad(m_kg, unit_z_offset, Twist::ZERO, acceleration),
        IsApprox(expected_wrench, kSmallTolerance));
  }

  {
    // Angular acceleration about the z-axis does not lead to any reaction.
    Acceleration acceleration = Acceleration::ZERO;
    acceleration.RZ() = 1.0;
    EXPECT_THAT(
        ComputeDynamicLoad(m_kg, unit_z_offset, Twist::ZERO, acceleration),
        IsApprox(Wrench::ZERO, kSmallTolerance));
  }

  {
    // Angular acceleration about the x-axis.
    Acceleration acceleration = Acceleration::ZERO;
    acceleration.RX() = 1.0;
    Wrench expected_wrench;
    expected_wrench << 0, -1., 0, 1, 0, 0;
    EXPECT_THAT(
        ComputeDynamicLoad(m_kg, unit_z_offset, Twist::ZERO, acceleration),
        IsApprox(expected_wrench, kSmallTolerance));
  }

  {
    // Angular acceleration about the y-axis.
    Acceleration acceleration = Acceleration::ZERO;
    acceleration.RY() = 1.0;
    Wrench expected_wrench;
    expected_wrench << 1, 0, 0, 0, 1, 0;
    EXPECT_THAT(
        ComputeDynamicLoad(m_kg, unit_z_offset, Twist::ZERO, acceleration),
        IsApprox(expected_wrench, kSmallTolerance));
  }
}

TEST(ComputeDynamicLoad, RotatingAUnitPointmassWithUnitOffset) {
  const double m_kg = 1.;
  eigenmath::Vector3d unit_z_offset = eigenmath::Vector3d::UnitZ();
  {
    // Angular rotation velocity about the z-axis leads to no reaction force
    // (since it is a point-mass).
    Twist twist = Twist::ZERO;
    twist.RZ() = 1.0;
    EXPECT_THAT(
        ComputeDynamicLoad(m_kg, unit_z_offset, twist, Acceleration::ZERO),
        IsApprox(Wrench::ZERO, kSmallTolerance));
  }

  {
    // Angular rotation velocity about the x-axis.
    Twist twist = Twist::ZERO;
    twist.RX() = 1.0;
    Wrench expected_wrench;
    expected_wrench << 0, 0, -1, 0, 0, 0;
    EXPECT_THAT(
        ComputeDynamicLoad(m_kg, unit_z_offset, twist, Acceleration::ZERO),
        IsApprox(expected_wrench, kSmallTolerance));
  }

  {
    // Angular rotation velocity about the y-axis.
    Twist twist = Twist::ZERO;
    twist.RY() = 1.0;
    Wrench expected_wrench;
    expected_wrench << 0, 0, -1, 0, 0, 0;
    EXPECT_THAT(
        ComputeDynamicLoad(m_kg, unit_z_offset, twist, Acceleration::ZERO),
        IsApprox(expected_wrench, kSmallTolerance));
  }
}

}  // namespace
}  // namespace intrinsic::icon
