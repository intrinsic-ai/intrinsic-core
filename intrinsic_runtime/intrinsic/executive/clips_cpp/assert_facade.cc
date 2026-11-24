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

#include "intrinsic/executive/clips_cpp/assert_facade.h"

#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/slot_value.h"
#include "intrinsic/executive/clips_cpp/template.h"

namespace intrinsic {
namespace executive {
namespace clips {

absl::Status EnvironmentAssertFacade::AssertFact(const std::string& fact_string)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex()) {
  return environment_->AssertFact(fact_string).status();
}

absl::Status EnvironmentAssertFacade::AssertFacts(
    absl::Span<const std::string> fact_strings)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex()) {
  return environment_->AssertFacts(fact_strings);
}

absl::StatusOr<Fact> EnvironmentAssertFacade::AssertFact(
    const std::string& template_name, absl::Span<const SlotValue> slots)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex()) {
  return environment_->AssertFact(template_name, slots);
}

absl::StatusOr<Fact> EnvironmentAssertFacade::AssertFact(
    const Template& fact_template, absl::Span<const SlotValue> slots)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex()) {
  return environment_->AssertFact(fact_template, slots);
}

void EnvironmentAssertFacade::NotifyRunner() { environment_->NotifyRunner(); }

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic
