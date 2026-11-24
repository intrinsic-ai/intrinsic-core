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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CC_TIME_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CC_TIME_H_

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/value.h"

namespace intrinsic::executive::clips {

// Returns current system time as CLIPS Values containing [seconds, nanoseconds]
// relative to Unix Epoch.
Values GetTimeNowAsClipsValues();

// Asserts a time fact to the environment with the current time as timestamp.
// The clips environment has to be initialized i.e., the deftemplate for the
// time fact needs to be known to the environment. See time.clp
absl::Status AssertTimeNow(Environment* env)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex());
}  // namespace intrinsic::executive::clips

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CC_TIME_H_
