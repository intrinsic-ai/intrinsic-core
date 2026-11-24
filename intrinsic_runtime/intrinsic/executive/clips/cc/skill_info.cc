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

#include "intrinsic/executive/clips/cc/skill_info.h"

#include <cstdint>
#include <string_view>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::executive {

absl::StatusOr<clips::DescriptorPoolId> GetParameterPoolIdForSkill(
    std::string_view skill_id, std::string_view operation_name,
    const clips::Environment& env) ABSL_EXCLUSIVE_LOCKS_REQUIRED(env.mutex()) {
  INTR_ASSIGN_OR_RETURN(
      clips::Fact skill_info_fact,
      env.GetUniqueFact("skill-info", {{"operation-name", operation_name},
                                       {"skill-id", skill_id}}));

  INTR_ASSIGN_OR_RETURN(
      clips::Value pool_id_value,
      skill_info_fact.GetSlotValue("parameter-descriptor-pool-id"));

  INTR_ASSIGN_OR_RETURN(int64_t pool_id, pool_id_value.GetInteger());

  return clips::DescriptorPoolId(pool_id);
}

absl::StatusOr<clips::ProtobufManager::DescriptorPoolInfo>
GetParameterPoolInfoForSkill(std::string_view skill_id,
                             std::string_view operation_name,
                             const clips::Environment& env,
                             clips::ProtobufManager& proto_manager)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env.mutex()) {
  INTR_ASSIGN_OR_RETURN(
      clips::DescriptorPoolId pool_id,
      GetParameterPoolIdForSkill(skill_id, operation_name, env));

  return proto_manager.GetDescriptorPool(pool_id);
}

}  // namespace intrinsic::executive
