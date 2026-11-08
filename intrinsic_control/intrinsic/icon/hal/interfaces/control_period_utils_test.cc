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

#include "intrinsic/icon/hal/interfaces/control_period_utils.h"

#include <gmock/gmock.h>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "internal/testing.h"
#include "intrinsic/icon/hal/interfaces/control_period.fbs.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/shared_memory_manager.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/testing/unique_segment_name.h"

namespace intrinsic_fbs {
namespace {

using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::HasSubstr;

TEST(ControlPeriodUtilsTest, BuildControlPeriod) {
  flatbuffers::DetachedBuffer buffer = BuildControlPeriod();
  const auto* config = flatbuffers::GetRoot<ControlPeriod>(buffer.data());
  EXPECT_EQ(config->control_period_ns(), 0);
}

TEST(ControlPeriodUtilsTest, UpdateControlPeriodWritesValidValue) {
  flatbuffers::DetachedBuffer buffer = BuildControlPeriod();

  auto shared_memory_manager_or = intrinsic::icon::SharedMemoryManager::Create(
      intrinsic::icon::UniqueMemoryNamespace(), "my_test_module");
  ASSERT_OK(shared_memory_manager_or.status());
  auto shared_memory_manager = std::move(shared_memory_manager_or).value();

  ASSERT_OK(shared_memory_manager->AddSegment("control_period", false,
                                              buffer.size()));

  auto segment_or = shared_memory_manager->Get<
      intrinsic::icon::ReadWriteMemorySegment<intrinsic_fbs::ControlPeriod>>(
      "control_period");
  ASSERT_OK(segment_or.status());
  intrinsic::icon::MutableHardwareInterfaceHandle<intrinsic_fbs::ControlPeriod>
      handle(std::move(segment_or).value());

  EXPECT_EQ(handle.NumUpdates(), 0);
  EXPECT_EQ(handle.LastUpdatedTime(), intrinsic::Clock::Zero());
  EXPECT_EQ(handle->control_period_ns(), 0);

  EXPECT_OK(intrinsic::icon::UpdateControlPeriod(
      handle, intrinsic::Nanoseconds(1000000)));
  EXPECT_EQ(handle->control_period_ns(), 1000000);
  EXPECT_EQ(handle.NumUpdates(), 1);
  EXPECT_NE(handle.LastUpdatedTime(), intrinsic::Clock::Zero());
}

TEST(ControlPeriodUtilsTest, UpdateControlPeriodFailsWithInvalidArgument) {
  flatbuffers::DetachedBuffer buffer = BuildControlPeriod();

  auto shared_memory_manager_or = intrinsic::icon::SharedMemoryManager::Create(
      intrinsic::icon::UniqueMemoryNamespace(), "my_test_module");
  ASSERT_OK(shared_memory_manager_or.status());
  auto shared_memory_manager = std::move(shared_memory_manager_or).value();

  ASSERT_OK(shared_memory_manager->AddSegment("control_period", false,
                                              buffer.size()));

  auto segment_or = shared_memory_manager->Get<
      intrinsic::icon::ReadWriteMemorySegment<intrinsic_fbs::ControlPeriod>>(
      "control_period");
  ASSERT_OK(segment_or.status());
  intrinsic::icon::MutableHardwareInterfaceHandle<intrinsic_fbs::ControlPeriod>
      handle(std::move(segment_or).value());

  EXPECT_THAT(
      intrinsic::icon::UpdateControlPeriod(handle, intrinsic::ZeroDuration()),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("Control period must be > 0, got 0 ns")));
  EXPECT_THAT(
      intrinsic::icon::UpdateControlPeriod(handle, intrinsic::Nanoseconds(-1)),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("Control period must be > 0, got -1 ns")));
}

TEST(ControlPeriodUtilsTest, FormatMismatchError) {
  std::string error = FormatControlPeriodMismatchError(
      "my_module", intrinsic::Nanoseconds(10000000),
      intrinsic::Nanoseconds(20000000));
  EXPECT_THAT(
      error, AllOf(HasSubstr("10000000 ns (100.0 Hz)"),
                   HasSubstr("20000000 ns (50.0 Hz)"), HasSubstr("my_module")));
}

TEST(ControlPeriodUtilsTest, FormatMismatchErrorZeroIsNaN) {
  std::string error = FormatControlPeriodMismatchError(
      "my_module", intrinsic::Nanoseconds(10000000), intrinsic::ZeroDuration());
  EXPECT_THAT(error, AllOf(HasSubstr("10000000 ns (100.0 Hz)"),
                           HasSubstr("0 ns (nan Hz)"), HasSubstr("my_module")));
}

TEST(ControlPeriodUtilsTest, FormatMismatchErrorNegativeIsNaN) {
  std::string error = FormatControlPeriodMismatchError(
      "my_module", intrinsic::Nanoseconds(10000000),
      -intrinsic::Nanoseconds(10000000));
  EXPECT_THAT(
      error, AllOf(HasSubstr("10000000 ns (100.0 Hz)"),
                   HasSubstr("-10000000 ns (nan Hz)"), HasSubstr("my_module")));
}

}  // namespace
}  // namespace intrinsic_fbs
