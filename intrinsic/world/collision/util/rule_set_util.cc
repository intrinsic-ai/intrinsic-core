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

#include "intrinsic/world/collision/util/rule_set_util.h"

#include <algorithm>
#include <cstdint>
#include <tuple>
#include <utility>
#include <variant>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "google/protobuf/repeated_field.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/proto/collision_action.pb.h"

namespace intrinsic {

const intrinsic_proto::world::CollisionAction& GetDefaultAction() {
  static const intrinsic_proto::world::CollisionAction* default_action = [] {
    auto* action = new intrinsic_proto::world::CollisionAction();
    constexpr double kDefaultMargin = 0.0;
    action->mutable_margin()->set_hard_margin(kDefaultMargin);
    return action;
  }();
  return *default_action;
}

void RemoveRepeatedRules(intrinsic_proto::RuleSet& rule_set) {
  using Ids = absl::InlinedVector<uint32_t, 2>;
  // Identifies a collision action: two actions are equal if and only if their
  // keys are equal.
  using ActionKey = std::variant<std::monostate, bool, float>;

  const auto sorted_ids =
      [](const google::protobuf::RepeatedField<uint32_t>& ids) {
        Ids sorted(ids.begin(), ids.end());
        absl::c_sort(sorted);
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        return sorted;
      };
  const auto action_key =
      [](const intrinsic_proto::world::CollisionAction& action) -> ActionKey {
    switch (action.action_case()) {
      case intrinsic_proto::world::CollisionAction::kIsExcluded:
        return ActionKey(std::in_place_type<bool>, action.is_excluded());
      case intrinsic_proto::world::CollisionAction::kMargin:
        return ActionKey(std::in_place_type<float>,
                         action.margin().hard_margin());
      case intrinsic_proto::world::CollisionAction::ACTION_NOT_SET:
        return std::monostate();
    }
    return std::monostate();
  };

  absl::flat_hash_set<std::tuple<Ids, Ids, ActionKey>> seen_rules;
  google::protobuf::RepeatedPtrField<intrinsic_proto::Rule> kept_rules;
  for (intrinsic_proto::Rule& rule : *rule_set.mutable_rules()) {
    // Drops the rule if an equal (id_1, id_2, action) rule was seen before.
    if (!seen_rules
             .emplace(sorted_ids(rule.id_1()), sorted_ids(rule.id_2()),
                      action_key(rule.action()))
             .second) {
      continue;
    }
    *kept_rules.Add() = std::move(rule);
  }
  rule_set.mutable_rules()->Swap(&kept_rules);
}

}  // namespace intrinsic
