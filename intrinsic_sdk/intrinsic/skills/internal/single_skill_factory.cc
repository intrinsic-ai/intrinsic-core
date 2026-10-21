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

#include "intrinsic/skills/internal/single_skill_factory.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/assets/id_utils.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/internal/runtime_data.h"

namespace intrinsic::skills::internal {

namespace {

absl::Status SkillAliasNotFound(absl::string_view skill_alias) {
  return absl::NotFoundError(
      absl::StrFormat("Could not find skill with alias: %s", skill_alias));
}

std::string NameFromIdOrDie(absl::string_view id) {
  absl::StatusOr<std::string> name = assets::NameFrom(id);
  CHECK_OK(name.status());
  return *name;
}

}  // namespace

SingleSkillFactory::SingleSkillFactory(
    const SkillRuntimeData& skill_runtime_data,
    const std::function<absl::StatusOr<std::unique_ptr<SkillInterface>>()>&
        create_skill)
    : skill_runtime_data_(skill_runtime_data),
      // SkillRuntimeData should always have a valid ID after construction, so
      // this should never die.
      // TODO(b/286896397)
      skill_alias_(NameFromIdOrDie(skill_runtime_data.GetId())),
      create_skill_(create_skill) {}

absl::StatusOr<std::unique_ptr<SkillInterface>> SingleSkillFactory::GetSkill(
    absl::string_view skill_alias) {
  if (skill_alias != skill_alias_) {
    return SkillAliasNotFound(skill_alias);
  }

  absl::MutexLock l(&create_skill_mutex_);
  return create_skill_();
}

std::vector<std::string> SingleSkillFactory::GetSkillAliases() const {
  return {skill_alias_};
}

absl::StatusOr<std::unique_ptr<SkillExecuteInterface>>
SingleSkillFactory::GetSkillExecute(absl::string_view skill_alias) {
  if (skill_alias != skill_alias_) {
    return SkillAliasNotFound(skill_alias);
  }

  absl::MutexLock l(&create_skill_mutex_);
  return create_skill_();
}

absl::StatusOr<std::unique_ptr<SkillProjectInterface>>
SingleSkillFactory::GetSkillProject(absl::string_view skill_alias) {
  if (skill_alias != skill_alias_) {
    return SkillAliasNotFound(skill_alias);
  }

  absl::MutexLock l(&create_skill_mutex_);
  return create_skill_();
}

absl::StatusOr<internal::SkillRuntimeData>
SingleSkillFactory::GetSkillRuntimeData(absl::string_view skill_alias) {
  if (skill_alias != skill_alias_) {
    return SkillAliasNotFound(skill_alias);
  }

  return skill_runtime_data_;
}

}  // namespace intrinsic::skills::internal
