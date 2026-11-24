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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CPP_READONLY_FACADE_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CPP_READONLY_FACADE_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/template.h"
#include "intrinsic/executive/clips_cpp/value.h"

namespace intrinsic {
namespace executive {
namespace clips {

// Facade class that (only) allows read access to CLIPS environment data.
class EnvironmentReadonlyFacade {
 public:
  explicit EnvironmentReadonlyFacade(const clips::Environment* environment)
      : environment_(environment) {}

  // Get all facts currently stored in the environment.
  // Returns only facts of matching template_name, all if passed name is empty.
  std::vector<Fact> GetFacts(absl::string_view template_name = "") const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex());

  // Gets the list of facts as formatted strings
  // Returns only facts of matching template_name, all if passed name is empty.
  std::vector<std::string> GetFactsAsStrings(
      absl::string_view template_name = "") const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex());

  // Get template information.
  std::vector<std::string> GetTemplateNames() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex());
  std::vector<Template> GetTemplates() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex());
  absl::StatusOr<Template> GetTemplate(const std::string& template_name) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex());

  // Access to global variables
  absl::StatusOr<ValueOrValues> GetGlobal(const std::string& global_name) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex());

  // Returns the mutex of the CLIPS environment (so that the caller can lock
  // it prior to calling RegisterFunction.
  absl::Mutex* GetClipsMutex() const ABSL_LOCK_RETURNED(environment_->mutex()) {
    return environment_->mutex();
  }

 private:
  const clips::Environment* environment_;  // Owned externally.
};

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CPP_READONLY_FACADE_H_
