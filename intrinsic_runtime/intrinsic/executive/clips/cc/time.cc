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

#include "intrinsic/executive/clips/cc/time.h"

#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/value.h"

namespace intrinsic::executive::clips {

Values GetTimeNowAsClipsValues() {
  absl::Duration d = absl::Now() - absl::UnixEpoch();
  const int64_t s = absl::IDivDuration(d, absl::Seconds(1), &d);
  const int64_t n = absl::IDivDuration(d, absl::Nanoseconds(1), &d);

  return {Value(s), Value(n)};
}

absl::Status AssertTimeNow(Environment* env) {
  return env->AssertFact("time", {{"timestamp", GetTimeNowAsClipsValues()}})
      .status();
}

}  // namespace intrinsic::executive::clips
