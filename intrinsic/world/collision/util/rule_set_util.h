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

#ifndef INTRINSIC_WORLD_UTIL_RULE_SET_UTIL_H_
#define INTRINSIC_WORLD_UTIL_RULE_SET_UTIL_H_

#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/proto/collision_action.pb.h"

namespace intrinsic {

// Gets the default action that imposes a hard and soft margin of 0.
const intrinsic_proto::world::CollisionAction& GetDefaultAction();

// Removes the rules from `rule_set` that repeat an earlier rule, which leaves
// the order of the remaining rules unchanged. A rule repeats an earlier one if
// it has the same action and the same entities on each of its two sides.
//
// A rule between A and B is not treated as a repetition of the same rule
// between B and A, although both have the same effect on collision checking
// (rules are matched in both directions).
void RemoveRepeatedRules(intrinsic_proto::RuleSet& rule_set);

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_UTIL_RULE_SET_UTIL_H_
