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

#include "intrinsic/icon/hal/interfaces/adio_utils.h"

#include <gtest/gtest.h>

#include "flatbuffers/buffer.h"
#include "intrinsic/icon/hal/interfaces/adio.fbs.h"

namespace intrinsic_fbs {
namespace {

TEST(ADIO, BuildDigitalOutputCommand) {
  auto buffer =
      BuildDigitalOutputCommand("name", /*value=*/false, /*bit_number=*/4);
  auto command =
      flatbuffers::GetMutableRoot<DigitalOutputCommand>(buffer.data());
  ASSERT_NE(command, nullptr);

  EXPECT_EQ(command->name()->string_view(), "name");
  EXPECT_EQ(command->value(), false);
  EXPECT_EQ(command->bit_number(), 4);

  // String can't be resized.
  command->mutable_name()->Mutate(0, 'l');
  EXPECT_EQ(command->name()->string_view(), "lame");

  command->mutate_value(true);
  EXPECT_EQ(command->value(), true);

  command->mutate_bit_number(42);
  EXPECT_EQ(command->bit_number(), 42);
}
TEST(ADIO, BuildDigitalInputStatus) {
  auto buffer =
      BuildDigitalInputStatus("name", /*value=*/false, /*bit_number=*/4);
  const auto status =
      flatbuffers::GetMutableRoot<DigitalInputStatus>(buffer.data());
  ASSERT_NE(status, nullptr);

  EXPECT_EQ(status->name()->string_view(), "name");
  EXPECT_EQ(status->value(), false);
  EXPECT_EQ(status->bit_number(), 4);
}

TEST(ADIO, BuildAnalogOutputCommand) {
  auto buffer =
      BuildAnalogOutputCommand("name", /*unit=*/AnalogInputUnit::kSievert,
                               /*value=*/27.3, /*is_enabled=*/true);
  const auto command =
      flatbuffers::GetMutableRoot<AnalogOutputCommand>(buffer.data());
  ASSERT_NE(command, nullptr);
  EXPECT_EQ(command->name()->string_view(), "name");
  EXPECT_EQ(command->unit(), AnalogInputUnit::kSievert);
  EXPECT_EQ(command->value(), 27.3);
  EXPECT_EQ(command->is_enabled(), true);

  // String can't be resized.
  command->mutable_name()->Mutate(0, 'l');
  EXPECT_EQ(command->name()->string_view(), "lame");

  command->mutate_unit(AnalogInputUnit::kBecquerel);
  EXPECT_EQ(command->unit(), AnalogInputUnit::kBecquerel);

  command->mutate_value(67.2);
  EXPECT_EQ(command->value(), 67.2);

  command->mutate_is_enabled(false);
  EXPECT_EQ(command->is_enabled(), false);
}

TEST(ADIO, BuildAnalogInputStatus) {
  auto buffer =
      BuildAnalogInputStatus("name", /*unit=*/AnalogInputUnit::kCandela,
                             /*value=*/27.3, /*is_enabled=*/true);
  const auto status =
      flatbuffers::GetMutableRoot<AnalogInputStatus>(buffer.data());
  ASSERT_NE(status, nullptr);

  EXPECT_EQ(status->name()->string_view(), "name");
  EXPECT_EQ(status->unit(), AnalogInputUnit::kCandela);
  EXPECT_EQ(status->value(), 27.3);
  EXPECT_EQ(status->is_enabled(), true);
}

}  // namespace
}  // namespace intrinsic_fbs
