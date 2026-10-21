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

#include "intrinsic/world/collision/util/make_collision_settings.h"

#include <algorithm>
#include <optional>
#include <string>

#include "absl/log/log.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_creation_utils.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/proto/collision_action.pb.h"
#include "intrinsic/world/proto/collision_settings.pb.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

namespace {

std::optional<std::string> GetLocalNameForEntityById(const World& world,
                                                     EntityId id) {
  auto entity_or_status = world.GetEntityById(id);
  if (!entity_or_status.ok()) {
    return std::nullopt;
  }
  return entity_or_status.value()->GetLocalName();
}

intrinsic_proto::world::ObjectOrEntityReference GetReferenceForEntity(
    AttachmentEntityId entity_id) {
  intrinsic_proto::world::ObjectOrEntityReference result;
  result.mutable_entity()->set_id(
      object_world::ObjectWorldResourceIdForEntity(entity_id).value());
  return result;
}

// Generates a reference to the entity, using the object world to find the
// object and entity name if possible, and falling back to the entity id if
// unavailable. This would ideally returns a reference with both an object and
// entity name if they are unique but can return a combination of name/id for
// both the object and the entity.
intrinsic_proto::world::ObjectOrEntityReference GetReferenceForEntity(
    AttachmentEntityId entity_id, const object_world::ObjectWorld& world) {
  intrinsic_proto::world::ObjectOrEntityReference result;

  const auto& entity_world = world.GetEntityWorld();

  // TODO(stoyang): Use the entity id to find the object, avoiding this loop.
  for (const auto& object : world.GetObjects()) {
    if (!object->GetEntityIds().contains(entity_id)) {
      continue;
    }

    if (object->NameIsGlobalAlias().value_or(false)) {
      result.mutable_object_with_filter()
          ->mutable_reference()
          ->mutable_by_name()
          ->set_object_name(object->GetName().value());
    } else {
      result.mutable_object_with_filter()->mutable_reference()->set_id(
          object->GetId().value());
    }

    const auto entity_name = GetLocalNameForEntityById(entity_world, entity_id);

    bool has_unique_name = false;
    if (entity_name.has_value() && !entity_name->empty()) {
      has_unique_name = true;
      for (const auto& id : object->GetEntityIds()) {
        if (entity_id == id) continue;

        const auto other_name = GetLocalNameForEntityById(entity_world, id);
        if (!other_name.has_value() || *other_name == *entity_name) {
          has_unique_name = false;
          break;
        }
      }
    }

    auto* entity_filter =
        result.mutable_object_with_filter()->mutable_entity_filter();
    if (entity_name.has_value() && !entity_name->empty() && has_unique_name) {
      entity_filter->add_entity_names(*entity_name);
    } else {
      entity_filter->add_entity_references()->set_id(
          object_world::ObjectWorldResourceIdForEntity(entity_id).value());
    }

    return result;
  }

  result.mutable_entity()->set_id(
      object_world::ObjectWorldResourceIdForEntity(entity_id).value());
  return result;
}

int GetReferencePrecedence(
    const intrinsic_proto::world::ObjectOrEntityReference& ref) {
  if (ref.has_entity()) return 0;
  if (ref.has_object()) return 1;
  if (ref.has_object_with_filter()) return 2;
  return 3;
}

bool CompareReferenceLists(
    const google::protobuf::RepeatedPtrField<
        intrinsic_proto::world::ObjectOrEntityReference>& a,
    const google::protobuf::RepeatedPtrField<
        intrinsic_proto::world::ObjectOrEntityReference>& b) {
  return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                      &CompareReferences);
}

bool CompareCollisionActions(const intrinsic_proto::world::CollisionAction& a,
                             const intrinsic_proto::world::CollisionAction& b) {
  if (a.action_case() != b.action_case()) {
    return a.action_case() < b.action_case();
  }
  switch (a.action_case()) {
    case intrinsic_proto::world::CollisionAction::kIsExcluded:
      // Orders `false` (not excluded) before `true` (excluded).
      return a.is_excluded() < b.is_excluded();
    case intrinsic_proto::world::CollisionAction::kMargin:
      return a.margin().hard_margin() < b.margin().hard_margin();
    case intrinsic_proto::world::CollisionAction::ACTION_NOT_SET:
      return false;
  }
  return false;
}

}  // namespace

bool CompareReferences(
    const intrinsic_proto::world::ObjectOrEntityReference& a,
    const intrinsic_proto::world::ObjectOrEntityReference& b) {
  int prec_a = GetReferencePrecedence(a);
  int prec_b = GetReferencePrecedence(b);
  if (prec_a != prec_b) {
    return prec_a < prec_b;
  }

  // If they are the same type, compare contents.
  if (a.has_entity()) {
    return a.entity().id().compare(b.entity().id()) < 0;
  }

  auto compare_object_ref =
      [](const intrinsic_proto::world::ObjectReference& obja,
         const intrinsic_proto::world::ObjectReference& objb) {
        if (obja.has_id()) {
          if (!objb.has_id()) {
            return -1;
          }
          return obja.id().compare(objb.id());
        }
        if (objb.has_id()) {
          return 1;
        }
        return obja.by_name().object_name().compare(
            objb.by_name().object_name());
      };

  if (a.has_object()) {
    return compare_object_ref(a.object(), b.object()) < 0;
  }

  // Both are object_with_filter.
  int obj_cmp = compare_object_ref(a.object_with_filter().reference(),
                                   b.object_with_filter().reference());
  if (obj_cmp != 0) {
    return obj_cmp < 0;
  }

  const auto& efa = a.object_with_filter().entity_filter();
  const auto& efb = b.object_with_filter().entity_filter();

  if (efa.include_base_entity() != efb.include_base_entity()) {
    return efa.include_base_entity();
  }

  if (efa.include_final_entity() != efb.include_final_entity()) {
    return efa.include_final_entity();
  }

  if (efa.include_all_entities() != efb.include_all_entities()) {
    return efa.include_all_entities();
  }

  if (efa.entity_references().size() != efb.entity_references().size()) {
    return efa.entity_references().size() < efb.entity_references().size();
  }

  for (int i = 0; i < efa.entity_references().size(); ++i) {
    int ercmp = efa.entity_references()[i].id().compare(
        efb.entity_references()[i].id());
    if (ercmp != 0) {
      return ercmp < 0;
    }
  }

  if (efa.entity_names().size() != efb.entity_names().size()) {
    return efa.entity_names().size() < efb.entity_names().size();
  }

  for (int i = 0; i < efa.entity_names().size(); ++i) {
    int ercmp = efa.entity_names()[i].compare(efb.entity_names()[i]);
    if (ercmp != 0) {
      return ercmp < 0;
    }
  }

  // References are equal, return false for strict weak ordering.
  return false;
}

bool CompareCollisionRules(
    const intrinsic_proto::world::CollisionSettings::CollisionRule& lhs,
    const intrinsic_proto::world::CollisionSettings::CollisionRule& rhs) {
  if (CompareReferenceLists(lhs.left(), rhs.left())) return true;
  if (CompareReferenceLists(rhs.left(), lhs.left())) return false;

  if (CompareReferenceLists(lhs.right(), rhs.right())) return true;
  if (CompareReferenceLists(rhs.right(), lhs.right())) return false;

  return CompareCollisionActions(lhs.collision_action(),
                                 rhs.collision_action());
}

void SortCollisionRules(
    intrinsic_proto::world::CollisionSettings& collision_settings) {
  std::sort(collision_settings.mutable_collision_rules()->begin(),
            collision_settings.mutable_collision_rules()->end(),
            &CompareCollisionRules);
}

namespace {

template <typename... WolrdType>
intrinsic_proto::world::CollisionSettings MakeCollisionSettingsImpl(
    const intrinsic_proto::RuleSet& rule_set, const WolrdType&... world) {
  intrinsic_proto::world::CollisionSettings collision_settings;
  for (const auto& rule : rule_set.rules()) {
    // If the rule doesn't have any IDs on either side, then at most it could be
    // setting a margin, in which case set the minimum margin in the settings.
    if (rule.id_1().empty() && rule.id_2().empty()) {
      if (rule.action().has_margin()) {
        double margin = collision_settings.minimum_margin();
        collision_settings.set_minimum_margin(
            std::max<double>(margin, rule.action().margin().hard_margin()));
      }
      continue;
    }

    auto* collision_rule = collision_settings.add_collision_rules();
    for (const auto& id : rule.id_1()) {
      *collision_rule->add_left() =
          GetReferenceForEntity(AttachmentEntityId(id), world...);
    }

    std::sort(collision_rule->mutable_left()->begin(),
              collision_rule->mutable_left()->end(), &CompareReferences);

    for (const auto& id : rule.id_2()) {
      *collision_rule->add_right() =
          GetReferenceForEntity(AttachmentEntityId(id), world...);
    }

    std::sort(collision_rule->mutable_right()->begin(),
              collision_rule->mutable_right()->end(), &CompareReferences);

    *collision_rule->mutable_collision_action() = rule.action();

    // For collision settings we expect that 'right' has entries iff 'left' has
    // entries. To signify some entities and match all others you must specify
    // the entities in 'left'.
    if (collision_rule->left_size() == 0 && collision_rule->right_size() != 0) {
      collision_rule->mutable_left()->Swap(collision_rule->mutable_right());
    }
  }

  SortCollisionRules(collision_settings);

  return collision_settings;
}

}  // namespace

intrinsic_proto::world::CollisionSettings MakeCollisionSettings(
    const intrinsic_proto::RuleSet& rule_set) {
  return MakeCollisionSettingsImpl(rule_set);
}

intrinsic_proto::world::CollisionSettings MakeCollisionSettings(
    const object_world::ObjectWorld& world,
    const intrinsic_proto::RuleSet& rule_set) {
  return MakeCollisionSettingsImpl(rule_set, world);
}

}  // namespace intrinsic
