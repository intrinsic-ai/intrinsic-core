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

#ifndef INTRINSIC_GEOMETRY_API_TIMEOUT_PREDICATE_H_
#define INTRINSIC_GEOMETRY_API_TIMEOUT_PREDICATE_H_

#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "intrinsic/marshal/riegeli_coder.h"

namespace intrinsic::geo {
// Internal state for a timeout, capturing the start time and the allowed
// duration.
struct TimeoutState {
  absl::Time start;
  absl::Duration timeout;

  // Initializes the state, capturing the current time as the start.
  explicit TimeoutState(absl::Duration timeout);
};

// A utility class used to track if a processing task has exceeded a specified
// time limit.
class TimeoutPredicate {
 public:
  // Creates an "Inactive" predicate. IsActive() will return false,
  // and HasTimedOut() will always return false.
  TimeoutPredicate() = default;

  // Factory method to create an active timeout predicate.
  // Returns an InvalidArgumentError if the provided timeout is negative.
  // A timeout of ZeroDuration is valid and will cause HasTimedOut()
  // to return true immediately.
  static absl::StatusOr<TimeoutPredicate> Create(absl::Duration timeout);

  // Returns true if the predicate was initialized with a duration.
  bool IsActive() const { return state_.has_value(); }

  // Returns true if the predicate is active and the elapsed time since
  // creation exceeds the allowed duration. Returns false if the predicate
  // is inactive.
  bool HasTimedOut() const;

 private:
  friend struct RiegeliCoder<TimeoutPredicate>;

  // Private constructor used by Create() to initialize the optional state.
  explicit TimeoutPredicate(absl::Duration timeout);

  std::optional<TimeoutState> state_;
};

}  // namespace intrinsic::geo

namespace intrinsic {

// RiegeliCoder specialization allows the TimeoutPredicate to be serialized.
// Note: When decoded, the timeout remains relative to the original start time.
template <>
struct RiegeliCoder<geo::TimeoutPredicate> {
  static std::string TypeName() { return "intrinsic::TimeoutPredicate"; }

  static absl::Status Encode(const geo::TimeoutPredicate& /*value*/,
                             riegeli::RecordWriterBase& /*writer*/) {
    return absl::OkStatus();
  };

  static absl::StatusOr<geo::TimeoutPredicate> Decode(
      riegeli::RecordReaderBase& /*reader*/) {
    return geo::TimeoutPredicate();
  };
};

}  // namespace intrinsic
#endif  // INTRINSIC_GEOMETRY_API_TIMEOUT_PREDICATE_H_
