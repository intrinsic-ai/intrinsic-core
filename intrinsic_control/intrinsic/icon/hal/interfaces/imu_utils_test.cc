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

#include "intrinsic/icon/hal/interfaces/imu_utils.h"

#include <gtest/gtest.h>

#include "flatbuffers/buffer.h"
#include "flatbuffers/detached_buffer.h"
#include "flatbuffers/verifier.h"
#include "intrinsic/icon/hal/interfaces/imu.fbs.h"

namespace intrinsic_fbs {

TEST(ImuStatusTest, CreateImuStatus) {
  flatbuffers::DetachedBuffer buffer = BuildImuStatus();
  flatbuffers::Verifier verifier(buffer.data(), buffer.size());
  ASSERT_EQ(verifier.VerifyBuffer<ImuStatus>(), true);

  auto status = flatbuffers::GetMutableRoot<ImuStatus>(buffer.data());
  ASSERT_NE(status, nullptr);

  EXPECT_EQ(status->linear_acceleration()->x(), 0.0);
  EXPECT_EQ(status->linear_acceleration()->y(), 0.0);
  EXPECT_EQ(status->linear_acceleration()->z(), 0.0);
  EXPECT_EQ(status->angular_velocity()->x(), 0.0);
  EXPECT_EQ(status->angular_velocity()->y(), 0.0);
  EXPECT_EQ(status->angular_velocity()->z(), 0.0);
  EXPECT_EQ(status->orientation()->qw(), 1.0);
  EXPECT_EQ(status->orientation()->qx(), 0.0);
  EXPECT_EQ(status->orientation()->qy(), 0.0);
  EXPECT_EQ(status->orientation()->qz(), 0.0);
}

}  // namespace intrinsic_fbs
