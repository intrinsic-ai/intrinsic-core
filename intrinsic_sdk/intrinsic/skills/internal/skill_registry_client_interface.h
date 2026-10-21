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

#ifndef INTRINSIC_SKILLS_INTERNAL_SKILL_REGISTRY_CLIENT_INTERFACE_H_
#define INTRINSIC_SKILLS_INTERNAL_SKILL_REGISTRY_CLIENT_INTERFACE_H_

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/skills/proto/skill_registry_config.pb.h"
#include "intrinsic/skills/proto/skills.pb.h"

namespace intrinsic {
namespace skills {

// A client interface for the Skill Registry service.
class SkillRegistryClientInterface {
 public:
  virtual ~SkillRegistryClientInterface() = default;

  // Fetches all available skill interfaces.
  //
  // This makes a blocking GRPC request.
  //
  // Returns `DeadlineExceededError` if the request hasn't completed after
  // `timeout`.
  //
  // Returns any errors from the GRPC invocation.
  //
  // Returns any errors reported in the response's `GetSkillsResponse::status`
  // field.
  virtual absl::StatusOr<std::vector<intrinsic_proto::skills::Skill>>
  GetSkills() const = 0;
  virtual absl::StatusOr<std::vector<intrinsic_proto::skills::Skill>> GetSkills(
      absl::Duration timeout) const = 0;

  // Fetches a Skill by id.
  //
  // This (indirectly) makes a blocking GRPC call.
  //
  // Returns `NotFoundError` if `skill_id` does not match a Skill in `client`.
  //
  // Returns any errors from the GRPC invocation; Returns any errors reported by
  // the Skill Registry Service.
  virtual absl::StatusOr<intrinsic_proto::skills::Skill> GetSkillById(
      absl::string_view skill_id) const = 0;

  // Returns the BehaviorTree that is registered for a specific skill with
  // `skill_id`. The requested skill must be a parameterizable BehaviorTree.
  //
  // This makes a blocking GRPC request.
  //
  // Returns `DeadlineExceededError` if the request hasn't completed after
  // `timeout`.
  //
  // Returns any errors from the GRPC invocation.
  virtual absl::StatusOr<intrinsic_proto::executive::BehaviorTree>
  GetBehaviorTree(absl::string_view skill_id) const = 0;
  virtual absl::StatusOr<intrinsic_proto::executive::BehaviorTree>
  GetBehaviorTree(absl::string_view skill_id, absl::Duration timeout) const = 0;

  // Registers a new skill (or updates an existing one). Skill registrations are
  // stored and retrieved by their skill name. If the registry already has a
  // skill registered by skill name then this call will update its registration.
  //
  // This makes a blocking GRPC request.
  //
  // Returns `FailedPreconditionError` if the registration is invalid.
  //
  // Returns any errors from the GRPC invocation; Returns any errors reported by
  // the Skill Registry Service.
  virtual absl::Status RegisterOrUpdateSkill(
      intrinsic_proto::skills::SkillRegistration skill_registration) const = 0;
  virtual absl::Status RegisterOrUpdateSkill(
      intrinsic_proto::skills::SkillRegistration skill_registration,
      absl::Duration timeout) const = 0;

  // Registers a BehaviorTree (or updates an existing one) in the skill
  // registry. The BehaviorTree is registered by its skill description's skill
  // id. If the registry already has a behavior tree registered by the skill id
  // then this call will update its registration.
  //
  // This makes a blocking GRPC request.
  //
  // Returns `FailedPreconditionError` if the registration is invalid.
  //
  // Returns any errors from the GRPC invocation; Returns any errors reported by
  // the Skill Registry Service.
  virtual absl::Status RegisterOrUpdateBehaviorTree(
      intrinsic_proto::skills::BehaviorTreeRegistration
          behavior_tree_registration) const = 0;
  virtual absl::Status RegisterOrUpdateBehaviorTree(
      intrinsic_proto::skills::BehaviorTreeRegistration
          behavior_tree_registration,
      absl::Duration timeout) const = 0;

  // Unregisters a BehaviorTree from the skill registry.
  //
  // This makes a blocking GRPC request.
  //
  // Returns any errors from the GRPC invocation; Returns any errors reported by
  // the Skill Registry Service.
  virtual absl::Status UnregisterBehaviorTree(std::string skill_id) const = 0;
  virtual absl::Status UnregisterBehaviorTree(std::string skill_id,
                                              absl::Duration timeout) const = 0;
};

}  // namespace skills
}  // namespace intrinsic

#endif  // INTRINSIC_SKILLS_INTERNAL_SKILL_REGISTRY_CLIENT_INTERFACE_H_
