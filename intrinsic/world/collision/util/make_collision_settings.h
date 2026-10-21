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

#ifndef INTRINSIC_WORLD_UTIL_MAKE_COLLISION_SETTINGS_H_
#define INTRINSIC_WORLD_UTIL_MAKE_COLLISION_SETTINGS_H_

#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/proto/collision_settings.pb.h"

namespace intrinsic {

// Compares two `ObjectOrEntityReference` references `a` and `b`
// lexicographically, returning `true` if `a` is shall come before `b`.
bool CompareReferences(
    const intrinsic_proto::world::ObjectOrEntityReference& a,
    const intrinsic_proto::world::ObjectOrEntityReference& b);

// Compares two CollisionRule messages using a strict weak ordering.
// Orders primarily by 'left' references, secondarily by 'right' references,
// and finally by the collision action (exclusion vs. margin).
bool CompareCollisionRules(
    const intrinsic_proto::world::CollisionSettings::CollisionRule& lhs,
    const intrinsic_proto::world::CollisionSettings::CollisionRule& rhs);

// Sorts the collision_rules repeated field within the given CollisionSettings
// proto using CompareCollisionRules.
void SortCollisionRules(
    intrinsic_proto::world::CollisionSettings& collision_settings);

// Returns a CollisionSettings proto that is equivalent to the given RuleSet.
intrinsic_proto::world::CollisionSettings MakeCollisionSettings(
    const intrinsic_proto::RuleSet& rule_set);

intrinsic_proto::world::CollisionSettings MakeCollisionSettings(
    const object_world::ObjectWorld& world,
    const intrinsic_proto::RuleSet& rule_set);

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_UTIL_MAKE_COLLISION_SETTINGS_H_
