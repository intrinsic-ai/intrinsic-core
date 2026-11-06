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

#include "intrinsic/conductor/retry.h"

#include <algorithm>
#include <cstdint>

#include "absl/functional/function_ref.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace intrinsic::conductor {

absl::Duration RetryPolicy::GetBackoff(int attempt) const {
  if (attempt < 2) {
    return initial_backoff;
  }
  // Caps the exponent to keep the shift well-defined; absl::Duration
  // multiplication saturates, so large attempts yield `max_backoff`.
  const int doublings = std::min(attempt - 2, 62);
  return std::min(initial_backoff * (int64_t{1} << doublings), max_backoff);
}

bool BackoffBeforeRetry(const RetryPolicy& policy, int attempt,
                        absl::string_view what, absl::string_view reason,
                        absl::FunctionRef<bool()> is_cancelled) {
  if (is_cancelled()) {
    return false;
  }
  const absl::Duration backoff = policy.GetBackoff(attempt);
  LOG(WARNING) << absl::Substitute(
      "Attempt $0/$1 to $2 failed ($3), retrying in $4...", attempt - 1,
      policy.max_attempts, what, reason, absl::FormatDuration(backoff));
  absl::SleepFor(backoff);
  return true;
}

bool IsTransientError(absl::StatusCode code) {
  switch (code) {
    case absl::StatusCode::kUnavailable:
    case absl::StatusCode::kDeadlineExceeded:
    case absl::StatusCode::kResourceExhausted:
    case absl::StatusCode::kAborted:
      return true;
    default:
      return false;
  }
}

}  // namespace intrinsic::conductor
