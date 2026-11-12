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

#include "intrinsic/geometry/internal/util/timeout_predicate.h"

#include "absl/status/status.h"

namespace intrinsic::geo {
TimeoutState::TimeoutState(absl::Duration timeout)
    : start(absl::Now()), timeout(timeout) {}

absl::StatusOr<TimeoutPredicate> TimeoutPredicate::Create(
    absl::Duration timeout) {
  if (timeout < absl::ZeroDuration()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Timeout must be non-negative. Got: ", absl::FormatDuration(timeout)));
  }
  return TimeoutPredicate(timeout);
}

bool TimeoutPredicate::HasTimedOut() const {
  if (!IsActive()) return false;
  return (absl::Now() - state_->start) > state_->timeout;
}

TimeoutPredicate::TimeoutPredicate(absl::Duration timeout)
    : state_(std::in_place, timeout) {}

}  // namespace intrinsic::geo
