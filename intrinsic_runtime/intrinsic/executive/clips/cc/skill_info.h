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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CC_SKILL_INFO_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CC_SKILL_INFO_H_

#include <string_view>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"

namespace intrinsic::executive {

// Returns the pool id of the parameter descriptor pool for the given skill as
// loaded for the given operation.
absl::StatusOr<clips::DescriptorPoolId> GetParameterPoolIdForSkill(
    std::string_view skill_id, std::string_view operation_name,
    const clips::Environment& env) ABSL_EXCLUSIVE_LOCKS_REQUIRED(env.mutex());

// Returns full pool info of the parameter descriptor pool for the given skill
// as loaded for the given operation.
absl::StatusOr<clips::ProtobufManager::DescriptorPoolInfo>
GetParameterPoolInfoForSkill(std::string_view skill_id,
                             std::string_view operation_name,
                             const clips::Environment& env,
                             clips::ProtobufManager& proto_manager)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env.mutex());

}  // namespace intrinsic::executive

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CC_SKILL_INFO_H_
