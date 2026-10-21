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

#include "intrinsic/util/time/time.h"

#include <chrono>  // NOLINT(build/c++11)

#include "absl/time/time.h"

namespace intrinsic {

// static
TimeSteady TimeSteady::Now() {
  return TimeSteady(std::chrono::steady_clock::now());
}

TimeSteady TimeSteady::operator+(absl::Duration duration) const {
  // Follow convention of absl::Duration/ Time for adding infinites.
  if (*this == InfiniteFuture()) return *this;
  if (*this == InfinitePast()) return *this;
  if (duration == absl::InfiniteDuration()) return InfiniteFuture();
  if (duration == -absl::InfiniteDuration()) return InfinitePast();

  if (duration >= absl::Duration()) {
    return InfiniteFuture().value_ - absl::ToChronoNanoseconds(duration) <
                   value_
               ? InfiniteFuture()
               : TimeSteady(value_ + absl::ToChronoNanoseconds(duration));
  } else {
    return InfinitePast().value_ - absl::ToChronoNanoseconds(duration) > value_
               ? InfinitePast()
               : TimeSteady(value_ + absl::ToChronoNanoseconds(duration));
  }
}

TimeSteady TimeSteady::operator-(absl::Duration duration) const {
  // Follow convention of absl::Duration/ Time for subtracting infinites.
  if (*this == InfiniteFuture()) return *this;
  if (*this == InfinitePast()) return *this;
  if (duration == absl::InfiniteDuration()) return InfiniteFuture();
  if (duration == -absl::InfiniteDuration()) return InfinitePast();

  if (duration >= absl::Duration()) {
    return InfinitePast().value_ + absl::ToChronoNanoseconds(duration) > value_
               ? InfinitePast()
               : TimeSteady(value_ - absl::ToChronoNanoseconds(duration));
  } else {
    return InfiniteFuture().value_ + absl::ToChronoNanoseconds(duration) <
                   value_
               ? InfiniteFuture()
               : TimeSteady(value_ - absl::ToChronoNanoseconds(duration));
  }
}

absl::Duration TimeSteady::operator-(TimeSteady other) const {
  if (*this == InfiniteFuture()) return absl::InfiniteDuration();
  if (*this == InfinitePast()) return -absl::InfiniteDuration();
  if (other == InfiniteFuture()) return -absl::InfiniteDuration();
  if (other == InfinitePast()) return absl::InfiniteDuration();

  if (other.value_ >= std::chrono::time_point<std::chrono::steady_clock,
                                              std::chrono::nanoseconds>()) {
    return std::chrono::nanoseconds::min() + other.value_ > value_
               ? -absl::InfiniteDuration()
               : absl::FromChrono(value_ - other.value_);
  } else {
    return std::chrono::nanoseconds::max() + other.value_ < value_
               ? absl::InfiniteDuration()
               : absl::FromChrono(value_ - other.value_);
  }
}

TimeSteady& TimeSteady::operator+=(absl::Duration duration) {
  return *this = (*this + duration);
}

TimeSteady& TimeSteady::operator-=(absl::Duration duration) {
  return *this = (*this - duration);
}

}  // namespace intrinsic
