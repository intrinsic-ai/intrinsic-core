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

#ifndef INTRINSIC_SKILLS_CC_SKILL_INTERFACE_UTILS_H_
#define INTRINSIC_SKILLS_CC_SKILL_INTERFACE_UTILS_H_

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/internal/execute_context_view.h"
#include "intrinsic/util/proto/any.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace skills {

// Implements SkillInterface::Preview() by calling SkillInterface::Execute().
//
// A skill can use this function to implement Preview() by calling
// PreviewViaExecute() from within its implementation. E.g.:
// ```
// absl::StatusOr<std::unique_ptr<::google::protobuf::Message>>
// MySkill::Preview(
//     const PreviewRequest& request, PreviewContext& context) {
//     ...
//     return PreviewViaExecute(*this, request, context);
// }
// ```
//
// A skill should only use this util to implement Preview() if its Execute()
// method does not require resources or modify the object world.
absl::StatusOr<std::unique_ptr<::google::protobuf::Message>> PreviewViaExecute(
    SkillExecuteInterface& skill, const PreviewRequest& request,
    PreviewContext& context);

// Converts a PreviewRequest to an ExecuteRequest.
absl::StatusOr<ExecuteRequest> PreviewToExecuteRequest(
    const PreviewRequest& request);

// Converts a PreviewContext to an ExecuteContextView.
//
// NOTE that the returned execute context will only be valid as long as the
// input preview context exists.
absl::StatusOr<ExecuteContextView> PreviewToExecuteContext(
    PreviewContext& context, const EquipmentPack& equipment);
}  // namespace skills
}  // namespace intrinsic

#endif  // INTRINSIC_SKILLS_CC_SKILL_INTERFACE_UTILS_H_
