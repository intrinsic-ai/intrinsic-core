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

#include "intrinsic/icon/hal/interfaces/force_sensor_utils.h"

#include <gtest/gtest.h>

#include "flatbuffers/buffer.h"
#include "flatbuffers/detached_buffer.h"
#include "flatbuffers/verifier.h"
#include "intrinsic/icon/hal/interfaces/force_sensor.fbs.h"

namespace intrinsic_fbs {
namespace {

using ::testing::Test;

TEST(ForceSensorUtilsTest, CreateForceSensorStatusBuffer) {
  flatbuffers::DetachedBuffer buffer = CreateForceSensorStatusBuffer();

  flatbuffers::Verifier verifier(buffer.data(), buffer.size());
  EXPECT_EQ(verifier.VerifyBuffer<ForceSensorStatus>(), true);

  auto status = flatbuffers::GetMutableRoot<ForceSensorStatus>(buffer.data());

  EXPECT_NE(status, nullptr);
  EXPECT_NE(status->wrench_at_ft(), nullptr);
  EXPECT_NE(status->wrench_at_target(), nullptr);
  EXPECT_NE(status->wrench_at_ft_dynamic_load(), nullptr);
  EXPECT_NE(status->wrench_at_target_dynamic_load(), nullptr);

  EXPECT_EQ(status->wrench_at_ft()->x(), 0);
  EXPECT_EQ(status->wrench_at_ft()->y(), 0);
  EXPECT_EQ(status->wrench_at_ft()->z(), 0);
  EXPECT_EQ(status->wrench_at_ft()->rx(), 0);
  EXPECT_EQ(status->wrench_at_ft()->ry(), 0);
  EXPECT_EQ(status->wrench_at_ft()->rz(), 0);

  EXPECT_EQ(status->wrench_at_target()->x(), 0);
  EXPECT_EQ(status->wrench_at_target()->y(), 0);
  EXPECT_EQ(status->wrench_at_target()->z(), 0);
  EXPECT_EQ(status->wrench_at_target()->rx(), 0);
  EXPECT_EQ(status->wrench_at_target()->ry(), 0);
  EXPECT_EQ(status->wrench_at_target()->rz(), 0);

  EXPECT_EQ(status->wrench_at_ft_dynamic_load()->x(), 0);
  EXPECT_EQ(status->wrench_at_ft_dynamic_load()->y(), 0);
  EXPECT_EQ(status->wrench_at_ft_dynamic_load()->z(), 0);
  EXPECT_EQ(status->wrench_at_ft_dynamic_load()->rx(), 0);
  EXPECT_EQ(status->wrench_at_ft_dynamic_load()->ry(), 0);
  EXPECT_EQ(status->wrench_at_ft_dynamic_load()->rz(), 0);

  EXPECT_EQ(status->wrench_at_target_dynamic_load()->x(), 0);
  EXPECT_EQ(status->wrench_at_target_dynamic_load()->y(), 0);
  EXPECT_EQ(status->wrench_at_target_dynamic_load()->z(), 0);
  EXPECT_EQ(status->wrench_at_target_dynamic_load()->rx(), 0);
  EXPECT_EQ(status->wrench_at_target_dynamic_load()->ry(), 0);
  EXPECT_EQ(status->wrench_at_target_dynamic_load()->rz(), 0);

  EXPECT_EQ(status->magnitude_force_at_target(), 0);
  EXPECT_EQ(status->magnitude_torque_at_target(), 0);
  EXPECT_EQ(status->tared(), false);

  status->mutate_magnitude_force_at_target(1);
  EXPECT_EQ(status->magnitude_force_at_target(), 1);

  status->mutate_magnitude_torque_at_target(2);
  EXPECT_EQ(status->magnitude_torque_at_target(), 2);

  status->mutate_tared(true);
  EXPECT_EQ(status->tared(), true);
}

TEST(ForceSensorUtilsTest, CreateForceSensorCommandBuffer) {
  flatbuffers::DetachedBuffer buffer =
      intrinsic_fbs::CreateForceSensorCommandBuffer();

  flatbuffers::Verifier verifier(buffer.data(), buffer.size());
  EXPECT_EQ(verifier.VerifyBuffer<intrinsic_fbs::ForceSensorCommand>(), true);

  auto command = flatbuffers::GetMutableRoot<intrinsic_fbs::ForceSensorCommand>(
      buffer.data());

  EXPECT_NE(command, nullptr);
  EXPECT_EQ(command->tare_sensor(), false);

  command->mutate_tare_sensor(true);
  EXPECT_EQ(command->tare_sensor(), true);

  EXPECT_EQ(command->num_taring_cycles(), 1);
  command->mutate_num_taring_cycles(42);
  EXPECT_EQ(command->num_taring_cycles(), 42);
}

}  // namespace
}  // namespace intrinsic_fbs
