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

#include "intrinsic/icon/hal/interfaces/force_torque_utils.h"

#include <gtest/gtest.h>

#include "absl/strings/match.h"
#include "flatbuffers/buffer.h"
#include "flatbuffers/detached_buffer.h"
#include "flatbuffers/verifier.h"
#include "intrinsic/icon/flatbuffers/transform_types.fbs.h"
#include "intrinsic/icon/hal/interfaces/force_sensor.fbs.h"
#include "intrinsic/icon/hal/interfaces/force_torque.fbs.h"

namespace intrinsic_fbs {
namespace {

using ::testing::Test;

using ::intrinsic_fbs::CreateFbsForceTorqueCommand;
using ::intrinsic_fbs::CreateFbsForceTorqueStatus;
using ::intrinsic_fbs::ForceTorqueCommand;
using ::intrinsic_fbs::ForceTorqueStatus;

TEST(ForceTorqueUtilsTest, CreateFbsForceTorqueStatus) {
  flatbuffers::DetachedBuffer buffer = CreateFbsForceTorqueStatus();
  flatbuffers::Verifier verifier(buffer.data(), buffer.size());
  ASSERT_EQ(verifier.VerifyBuffer<ForceTorqueStatus>(), true);

  auto status = flatbuffers::GetMutableRoot<ForceTorqueStatus>(buffer.data());
  ASSERT_NE(status, nullptr);

  EXPECT_EQ(status->enabled(), false);
  EXPECT_TRUE(status->mutate_enabled(true));
  EXPECT_EQ(status->enabled(), true);

  EXPECT_EQ(status->retare_suggested(), false);
  EXPECT_TRUE(status->mutate_retare_suggested(true));
  EXPECT_EQ(status->retare_suggested(), true);

  EXPECT_EQ(status->retare_completed(), false);
  EXPECT_TRUE(status->mutate_retare_completed(false));
  EXPECT_EQ(status->retare_completed(), false);

  // mutable_wrench is a struct, so testing one assignment should be enough.
  ASSERT_NE(status->mutable_wrench(), nullptr);
  status->mutable_wrench()->mutate_rx(1.23);
  EXPECT_EQ(status->wrench()->rx(), 1.23);
}

TEST(ForceTorqueUtilsTest, CreateFbsForceTorqueCommand) {
  flatbuffers::DetachedBuffer buffer = CreateFbsForceTorqueCommand();
  flatbuffers::Verifier verifier(buffer.data(), buffer.size());
  ASSERT_EQ(verifier.VerifyBuffer<ForceTorqueCommand>(), true);

  auto command = flatbuffers::GetMutableRoot<ForceTorqueCommand>(buffer.data());
  ASSERT_NE(command, nullptr);

  EXPECT_EQ(command->retare(), false);
  EXPECT_TRUE(command->mutate_retare(true));
  EXPECT_EQ(command->retare(), true);

  EXPECT_EQ(command->num_taring_cycles(), 1);
  EXPECT_TRUE(command->mutate_num_taring_cycles(42));
  EXPECT_EQ(command->num_taring_cycles(), 42);
}

TEST(ForceTorqueUtilsTest, StatusCodeConversionWorks) {
  EXPECT_TRUE(absl::StrContains(
      intrinsic_fbs::ToFixedString(intrinsic_fbs::ForceSensorStatusCode::Ok),
      "Ok"));
  EXPECT_TRUE(
      absl::StrContains(intrinsic_fbs::ToFixedString(
                            intrinsic_fbs::ForceSensorStatusCode::GenericError),
                        "Generic"));
  EXPECT_TRUE(absl::StrContains(
      intrinsic_fbs::ToFixedString(intrinsic_fbs::ForceSensorStatusCode::
                                       InternalErrorRequiringPowerCycling),
      "InternalErrorRequiringPowerCycling"));
  EXPECT_TRUE(absl::StrContains(
      intrinsic_fbs::ToFixedString(
          intrinsic_fbs::ForceSensorStatusCode::MechanicalOverloadDetected),
      "MechanicalOverloadDetected"));
  EXPECT_TRUE(absl::StrContains(
      intrinsic_fbs::ToFixedString(
          intrinsic_fbs::ForceSensorStatusCode::SupplyOutOfRange),
      "SupplyOutOfRange"));
  EXPECT_TRUE(absl::StrContains(
      intrinsic_fbs::ToFixedString(
          intrinsic_fbs::ForceSensorStatusCode::ReadingsSaturated),
      "ReadingsSaturated"));
  EXPECT_TRUE(absl::StrContains(
      intrinsic_fbs::ToFixedString(
          intrinsic_fbs::ForceSensorStatusCode::CalibrationChecksumError),
      "CalibrationChecksumError"));
  EXPECT_TRUE(absl::StrContains(
      intrinsic_fbs::ToFixedString(
          intrinsic_fbs::ForceSensorStatusCode::SimulatedError),
      "SimulatedError"));
}

}  // namespace
}  // namespace intrinsic_fbs
