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

#ifndef INTRINSIC_EXECUTIVE_ENGINE_SKILL_ACTION_H_
#define INTRINSIC_EXECUTIVE_ENGINE_SKILL_ACTION_H_

namespace intrinsic {
namespace executive {

// Represents skill actions that can be dispatched by ClipsSkillDispatcher.
enum class SkillAction {
  Projection = 0,
  Execution,
  PreviewExecution,
  FootprintChecking,
};

}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_ENGINE_SKILL_ACTION_H_
