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

#ifndef INTRINSIC_UTIL_TIME_TIME_H_
#define INTRINSIC_UTIL_TIME_TIME_H_

#include <chrono>  // NOLINT(build/c++11)

#include "absl/time/time.h"
#include "gtest/gtest_prod.h"
#include "intrinsic/icon/testing/realtime_annotations.h"

namespace intrinsic {

// `TimeSteady` represents an abstract timepoint for a monotonically
// increasing clock, commonly used for measuring elapsed time. Note that a
// monotonically increasing clock may stand still. This is not convertible to a
// human-readable time. `TimeSteady` values should not be compared across
// processes, as the epoch of each clock is not guaranteed to be consistent
// across processes. In general, the epoch of a clock depends on the operating
// system. `TimeSteady` should be passed by value rather than const reference.
//
// `TimeSteady` is compatible with the absl::Duration library and is provided as
// an alternate to absl::Time when monotonicity is desired. The relationship of
// `TimeSteady` to `absl::Time` is analogous to the relationship between
// `std::chrono::time_points<std::chrono::steady_clock,...>` and
// `std::chrono::time_points<std::chrono::system_clock,...>`. Likewise there is
// a similar relationship between TimeSteady::Now() and absl::Now().
//
// TimeSteady::Now() should be used over absl::Now() when the goal is to measure
// elapsed time within a process.
//
// The motivation for using `TimeSteady` over `std::chrono::time_point<>` is to
// provide compatibility with `absl::Duration` for both consistency with code
// where typically `absl::Time` is used, and to leverage `absl::Duration`'s
// methods and associated helper functions. These provide better guarantees and
// additional functionality compared to `std::chrono::duration<>`, including
// saturation, conversion utilities, a concept of infinity, etc...
class TimeSteady {
 public:
  constexpr TimeSteady() = default;

  // Copyable.
  constexpr TimeSteady(const TimeSteady& t) = default;
  TimeSteady& operator=(const TimeSteady& t) = default;

  static constexpr TimeSteady InfiniteFuture() INTRINSIC_CHECK_REALTIME_SAFE {
    return TimeSteady(std::chrono::time_point<std::chrono::steady_clock,
                                              std::chrono::nanoseconds>::max());
  }
  static constexpr TimeSteady InfinitePast() INTRINSIC_CHECK_REALTIME_SAFE {
    return TimeSteady(std::chrono::time_point<std::chrono::steady_clock,
                                              std::chrono::nanoseconds>::min());
  }

  // Returns the current time, expressed as a `TimeSteady` absolute time value.
  // Values returned from subsequent calls to Now() are guaranteed to be
  // monotonically increasing.
  static TimeSteady Now() INTRINSIC_CHECK_REALTIME_SAFE;

  // Mathematical operations with other `TimeSteady`s and `absl::Duration`s
  // Follow the conventions of infinite math from absl::Time/Duration, see:
  // cs/third_party/absl/time/time.h
  TimeSteady operator+(absl::Duration duration) const
      INTRINSIC_CHECK_REALTIME_SAFE;
  TimeSteady operator-(absl::Duration duration) const
      INTRINSIC_CHECK_REALTIME_SAFE;
  absl::Duration operator-(TimeSteady other) const
      INTRINSIC_CHECK_REALTIME_SAFE;
  TimeSteady& operator+=(absl::Duration duration) INTRINSIC_CHECK_REALTIME_SAFE;
  TimeSteady& operator-=(absl::Duration duration) INTRINSIC_CHECK_REALTIME_SAFE;

  // Comparisons with other `TimeSteady`s
  constexpr bool operator==(TimeSteady other) const
      INTRINSIC_CHECK_REALTIME_SAFE {
    return value_ == other.value_;
  }

  constexpr bool operator!=(TimeSteady other) const
      INTRINSIC_CHECK_REALTIME_SAFE {
    return value_ != other.value_;
  }

  constexpr bool operator<(TimeSteady other) const
      INTRINSIC_CHECK_REALTIME_SAFE {
    return value_ < other.value_;
  }

  constexpr bool operator<=(TimeSteady other) const
      INTRINSIC_CHECK_REALTIME_SAFE {
    return value_ <= other.value_;
  }

  constexpr bool operator>(TimeSteady other) const
      INTRINSIC_CHECK_REALTIME_SAFE {
    return value_ > other.value_;
  }

  constexpr bool operator>=(TimeSteady other) const
      INTRINSIC_CHECK_REALTIME_SAFE {
    return value_ >= other.value_;
  }

 private:
  FRIEND_TEST(TimeSteadyTest, Comparison);

  constexpr explicit TimeSteady(
      std::chrono::time_point<std::chrono::steady_clock,
                              std::chrono::nanoseconds>
          value)
      : value_(value) {}

  // Store this internally using `std::chrono::time_point<>` with a duration of
  // `std::chrono::nanoseconds`. `std::chrono::nanoseconds` is guaranteed to be
  // represented by a signed integer of at least 64 bits
  // (https://en.cppreference.com/w/cpp/chrono/duration).
  std::chrono::time_point<std::chrono::steady_clock, std::chrono::nanoseconds>
      value_;
};  // namespace intrinsic

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_TIME_TIME_H_
