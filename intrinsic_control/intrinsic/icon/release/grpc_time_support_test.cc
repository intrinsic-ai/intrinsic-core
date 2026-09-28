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

#include "intrinsic/icon/release/grpc_time_support.h"

#include <initializer_list>

#include "absl/time/time.h"
#include "grpc/support/time.h"
#include "grpcpp/client_context.h"
#include "gtest/gtest.h"

namespace grpc {
namespace {

TEST(GrpcTimeSupportTest, PreservesUnixEpoch) {
  const gpr_timespec result = GprTimeSpecFromTime(absl::UnixEpoch());
  EXPECT_EQ(result.clock_type, GPR_CLOCK_REALTIME);
  EXPECT_EQ(result.tv_sec, 0);
  EXPECT_EQ(result.tv_nsec, 0);
  EXPECT_EQ(TimeFromGprTimespec(result), absl::UnixEpoch());
}

TEST(GrpcTimeSupportTest, PreservesNanoseconds) {
  const absl::Time time =
      absl::FromUnixSeconds(42) + absl::Nanoseconds(123456789);
  const gpr_timespec result = GprTimeSpecFromTime(time);
  EXPECT_EQ(result.clock_type, GPR_CLOCK_REALTIME);
  EXPECT_EQ(result.tv_sec, 42);
  EXPECT_EQ(result.tv_nsec, 123456789);
  EXPECT_EQ(TimeFromGprTimespec(result), time);
}

TEST(GrpcTimeSupportTest, PreservesTimeBeforeUnixEpoch) {
  const absl::Time time = absl::UnixEpoch() - absl::Nanoseconds(1);
  const gpr_timespec result = GprTimeSpecFromTime(time);
  EXPECT_EQ(result.clock_type, GPR_CLOCK_REALTIME);
  EXPECT_EQ(result.tv_sec, -1);
  EXPECT_EQ(result.tv_nsec, 999999999);
  EXPECT_EQ(TimeFromGprTimespec(result), time);
}

TEST(GrpcTimeSupportTest, PreservesInfiniteFuture) {
  const gpr_timespec result = GprTimeSpecFromTime(absl::InfiniteFuture());
  EXPECT_EQ(result.clock_type, GPR_CLOCK_REALTIME);
  EXPECT_EQ(gpr_time_cmp(result, gpr_inf_future(GPR_CLOCK_REALTIME)), 0);
  EXPECT_EQ(TimeFromGprTimespec(result), absl::InfiniteFuture());
}

TEST(GrpcTimeSupportTest, PreservesInfinitePast) {
  const gpr_timespec result = GprTimeSpecFromTime(absl::InfinitePast());
  EXPECT_EQ(result.clock_type, GPR_CLOCK_REALTIME);
  EXPECT_EQ(gpr_time_cmp(result, gpr_inf_past(GPR_CLOCK_REALTIME)), 0);
  EXPECT_EQ(TimeFromGprTimespec(result), absl::InfinitePast());
}

TEST(GrpcTimeSupportTest, SupportsAbslTimeClientDeadline) {
  const absl::Time deadline =
      absl::FromUnixSeconds(42) + absl::Nanoseconds(123456789);
  ClientContext context;
  context.set_deadline(deadline);
  EXPECT_EQ(context.raw_deadline().clock_type, GPR_CLOCK_REALTIME);
  EXPECT_EQ(TimeFromGprTimespec(context.raw_deadline()), deadline);
}

TEST(GrpcTimeSupportTest, PreservesFiniteDurations) {
  for (const absl::Duration duration :
       {absl::ZeroDuration(), absl::Seconds(2) + absl::Nanoseconds(123456789),
        -absl::Nanoseconds(1)}) {
    const gpr_timespec result = GprTimeSpecFromDuration(duration);
    EXPECT_EQ(result.clock_type, GPR_TIMESPAN);
    EXPECT_EQ(DurationFromGprTimespec(result), duration);
  }
}

TEST(GrpcTimeSupportTest, PreservesInfiniteDurations) {
  const gpr_timespec future = GprTimeSpecFromDuration(absl::InfiniteDuration());
  const gpr_timespec past = GprTimeSpecFromDuration(-absl::InfiniteDuration());
  EXPECT_EQ(future.clock_type, GPR_TIMESPAN);
  EXPECT_EQ(past.clock_type, GPR_TIMESPAN);
  EXPECT_EQ(gpr_time_cmp(future, gpr_inf_future(GPR_TIMESPAN)), 0);
  EXPECT_EQ(gpr_time_cmp(past, gpr_inf_past(GPR_TIMESPAN)), 0);
}

TEST(GrpcTimeSupportTest, CreatesMonotonicDeadlines) {
  for (const absl::Duration duration : {absl::Seconds(2), -absl::Seconds(2)}) {
    const gpr_timespec before = gpr_now(GPR_CLOCK_MONOTONIC);
    const gpr_timespec result = DeadlineFromDuration(duration);
    const gpr_timespec after = gpr_now(GPR_CLOCK_MONOTONIC);
    const gpr_timespec span = GprTimeSpecFromDuration(duration);
    EXPECT_EQ(result.clock_type, GPR_CLOCK_MONOTONIC);
    EXPECT_GE(gpr_time_cmp(result, gpr_time_add(before, span)), 0);
    EXPECT_LE(gpr_time_cmp(result, gpr_time_add(after, span)), 0);
  }
}

TEST(GrpcTimeSupportTest, PreservesInfiniteDeadlines) {
  const gpr_timespec future = DeadlineFromDuration(absl::InfiniteDuration());
  const gpr_timespec past = DeadlineFromDuration(-absl::InfiniteDuration());
  EXPECT_EQ(future.clock_type, GPR_CLOCK_MONOTONIC);
  EXPECT_EQ(past.clock_type, GPR_CLOCK_MONOTONIC);
  EXPECT_EQ(gpr_time_cmp(future, gpr_inf_future(GPR_CLOCK_MONOTONIC)), 0);
  EXPECT_EQ(gpr_time_cmp(past, gpr_inf_past(GPR_CLOCK_MONOTONIC)), 0);
}

}  // namespace
}  // namespace grpc
