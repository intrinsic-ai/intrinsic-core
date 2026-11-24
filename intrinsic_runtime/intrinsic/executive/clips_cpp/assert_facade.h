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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CPP_ASSERT_FACADE_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CPP_ASSERT_FACADE_H_

#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/slot_value.h"
#include "intrinsic/executive/clips_cpp/template.h"

namespace intrinsic {
namespace executive {
namespace clips {

// Facade class that (only) allows the assertion of new facts.
class EnvironmentAssertFacade {
 public:
  explicit EnvironmentAssertFacade(clips::Environment* environment)
      : environment_(environment) {}

  // Assert a fact into the fact base.
  absl::Status AssertFact(const std::string& fact_string)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex());

  // Call AssertFact on a number of facts. Aborts insertion as soon as any
  // fact fails to be asserted.
  absl::Status AssertFacts(absl::Span<const std::string> fact_strings)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex());

  // Assert a fact into the fact base from template and slot values.
  // Note that the behavior also depends on the environment configuration,
  // which can be modified from CLIPS code within the environment. For
  // example, (set-fact-duplication) can be used to modify this behavior.
  // We generally recommend to keep it disabled and use other means to
  // disambiguate facts.
  absl::StatusOr<Fact> AssertFact(const std::string& template_name,
                                  absl::Span<const SlotValue> slots)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex());
  absl::StatusOr<Fact> AssertFact(const Template& fact_template,
                                  absl::Span<const SlotValue> slots)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex());

  // Returns the mutex of the CLIPS environment (so that the caller can lock
  // it prior to calling RegisterFunction.
  absl::Mutex* GetClipsMutex() const ABSL_LOCK_RETURNED(environment_->mutex()) {
    return environment_->mutex();
  }

  void NotifyRunner() ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex());

 private:
  clips::Environment* environment_;  // Owned externally.
};

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CPP_ASSERT_FACADE_H_
