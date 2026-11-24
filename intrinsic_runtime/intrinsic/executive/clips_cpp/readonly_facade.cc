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

#include "intrinsic/executive/clips_cpp/readonly_facade.h"

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/template.h"
#include "intrinsic/executive/clips_cpp/value.h"

namespace intrinsic {
namespace executive {
namespace clips {

std::vector<Fact> EnvironmentReadonlyFacade::GetFacts(
    absl::string_view template_name) const {
  return environment_->GetFacts(template_name);
}

std::vector<std::string> EnvironmentReadonlyFacade::GetFactsAsStrings(
    absl::string_view template_name) const {
  return environment_->GetFactsAsStrings(template_name);
}

std::vector<std::string> EnvironmentReadonlyFacade::GetTemplateNames() const {
  return environment_->GetTemplateNames();
}

std::vector<Template> EnvironmentReadonlyFacade::GetTemplates() const {
  return environment_->GetTemplates();
}

absl::StatusOr<Template> EnvironmentReadonlyFacade::GetTemplate(
    const std::string& template_name) const {
  return environment_->GetTemplate(template_name);
}

absl::StatusOr<ValueOrValues> EnvironmentReadonlyFacade::GetGlobal(
    const std::string& global_name) const {
  return environment_->GetGlobal(global_name);
}

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic
