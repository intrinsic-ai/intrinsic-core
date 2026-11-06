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

#ifndef INTRINSIC_CONDUCTOR_RETRY_H_
#define INTRINSIC_CONDUCTOR_RETRY_H_

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace intrinsic::conductor {

// Policy for retrying an operation on transient errors (see IsTransientError).
struct RetryPolicy {
  // Total number of attempts, including the first one.
  int max_attempts = 3;
  // Delay before the second attempt, doubled for every further attempt up to
  // `max_backoff`.
  absl::Duration initial_backoff = absl::Seconds(2);
  absl::Duration max_backoff = absl::Seconds(2);

  constexpr bool IsValid() const {
    return max_attempts >= 1 && initial_backoff >= absl::ZeroDuration() &&
           initial_backoff <= max_backoff &&
           max_backoff < absl::InfiniteDuration();
  }

  // Returns the delay before attempt `attempt` (2-based, i.e., `attempt` = 2 is
  // the delay between the first and the second attempt). Returns
  // `initial_backoff` if `attempt` < 2.
  absl::Duration GetBackoff(int attempt) const;
};

// Sleeps before attempt `attempt` according to `policy` after logging that the
// previous attempt to `what` failed due to `reason`. Returns false without
// sleeping if the operation has already been cancelled.
bool BackoffBeforeRetry(
    const RetryPolicy& policy, int attempt, absl::string_view what,
    absl::string_view reason,
    absl::FunctionRef<bool()> is_cancelled = [] { return false; });

// Returns true if `code` denotes an error which may resolve itself when the
// operation is retried, e.g., while a dependent service is (re)starting.
bool IsTransientError(absl::StatusCode code);

}  // namespace intrinsic::conductor

#endif  // INTRINSIC_CONDUCTOR_RETRY_H_
