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

#include "intrinsic/world/service/object_world_service.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>  // NOLINT(build/c++11)
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/call_once.h"
#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/synchronization/mutex.h"
#include "google/protobuf/empty.pb.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/util/field_comparator.h"
#include "google/protobuf/util/message_differencer.h"
#include "google/protobuf/wrappers.pb.h"
#include "google/rpc/status.pb.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/storage/dummy_storage.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/icon/proto/cart_space_conversion.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/pose.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/resources/proto/geometric_resource_data.pb.h"
#include "intrinsic/resources/proto/resource_instance.pb.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/skills/footprint_util.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/stats/metrics_utils.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/aggregate_type.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/proto/pb_hash.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/util/unique_id.h"
#include "intrinsic/world/collision/coal_collision_checker.h"
#include "intrinsic/world/collision/collision_checker_utils.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/collision/util/make_collision_settings.h"
#include "intrinsic/world/collision/util/make_rule_set.h"
#include "intrinsic/world/component/attachment_component.h"
#include "intrinsic/world/component/collections_component.h"
#include "intrinsic/world/component/collision_component.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/component/physics_component.h"
#include "intrinsic/world/component/ppr_component.h"
#include "intrinsic/world/component/simulation_component.h"
#include "intrinsic/world/conversion/world_from_object_proto.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/footprint_object_utils.h"
#include "intrinsic/world/objects/frame_internal.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/lazy_object_view.h"
#include "intrinsic/world/objects/object_entity_filter.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_creation_utils.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/object_world_proto_utils.h"
#include "intrinsic/world/objects/simple_transform_node_visitor.h"
#include "intrinsic/world/objects/transform_node_internal.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"
#include "intrinsic/world/proto/world_entity.pb.h"
#include "intrinsic/world/proto/world_updates.pb.h"
#include "intrinsic/world/robot_payload/robot_payload.h"
#include "intrinsic/world/service/objects/object_world_converter.h"
#include "intrinsic/world/service/objects/object_world_updates_utils.h"
#include "intrinsic/world/service/objects/world_comparator.h"
#include "intrinsic/world/service/world_compatibility_service.pb.h"
#include "intrinsic/world/service/world_mutex.h"
#include "intrinsic/world/service/world_storage.h"
#include "intrinsic/world/update_world.h"
#include "intrinsic/world/util/compose_world_util.h"
#include "intrinsic/world/world.h"
#include "opentelemetry/metrics/sync_instruments.h"
#include "re2/re2.h"

ABSL_FLAG(bool, enable_on_change_pubsub, true,
          "Enables the publishing of world states for each of the "
          "worlds maintained by the world service when a change is made.");

namespace intrinsic {
namespace object_world {

using ::intrinsic::world::CreateEntityWorldFromProto;
using ::intrinsic_proto::world::CreateWorldRequest;
using ::intrinsic_proto::world::GetObjectRequest;
using ::intrinsic_proto::world::ListObjectsResponse;
using ::intrinsic_proto::world::ObjectReference;
using ::intrinsic_proto::world::ObjectWorldUpdate;
using ::intrinsic_proto::world::WorldMetadata;

namespace {

constexpr char kWorldIdHintPattern[] = "[_a-zA-Z][_a-zA-Z0-9]*";
const LazyRE2 kWorldIdHintRegexp = {kWorldIdHintPattern};

// Sorts objects or frames by name.
template <typename T>
void SortByName(std::vector<T*>& items) {
  absl::c_sort(items, [](const T* a, const T* b) {
    return a->GetName().value() < b->GetName().value();
  });
}

// Returns the given entity ids of an object sorted by local entity name,
// which are unique within the scope of an object.
template <typename Container>
std::vector<typename Container::value_type> SortedByLocalName(
    const World& world, const Container& entity_ids) {
  using Id = typename Container::value_type;

  // Creates a vector to only call `GetLocalNameForEntityById` once.
  std::vector<std::pair<std::string, Id>> keyed;
  keyed.reserve(entity_ids.size());
  for (const Id& id : entity_ids) {
    keyed.emplace_back(world.GetLocalNameForEntityById(id), id);
  }
  absl::c_sort(keyed,
               [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<Id> result;
  result.reserve(keyed.size());
  for (const auto& [_, id] : keyed) {
    result.push_back(id);
  }
  return result;
}

bool ShouldKeep(const WorldObject& object,
                const WorldHashSet<std::string>& object_ids,
                const WorldHashSet<std::string>& object_names) {
  if (object_ids.empty() && object_names.empty()) {
    return true;
  }

  if (object_names.contains(object.GetName().value())) {
    return true;
  }
  if (object_ids.contains(object.GetId().value())) {
    return true;
  }
  return false;
}

absl::StatusOr<
    google::protobuf::RepeatedPtrField<intrinsic_proto::world::Object>>
AllWorldObjects(const ObjectWorld& world, const std::string& world_id,
                const ::google::protobuf::RepeatedPtrField<ObjectReference>&
                    query_object_refs,
                intrinsic_proto::world::ObjectView view) {
  google::protobuf::RepeatedPtrField<intrinsic_proto::world::Object> result;
  result.Reserve(world.GetObjects().size());

  WorldHashSet<std::string> object_ids;
  WorldHashSet<std::string> object_names;
  const bool should_filter = !query_object_refs.empty();

  // Collect the expected object names and ids.
  for (const auto& query_object_ref : query_object_refs) {
    switch (query_object_ref.object_reference_case()) {
      case ObjectReference::kId:
        object_ids.insert(query_object_ref.id());
        break;
      case ObjectReference::kByName:
        object_names.insert(query_object_ref.by_name().object_name());
        break;
      default:
        return absl::InvalidArgumentError(absl::StrCat(
            "Unsupported object reference case: ", query_object_ref));
    }
  }

  for (const WorldObject* object : world.GetObjects()) {
    if (should_filter && !ShouldKeep(*object, object_ids, object_names)) {
      continue;
    }

    INTR_ASSIGN_OR_RETURN(*result.Add(), ToProto(*object, world_id, view));
  }
  return result;
}

// Creates an entity filter to address current entity with the object. Returns a
// FailedPreconditionError if the entity can't be addressed with the means of
// entities filters.
absl::StatusOr<intrinsic_proto::world::ObjectEntityFilter>
EntityFilterForEntity(const EntityId& entity_id, const WorldObject& object) {
  intrinsic_proto::world::ObjectEntityFilter entity_filter;
  INTR_ASSIGN_OR_RETURN(const auto object_root_entity_id,
                        object.GetRootEntityId());
  if (object_root_entity_id == entity_id) {
    entity_filter.set_include_base_entity(true);
    return entity_filter;
  }
  absl::StatusOr<AttachmentEntityId> last_entity_id =
      object.FinalEntityIfKinematicObjectOrElseRootEntity();
  if (last_entity_id.ok() && *last_entity_id == entity_id) {
    entity_filter.set_include_final_entity(true);
    return entity_filter;
  }

  INTR_ASSIGN_OR_RETURN(auto expected_entity,
                        object.GetEntityWorld().GetEntityById(entity_id));
  for (const auto& some_entity_id : object.GetEntityIds()) {
    if (some_entity_id == entity_id) {
      continue;
    }

    INTR_ASSIGN_OR_RETURN(
        auto some_entity,
        object.GetEntityWorld().GetEntityById(some_entity_id));
    if (some_entity->GetLocalName() == expected_entity->GetLocalName()) {
      return absl::FailedPreconditionError(absl::StrCat(
          "Entity can't be reflected as entity_filter: object=",
          object.GetName().value(), " entity_id=", entity_id.value(),
          " object_root_entity_id=", object_root_entity_id.value(),
          " last_entity_id=",
          last_entity_id.ok() ? last_entity_id->value()
                              : kInvalidEntityId.value(),
          " some other entity[", some_entity_id.value(),
          "] has the same local name \"", expected_entity->GetLocalName(),
          "\""));
    }
  }

  entity_filter.add_entity_names(expected_entity->GetLocalName());
  return entity_filter;
}

// Returns a ReparentObjectRequest that will reattach the object to the correct
// parent object. Returns nullopt if the object is attached to the root.
// A plausible error case is when the reparent request can't be constructed
// depending on where the object is attached to. In this case a
// FailedPreconditionError() is returned which indicates that the
// ObjectEntityFilter message needs to be extended.
absl::StatusOr<std::optional<intrinsic_proto::world::ReparentObjectRequest>>
ExtractReparentRequest(
    const World& world, const WorldObject* object,
    const WorldHashMap<EntityId, const WorldObject*>& entity_id_to_object) {
  intrinsic_proto::world::ReparentObjectRequest reparent;
  // TODO(b/260390260) remove this check and use references by id to objects
  // once resource ids are stable across world decomposition and recomposition
  // into resources
  INTR_ASSIGN_OR_RETURN(bool object_has_global_alias,
                        object->NameIsGlobalAlias());
  if (!object_has_global_alias) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Failed to extract reparent request because object $0 does not have a "
        "globally unique name and thus cannot be referenced in a stable way",
        absl::StrJoin(object->GetFullPathName(), ".",
                      absl::StreamFormatter())));
  }
  *reparent.mutable_object()->mutable_by_name()->mutable_object_name() =
      object->GetName().value();
  INTR_ASSIGN_OR_RETURN(const auto root_entity_id, object->GetRootEntityId(),
                        _.LogError());
  INTR_ASSIGN_OR_RETURN(
      const auto* attachment_component,
      world.GetComponentByEntityId<AttachmentComponent>(root_entity_id),
      _.LogError());

  // Object is attached to root, no reparent request necessary.
  if (attachment_component->GetParentId() == kRootEntityId) {
    return std::nullopt;
  }

  const auto parent_iter =
      entity_id_to_object.find(attachment_component->GetParentId());
  if (parent_iter == entity_id_to_object.end()) {
    return absl::FailedPreconditionError("Parent object not found");
  }

  const auto* parent_object = parent_iter->second;
  // TODO(b/260390260) remove this check and use references by id to objects
  // once resource ids are stable across world decomposition and recomposition
  // into resources
  INTR_ASSIGN_OR_RETURN(bool parent_has_global_alias,
                        parent_object->NameIsGlobalAlias());
  if (!parent_has_global_alias) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Failed to extract reparent request because object $0's parent object "
        "$1 does not have a globally unique name and thus cannot be referenced "
        "in a stable way",
        absl::StrJoin(object->GetFullPathName(), ".", absl::StreamFormatter()),
        absl::StrJoin(parent_object->GetFullPathName(), ".",
                      absl::StreamFormatter())));
  }
  reparent.mutable_new_parent()
      ->mutable_reference()
      ->mutable_by_name()
      ->set_object_name(parent_object->GetName().value());
  INTR_ASSIGN_OR_RETURN(
      *reparent.mutable_new_parent()->mutable_entity_filter(),
      EntityFilterForEntity(attachment_component->GetParentId(),
                            *parent_object));

  return reparent;
}

struct KinematicObjectVisitor : public WorldObjectConstVisitor {
 public:
  absl::Status Visit(const RootObject& root_object) override {
    return absl::OkStatus();
  }
  absl::Status Visit(const PhysicalObject& physical_object) override {
    return absl::OkStatus();
  }
  absl::Status Visit(const KinematicObject& kinematic_object) override {
    INTR_ASSIGN_OR_RETURN(joint_configurations,
                          kinematic_object.GetNamedJointConfigurations());
    INTR_ASSIGN_OR_RETURN(ik_solvers, kinematic_object.GetIkSolvers());
    INTR_ASSIGN_OR_RETURN(are_kinematics_updated,
                          kinematic_object.AreRobotKinematicsUpdated());
    INTR_ASSIGN_OR_RETURN(kinematic_chain,
                          kinematic_object.ExtractRobotKinematicsProperties());
    INTR_ASSIGN_OR_RETURN(mounted_payload,
                          kinematic_object.GetMountedPayload());
    INTR_ASSIGN_OR_RETURN(cartesian_limits,
                          kinematic_object.GetCartesianLimits());
    return absl::OkStatus();
  }

 public:
  WorldHashMap<std::string, eigenmath::VectorXd> joint_configurations;
  std::vector<std::pair<world::ObjectEntityFilter, Pose3d>> kinematic_chain;
  WorldHashMap<AttachmentEntityId,
               WorldHashMap<AttachmentEntityId, std::string>>
      ik_solvers;
  std::optional<RobotPayload> mounted_payload = std::nullopt;
  bool are_kinematics_updated = false;
  CartesianLimits cartesian_limits;
};

// Returns a UpdateKinematicObjectPropertiesRequest that will assign the correct
// named_joint_configurations back into the robot if it is a kinematic object.
// Returns nullopt if the object is not a kinematic object.
absl::StatusOr<std::optional<
    intrinsic_proto::world::UpdateKinematicObjectPropertiesRequest>>
MaybeExtractUpdateKinematicObjectPropertiesRequest(
    const WorldObject* baseline_object, const World& world,
    const WorldObject* object) {
  // TODO(b/260390260) remove this check and use references by id to objects
  // once resource ids are stable across world decomposition and recomposition
  // into resources
  INTR_ASSIGN_OR_RETURN(bool object_has_global_alias,
                        object->NameIsGlobalAlias());
  if (!object_has_global_alias) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Failed to extract kinematic object update request because object $0 "
        "does not have a globally unique name and thus cannot be referenced in "
        "a stable way",
        absl::StrJoin(object->GetFullPathName(), ".",
                      absl::StreamFormatter())));
  }

  KinematicObjectVisitor visitor;
  INTR_RETURN_IF_ERROR(object->Accept(visitor));

  KinematicObjectVisitor baseline_visitor;
  if (baseline_object != nullptr) {
    INTR_RETURN_IF_ERROR(baseline_object->Accept(baseline_visitor));
  }

  intrinsic_proto::world::UpdateKinematicObjectPropertiesRequest object_update;
  bool has_updates = false;

  // Named joint configurations are processed in name order to generate a stable
  // order of updates.
  const auto sorted_names = [](const auto& configurations) {
    auto keys = std::views::keys(configurations);
    std::vector<std::string> names(keys.begin(), keys.end());
    absl::c_sort(names);
    return names;
  };

  // Removes named joint configurations that are in the baseline object but not
  // in the updated object.
  for (const std::string& name :
       sorted_names(baseline_visitor.joint_configurations)) {
    if (!visitor.joint_configurations.contains(name)) {
      object_update.add_named_joint_configurations_to_remove(name);
      has_updates = true;
    }
  }

  // Adds the new named joint configurations.
  for (const std::string& name : sorted_names(visitor.joint_configurations)) {
    const eigenmath::VectorXd& config = visitor.joint_configurations.at(name);
    const auto& baseline_configs = baseline_visitor.joint_configurations;
    bool approx_equal = baseline_configs.contains(name);
    for (int i = 0; i < config.size(); ++i) {
      approx_equal = approx_equal &&
                     std::abs(config[i] - baseline_configs.at(name)[i]) < 1e-6;
    }

    if (!approx_equal) {
      auto* config_proto =
          object_update.add_named_joint_configurations_to_set();
      config_proto->set_name(name);
      VectorXdToRepeatedDouble(config, config_proto->mutable_joint_positions());
      has_updates = true;
    }
  }

  if (!visitor.kinematic_chain.empty() && visitor.are_kinematics_updated) {
    std::vector<intrinsic_proto::world::RobotEntityKinematicProperties::Entity>
        entity_updates;
    entity_updates.reserve(visitor.kinematic_chain.size());
    for (const auto& entity_data : visitor.kinematic_chain) {
      intrinsic_proto::world::RobotEntityKinematicProperties::Entity update;
      if (entity_data.first.EntityNames().size() != 1) {
        return absl::FailedPreconditionError(
            "Every kinematic chain entity filter must specify exactly one "
            "entity.");
      }
      update.set_entity_name(*entity_data.first.EntityNames().begin());
      *update.mutable_parent_t_entity() = ToProto(entity_data.second);
      entity_updates.push_back(std::move(update));
    }
    // The kinematic chain is sorted by entity name to generate a stable order
    // of updates.
    absl::c_sort(entity_updates, [](const auto& a, const auto& b) {
      return a.entity_name() < b.entity_name();
    });
    for (auto& update : entity_updates) {
      *object_update.mutable_robot_kinematics_update()
           ->mutable_robot_entity_updates()
           ->add_entities() = std::move(update);
      has_updates = true;
    }
  }

  // We only define an update for the ik solver if a single solver pair is
  // defined and it is different from the baseline (or the baseline has no
  // solver defined).
  if ((visitor.ik_solvers.size() == 1 &&
       visitor.ik_solvers.begin()->second.size() == 1 &&
       !visitor.ik_solvers.begin()->second.begin()->second.empty()) &&
      ((baseline_visitor.ik_solvers.size() == 1 &&
        baseline_visitor.ik_solvers.begin()->second.size() == 1 &&
        visitor.ik_solvers.begin()->second.begin()->second !=
            baseline_visitor.ik_solvers.begin()->second.begin()->second) ||
       baseline_visitor.ik_solvers.empty())) {
    object_update.mutable_robot_kinematics_update()->set_ik_solver_key(
        visitor.ik_solvers.begin()->second.begin()->second);
    has_updates = true;
  }

  if (visitor.mounted_payload) {
    *object_update.mutable_mounted_payload() =
        ToProto(*visitor.mounted_payload);
    has_updates = true;
  }

  // Only update cartesian limits if they are different from the baseline.
  if (visitor.cartesian_limits != baseline_visitor.cartesian_limits) {
    *object_update.mutable_cartesian_limits() =
        intrinsic::icon::ToProto(visitor.cartesian_limits);
    has_updates = true;
  }

  if (has_updates) {
    *object_update.mutable_object()->mutable_by_name()->mutable_object_name() =
        object->GetName().value();
    return object_update;
  }
  return std::nullopt;
}

template <typename ComponentType, typename ComponentProtoType,
          typename... ExtraArgs>
absl::StatusOr<bool> HasTheSameComponent(
    const WorldEntity* entity, const ComponentProtoType& expected_component,
    ExtraArgs... extra_args) {
  if (entity == nullptr) {
    return false;
  }
  INTR_ASSIGN_OR_RETURN(auto* component, entity->GetComponent<ComponentType>());

  auto component_proto = component->ToProto(extra_args...);
  if (!component_proto.ok()) {
    LOG(WARNING) << "Failed to generate baseline component proto: "
                 << component_proto.status();
    return false;
  }

  // TODO(stoyang): We should not rely on pb_hash and instead we should
  // compare the actual components for equality.
  size_t component_hash = intrinsic::pb_hash{}(*component_proto);
  size_t expected_hash = intrinsic::pb_hash{}(expected_component);
  return component_hash == expected_hash;
}

absl::StatusOr<google::protobuf::RepeatedPtrField<
    intrinsic_proto::world::ObjectWorldUpdate>>
ExtractUpdateEntityPropertiesRequest(const World& baseline_world,
                                     const WorldObject* baseline_object,
                                     const World& world,
                                     const WorldObject* object, bool is_ppr) {
  google::protobuf::RepeatedPtrField<intrinsic_proto::world::ObjectWorldUpdate>
      result;

  std::optional<CollectionsEntityId> object_collections_id =
      object->GetCollectionEntity();
  if (!object_collections_id.has_value()) {
    return absl::InternalError(
        "Tried to extract entity properties of root object");
  }
  INTR_ASSIGN_OR_RETURN(const WorldEntity* collections_entity,
                        world.GetEntityById(*object_collections_id));

  // For each link in the object, add an entity update
  INTR_ASSIGN_OR_RETURN(
      const CollectionsComponent* collections,
      collections_entity->GetComponent<CollectionsComponent>());
  const std::vector<CollectionsMemberEntityId>& links =
      collections->GetCollectionMembers(CollectionsComponent::kLinks);

  for (const auto& link_id : links) {
    INTR_ASSIGN_OR_RETURN(AttachmentEntityId link_attachment_id,
                          world.ValidateEntity<AttachmentEntityId>(link_id));

    INTR_ASSIGN_OR_RETURN(
        intrinsic_proto::world::Entity entity_proto,
        ToProto(link_attachment_id, /*world_id=*/"", *object));

    const WorldEntity* baseline_link_entity = nullptr;
    if (baseline_object != nullptr) {
      const std::optional<CollectionsEntityId> baseline_collections_entity_id =
          baseline_object->GetCollectionEntity();
      INTR_ASSIGN_OR_RETURN(
          const WorldEntity* baseline_collection_entity,
          baseline_world.GetEntityById(*baseline_collections_entity_id));
      INTR_ASSIGN_OR_RETURN(
          const CollectionsComponent* baseline_collection,
          baseline_collection_entity->GetComponent<CollectionsComponent>());
      for (const auto baseline_link_id :
           baseline_collection->GetCollectionMembers(
               CollectionsComponent::kLinks)) {
        INTR_ASSIGN_OR_RETURN(const WorldEntity* link_entity,
                              baseline_world.GetEntityById(baseline_link_id));
        if (link_entity->GetLocalName() == entity_proto.name()) {
          baseline_link_entity = link_entity;
          break;
        }
      }
    }

    intrinsic_proto::world::UpdateEntityPropertiesRequest entity_props;

    entity_props.mutable_entity()
        ->mutable_reference()
        ->mutable_by_name()
        ->set_object_name(object->GetName().value());
    // If this is a PPR object then the link names are stable. If it's not, then
    // we know it's a single entity object, so we can use the base entity.
    if (is_ppr) {
      entity_props.mutable_entity()->mutable_entity_filter()->add_entity_names(
          entity_proto.name());
    } else {
      entity_props.mutable_entity()
          ->mutable_entity_filter()
          ->set_include_base_entity(true);
    }

    bool has_updates = false;
    if (entity_proto.has_geometry_component()) {
      INTR_ASSIGN_OR_RETURN(
          bool has_the_same_geometry,
          HasTheSameComponent<GeometryComponent>(
              baseline_link_entity, entity_proto.geometry_component()));
      if (!has_the_same_geometry) {
        has_updates = true;
        *(entity_props.mutable_geometry_component()) =
            entity_proto.geometry_component();
      }
    }

    if (entity_proto.has_physics_component()) {
      INTR_ASSIGN_OR_RETURN(
          bool has_the_same_physics,
          HasTheSameComponent<PhysicsComponent>(
              baseline_link_entity, entity_proto.physics_component()));
      if (!has_the_same_physics) {
        LOG(WARNING)
            << "Cannot save physics component for entity '"
            << entity_proto.name()
            << "'. It has changed from the baseline. Physics properties should "
               "only be modified at the type level.";
      }
    }

    if (has_updates) {
      *result.Add()->mutable_update_entity_properties() =
          std::move(entity_props);
    }
  }

  return result;
}

struct JointUpdateVisitor : public WorldObjectConstVisitor {
 public:
  absl::Status Visit(const RootObject& root_object) override {
    return absl::OkStatus();
  }
  absl::Status Visit(const PhysicalObject& physical_object) override {
    return absl::OkStatus();
  }
  absl::Status Visit(const KinematicObject& kinematic_object) override {
    INTR_ASSIGN_OR_RETURN(positions, kinematic_object.GetJointPositions());
    INTR_ASSIGN_OR_RETURN(system_limits,
                          kinematic_object.GetJointSystemLimits());
    INTR_ASSIGN_OR_RETURN(application_limits,
                          kinematic_object.GetJointApplicationLimits());
    return absl::OkStatus();
  }

 public:
  std::optional<eigenmath::VectorXd> positions = std::nullopt;
  std::optional<JointLimitsXd> system_limits = std::nullopt;
  std::optional<JointLimitsXd> application_limits = std::nullopt;
};

template <typename T>
bool ContainerDoublesEqual(const T& a, const T& b) {
  if (a.size() != b.size()) return false;
  for (int i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

// Returns true if the limits are equal, and false otherwise.
bool CompareLimits(const JointLimitsXd& a, const JointLimitsXd& b) {
  return ContainerDoublesEqual(a.min_position, b.min_position) &&
         ContainerDoublesEqual(a.max_position, b.max_position) &&
         ContainerDoublesEqual(a.max_velocity, b.max_velocity) &&
         ContainerDoublesEqual(a.max_acceleration, b.max_acceleration) &&
         ContainerDoublesEqual(a.max_torque, b.max_torque) &&
         ContainerDoublesEqual(a.max_jerk, b.max_jerk);
}

// Returns true if all of the non-frame entities in object_a share the same
// pose as their named counterparts in object_b.
absl::StatusOr<bool> NonFrameEntityPosesEqual(const WorldObject* object_a,
                                              const WorldObject* object_b) {
  WorldHashSet<std::string> entity_names;
  WorldHashMap<std::string, Pose3d> a_entity_to_pose;
  WorldHashMap<std::string, Pose3d> b_entity_to_pose;

  const auto process_entity_id =
      [&entity_names](const WorldObject* object, AttachmentEntityId id,
                      WorldHashMap<std::string, Pose3d>& entity_to_pose) {
        INTR_ASSIGN_OR_RETURN(AttachmentEntityId root,
                              object->GetRootEntityId());

        // The root entity's pose may be different because the object was moved.
        if (id == root) return absl::OkStatus();

        // Make sure entity is not a frame.
        if (absl::c_any_of(object->GetFrames(), [id](const Frame* f) {
              return f->GetEntityId() == id;
            })) {
          return absl::OkStatus();
        }

        const World& world = object->GetEntityWorld();
        std::string entity_name = world.GetLocalNameForEntityById(id);
        INTR_ASSIGN_OR_RETURN(
            const AttachmentComponent* attachment,
            world.GetComponentByEntityId<AttachmentComponent>(id));
        entity_names.insert(entity_name);
        entity_to_pose[entity_name] = attachment->GetParentTThis();
        return absl::OkStatus();
      };

  for (const AttachmentEntityId id : object_a->GetEntityIds()) {
    INTR_RETURN_IF_ERROR(process_entity_id(object_a, id, a_entity_to_pose));
  }

  for (const AttachmentEntityId id : object_b->GetEntityIds()) {
    INTR_RETURN_IF_ERROR(process_entity_id(object_b, id, b_entity_to_pose));
  }

  if (a_entity_to_pose.size() != entity_names.size() ||
      b_entity_to_pose.size() != entity_names.size()) {
    return false;
  }

  for (const std::string& name : entity_names) {
    Pose3d pose_a = a_entity_to_pose[name];
    Pose3d pose_b = b_entity_to_pose[name];

    if (!pose_a.isApprox(pose_b, 1e-6)) {
      return false;
    }
  }

  return true;
}

// Returns whether `object` is a KinematicObject.
absl::StatusOr<bool> IsKinematicObject(const WorldObject& object) {
  struct IsKinematicVisitor : public WorldObjectConstVisitor {
    absl::Status Visit(const RootObject& root_object) override {
      return absl::OkStatus();
    }
    absl::Status Visit(const PhysicalObject& physical_object) override {
      return absl::OkStatus();
    }
    absl::Status Visit(const KinematicObject& kinematic_object) override {
      is_kinematic = true;
      return absl::OkStatus();
    }
    bool is_kinematic = false;
  };
  IsKinematicVisitor visitor;
  INTR_RETURN_IF_ERROR(object.Accept(visitor));
  return visitor.is_kinematic;
}

// Returns whether the kinematic state of `object` (joint values and kinematic
// object properties) is memorized as ObjectWorldUpdates. Such updates reference
// the object as a KinematicObject and thus are only applicable if the object
// is a KinematicObject:
absl::StatusOr<bool> ShouldExtractKinematicUpdates(
    const WorldObject& object, const WorldObject* baseline_object) {
  INTR_ASSIGN_OR_RETURN(const bool is_kinematic, IsKinematicObject(object));
  if (!is_kinematic) {
    return false;
  }
  // `baseline_object` should never really be nullptr for a kinematic object,
  // but it happens, we try to extract updates for it anyway.
  if (baseline_object == nullptr) {
    return true;
  }
  INTR_ASSIGN_OR_RETURN(const bool baseline_is_kinematic,
                        IsKinematicObject(*baseline_object));
  LOG_IF(INFO, !baseline_is_kinematic)
      << "Not memorizing kinematic state of '" << object.GetName().value()
      << "' since it is not a kinematic object when composed from asset "
         "instance.";
  return baseline_is_kinematic;
}

// Returns a UpdateObjectJointsRequest that will position the object correctly
// if it is a kinematic object. Returns nullopt if the object is not a kinematic
// object.
absl::StatusOr<std::optional<intrinsic_proto::world::UpdateObjectJointsRequest>>
MaybeExtractUpdateObjectJointsRequest(
    const WorldObject* baseline_object, const World& world,
    const WorldObject* object,
    const WorldHashMap<EntityId, const WorldObject*>& entity_id_to_object) {
  JointUpdateVisitor visitor;
  INTR_RETURN_IF_ERROR(object->Accept(visitor));

  // If we did not get any updatable values we can skip this object.
  if (!visitor.positions.has_value() && !visitor.system_limits.has_value() &&
      !visitor.application_limits.has_value()) {
    return std::nullopt;
  }

  // Kinematic objects without any DOF (e.g., legacy multi-camera objects which
  // only carry a RobotComponent for simulated devices) have no joint state to
  // memorize. An update would contain neither joint positions nor limits and
  // could not be applied when composing the world.
  if (visitor.positions.has_value() && visitor.positions->size() == 0) {
    return std::nullopt;
  }

  JointUpdateVisitor baseline_visitor;
  if (baseline_object != nullptr) {
    INTR_RETURN_IF_ERROR(baseline_object->Accept(baseline_visitor));
  }

  // TODO(b/260390260) remove this check and use references by id to objects
  // once resource ids are stable across world decomposition and recomposition
  // into resources
  INTR_ASSIGN_OR_RETURN(bool object_has_global_alias,
                        object->NameIsGlobalAlias());
  if (!object_has_global_alias) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Failed to extract joint update request because object $0 does not have"
        " a globally unique name and thus cannot be referenced in a stable way",
        absl::StrJoin(object->GetFullPathName(), ".",
                      absl::StreamFormatter())));
  }

  bool has_update = false;

  intrinsic_proto::world::UpdateObjectJointsRequest joint_update;
  *joint_update.mutable_object()->mutable_by_name()->mutable_object_name() =
      object->GetName().value();

  if (visitor.positions.has_value() &&
      (!baseline_visitor.positions.has_value() ||
       !ContainerDoublesEqual(*visitor.positions,
                              *baseline_visitor.positions) ||
       !NonFrameEntityPosesEqual(baseline_object, object).value_or(false))) {
    VectorXdToRepeatedDouble(*visitor.positions,
                             joint_update.mutable_joint_positions());
    has_update = true;
  }

  if (visitor.system_limits.has_value() &&
      (!baseline_visitor.system_limits.has_value() ||
       !CompareLimits(*visitor.system_limits,
                      *baseline_visitor.system_limits))) {
    *joint_update.mutable_joint_system_limits() =
        ToJointLimitsUpdate(*visitor.system_limits);
    has_update = true;
  }

  if (visitor.application_limits.has_value() &&
      (!baseline_visitor.application_limits.has_value() ||
       !CompareLimits(*visitor.application_limits,
                      *baseline_visitor.application_limits))) {
    *joint_update.mutable_joint_application_limits() =
        ToJointLimitsUpdate(*visitor.application_limits);
    has_update = true;
  }

  if (has_update) {
    return joint_update;
  } else {
    return std::nullopt;
  }
}

// Returns an UpdateObjectPropertiesRequest if the object has a simulation
// component associated with it, otherwise returns nullopt.
absl::StatusOr<
    std::optional<intrinsic_proto::world::UpdateObjectPropertiesRequest>>
MaybeExtractUpdateObjectPropertiesRequest(const WorldObject* baseline_object,
                                          const WorldObject& object) {
  // TODO(b/260390260) remove this check and use references by id to objects
  // once resource ids are stable across world decomposition and recomposition
  // into resources
  INTR_ASSIGN_OR_RETURN(bool object_has_global_alias,
                        object.NameIsGlobalAlias());
  if (!object_has_global_alias) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Failed to extract joint update request because object $0 does not "
        "have a globally unique name and thus cannot be referenced in a stable "
        "way",
        absl::StrJoin(object.GetFullPathName(), ".", absl::StreamFormatter())));
  }

  // Nothing to update if the object does not have a simulation component.
  absl::StatusOr<const SimulationComponent*> simulation_component =
      object.GetSimulationComponent();
  if (!simulation_component.ok()) {
    if (absl::IsNotFound(simulation_component.status())) {
      return std::nullopt;
    } else {
      return simulation_component.status();
    }
  }

  // Only changes to `is_static` and `disabled` attributes are saved back as
  // an ObjectWorldUpdate.
  bool baseline_is_static = false;
  bool baseline_disabled = false;
  if (baseline_object != nullptr) {
    absl::StatusOr<const SimulationComponent*> baseline_simulation_component =
        baseline_object->GetSimulationComponent();
    if (baseline_simulation_component.ok()) {
      baseline_is_static = (*baseline_simulation_component)->IsStatic();
      baseline_disabled = (*baseline_simulation_component)->IsDisabled();
    }
  }

  if (baseline_is_static == (*simulation_component)->IsStatic() &&
      baseline_disabled == (*simulation_component)->IsDisabled()) {
    return std::nullopt;
  }

  // Either the baseline object did not have a simulation component or one of
  // the attributes is different. In either case, save the change back as an
  // object world update.
  intrinsic_proto::world::UpdateObjectPropertiesRequest properties_update;
  *properties_update.mutable_object()
       ->mutable_by_name()
       ->mutable_object_name() = object.GetName().value();
  properties_update.mutable_simulation_component()->set_is_static(
      (*simulation_component)->IsStatic());
  properties_update.mutable_simulation_component()->set_disabled(
      (*simulation_component)->IsDisabled());
  return properties_update;
}

// Returns a CreateObjectRequest if the object only has a single geometric
// entity. Also changes the local name of the geometric entity to be 'base_link'
// to better match the semantics when we load the update.
absl::StatusOr<intrinsic_proto::world::CreateObjectRequest>
ExtractCreateObjectRequest(
    World& world, const WorldObject* object,
    const WorldHashMap<EntityId, const WorldObject*>& entity_id_to_object) {
  WorldHashSet<GeometryEntityId> geometric_entity_ids;
  for (AttachmentEntityId entity_id : object->GetEntityIds()) {
    auto geo_entity_id = world.ValidateEntity<GeometryEntityId>(entity_id);
    if (geo_entity_id.ok()) {
      geometric_entity_ids.insert(*geo_entity_id);
    } else {
      LOG_IF(WARNING, absl::c_none_of(object->GetFrames(),
                                      [entity_id](const Frame* frame) {
                                        return frame->GetEntityId() ==
                                               entity_id;
                                      }))
          << "Ignoring non-frame entity (" << entity_id << ") when serializing"
          << "non-PPR object '" << object->GetName() << "'.";
    }
  }
  if (geometric_entity_ids.size() != 1) {
    return absl::UnimplementedError(
        absl::Substitute("Can't serialize object '$0': only single mesh "
                         "objects are supported",
                         object->GetName().value()));
  }
  auto geometric_entity_id = *geometric_entity_ids.begin();
  INTR_ASSIGN_OR_RETURN(auto* geometric_entity,
                        world.GetEntityById(geometric_entity_id));
  INTR_ASSIGN_OR_RETURN(const auto* geo_component,
                        geometric_entity->GetComponent<GeometryComponent>());

  intrinsic_proto::world::CreateObjectRequest create_object;

  create_object.set_name(object->GetName().value());
  INTR_ASSIGN_OR_RETURN(bool name_is_global_alias, object->NameIsGlobalAlias());
  create_object.set_name_is_global_alias(name_is_global_alias);

  intrinsic_proto::world::ObjectReferenceWithEntityFilter* parent_ref =
      create_object.mutable_parent_object();

  parent_ref->mutable_reference()->mutable_by_name()->set_object_name(
      object->GetParent()->GetName().value());

  INTR_ASSIGN_OR_RETURN(auto attachment_component,
                        geometric_entity->GetComponent<AttachmentComponent>());
  AttachmentEntityId parent_entity_id = attachment_component->GetParentId();
  auto parent_obj_iter = entity_id_to_object.find(parent_entity_id);
  if (parent_obj_iter == entity_id_to_object.end()) {
    return absl::InternalError(
        absl::Substitute("Base entity of object '$0' has parent entity "
                         "without a corresponding object!",
                         object->GetName().value()));
  }
  INTR_ASSIGN_OR_RETURN(
      *parent_ref->mutable_entity_filter(),
      EntityFilterForEntity(parent_entity_id, *parent_obj_iter->second));

  INTR_ASSIGN_OR_RETURN(Pose3d parent_t_this, object->GetParentTThis());
  *create_object.mutable_parent_object_t_created_object() =
      ToProto(parent_t_this);

  create_object.mutable_create_single_entity_object()->set_entity_local_name(
      geometric_entity->GetLocalName());
  INTR_ASSIGN_OR_RETURN(*create_object.mutable_create_single_entity_object()
                             ->mutable_geometry_component(),
                        geo_component->ToProto());

  if (auto user_data_protos = object->GetUserDataProtos();
      user_data_protos.ok()) {
    for (const auto& [key, value] : **user_data_protos) {
      (*create_object.mutable_user_data())[key] = value;
    }
  }

  return create_object;
}

// Extract graph of child frames of given a starting frame in form of
// ObjectWorldUpdate operations.
// Note: this does not check whether frames lower in the tree have a
// PPRComponent. Currently it assumes that if the parent had a PPRComponent
// (which is checked before calling this method), then it is safe to traverse
// all the children.
// TODO(b/273549358): Consider whether frames without PPRComponents need to be
// handled at this stage or whether this should remain unsupported.
absl::StatusOr<google::protobuf::RepeatedPtrField<
    intrinsic_proto::world::ObjectWorldUpdate>>
ExtractChildFramesTree(const Frame* frame, const std::string& object_name) {
  google::protobuf::RepeatedPtrField<intrinsic_proto::world::ObjectWorldUpdate>
      updates;
  std::vector<const Frame*> stack;
  stack.push_back(frame);

  while (!stack.empty()) {
    const Frame* current_frame = stack.back();
    stack.pop_back();

    auto* create_frame = updates.Add()->mutable_create_frame();
    create_frame->set_new_frame_name(current_frame->GetName().value());
    INTR_ASSIGN_OR_RETURN(Pose3d parent_t_current_frame,
                          current_frame->GetParentTThis());
    *create_frame->mutable_parent_t_new_frame() =
        ToProto(parent_t_current_frame);
    create_frame->set_designate_as_attachment_frame(
        current_frame->IsAttachmentFrame());

    const Frame* parent_frame = current_frame->GetParentFrame();
    if (parent_frame == nullptr ||
        parent_frame->GetName() == FlangeFrameName() ||
        parent_frame->GetName() == SensorFrameName()) {
      const WorldObject* parent_object = current_frame->GetParent();
      const World& world = parent_object->GetEntityWorld();
      create_frame->mutable_parent_object_with_filter()
          ->mutable_reference()
          ->mutable_by_name()
          ->set_object_name(object_name);

      if (const auto attachment =
              world.GetComponentByEntityId<AttachmentComponent>(
                  current_frame->GetEntityId());
          attachment.ok()) {
        AttachmentEntityId parent_entity_id = attachment.value()->GetParentId();
        INTR_ASSIGN_OR_RETURN(AttachmentEntityId root_entity_id,
                              parent_object->GetRootEntityId());
        if (parent_entity_id != root_entity_id &&
            parent_object->GetEntityIds().contains(parent_entity_id)) {
          INTR_ASSIGN_OR_RETURN(const WorldEntity* parent_entity,
                                world.GetEntityById(parent_entity_id));
          create_frame->mutable_parent_object_with_filter()
              ->mutable_entity_filter()
              ->add_entity_names(parent_entity->GetLocalName());
        }
      }
    } else {
      auto* by_name = create_frame->mutable_parent_frame()->mutable_by_name();
      by_name->set_frame_name(parent_frame->GetName().value());
      by_name->set_object_name(object_name);
    }

    // Child frames are pushed onto the stack in reverse name order so that
    // popping from the back visits siblings in ascending alphabetical order
    // while preserving topological (parent-before-child) pre-order traversal.
    const std::vector<const Frame*> child_frames =
        current_frame->GetChildFramesSorted();
    stack.insert(stack.end(), child_frames.rbegin(), child_frames.rend());
  }
  return updates;
}

// Extract graph of child frames of an object in form of ObjectWorldUpdate
// operations.
absl::StatusOr<google::protobuf::RepeatedPtrField<
    intrinsic_proto::world::ObjectWorldUpdate>>
ExtractChildFrames(const WorldObject* baseline_object,
                   const WorldObject& object) {
  const auto& world = object.GetEntityWorld();
  INTR_ASSIGN_OR_RETURN(std::optional<std::string> obj_resource_name,
                        object.GetResourceName());
  google::protobuf::RepeatedPtrField<intrinsic_proto::world::ObjectWorldUpdate>
      updates;
  // TODO(b/260390260) remove this check and use references by id to objects
  // once resource ids are stable across world decomposition and recomposition
  // into resources
  INTR_ASSIGN_OR_RETURN(bool object_has_global_alias,
                        object.NameIsGlobalAlias());
  if (!object_has_global_alias) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Failed to extract child frames because object $0 does not have a "
        "globally unique name and thus cannot be referenced in a stable way",
        absl::StrJoin(object.GetFullPathName(), ".", absl::StreamFormatter())));
  }

  // Frames are pushed onto the stack in reverse name order so that popping from
  // the back visits siblings in ascending alphabetical order while preserving
  // topological (parent-before-child) pre-order traversal.
  std::vector<const Frame*> frames_to_check = object.GetChildFramesSorted();
  absl::c_reverse(frames_to_check);
  while (!frames_to_check.empty()) {
    const Frame* frame = frames_to_check.back();
    frames_to_check.pop_back();

    // Skip frames that are part of the resource instance itself.
    INTR_ASSIGN_OR_RETURN(const WorldEntity* frameEntity,
                          world.GetEntityById(frame->GetEntityId()));
    if (frameEntity->HasComponent<PPRComponent>()) {
      INTR_ASSIGN_OR_RETURN(const auto* ppr_component,
                            frameEntity->GetComponent<PPRComponent>());
      absl::StatusOr<const Frame*> baseline_frame =
          baseline_object == nullptr
              ? absl::StatusOr<const Frame*>(
                    absl::NotFoundError("No baseline object"))
              : baseline_object->GetFrame(frame->GetName());

      const bool frames_have_parent_object =
          baseline_frame.ok() &&
          (*baseline_frame)->GetParentFrame() == nullptr &&
          frame->GetParentFrame() == nullptr;

      const bool frames_have_parent_frame =
          baseline_frame.ok() &&
          (*baseline_frame)->GetParentFrame() != nullptr &&
          frame->GetParentFrame() != nullptr;

      const bool frames_have_same_parent =
          frames_have_parent_object ||
          (frames_have_parent_frame &&
           (*baseline_frame)->GetParentFrame()->GetName() ==
               frame->GetParentFrame()->GetName());

      const bool frame_matches_baseline =
          ppr_component->ResourceName() == obj_resource_name &&
          frames_have_same_parent;

      if (frame_matches_baseline) {
        // Check if it's movable before trying to move it.
        absl::Status movable = frame->CheckIsMovable();
        if (movable.ok()) {
          INTR_ASSIGN_OR_RETURN(const Pose3d frame_t_object,
                                frame->GetTransform(&object));
          INTR_ASSIGN_OR_RETURN(const Pose3d baseline_frame_t_object,
                                (*baseline_frame)->GetTransform(&object));

          const bool frame_pose_different =
              !frame_t_object.isApprox(baseline_frame_t_object);

          // Emit pose update for frames that have a matching counterpart
          if (frame_pose_different) {
            intrinsic_proto::world::UpdateTransformRequest* update_frame =
                updates.Add()->mutable_update_transform();
            *update_frame->mutable_a_t_b() = ToProto(frame_t_object);
            intrinsic_proto::world::FrameReferenceByName* frame_ref =
                update_frame->mutable_node_a()
                    ->mutable_by_name()
                    ->mutable_frame();
            frame_ref->set_object_name(object.GetName().value());
            frame_ref->set_frame_name(frame->GetName().value());
            update_frame->mutable_node_b()
                ->mutable_by_name()
                ->mutable_object()
                ->set_object_name(object.GetName().value());
            *update_frame->mutable_node_to_update() = update_frame->node_a();
          }

          // Emits frame property update if the attachment frame designation is
          // different.
          const bool frame_attachment_different =
              frame->IsAttachmentFrame() !=
              (*baseline_frame)->IsAttachmentFrame();
          if (frame_attachment_different) {
            intrinsic_proto::world::UpdateFramePropertiesRequest*
                update_frame_properties =
                    updates.Add()->mutable_update_frame_properties();
            update_frame_properties->mutable_is_attachment_frame()->set_value(
                frame->IsAttachmentFrame());
            intrinsic_proto::world::FrameReferenceByName* frame_ref =
                update_frame_properties->mutable_frame()->mutable_by_name();
            frame_ref->set_object_name(object.GetName().value());
            frame_ref->set_frame_name(frame->GetName().value());
          }
        } else {
          LOG(INFO)
              << "Not saving transform information for non-movable frame '"
              << frame->GetName() << "': " << movable;
        }

        // Added child frames should be saved.
        std::vector<const Frame*> ppr_child_frames;
        for (const Frame* child_frame : frame->GetChildFramesSorted()) {
          INTR_ASSIGN_OR_RETURN(
              const WorldEntity* child_frame_entity,
              world.GetEntityById(child_frame->GetEntityId()));
          if (!child_frame_entity->HasComponent<PPRComponent>()) {
            INTR_ASSIGN_OR_RETURN(
                const auto frame_updates,
                ExtractChildFramesTree(child_frame, object.GetName().value()));
            updates.MergeFrom(frame_updates);
          } else {
            ppr_child_frames.push_back(child_frame);
          }
        }
        // Resource child frames are pushed in reverse name order so that
        // popping from `frames_to_check` visits them in ascending name order.
        frames_to_check.insert(frames_to_check.end(), ppr_child_frames.rbegin(),
                               ppr_child_frames.rend());

        continue;
      }
    }

    if (frame->GetName() == FlangeFrameName() ||
        frame->GetName() == SensorFrameName()) {
      LOG(WARNING) << "Encountered frame named '" << frame->GetName()
                   << "' which doesn't belong to its parent object instance '"
                   << object.GetName()
                   << "'. Skipping this virtual frame but saving its children.";
      for (const Frame* child_frame : frame->GetChildFramesSorted()) {
        INTR_ASSIGN_OR_RETURN(
            const auto frame_updates,
            ExtractChildFramesTree(child_frame, object.GetName().value()));
        updates.MergeFrom(frame_updates);
      }
      continue;
    }

    // Otherwise, create this frame (and its descendants) from scratch.
    INTR_ASSIGN_OR_RETURN(
        const auto frame_updates,
        ExtractChildFramesTree(frame, object.GetName().value()));
    updates.MergeFrom(frame_updates);
  }
  return updates;
}

absl::StatusOr<WorldHashMap<EntityId, const WorldObject*>>
ExtractEntityIdToWorldObjectMapping(const ObjectWorld& object_world) {
  const auto& world = object_world.GetEntityWorld();

  WorldHashMap<EntityId, const WorldObject*> entity_id_to_object;
  for (const WorldObject* object : object_world.GetObjects()) {
    for (const auto& entity_id : object->GetEntityIds()) {
      entity_id_to_object[entity_id] = object;
    }

    const auto collection_id = object->GetCollectionEntity();
    if (collection_id.has_value()) {
      INTR_ASSIGN_OR_RETURN(auto members,
                            world.GetCollectionMembers(collection_id.value()));
      for (const auto& member : members) {
        entity_id_to_object[member] = object;
      }
    }
  }
  return entity_id_to_object;
}

absl::StatusOr<WorldHashSet<std::pair<EntityId, EntityId>>>
GetAllCollisionExclusions(const WorldObject& object, const World& world) {
  WorldHashSet<std::pair<EntityId, EntityId>> collision_exclusions;
  for (const AttachmentEntityId& entity_id : object.GetEntityIds()) {
    INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                          world.GetEntityById(entity_id));
    if (!entity->HasComponent<CollisionComponent>()) {
      continue;
    }
    INTR_ASSIGN_OR_RETURN(const auto* collision_component,
                          entity->GetComponent<CollisionComponent>());
    for (const auto& collision_entity_id :
         collision_component->GetExclusions()) {
      collision_exclusions.insert({entity_id, collision_entity_id});
    }
  }
  return collision_exclusions;
}

absl::StatusOr<WorldHashMap<const WorldObject*, WorldHashSet<EntityId>>>
GetAllCollisionExclusionsByObject(
    const WorldObject& object, const World& world,
    const WorldHashMap<EntityId, const WorldObject*>& entity_id_to_object) {
  WorldHashMap<const WorldObject*, WorldHashSet<EntityId>> collision_exclusions;
  for (const AttachmentEntityId& entity_id : object.GetEntityIds()) {
    INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                          world.GetEntityById(entity_id));
    if (!entity->HasComponent<CollisionComponent>()) {
      continue;
    }
    INTR_ASSIGN_OR_RETURN(const auto* collision_component,
                          entity->GetComponent<CollisionComponent>());
    for (const auto& collision_entity_id :
         collision_component->GetExclusions()) {
      INTR_RET_CHECK(entity_id_to_object.contains(collision_entity_id))
          << "Entity id doesn't map to a world object: "
          << collision_entity_id.value();
      const auto* collision_object =
          entity_id_to_object.at(collision_entity_id);
      // Ignore collision exclusions within object.
      if (collision_object->GetName() == object.GetName()) {
        continue;
      }
      collision_exclusions[collision_object].insert(collision_entity_id);
    }
  }
  return collision_exclusions;
}

absl::StatusOr<intrinsic_proto::world::ObjectWorldUpdates>
ExtractCollisionExclusions(
    const WorldObject& object, const World& world,
    const WorldHashMap<EntityId, const WorldObject*>& entity_id_to_object) {
  intrinsic_proto::world::ObjectWorldUpdates updates;
  // TODO(b/260390260) remove this check and use references by id to objects
  // once resource ids are stable across world decomposition and recomposition
  // into resources
  INTR_ASSIGN_OR_RETURN(bool object_has_global_alias,
                        object.NameIsGlobalAlias());
  if (!object_has_global_alias) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Failed to extract collision exclusion because object $0 does not have "
        "a globally unique name and thus cannot be referenced in a stable way",
        absl::StrJoin(object.GetFullPathName(), ".", absl::StreamFormatter())));
  }

  INTR_ASSIGN_OR_RETURN(auto collision_exclusions,
                        GetAllCollisionExclusions(object, world));
  INTR_ASSIGN_OR_RETURN(
      const auto collision_exclusions_by_object,
      GetAllCollisionExclusionsByObject(object, world, entity_id_to_object));
  // Iterates in a stable order.
  auto object_keys = std::views::keys(collision_exclusions_by_object);
  std::vector<const WorldObject*> other_objects(object_keys.begin(),
                                                object_keys.end());
  SortByName(other_objects);
  const std::vector<AttachmentEntityId> sorted_entity_ids =
      SortedByLocalName(world, object.GetEntityIds());
  for (const WorldObject* other_object : other_objects) {
    const std::vector<EntityId> other_entities = SortedByLocalName(
        world, collision_exclusions_by_object.at(other_object));
    // TODO(b/260390260) remove this check and use references by id to objects
    // once resource ids are stable across world decomposition and recomposition
    // into resources
    INTR_ASSIGN_OR_RETURN(bool other_object_has_global_alias,
                          other_object->NameIsGlobalAlias());
    if (!other_object_has_global_alias) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Failed to extract collision exclusion for object $0 because other "
          "object $1 does not have a globally unique name and thus cannot be "
          "referenced in a stable way",
          absl::StrJoin(object.GetFullPathName(), ".", absl::StreamFormatter()),
          absl::StrJoin(other_object->GetFullPathName(), ".",
                        absl::StreamFormatter())));
    }
    intrinsic_proto::world::ToggleCollisionsRequest toggle_collisions_template;
    toggle_collisions_template.set_toggle_mode(
        intrinsic_proto::world::TOGGLE_MODE_DISABLE);
    toggle_collisions_template.mutable_object_a()
        ->mutable_reference()
        ->mutable_by_name()
        ->set_object_name(object.GetName().value());
    toggle_collisions_template.mutable_object_b()
        ->mutable_reference()
        ->mutable_by_name()
        ->set_object_name(other_object->GetName().value());
    // Case 1: The entire object is excluded from collisions with the other
    // object.
    INTR_ASSIGN_OR_RETURN(const bool entire_object_excluded,
                          object.IsCollisionExcluded(*other_object));
    if (entire_object_excluded) {
      auto* toggle_collision =
          updates.add_updates()->mutable_toggle_collisions();
      *toggle_collision = toggle_collisions_template;
      toggle_collision->mutable_object_a()
          ->mutable_entity_filter()
          ->set_include_all_entities(true);
      toggle_collision->mutable_object_b()
          ->mutable_entity_filter()
          ->set_include_all_entities(true);
      continue;
    }

    // Case 2: Entire object is collision excluded from other entity
    for (const auto other_entity_id : other_entities) {
      INTR_ASSIGN_OR_RETURN(const bool other_entity_complete_excluded,
                            object.IsCollisionExcluded(other_entity_id));
      if (other_entity_complete_excluded) {
        auto* toggle_collision =
            updates.add_updates()->mutable_toggle_collisions();
        *toggle_collision = toggle_collisions_template;
        toggle_collision->mutable_object_a()
            ->mutable_entity_filter()
            ->set_include_all_entities(true);
        INTR_ASSIGN_OR_RETURN(
            *toggle_collision->mutable_object_b()->mutable_entity_filter(),
            EntityFilterForEntity(other_entity_id, *other_object),
            _ << " object=" << object.GetName().value());
        // Remove all collision exclusions that are now covered by the
        // toggle_collision update defined above.
        for (const AttachmentEntityId& entity_id : object.GetEntityIds()) {
          collision_exclusions.erase(
              std::make_pair(entity_id, other_entity_id));
        }
      }
    }

    // Case 3: entity is collision excluded from entire other object.
    for (const AttachmentEntityId& entity_id : sorted_entity_ids) {
      INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                            world.GetEntityById(entity_id));
      if (!entity->HasComponent<CollisionComponent>()) {
        continue;
      }
      INTR_ASSIGN_OR_RETURN(const bool entity_complete_excluded,
                            other_object->IsCollisionExcluded(entity_id));
      if (entity_complete_excluded) {
        auto* toggle_collision =
            updates.add_updates()->mutable_toggle_collisions();
        *toggle_collision = toggle_collisions_template;
        INTR_ASSIGN_OR_RETURN(
            *toggle_collision->mutable_object_a()->mutable_entity_filter(),
            EntityFilterForEntity(entity_id, object),
            _ << " other_object=" << other_object->GetName().value());
        toggle_collision->mutable_object_b()
            ->mutable_entity_filter()
            ->set_include_all_entities(true);
        continue;
      }

      // Case 4: Single entity to entity collision exclusion.
      for (const auto& other_entity_id : other_entities) {
        // Ignore entities which aren't collision excluded from the other
        // entity. The collision_exclusions table also tracks which collision
        // exclusions are not yet covered by the current list of updates.
        if (!collision_exclusions.contains(
                std::make_pair(entity_id, other_entity_id))) {
          continue;
        }
        auto* toggle_collision =
            updates.add_updates()->mutable_toggle_collisions();
        *toggle_collision = toggle_collisions_template;
        INTR_ASSIGN_OR_RETURN(
            *toggle_collision->mutable_object_a()->mutable_entity_filter(),
            EntityFilterForEntity(entity_id, object));
        INTR_ASSIGN_OR_RETURN(
            *toggle_collision->mutable_object_b()->mutable_entity_filter(),
            EntityFilterForEntity(other_entity_id, *other_object));
      }
    }
  }

  return updates;
}

absl::StatusOr<intrinsic_proto::world::ObjectWorldUpdates>
ExtractResourceDeletions(const World& baseline_entity_world,
                         const World& current_entity_world) {
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<const ObjectWorld> baseline_world,
                        ObjectWorld::CreateView(baseline_entity_world));
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<const ObjectWorld> current_world,
                        ObjectWorld::CreateView(current_entity_world));

  intrinsic_proto::world::ObjectWorldUpdates deletion_updates;
  for (const WorldObject* baseline_object :
       baseline_world->GetObjectsSorted()) {
    absl::StatusOr<const WorldObject*> current_object =
        current_world->GetObject(baseline_object->GetName());
    if (!current_object.ok()) {
      // TODO: b/372533357 -- This should not be necessary once we distinguish
      // between external and internal APIs. In general, objects should only be
      // created or deleted via internal APIs, where we can guard against
      // modifying the world directly to delete resources.

      // We expect the object wasn't found. Other errors should be reported.
      if (!absl::IsNotFound(current_object.status())) {
        return current_object.status();
      }

      // Look for any other object that matches the same PPR information. In
      // this case, the object would have been renamed.
      for (const WorldObject* co : current_world->GetObjects()) {
        if (co->GetResourceName() == baseline_object->GetResourceName()) {
          current_object = co;
        }
      }

      if (current_object.ok()) {
        // If we can find it, then we simply rename it.
        intrinsic_proto::world::ObjectWorldUpdate* rename_object_update =
            deletion_updates.mutable_updates()->Add();
        intrinsic_proto::world::UpdateObjectNameRequest* rename_object =
            rename_object_update->mutable_update_object_name();
        rename_object->mutable_object()->mutable_by_name()->set_object_name(
            baseline_object->GetName().value());
        rename_object->set_name((*current_object)->GetName().value());
        rename_object->set_name_is_global_alias(true);
      } else {
        // If we still can't find it then force delete it. We emit deletion
        // updates first, which means that this will have no children right
        // after composition. Each object that doesn't exist in the baseline
        // world can be safely deleted this way.
        intrinsic_proto::world::ObjectWorldUpdate* delete_object_update =
            deletion_updates.mutable_updates()->Add();
        intrinsic_proto::world::DeleteObjectRequest* delete_object =
            delete_object_update->mutable_delete_object();
        delete_object->mutable_object()->mutable_by_name()->set_object_name(
            baseline_object->GetName().value());
        delete_object->set_force(true);

        // No frames matter after this point.
        continue;
      }
    }

    // Traverse child frames in breadth-first ascending name order, recording
    // the ones that were deleted. Resource frames cannot be deleted through
    // the API when asset frame edits are disabled, but deletions are still
    // emitted when such edits are enabled or when the baseline asset differs
    // from the one the world was composed from.
    std::vector<const Frame*> baseline_frames =
        baseline_object->GetChildFramesSorted();
    std::deque<const Frame*> baseline_frame_queue(baseline_frames.begin(),
                                                  baseline_frames.end());
    while (!baseline_frame_queue.empty()) {
      const Frame* baseline_frame = baseline_frame_queue.front();
      baseline_frame_queue.pop_front();

      const absl::StatusOr<const Frame*> current_frame =
          (*current_object)->GetFrame(baseline_frame->GetName());

      bool should_delete_frame = false;
      if (!current_frame.ok()) {
        // We expect the frame wasn't found. Other errors should be reported.
        if (!current_frame.ok() && !absl::IsNotFound(current_frame.status())) {
          return current_frame.status();
        }

        should_delete_frame = true;
      } else if ((*current_frame)->GetParentFrame() ==
                     baseline_frame->GetParentFrame() ||
                 ((*current_frame)->GetParentFrame() != nullptr &&
                  baseline_frame->GetParentFrame() != nullptr &&
                  (*current_frame)->GetParentFrame()->GetName() ==
                      baseline_frame->GetParentFrame()->GetName())) {
        INTR_ASSIGN_OR_RETURN(
            const WorldEntity* current_frame_entity,
            (*current_frame)
                ->GetEntityWorld()
                .GetEntityById((*current_frame)->GetEntityId()));
        const absl::StatusOr<const PPRComponent*> current_frame_ppr =
            current_frame_entity->GetComponent<PPRComponent>();
        INTR_ASSIGN_OR_RETURN(std::optional<std::string> obj_resource_name,
                              (*current_frame)->GetParent()->GetResourceName());
        should_delete_frame =
            !current_frame_ppr.ok() ||
            (*current_frame_ppr)->ResourceName() != obj_resource_name;
      } else {
        // baseline frame and current frame don't share a parent. That means
        // 'current_frame' will be recreated in the updates for
        // ExtractChildFrames, so delete it from the baseline.
        should_delete_frame = true;
      }

      if (should_delete_frame) {
        // For each frame that was deleted, delete it and all of its children.
        intrinsic_proto::world::ObjectWorldUpdate* delete_frame_update =
            deletion_updates.mutable_updates()->Add();
        intrinsic_proto::world::DeleteFrameRequest* delete_frame =
            delete_frame_update->mutable_delete_frame();
        intrinsic_proto::world::FrameReferenceByName* frame_by_name =
            delete_frame->mutable_frame()->mutable_by_name();
        frame_by_name->set_object_name((*current_object)->GetName().value());
        frame_by_name->set_frame_name(baseline_frame->GetName().value());
        delete_frame->set_force(true);
      } else {
        for (const Frame* child_frame :
             baseline_frame->GetChildFramesSorted()) {
          baseline_frame_queue.push_back(child_frame);
        }
      }
    }
  }

  return deletion_updates;
}

// TODO(b/259728627) Remove this method once the PoseGenerator publishes object
// world updates
absl::Status HandleEntityWorldUpdates(
    const ::google::protobuf::RepeatedPtrField<
        ::intrinsic_proto::world::WorldUpdate>& updates,
    intrinsic::World* world) {
  if (updates.empty()) {
    return absl::OkStatus();
  }

  intrinsic_proto::world::internal::WorldServerUpdates outgoing_entity_updates;
  for (const intrinsic_proto::world::WorldUpdate& update : updates) {
    // Need to map from WorldUpdate to WorldServerUpdate, not all updates are
    // supported.
    auto* outgoing_update = outgoing_entity_updates.mutable_updates()->Add();
    if (update.has_set_robot_dof_values()) {
      *(outgoing_update->mutable_set_robot_dof_values()) =
          update.set_robot_dof_values();
    } else if (update.has_reparent_entity()) {
      *(outgoing_update->mutable_reparent_entity()) = update.reparent_entity();
    } else if (update.has_update_attachment()) {
      *(outgoing_update->mutable_update_attachment()) =
          update.update_attachment();
    } else if (update.has_set_named_configuration()) {
      *(outgoing_update->mutable_set_named_configuration()) =
          update.set_named_configuration();
    } else if (update.has_add_label()) {
      *(outgoing_update->mutable_add_label()) = update.add_label();
    } else if (update.has_remove_label()) {
      *(outgoing_update->mutable_remove_label()) = update.remove_label();
    } else if (update.has_set_dof_values()) {
      *(outgoing_update->mutable_set_dof_values()) = update.set_dof_values();
    } else {
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported world update: ", update.update_case()));
    }
  }

  return intrinsic::UpdateWorld(outgoing_entity_updates, world);
}

bool IsOnlyStateChange(const ::google::protobuf::RepeatedPtrField<
                       ::intrinsic_proto::world::WorldUpdate>& updates) {
  for (const intrinsic_proto::world::WorldUpdate& world_update : updates) {
    if (!intrinsic::IsOnlyStateChange(world_update)) {
      return false;
    }
  }

  return true;
}

constexpr absl::string_view kRemainingEntitiesAfterResourceExtractionName =
    "intrinsic/object_world_service/"
    "remaining_entities_after_resource_extraction";

// Instruments in this file are intentionally released so they remain alive
// for the program's lifetime, avoiding global destructor ordering errors on
// service destruction. Each is a one-time allocation, so this doesn't leak.
opentelemetry::metrics::Counter<uint64_t>&
RemainingEntitiesAfterResourceExtraction() {
  static opentelemetry::metrics::Counter<uint64_t>* counter =
      stats::GetMeter()
          ->CreateUInt64Counter(
              kRemainingEntitiesAfterResourceExtractionName,
              "Count of entities remaining in the world after resource "
              "extraction.",
              "1")
          .release();
  return *counter;
}

void RegisterMetrics() {
  static absl::once_flag called;
  absl::call_once(called, []() {
    // Eagerly initialize the instruments to register them. The counter only
    // records counts > 0, so add 0 to export its time series from the start.
    RemainingEntitiesAfterResourceExtraction().Add(0);
    RegisterObjectWorldUpdatesMetrics();
  });
}

absl::StatusOr<intrinsic_proto::world::World> BuildWorldProto(
    const std::string& world_id, WorldAndMutex& world_ptr,
    const intrinsic_proto::world::WorldView& world_view)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(world_ptr.mtx);

}  // namespace

absl::StatusOr<std::unique_ptr<ObjectWorldService>>
ObjectWorldService::CreateService(
    WorldStorage& world_storage,
    absl::AnyInvocable<std::unique_ptr<GeometryLibrary>(grpc::ServerContext*)
                           const>
        make_geo_lib) {
  std::future<WorldStorage*> world_storage_future = std::async(
      std::launch::async, [&world_storage]() { return &world_storage; });
  return CreateService(std::move(world_storage_future),
                       std::move(make_geo_lib));
}

absl::StatusOr<std::unique_ptr<ObjectWorldService>>
ObjectWorldService::CreateService(
    std::future<WorldStorage*> world_storage,
    absl::AnyInvocable<std::unique_ptr<GeometryLibrary>(grpc::ServerContext*)
                           const>
        make_geo_lib) {
  auto service = absl::WrapUnique(new ObjectWorldService(
      std::move(world_storage), std::move(make_geo_lib)));

  // If on-change pubsub is enabled, we need to add callbacks to the world store
  // for creating/deleting publishers.
  if (absl::GetFlag(FLAGS_enable_on_change_pubsub)) {
    service->WorldStore().SetOnDeleteWorldCallback(
        [service = service.get()](absl::string_view world_id,
                                  const std::shared_ptr<WorldAndMutex>& world)
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(world->mtx) {
              service->DeletePublisherForWorld(world_id, world);
            });

    service->WorldStore().SetOnWorldChangeCallback(
        [service = service.get()](absl::string_view world_id,
                                  const std::shared_ptr<WorldAndMutex>& world,
                                  bool has_state_change)
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(world->mtx) {
              service->OnWorldChanged(world_id, world, has_state_change);
            });

    LOG(INFO) << "On-change publisher enabled.";
  } else {
    LOG(INFO) << "On-change publisher is not enabled.";
  }

  return service;
}

ObjectWorldService::ObjectWorldService(
    std::future<WorldStorage*> world_storage,
    absl::AnyInvocable<std::unique_ptr<GeometryLibrary>(grpc::ServerContext*)
                           const>
        make_geo_lib)
    : world_storage_future_(std::move(world_storage)),
      make_geo_lib_(std::move(make_geo_lib)) {
  RegisterMetrics();
}

WorldStorage& ObjectWorldService::WorldStore() {
  absl::MutexLock l(world_storage_mutex_);
  if (world_storage_ == nullptr) {
    world_storage_ = world_storage_future_.get();
  }
  return *world_storage_;
}

std::unique_ptr<GeometryLibrary> ObjectWorldService::GeoLib(
    grpc::ServerContext* context) const {
  return make_geo_lib_(context);
}

grpc::Status ObjectWorldService::GetWorld(
    grpc::ServerContext* context,
    const intrinsic_proto::world::GetWorldRequest* request,
    intrinsic_proto::world::World* response) {
  const stats::ScopedSpan span("ObjectWorldService/GetWorld", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(
      *response,
      BuildWorldProto(request->world_id(), *world_ptr, request->view()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::CloneWorld(
    grpc::ServerContext* context,
    const intrinsic_proto::world::CloneWorldRequest* request,
    intrinsic_proto::world::WorldMetadata* response) {
  const stats::ScopedSpan span("ObjectWorldService/CloneWorld", context);

  absl::WriterMutexLock worlds_lock(WorldStore().worlds_mtx_);

  // Grab a copy of the existing world pointer
  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> base_world_ptr,
                             WorldStore().GetWorldLocked(request->world_id()));

  if (!request->cloned_world_id().empty() &&
      !WorldStore().ValidCloneDestination(request->cloned_world_id())) {
    return ToGrpcStatus(absl::InvalidArgumentError(absl::StrFormat(
        "World ID %s is an invalid clone target", request->cloned_world_id())));
  }

  std::string clone_tag = "_Clone_";
  if (!request->cloned_world_hint().empty()) {
    if (!RE2::FullMatch(request->cloned_world_hint(), *kWorldIdHintRegexp)) {
      return InvalidArgumentErrorBuilderGrpc().LogError()
             << "'" << request->cloned_world_hint()
             << "' is not a valid cloned_world_hint.";
    }
    absl::StrAppend(&clone_tag, request->cloned_world_hint(), "_");
  }

  const std::string updated_world_id =
      request->cloned_world_id().empty()
          ? absl::StrCat(request->world_id(), clone_tag, WebSafeUuid())
          : std::string(request->cloned_world_id());

  LOG(INFO) << "Cloning world: " << request->world_id() << " into "
            << updated_world_id;

  // If we are cloning onto the same id, then it's a no-op and we just return
  // early and skip the rest of the logic.
  if (updated_world_id == request->world_id()) {
    const stats::ScopedSpan span("ObjectWorldService/CloneWorld/NoOp", context);

    absl::ReaderMutexLock lock(*base_world_ptr->mtx);
    INTR_ASSIGN_OR_RETURN_GRPC(
        *response, ToProtoLocked(*base_world_ptr, updated_world_id));
    return grpc::Status::OK;
  }

  std::shared_ptr<WorldAndMutex> world_ptr;

  auto maybe_world_ptr = WorldStore().GetWorldLocked(updated_world_id);

  // Variables to copy `base_world` state while it is locked.
  absl::Time base_world_last_update;
  std::string base_world_user_tag;

  if (maybe_world_ptr.ok()) {
    if (!request->allow_overwrite()) {
      return AlreadyExistsErrorBuilderGrpc().LogError()
             << "World with id '" << updated_world_id << "' already exists";
    }
    const stats::ScopedSpan span("ObjectWorldService/CloneWorld/Exchange",
                                 context);

    // The target world exists, so clone the base world and swap it for the
    // existing one.
    std::unique_ptr<Publisher> old_publisher;

    {  // Grab the publisher from the existing world.
      absl::MutexLock lock(*(*maybe_world_ptr)->mtx);
      old_publisher = std::move((*maybe_world_ptr)->publisher);
    }
    {  // Create a new world from the base world.
      absl::ReaderMutexLock lock(*base_world_ptr->mtx);
      world_ptr =
          std::make_shared<WorldAndMutex>(base_world_ptr->world.Clone());
      base_world_last_update = base_world_ptr->LastUpdate();
      base_world_user_tag = base_world_ptr->user_tag;
    }
    {  // Copy over the publisher from the old world.
      absl::MutexLock lock(*world_ptr->mtx);
      world_ptr->publisher = std::move(old_publisher);
    }

    // Swap the new world for the old one in the world store.
    INTR_ASSIGN_OR_RETURN_GRPC(
        auto _, WorldStore().ExchangeWorldLocked(updated_world_id, world_ptr,
                                                 /*skip_compat_check=*/true));
  } else if (absl::IsNotFound(maybe_world_ptr.status())) {
    const stats::ScopedSpan span("ObjectWorldService/CloneWorld/Add", context);

    absl::ReaderMutexLock lock(*base_world_ptr->mtx);

    // The target world does not exist, so add a new world from a clone of the
    // base world.
    INTR_ASSIGN_OR_RETURN_GRPC(
        world_ptr, WorldStore().AddWorldLocked(updated_world_id,
                                               base_world_ptr->world.Clone(),
                                               /*skip_compat_check=*/true));
    base_world_last_update = base_world_ptr->LastUpdate();
    base_world_user_tag = base_world_ptr->user_tag;

  } else {
    // Other errors are raised to the caller.
    return ToGrpcStatus(maybe_world_ptr.status());
  }

  absl::WriterMutexLock writer_lock(*world_ptr->mtx);
  world_ptr->UpdateTimestamp(base_world_last_update);
  if (!request->user_tag().empty()) {
    world_ptr->user_tag = request->user_tag();
  } else {
    world_ptr->user_tag = base_world_user_tag;
  }

  INTR_ASSIGN_OR_RETURN_GRPC(*response,
                             ToProtoLocked(*world_ptr, updated_world_id));

  if (maybe_world_ptr.ok()) {
    // If we were cloning onto an existing world, then we need to notify the
    // world store that the world has changed.
    WorldStore().MarkWorldAsChanged(updated_world_id, world_ptr,
                                    /*has_state_change=*/true);
  } else {
    WorldStore().MarkWorldForSaving(updated_world_id, world_ptr);
  }

  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::SwapWorld(
    grpc::ServerContext* context,
    const intrinsic_proto::world::SwapWorldRequest* request,
    google::protobuf::Empty* response) {
  const stats::ScopedSpan span("ObjectWorldService/SwapWorld", context);

  absl::WriterMutexLock worlds_lock(WorldStore().worlds_mtx_);

  if (!WorldStore().ValidCloneDestination(request->target_world_id())) {
    return ToGrpcStatus(absl::InvalidArgumentError(absl::StrFormat(
        "World ID %s is an invalid swap target", request->target_world_id())));
  }

  INTR_RETURN_IF_ERROR_GRPC(WorldStore().SwapWorldLocked(
      request->world_id(), request->target_world_id()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::DeleteWorld(
    grpc::ServerContext* context,
    const intrinsic_proto::world::DeleteWorldRequest* request,
    google::protobuf::Empty* response) {
  const stats::ScopedSpan span("ObjectWorldService/DeleteWorld", context);

  INTR_RETURN_IF_ERROR_GRPC(WorldStore().DeleteWorld(request->world_id()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::ListWorlds(
    grpc::ServerContext* context,
    const intrinsic_proto::world::ListWorldsRequest* request,
    intrinsic_proto::world::ListWorldsResponse* response) {
  const stats::ScopedSpan span("ObjectWorldService/ListWorlds", context);

  for (const auto& [world_id, world_ptr] : WorldStore().GetAllWorlds()) {
    intrinsic_proto::world::WorldMetadata* world_proto =
        response->add_world_metadatas();
    world_proto->set_id(world_id);

    absl::ReaderMutexLock lock(*world_ptr->mtx);
    world_proto->set_world_structure_hash(world_ptr->world_structure_hash);
    world_proto->set_user_tag(world_ptr->user_tag);
  }

  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::CreateWorld(grpc::ServerContext* context,
                                             const CreateWorldRequest* request,
                                             WorldMetadata* response) {
  const stats::ScopedSpan span("ObjectWorldService/CreateWorld", context);

  ListObjectsResponse list_objects_response;
  *list_objects_response.mutable_objects() = request->world().objects();

  INTR_ASSIGN_OR_RETURN_GRPC(
      World created_world,
      CreateEntityWorldFromProto(list_objects_response,
                                 request->world().collision_settings()));

  absl::WriterMutexLock worlds_lock(WorldStore().worlds_mtx_);

  const std::string& requested_id = request->world().world_metadata().id();
  std::string world_id;
  if (!requested_id.empty() &&
      !WorldStore().GetWorldLocked(requested_id).ok()) {
    world_id = requested_id;
  } else {
    const std::string prefix = requested_id.empty() ? "World" : requested_id;
    do {
      world_id = absl::StrCat(prefix, "_", WebSafeUuid());
    } while (WorldStore().GetWorldLocked(world_id).ok());
  }

  INTR_ASSIGN_OR_RETURN_GRPC(
      std::shared_ptr<WorldAndMutex> world_ptr,
      WorldStore().AddWorldLocked(world_id, std::move(created_world),
                                  /*skip_compat_check=*/false));

  absl::WriterMutexLock writer_lock(*world_ptr->mtx);
  world_ptr->user_tag = request->world().world_metadata().user_tag();
  world_ptr->UpdateTimestamp();

  INTR_ASSIGN_OR_RETURN_GRPC(*response, ToProtoLocked(*world_ptr, world_id));

  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::GetObject(
    grpc::ServerContext* context, const GetObjectRequest* request,
    intrinsic_proto::world::Object* response) {
  const stats::ScopedSpan span("ObjectWorldService/GetObject", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::MutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(const auto world, world_ptr->GetObjectWorld());

  const WorldObject* object = nullptr;
  switch (request->object_query_case()) {
    case GetObjectRequest::ObjectQueryCase::kObject: {
      INTR_ASSIGN_OR_RETURN_GRPC(
          object, GetObjectByReference(*world, request->object()));
      break;
    }
    case GetObjectRequest::ObjectQueryCase::kResourceHandleName: {
      INTR_ASSIGN_OR_RETURN_GRPC(object, world->GetObjectForSceneObjectInstance(
                                             request->resource_handle_name()));
      break;
    }
    case GetObjectRequest::ObjectQueryCase::kObjectFullPath: {
      std::vector<WorldObjectName> names(
          request->object_full_path().object_names().begin(),
          request->object_full_path().object_names().end());
      INTR_ASSIGN_OR_RETURN_GRPC(object, world->GetObjectByFullPath(names));
      break;
    }
    case GetObjectRequest::ObjectQueryCase::OBJECT_QUERY_NOT_SET:
      return ToGrpcStatus(absl::InvalidArgumentError(
          "GetObjectRequest.object_query must be set."));
  }

  INTR_ASSIGN_OR_RETURN_GRPC(
      *response, ToProto(*object, request->world_id(), request->view()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::ListObjects(
    grpc::ServerContext* context,
    const intrinsic_proto::world::ListObjectsRequest* request,
    intrinsic_proto::world::ListObjectsResponse* response) {
  const stats::ScopedSpan span("ObjectWorldService/ListObjects", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::MutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(const auto world, world_ptr->GetObjectWorld());

  INTR_ASSIGN_OR_RETURN_GRPC(
      *response->mutable_objects(),
      AllWorldObjects(*world, request->world_id(), request->query_objects(),
                      request->view()));

  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::CreateObject(
    grpc::ServerContext* context,
    const intrinsic_proto::world::CreateObjectRequest* request,
    intrinsic_proto::world::Object* response) {
  const stats::ScopedSpan span("ObjectWorldService/CreateObject", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  INTR_ASSIGN_OR_RETURN_GRPC(
      const WorldObject* object,
      HandleCreateObjectRequest(*world, *request, GeoLib(context).get()));

  world_ptr->UpdateTimestamp();
  world_ptr->UpdateStructureHash();
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/false);

  INTR_ASSIGN_OR_RETURN_GRPC(
      *response, ToProto(*object, request->world_id(), request->view()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::DeleteObject(
    grpc::ServerContext* context,
    const intrinsic_proto::world::DeleteObjectRequest* request,
    google::protobuf::Empty* response) {
  const stats::ScopedSpan span("ObjectWorldService/DeleteObject", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  INTR_RETURN_IF_ERROR_GRPC(HandleDeleteObjectRequest(*world, *request));

  world_ptr->UpdateTimestamp();
  world_ptr->UpdateStructureHash();
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/false);

  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::UpdateObject(
    grpc::ServerContext* context,
    const intrinsic_proto::world::UpdateObjectRequest* request,
    intrinsic_proto::world::Object* response) {
  const stats::ScopedSpan span("ObjectWorldService/UpdateObject", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld(),
                             _.LogError());

  INTR_ASSIGN_OR_RETURN_GRPC(
      const WorldObject* object,
      HandleUpdateObjectRequest(*world, *request, GeoLib(context).get()),
      _.LogError());

  // `UpdateObject` can change the internal structure of the object world, so
  // we need to update the timestamp and structure hash.
  world_ptr->UpdateTimestamp();
  world_ptr->UpdateStructureHash();
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/false);

  INTR_ASSIGN_OR_RETURN_GRPC(
      *response, ToProto(*object, request->world_id(), request->view()),
      _.LogError());

  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::UpdateObjectName(
    grpc::ServerContext* context,
    const intrinsic_proto::world::UpdateObjectNameRequest* request,
    intrinsic_proto::world::Object* response) {
  const stats::ScopedSpan span("ObjectWorldService/UpdateObjectName", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  INTR_ASSIGN_OR_RETURN_GRPC(const WorldObject* object,
                             HandleUpdateObjectNameRequest(*world, *request));

  world_ptr->UpdateTimestamp();
  world_ptr->UpdateStructureHash();
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/false);

  INTR_ASSIGN_OR_RETURN_GRPC(
      *response, ToProto(*object, request->world_id(), request->view()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::ReparentObject(
    grpc::ServerContext* context,
    const intrinsic_proto::world::ReparentObjectRequest* request,
    intrinsic_proto::world::Object* response) {
  const stats::ScopedSpan span("ObjectWorldService/ReparentObject", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  INTR_ASSIGN_OR_RETURN_GRPC(const WorldObject* object,
                             HandleReparentObjectRequest(*world, *request));

  world_ptr->UpdateTimestamp();
  world_ptr->UpdateStructureHash();
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/false);

  INTR_ASSIGN_OR_RETURN_GRPC(
      *response, ToProto(*object, request->world_id(), request->view()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::ToggleCollisions(
    grpc::ServerContext* context,
    const intrinsic_proto::world::ToggleCollisionsRequest* request,
    intrinsic_proto::world::Objects* response) {
  const stats::ScopedSpan span("ObjectWorldService/ToggleCollisions", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  INTR_ASSIGN_OR_RETURN_GRPC(
      (std::pair<const WorldObject*, const WorldObject*> objects),
      HandleToggleCollisionsRequest(*world, *request));

  world_ptr->UpdateTimestamp();
  world_ptr->UpdateStructureHash();
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/false);

  INTR_ASSIGN_OR_RETURN_GRPC(
      *response->add_objects(),
      ToProto(*objects.first, request->world_id(), request->view()));
  INTR_ASSIGN_OR_RETURN_GRPC(
      *response->add_objects(),
      ToProto(*objects.second, request->world_id(), request->view()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::UpdateObjectJoints(
    grpc::ServerContext* context,
    const intrinsic_proto::world::UpdateObjectJointsRequest* request,
    intrinsic_proto::world::Object* response) {
  const stats::ScopedSpan span("ObjectWorldService/UpdateObjectJoints",
                               context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  INTR_ASSIGN_OR_RETURN_GRPC(const KinematicObject* object,
                             HandleUpdateObjectJointsRequest(*world, *request));

  world_ptr->UpdateTimestamp();
  if (request->has_joint_application_limits() ||
      request->has_joint_system_limits()) {
    world_ptr->UpdateStructureHash();
  }
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/true);

  INTR_ASSIGN_OR_RETURN_GRPC(
      *response, ToProto(*object, request->world_id(), request->view()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::UpdateKinematicObjectProperties(
    grpc::ServerContext* context,
    const intrinsic_proto::world::UpdateKinematicObjectPropertiesRequest*
        request,
    intrinsic_proto::world::Object* response) {
  const stats::ScopedSpan span(
      "ObjectWorldService/UpdateKinematicObjectProperties", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  INTR_ASSIGN_OR_RETURN_GRPC(
      const KinematicObject* object,
      HandleUpdateKinematicObjectPropertiesRequest(*world, *request));

  world_ptr->UpdateTimestamp();
  world_ptr->UpdateStructureHash();
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/false);

  INTR_ASSIGN_OR_RETURN_GRPC(
      *response, ToProto(*object, request->world_id(), request->view()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::UpdateObjectProperties(
    grpc::ServerContext* context,
    const intrinsic_proto::world::UpdateObjectPropertiesRequest* request,
    intrinsic_proto::world::Object* response) {
  const stats::ScopedSpan span("ObjectWorldService/UpdateObjectProperties",
                               context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  // TODO: b/469159066 -- We need to deserialize the geometry here in order to
  // set the geometry options due to our design choices when defining our protos
  INTR_ASSIGN_OR_RETURN_GRPC(const WorldObject* object,
                             HandleUpdateObjectPropertiesRequest(
                                 *world, *request, GeoLib(context).get()));

  world_ptr->UpdateTimestamp();
  world_ptr->UpdateStructureHash();
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/false);

  INTR_ASSIGN_OR_RETURN_GRPC(
      *response, ToProto(*object, request->world_id(), request->view()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::UpdateEntityProperties(
    grpc::ServerContext* context,
    const intrinsic_proto::world::UpdateEntityPropertiesRequest* request,
    intrinsic_proto::world::Entity* response) {
  const stats::ScopedSpan span("ObjectWorldService/UpdateEntityProperties",
                               context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  // TODO: b/469159066 -- We need to deserialize the geometry here in order to
  // set the geometry options due to our design choices when defining our protos
  INTR_ASSIGN_OR_RETURN_GRPC(AttachmentEntityId entity_id,
                             HandleUpdateEntityPropertiesRequest(
                                 *world, *request, GeoLib(context).get()));

  world_ptr->UpdateTimestamp();
  world_ptr->UpdateStructureHash();
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/false);

  INTR_ASSIGN_OR_RETURN_GRPC(const WorldObject* object,
                             world->GetObjectByMemberEntityId(entity_id));
  INTR_ASSIGN_OR_RETURN_GRPC(*response,
                             ToProto(entity_id, request->world_id(), *object));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::GetFrame(
    grpc::ServerContext* context,
    const intrinsic_proto::world::GetFrameRequest* request,
    intrinsic_proto::world::Frame* response) {
  const stats::ScopedSpan span("ObjectWorldService/GetFrame", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::MutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(const auto world, world_ptr->GetObjectWorld());

  INTR_ASSIGN_OR_RETURN_GRPC(const Frame* frame,
                             GetFrameByReference(*world, request->frame()));

  INTR_ASSIGN_OR_RETURN_GRPC(*response, ToProto(*frame, request->world_id()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::ListFrames(
    grpc::ServerContext* context,
    const intrinsic_proto::world::ListFramesRequest* request,
    intrinsic_proto::world::ListFramesResponse* response) {
  const stats::ScopedSpan span("ObjectWorldService/ListFrames", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::MutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(const auto world, world_ptr->GetObjectWorld());

  std::vector<const WorldObject*> objects;
  if (request->has_object()) {
    INTR_ASSIGN_OR_RETURN_GRPC(const WorldObject* object,
                               GetObjectByReference(*world, request->object()));
    objects.push_back(object);
  } else {
    objects = world->GetObjects();
  }

  for (const WorldObject* object : objects) {
    for (const Frame* frame : object->GetFrames()) {
      INTR_ASSIGN_OR_RETURN_GRPC(*response->add_frames(),
                                 ToProto(*frame, request->world_id()));
    }
  }

  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::CreateFrame(
    grpc::ServerContext* context,
    const intrinsic_proto::world::CreateFrameRequest* request,
    intrinsic_proto::world::Frame* response) {
  const stats::ScopedSpan span("ObjectWorldService/CreateFrame", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  INTR_ASSIGN_OR_RETURN_GRPC(const Frame* frame,
                             HandleCreateFrameRequest(*world, *request));

  world_ptr->UpdateTimestamp();
  world_ptr->UpdateStructureHash();
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/false);

  INTR_ASSIGN_OR_RETURN_GRPC(*response, ToProto(*frame, request->world_id()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::DeleteFrame(
    grpc::ServerContext* context,
    const intrinsic_proto::world::DeleteFrameRequest* request,
    google::protobuf::Empty* response) {
  const stats::ScopedSpan span("ObjectWorldService/DeleteFrame", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  INTR_RETURN_IF_ERROR_GRPC(
      HandleDeleteFrameRequest(*world, *request, disable_asset_frame_edits_));

  world_ptr->UpdateTimestamp();
  world_ptr->UpdateStructureHash();
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/false);

  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::UpdateFrameName(
    grpc::ServerContext* context,
    const intrinsic_proto::world::UpdateFrameNameRequest* request,
    intrinsic_proto::world::Frame* response) {
  const stats::ScopedSpan span("ObjectWorldService/UpdateFrameName", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  INTR_ASSIGN_OR_RETURN_GRPC(const Frame* frame,
                             HandleUpdateFrameNameRequest(
                                 *world, *request, disable_asset_frame_edits_));

  world_ptr->UpdateTimestamp();
  world_ptr->UpdateStructureHash();
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/false);

  INTR_ASSIGN_OR_RETURN_GRPC(*response, ToProto(*frame, request->world_id()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::ReparentFrame(
    grpc::ServerContext* context,
    const intrinsic_proto::world::ReparentFrameRequest* request,
    intrinsic_proto::world::Frame* response) {
  const stats::ScopedSpan span("ObjectWorldService/ReparentFrame", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  INTR_ASSIGN_OR_RETURN_GRPC(
      const Frame* frame,
      HandleReparentFrameRequest(*world, *request, disable_asset_frame_edits_));

  world_ptr->UpdateTimestamp();
  world_ptr->UpdateStructureHash();
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/false);

  INTR_ASSIGN_OR_RETURN_GRPC(*response, ToProto(*frame, request->world_id()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::UpdateFrameProperties(
    grpc::ServerContext* context,
    const intrinsic_proto::world::UpdateFramePropertiesRequest* request,
    intrinsic_proto::world::Frame* response) {
  const stats::ScopedSpan span("ObjectWorldService/UpdateFrameProperties",
                               context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  INTR_ASSIGN_OR_RETURN_GRPC(
      const Frame* frame, HandleUpdateFramePropertiesRequest(*world, *request));

  world_ptr->UpdateTimestamp();
  world_ptr->UpdateStructureHash();
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/false);

  INTR_ASSIGN_OR_RETURN_GRPC(*response, ToProto(*frame, request->world_id()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::GetTransform(
    grpc::ServerContext* context,
    const intrinsic_proto::world::GetTransformRequest* request,
    intrinsic_proto::world::GetTransformResponse* response) {
  const stats::ScopedSpan span("ObjectWorldService/GetTransform", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::MutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(const auto world, world_ptr->GetObjectWorld());

  INTR_ASSIGN_OR_RETURN_GRPC(
      const TransformNode* node_a,
      GetTransformNodeByReference(*world, request->node_a()));
  INTR_ASSIGN_OR_RETURN_GRPC(
      const TransformNode* node_b,
      GetTransformNodeByReference(*world, request->node_b()));

  std::optional<world::ObjectEntityFilter> node_a_filter = std::nullopt;
  if (request->has_node_a_filter()) {
    node_a_filter = FromProto(request->node_a_filter());
  }
  std::optional<world::ObjectEntityFilter> node_b_filter = std::nullopt;
  if (request->has_node_b_filter()) {
    node_b_filter = FromProto(request->node_b_filter());
  }

  INTR_ASSIGN_OR_RETURN_GRPC(
      Pose3d a_t_b, node_a->GetTransform(node_a_filter, node_b, node_b_filter));
  *response->mutable_a_t_b() = ToProto(a_t_b);
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::UpdateTransform(
    grpc::ServerContext* context,
    const intrinsic_proto::world::UpdateTransformRequest* request,
    intrinsic_proto::world::UpdateTransformResponse* response) {
  const stats::ScopedSpan span("ObjectWorldService/UpdateTransform", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));

  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  INTR_ASSIGN_OR_RETURN_GRPC(const TransformNode* node_to_update,
                             HandleUpdateTransformRequest(*world, *request));

  world_ptr->UpdateTimestamp();
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/true);

  SimpleTransformNodeConstVisitor to_response_proto_visitor(
      [&](const Frame& frame) {
        INTR_ASSIGN_OR_RETURN(*response->mutable_frame(),
                              ToProto(frame, request->world_id()));
        return absl::OkStatus();
      },
      [&](const WorldObject& object) {
        INTR_ASSIGN_OR_RETURN(
            *response->mutable_object(),
            ToProto(object, request->world_id(), request->view()));
        return absl::OkStatus();
      });
  INTR_RETURN_IF_ERROR_GRPC(node_to_update->Accept(to_response_proto_visitor));

  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::UpdateWorldResources(
    grpc::ServerContext* context,
    const intrinsic_proto::world::UpdateWorldResourcesRequest* request,
    intrinsic_proto::world::UpdateWorldResourcesResponse* response) {
  const stats::ScopedSpan span("ObjectWorldService/UpdateWorldResources",
                               context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  // TODO(b/259728627) Remove this check once the PoseGenerator publishes object
  // world updates instead of entity world updates (until this change is made,
  // this method temporarily supports entity world updates as well since the
  // WorldServiceUpdater no longer uses the WorldService - it only uses the
  // ObjectWorldService)
  if (!request->world_updates().updates().empty() ||
      !request->world_updates().entity_updates().empty()) {
    // TODO(b/204178012): On an update error, roll back to before first
    // update.
    INTR_RETURN_IF_ERROR_GRPC(HandleObjectWorldUpdates(
        *world, request->world_updates(), GeoLib(context).get(),
        disable_asset_frame_edits_));

    // TODO(b/259728627) Remove this call once the PoseGenerator publishes
    // object world updates
    INTR_RETURN_IF_ERROR_GRPC(
        HandleEntityWorldUpdates(request->world_updates().entity_updates(),
                                 &world_ptr->world))
        .LogError();

    world_ptr->UpdateTimestamp();

    // Skip hash updates if there are only state changes.
    if (!IsOnlyStateChange(request->world_updates()) ||
        !IsOnlyStateChange(request->world_updates().entity_updates())) {
      world_ptr->UpdateStructureHash();
    }
    // Only clear the object world pointer if we had entity updates
    if (!IsOnlyStateChange(request->world_updates().entity_updates())) {
      world_ptr->ClearObjectWorld();
    }
    WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                    /*has_state_change=*/true);
  }

  for (const WorldObject* object : world->GetObjects()) {
    INTR_ASSIGN_OR_RETURN_GRPC(
        *response->add_objects(),
        ToProto(*object, request->world_id(), request->view()));
  }

  *response->mutable_collision_settings() =
      MakeCollisionSettings((*world_ptr)->GetDefaultRuleSet());

  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::AreFootprintsCompatible(
    grpc::ServerContext* context,
    const intrinsic_proto::world::AreFootprintsCompatibleRequest* request,
    intrinsic_proto::world::AreFootprintsCompatibleResponse* response) {
  const stats::ScopedSpan span("ObjectWorldService/AreFootprintsCompatible",
                               context);

  response->set_world_id(request->world_id());

  // If both sets are empty, short-circuit.
  if (request->left_set_size() == 0 || request->right_set_size() == 0) {
    return grpc::Status::OK;
  }

  auto geolib = GeoLib(context);
  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));

  std::vector<intrinsic_proto::skills::Footprint> left_entity_footprints;
  std::vector<intrinsic_proto::skills::Footprint> right_entity_footprints;

  {
    absl::ReaderMutexLock lock(*world_ptr->mtx);
    // TODO(stoyang): Cache the ObjectWorld view to improve performance.
    absl::AnyInvocable<absl::StatusOr<const ObjectWorld*>()>
        world_view_provider = LazyObjectView(world_ptr->world);

    left_entity_footprints.reserve(request->left_set_size());
    for (const intrinsic_proto::skills::Footprint& left_footprint :
         request->left_set()) {
      INTR_ASSIGN_OR_RETURN_GRPC(left_entity_footprints.emplace_back(),
                                 ConvertObjectFootprintToEntityFootprint(
                                     left_footprint, world_view_provider));
    }
    right_entity_footprints.reserve(request->right_set_size());
    for (const intrinsic_proto::skills::Footprint& right_footprint :
         request->right_set()) {
      INTR_ASSIGN_OR_RETURN_GRPC(right_entity_footprints.emplace_back(),
                                 ConvertObjectFootprintToEntityFootprint(
                                     right_footprint, world_view_provider));
    }

    const World& world = world_ptr->world;
    for (int left_index = 0; left_index < left_entity_footprints.size();
         ++left_index) {
      for (int right_index = 0; right_index < right_entity_footprints.size();
           ++right_index) {
        auto compatible_status = ::intrinsic::AreFootprintsCompatible(
            world, geolib->Deserializer(), left_entity_footprints[left_index],
            right_entity_footprints[right_index]);
        if (compatible_status.ok()) {
          continue;
        }

        auto* conflict = response->add_pairs();
        conflict->set_left_index(left_index);
        conflict->set_right_index(right_index);
        *conflict->mutable_reason() = SaveStatusAsRpcStatus(compatible_status);

        // If we only want the first result then we can return now.
        if (!request->return_all_incompatible_pairs()) {
          return grpc::Status::OK;
        }
      }
    }
  }

  return grpc::Status::OK;
}

namespace {

absl::StatusOr<intrinsic_proto::world::CollisionSettings>
GetCollisionSettingsInternal(WorldAndMutex& world_ptr)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(world_ptr.mtx) {
  INTR_ASSIGN_OR_RETURN(const auto world, world_ptr.GetObjectWorld());
  std::multiset<intrinsic_proto::Rule> rules =
      FillRuleSet(world_ptr.world, world_ptr.world.GetDefaultRuleSet());
  intrinsic_proto::RuleSet filled_rule_set;
  *filled_rule_set.mutable_rules() = {rules.begin(), rules.end()};
  return MakeCollisionSettings(*world, filled_rule_set);
}

absl::StatusOr<intrinsic_proto::world::World> BuildWorldProto(
    const std::string& world_id, WorldAndMutex& world_ptr,
    const intrinsic_proto::world::WorldView& world_view)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(world_ptr.mtx) {
  const stats::ScopedSpan span("ObjectWorldService/BuildWorldProto");

  intrinsic_proto::world::World response;
  INTR_ASSIGN_OR_RETURN(*response.mutable_world_metadata(),
                        ToProtoLocked(world_ptr, world_id));

  switch (world_view) {
    case intrinsic_proto::world::WorldView::WORLD_VIEW_UNSPECIFIED:
    case intrinsic_proto::world::WorldView::WORLD_VIEW_BASIC: {
      INTR_ASSIGN_OR_RETURN(const auto world, world_ptr.GetObjectWorld());
      INTR_ASSIGN_OR_RETURN(
          *response.mutable_objects(),
          AllWorldObjects(*world, world_id, {},
                          intrinsic_proto::world::ObjectView::BASIC));
      return response;
    }
    case intrinsic_proto::world::WorldView::WORLD_VIEW_FULL: {
      INTR_ASSIGN_OR_RETURN(const auto world, world_ptr.GetObjectWorld());
      INTR_ASSIGN_OR_RETURN(
          *response.mutable_objects(),
          AllWorldObjects(*world, world_id, {},
                          intrinsic_proto::world::ObjectView::FULL));
      INTR_ASSIGN_OR_RETURN(*response.mutable_collision_settings(),
                            GetCollisionSettingsInternal(world_ptr));
      return response;
    }
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported world view: ", world_view));
  }
}

absl::Status PopulateGetWorldStateResponseExceptMetadata(
    const World& world,
    intrinsic_proto::world::GetWorldStateResponse* response) {
  // Go through the robots and add the relevant dof values
  for (const RobotCollectionsEntityId& robot_collection_entity_id :
       world.GetTypedEntityIds<RobotCollectionsEntityId>()) {
    ObjectWorldResourceId object_id =
        ObjectWorldResourceIdForObject(robot_collection_entity_id);

    INTR_ASSIGN_OR_RETURN(const auto dof_view, world.GetDofKinematicView(
                                                   robot_collection_entity_id));

    auto* kinematic_object_proto = response->add_kinematic_object_joints();
    kinematic_object_proto->set_object_id(object_id.value());
    VectorXdToRepeatedDouble(dof_view->GetDofValues(),
                             kinematic_object_proto->mutable_joint_positions());
  }

  // Go through all the entities and add the parent_t_this poses.
  const std::vector<AttachmentEntityId> entities =
      world.GetTypedEntityIds<AttachmentEntityId>();
  for (const AttachmentEntityId& entity_id : entities) {
    INTR_ASSIGN_OR_RETURN(
        const auto* component,
        world.GetComponentByEntityId<AttachmentComponent>(entity_id));

    auto* entity_pose_proto = response->add_entity_poses();
    entity_pose_proto->set_entity_id(entity_id.value());
    *entity_pose_proto->mutable_parent_t_this() =
        ToProto(component->GetParentTThis());
  }

  return absl::OkStatus();
}

absl::StatusOr<const WorldObject*> GetBaselineObject(
    const ObjectWorld* baseline_object_world, const WorldObject* object) {
  absl::StatusOr<const WorldObject*> baseline_object =
      baseline_object_world->GetObject(object->GetName());
  if (baseline_object.ok()) {
    return baseline_object;
  } else if (absl::IsNotFound(baseline_object.status())) {
    INTR_ASSIGN_OR_RETURN(std::optional<std::string> resource_name,
                          object->GetResourceName());
    if (resource_name.has_value() && !resource_name.value().empty()) {
      for (const WorldObject* obj : baseline_object_world->GetObjects()) {
        INTR_ASSIGN_OR_RETURN(std::optional<std::string> other_resource_name,
                              obj->GetResourceName());
        if (resource_name == other_resource_name) {
          return obj;
        }
      }
    }
  } else {
    return baseline_object;
  }

  return nullptr;
}

}  // namespace

grpc::Status ObjectWorldService::GetCollisionSettings(
    grpc::ServerContext* context,
    const intrinsic_proto::world::GetCollisionSettingsRequest* request,
    intrinsic_proto::world::CollisionSettings* response) {
  const stats::ScopedSpan span("ObjectWorldService/GetCollisionSettings",
                               context);
  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(*response,
                             GetCollisionSettingsInternal(*world_ptr));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::UpdateCollisionSettings(
    grpc::ServerContext* context,
    const intrinsic_proto::world::UpdateCollisionSettingsRequest* request,
    intrinsic_proto::world::World* response) {
  const stats::ScopedSpan span("ObjectWorldService/UpdateCollisionSettings",
                               context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));

  absl::WriterMutexLock lock(*world_ptr->mtx);
  INTR_ASSIGN_OR_RETURN_GRPC(auto world, world_ptr->GetObjectWorld());

  INTR_RETURN_IF_ERROR_GRPC(
      HandleUpdateCollisionSettingsRequest(*world, *request));

  world_ptr->UpdateTimestamp();
  world_ptr->UpdateStructureHash();
  WorldStore().MarkWorldAsChanged(request->world_id(), world_ptr,
                                  /*has_state_change=*/false);

  // Get access to objects and entities of given world.
  INTR_ASSIGN_OR_RETURN_GRPC(
      *response,
      BuildWorldProto(request->world_id(), *world_ptr,
                      intrinsic_proto::world::WorldView::WORLD_VIEW_FULL));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::ExtractResourceInstances(
    grpc::ServerContext* context,
    const intrinsic_proto::world::ExtractResourceInstancesRequest* request,
    intrinsic_proto::world::ExtractResourceInstancesResponse* response) {
  const stats::ScopedSpan span("ObjectWorldService/ExtractResourceInstances",
                               context);

  // Clone the original world. All resource instances will be removed from the
  // clone.
  std::unique_ptr<World> cloned_world;
  {
    INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                               WorldStore().GetWorld(request->world_id()));
    absl::MutexLock lock(*world_ptr->mtx);
    cloned_world = std::make_unique<World>(world_ptr->world.Clone());
  }

  // Create an object world view of the cloned world.
  INTR_ASSIGN_OR_RETURN_GRPC(auto object_world,
                             ObjectWorld::CreateView(*cloned_world));

  INTR_ASSIGN_OR_RETURN_GRPC(
      const auto entity_id_to_object,
      ExtractEntityIdToWorldObjectMapping(*object_world));

  // Query all objects and sort leaf-first to allow sequential removal.
  // Important invariant: Resource instances can only have other resource
  // instances as child objects.
  INTR_ASSIGN_OR_RETURN_GRPC(auto root_object,
                             object_world->GetObject(RootObjectName()));
  std::vector<WorldObject*> stack;
  stack.push_back(root_object);
  std::vector<WorldObject*> objects_to_delete;
  // We need to create objects first before making any references in subsequence
  // updates based on created object.
  intrinsic_proto::world::ObjectWorldUpdates object_creation_updates;
  intrinsic_proto::world::ObjectWorldUpdates composition_updates;

  // Generate baseline world to see if anything has been deleted at the asset
  // level. If it was deleted after being added via object world updates, then
  // the new generated updates should not have the object anymore.
  INTR_ASSIGN_OR_RETURN_GRPC(
      const World baseline_world,
      ([this, context, request]() -> absl::StatusOr<World> {
        std::vector<intrinsic_proto::resources::GeometricResourceInstanceData>
            resource_instance_data(request->resource_instance_data().begin(),
                                   request->resource_instance_data().end());
        INTR_ASSIGN_OR_RETURN(ComposeWorldResult result,
                              ComposeResourceInstancesIntoWorld(
                                  resource_instance_data, {}, *GeoLib(context),
                                  ComposeWorldOptions{.quiet = true}));
        return std::move(result).world;
      }()));

  // Create ObjectWorld view of the baseline world.
  INTR_ASSIGN_OR_RETURN_GRPC(
      std::unique_ptr<const ObjectWorld> baseline_object_world,
      ObjectWorld::CreateView(baseline_world));

  while (!stack.empty()) {
    auto* object = stack.back();
    stack.pop_back();
    // Children are pushed in descending name order, so that they are visited
    // in ascending name and deterministic order.
    std::vector<WorldObject*> children = object->GetChildren();
    SortByName(children);
    stack.insert(stack.end(), children.rbegin(), children.rend());

    // Skip root object
    if (!object->GetCollectionEntity().has_value()) {
      continue;
    }

    // Resource instances or products must be removed from the world.
    objects_to_delete.push_back(object);

    // If the object is not a PPR object, then add an update to simply add the
    // object.
    const bool is_ppr =
        !object->GetResourceName().value_or(std::nullopt).value_or("").empty();

    // Assume non-ppr objects were added as single objects via an import
    // from the frontend or similar. That way, each one is a single entity
    // object and can be recreated as such.
    if (!is_ppr) {
      INTR_ASSIGN_OR_RETURN_GRPC(
          auto create_object_update,
          ExtractCreateObjectRequest(*cloned_world, object,
                                     entity_id_to_object),
          _ << "Failed to extract create update for non-ppr object: "
            << object->GetName().value());
      *object_creation_updates.add_updates()->mutable_create_object() =
          std::move(create_object_update);
    }

    // Gets the baseline object for the current object if possible.
    INTR_ASSIGN_OR_RETURN_GRPC(bool object_has_global_alias,
                               object->NameIsGlobalAlias());
    const WorldObject* baseline_object = nullptr;
    if (object_has_global_alias) {
      INTR_ASSIGN_OR_RETURN_GRPC(
          baseline_object,
          GetBaselineObject(baseline_object_world.get(), object));
    }

    LOG_IF(INFO, baseline_object != nullptr)
        << "Baseline object found: " << object->GetName().value();
    LOG_IF(INFO, baseline_object == nullptr)
        << "Baseline object not found: " << object->GetName().value();

    INTR_ASSIGN_OR_RETURN_GRPC(
        const bool extract_kinematic_updates,
        ShouldExtractKinematicUpdates(*object, baseline_object));

    if (extract_kinematic_updates) {
      // Memorize the kinematic object joint values.
      INTR_ASSIGN_OR_RETURN_GRPC(
          auto joint_update,
          MaybeExtractUpdateObjectJointsRequest(baseline_object, *cloned_world,
                                                object, entity_id_to_object),
          _ << " Failed to extract joint update for asset "
               "instance in the world.");
      if (joint_update.has_value()) {
        *composition_updates.add_updates()->mutable_update_object_joints() =
            std::move(joint_update).value();
      }

      // Memorize the kinematic object property data.
      INTR_ASSIGN_OR_RETURN_GRPC(
          auto kinematic_object_update,
          MaybeExtractUpdateKinematicObjectPropertiesRequest(
              baseline_object, *cloned_world, object),
          _ << " Failed to extract kinematic object properties for "
               "asset instance in the world.");
      if (kinematic_object_update.has_value()) {
        *composition_updates.add_updates()
             ->mutable_update_kinematic_object_properties() =
            std::move(kinematic_object_update).value();
      }
    }

    // Memorize entity properties for all links.
    INTR_ASSIGN_OR_RETURN_GRPC(
        auto entity_properties_updates,
        ExtractUpdateEntityPropertiesRequest(baseline_world, baseline_object,
                                             *cloned_world, object, is_ppr),
        _ << " Failed to extract entity properties from world.");
    composition_updates.mutable_updates()->MergeFrom(entity_properties_updates);

    // Memorize how resource object is attachment to the world.
    INTR_ASSIGN_OR_RETURN_GRPC(
        auto reparent,
        ExtractReparentRequest(*cloned_world, object, entity_id_to_object),
        _ << " Failed to extract resource instances from world.");
    if (reparent.has_value()) {
      *composition_updates.add_updates()->mutable_reparent_object() =
          std::move(reparent).value();
    }

    // Memorize tree of child frames.
    INTR_ASSIGN_OR_RETURN_GRPC(
        const auto child_frame_updates,
        ExtractChildFrames(baseline_object, *object),
        _ << " Failed to extract resource instances from world.");
    composition_updates.mutable_updates()->MergeFrom(child_frame_updates);

    // TODO(b/260390260) remove this check and use references by id to objects
    // once resource ids are stable across world decomposition and recomposition
    // into resources

    if (!object_has_global_alias) {
      return ToGrpcStatus(absl::InvalidArgumentError(
          absl::Substitute("Failed to memoize transform because "
                           "object $0 does not have "
                           "a globally unique name and thus cannot be "
                           "referenced in a stable way. Failed to extract "
                           "resource instances from world",
                           absl::StrJoin(object->GetFullPathName(), ".",
                                         absl::StreamFormatter()))));
    }
    INTR_ASSIGN_OR_RETURN_GRPC(bool parent_object_has_global_alias,
                               object->GetParent()->NameIsGlobalAlias());
    if (!parent_object_has_global_alias) {
      return ToGrpcStatus(absl::InvalidArgumentError(
          absl::Substitute("Failed to memoize transform because "
                           "object $0 does not have "
                           "a globally unique name and thus cannot be "
                           "referenced in a stable way. Failed to extract "
                           "resource instances from world",
                           absl::StrJoin(object->GetFullPathName(), ".",
                                         absl::StreamFormatter()))));
    }

    // Memorize parent_t_object transformation in separate update.
    auto* transform =
        composition_updates.add_updates()->mutable_update_transform();
    INTR_ASSIGN_OR_RETURN_GRPC(const Pose3d parent_t_object,
                               object->GetParentTThis());
    *transform->mutable_a_t_b() = ToProto(parent_t_object);
    transform->mutable_node_a()
        ->mutable_by_name()
        ->mutable_object()
        ->set_object_name(object->GetParent()->GetName().value());
    transform->mutable_node_b()
        ->mutable_by_name()
        ->mutable_object()
        ->set_object_name(object->GetName().value());
    *transform->mutable_node_to_update() = transform->node_b();

    // Memorize collision exclusion pairs.
    INTR_ASSIGN_OR_RETURN_GRPC(
        intrinsic_proto::world::ObjectWorldUpdates updates,
        ExtractCollisionExclusions(*objects_to_delete.back(), *cloned_world,
                                   entity_id_to_object),
        _ << " Failed to extract resource instances from world.");
    composition_updates.mutable_updates()->MergeFrom(updates.updates());

    // Memorize object properties
    INTR_ASSIGN_OR_RETURN_GRPC(
        auto properties_update,
        MaybeExtractUpdateObjectPropertiesRequest(baseline_object, *object),
        _ << " Failed to extract object properties update for "
             "resource instance in the world.");
    if (properties_update.has_value()) {
      *composition_updates.add_updates()->mutable_update_object_properties() =
          std::move(properties_update).value();
    }
  }

  {  // Save collision settings.
    const intrinsic_proto::RuleSet rule_set = cloned_world->GetDefaultRuleSet();
    auto collision_settings = MakeCollisionSettings(*object_world, rule_set);

    const intrinsic_proto::RuleSet baseline_rule_set =
        baseline_world.GetDefaultRuleSet();
    auto baseline_collision_settings =
        MakeCollisionSettings(*baseline_object_world, baseline_rule_set);

    google::protobuf::util::DefaultFieldComparator comparator;
    comparator.set_treat_nan_as_equal(true);
    comparator.set_float_comparison(
        google::protobuf::util::DefaultFieldComparator::APPROXIMATE);

    google::protobuf::util::MessageDifferencer differencer;
    differencer.set_field_comparator(&comparator);

    if (!differencer.Compare(collision_settings, baseline_collision_settings)) {
      *composition_updates.add_updates()
           ->mutable_update_collision_settings()
           ->mutable_collision_settings() = std::move(collision_settings);
    }
  }

  INTR_ASSIGN_OR_RETURN_GRPC(
      intrinsic_proto::world::ObjectWorldUpdates deletion_updates,
      ExtractResourceDeletions(baseline_world, *cloned_world));

  response->mutable_update_world_resources()->mutable_updates()->MergeFrom(
      deletion_updates.updates());
  response->mutable_update_world_resources()->mutable_updates()->MergeFrom(
      object_creation_updates.updates());
  response->mutable_update_world_resources()->mutable_updates()->MergeFrom(
      composition_updates.updates());

  // Delete objects back-to-front order (leaf first).
  while (!objects_to_delete.empty()) {
    LOG(INFO) << "Attempting to delete " << objects_to_delete.back()->GetName();
    INTR_RETURN_IF_ERROR_GRPC(
        objects_to_delete.back()->DeleteIfNoChildObjects());
    objects_to_delete.pop_back();
  }

  // Root is handled specially because it is not a resource, but can still have
  // child frames composed in using world updates.
  // Root child frames are deleted while iterating, so the mutable frames are
  // sorted here instead of using the const-only `GetChildFramesSorted()`.
  std::vector<Frame*> root_child_frames = root_object->GetChildFrames();
  SortByName(root_child_frames);
  for (Frame* frame : root_child_frames) {
    INTR_ASSIGN_OR_RETURN_GRPC(
        const auto frame_updates,
        ExtractChildFramesTree(frame, root_object->GetName().value()),
        _ << " Failed to extract child frames from root.");
    response->mutable_update_world_resources()->mutable_updates()->MergeFrom(
        frame_updates);
    INTR_RETURN_IF_ERROR_GRPC(frame->DeleteIncludingChildFrames());
  }

  int64_t remaining_entity_count = 0;
  // Explicitly validate postcondition that no entities with PPRComponent remain
  // after deletions.
  for (const auto& entity_id : cloned_world->GetEntityIds()) {
    // We don't need to validate the root entity, it's always present.
    if (entity_id == kRootEntityId) {
      continue;
    }

    INTR_ASSIGN_OR_RETURN_GRPC(const auto* entity,
                               cloned_world->GetEntityById(entity_id));
    std::string alias =
        entity->GetAlias().empty()
            ? ""
            : absl::Substitute(", alias=\"$0\"", entity->GetAlias());
    std::string entity_string =
        absl::Substitute("Entity($0, name=\"$1\"$2)", entity_id.value(),
                         entity->GetLocalName(), alias);
    if (cloned_world->ValidateEntity<PPRComponentType>(entity_id)
            .status()
            .ok()) {
      // clang-format off (indents wrong)
      LOG(ERROR) << entity_string << " has PPRComponent after deletions!";
      return ToGrpcStatus(absl::InternalError(
          absl::StrCat(entity_string, " has PPRComponent after deletions!")));
      // clang-format on
    }

    LOG(ERROR) << entity_string
               << " remains after extracting resource instances.";
    remaining_entity_count++;
  }

  if (remaining_entity_count > 0) {
    RemainingEntitiesAfterResourceExtraction().Add(remaining_entity_count);
  }

  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::CheckCollisions(
    grpc::ServerContext* context,
    const intrinsic_proto::world::CheckCollisionsRequest* request,
    intrinsic_proto::world::CheckCollisionsResponse* response) {
  const stats::ScopedSpan span("ObjectWorldService/CheckCollisions", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore().GetWorld(request->world_id()));

  std::unique_ptr<World> world_copy;

  {
    // We make a copy here to avoid holding the lock while checking collisions.
    // This check may become expensive if we have to prepare the geometry for
    // distance queries, aka construct Octrees.
    absl::MutexLock lock(*world_ptr->mtx);
    world_copy = std::make_unique<World>(world_ptr->world.Clone());
    if (request->has_collision_settings()) {
      INTR_ASSIGN_OR_RETURN_GRPC(const auto world, world_ptr->GetObjectWorld());
      INTR_ASSIGN_OR_RETURN_GRPC(
          auto rule_set, MakeRuleSet(request->collision_settings(), *world));
      INTR_RETURN_IF_ERROR_GRPC(world_copy->SetDefaultRuleSet(rule_set));
    }
  }

  // Deserialize the geometries, since we need them for actual collision
  // checking.
  for (const auto entity_id :
       world_copy->GetTypedEntityIds<GeometryEntityId>()) {
    INTR_ASSIGN_OR_RETURN_GRPC(
        GeometryComponent * geo_component,
        world_copy->GetComponentByEntityId<GeometryComponent>(entity_id));

    for (const std::string& geo_set_name : geo_component->GetGeometryNames()) {
      INTR_RETURN_IF_ERROR_GRPC(
          geo_component
              ->GetGeometry(geo_set_name, GeoLib(context)->Deserializer())
              .status());
    }
  }

  std::vector<std::pair<PhysicalEntityId, PhysicalEntityId>> collision_pairs =
      GetCollisionsBetweenSets(*world_copy, {}, {},
                               /*check_upper_triangle_only=*/true);

  for (const auto& collision_pair : collision_pairs) {
    auto* pair = response->add_collision_pairs();
    pair->set_left_entity_id(
        ObjectWorldResourceIdForEntity(collision_pair.first).value());
    pair->set_right_entity_id(
        ObjectWorldResourceIdForEntity(collision_pair.second).value());
  }

  return grpc::Status::OK;
}

namespace {

template <typename RequestT>
absl::Status CreateWorldFromResourceInstancesImpl(
    const RequestT* request, const ComposeWorldOptions& options,
    intrinsic_proto::world::CreateWorldFromResourceSetDataResponse* response,
    GeometryLibrary& geolib, WorldStorage& world_storage) {
  // Compose the new world from the given resources.
  std::vector<intrinsic_proto::resources::GeometricResourceInstanceData>
      resource_instance_data(request->resource_instance_data().begin(),
                             request->resource_instance_data().end());

  INTR_ASSIGN_OR_RETURN(
      ComposeWorldResult compose_result,
      ComposeResourceInstancesIntoWorld(resource_instance_data,
                                        request->update_world_resources(),
                                        geolib, options));

  for (const ComposeWorldResult::CompositionError& error_data :
       compose_result.composition_errors) {
    intrinsic_proto::world::CreateWorldFromResourceSetDataResponse::
        CompositionError* composition_error =
            response->add_composition_errors();
    if (std::holds_alternative<std::string>(error_data.problem)) {
      composition_error->set_problem_resource_name(
          std::get<std::string>(error_data.problem));
    } else {
      const auto& update_error =
          std::get<ComposeWorldResult::UpdateError>(error_data.problem);
      intrinsic_proto::world::CreateWorldFromResourceSetDataResponse::
          UpdateError* problem_update =
              composition_error->mutable_problem_update();
      problem_update->set_index(update_error.index);
      *problem_update->mutable_update() = update_error.problem_update;
    }
    *composition_error->mutable_status() =
        SaveStatusAsRpcStatus(error_data.status);
  }

  // Adds the world into world storage.
  std::shared_ptr<WorldAndMutex> world_ptr;
  std::string world_id;
  while (true) {
    world_id = absl::StrCat("ResourceWorld_", intrinsic::WebSafeUuid());
    auto world = world_storage.AddWorld(world_id, compose_result.world.Clone(),
                                        /*skip_compat_check=*/false);
    if (world.ok()) {
      world_ptr = *std::move(world);
      break;
    }

    // Retries if the world with this id already exists. The actual id does not
    // matter as long as it is unique.
    if (!absl::IsAlreadyExists(world.status())) {
      return world.status();
    }
  }

  // Get access to objects and entities of given world.
  absl::MutexLock lock(*world_ptr->mtx);
  world_ptr->user_tag = request->user_tag();
  INTR_ASSIGN_OR_RETURN(*response->mutable_metadata(),
                        ToProtoLocked(*world_ptr, world_id));
  return absl::OkStatus();
}

}  // namespace

grpc::Status ObjectWorldService::CreateWorldFromResourceInstances(
    grpc::ServerContext* context,
    const intrinsic_proto::world::CreateWorldFromResourceInstancesRequest*
        request,
    intrinsic_proto::world::WorldMetadata* response) {
  const stats::ScopedSpan span(
      "ObjectWorldService/CreateWorldFromResourceInstances", context);
  ComposeWorldOptions options = ComposeWorldOptions::Default();
  options.quiet = true;
  intrinsic_proto::world::CreateWorldFromResourceSetDataResponse full_response;
  INTR_RETURN_IF_ERROR_GRPC(CreateWorldFromResourceInstancesImpl(
      request, options, &full_response, *GeoLib(context), WorldStore()));

  *response = full_response.metadata();
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::CreateWorldFromResourceSetData(
    grpc::ServerContext* context,
    const intrinsic_proto::world::CreateWorldFromResourceSetDataRequest*
        request,
    intrinsic_proto::world::CreateWorldFromResourceSetDataResponse* response) {
  const stats::ScopedSpan span(
      "ObjectWorldService/CreateWorldFromResourceSetData", context);
  ComposeWorldOptions options = ComposeWorldOptions::Default();
  options.quiet = true;
  switch (request->update_policy()) {
    case intrinsic_proto::world::CreateWorldFromResourceSetDataRequest::
        UPDATE_POLICY_FAIL_ON_FIRST_ERROR:
      options.update_policy =
          ComposeWorldOptions::UpdatePolicy::kFailOnFirstError;
      break;
    case intrinsic_proto::world::CreateWorldFromResourceSetDataRequest::
        UPDATE_POLICY_SKIP_FAILED_UPDATES:
      options.update_policy =
          ComposeWorldOptions::UpdatePolicy::kSkipFailedUpdates;
      break;
    case intrinsic_proto::world::CreateWorldFromResourceSetDataRequest::
        UPDATE_POLICY_DEFAULT:
      ABSL_FALLTHROUGH_INTENDED;
    default:
      options.update_policy = ComposeWorldOptions::UpdatePolicy::kDefault;
      break;
  }

  INTR_RETURN_IF_ERROR_GRPC(CreateWorldFromResourceInstancesImpl(
      request, options, response, *GeoLib(context), WorldStore()));
  return grpc::Status::OK;
}

grpc::Status ObjectWorldService::CompareWorlds(
    grpc::ServerContext* context,
    const intrinsic_proto::world::CompareWorldsRequest* request,
    intrinsic_proto::world::CompareWorldsResponse* response) {
  const stats::ScopedSpan span("ObjectWorldService/CompareWorlds", context);

  // If we are comparing the same world, we can just return the objects.
  if (request->base_world_id() == request->changed_world_id()) {
    if (request->view() !=
        intrinsic_proto::world::ObjectView::OBJECT_VIEW_UNSPECIFIED) {
      INTR_ASSIGN_OR_RETURN_GRPC(
          std::shared_ptr<WorldAndMutex> world_ptr,
          WorldStore().GetWorld(request->base_world_id()));

      absl::MutexLock world_lock(*world_ptr->mtx);
      INTR_ASSIGN_OR_RETURN_GRPC(const auto world, world_ptr->GetObjectWorld());
      INTR_ASSIGN_OR_RETURN_GRPC(
          *response->mutable_base_world_objects(),
          AllWorldObjects(*world, request->base_world_id(), {},
                          request->view()));
      *response->mutable_changed_world_objects() =
          response->base_world_objects();
    }
    return grpc::Status::OK;
  }

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> base_world_ptr,
                             WorldStore().GetWorld(request->base_world_id()));
  INTR_ASSIGN_OR_RETURN_GRPC(
      std::shared_ptr<WorldAndMutex> changed_world_ptr,
      WorldStore().GetWorld(request->changed_world_id()));

  intrinsic_proto::world::World base_world_proto, changed_world_proto;
  {
    // Lock storage here to make sure snapshots are in sync.
    absl::MutexLock worlds_lock(WorldStore().worlds_mtx_);
    {
      absl::MutexLock base_world_lock(*base_world_ptr->mtx);
      INTR_ASSIGN_OR_RETURN_GRPC(const auto base_world,
                                 base_world_ptr->GetObjectWorld());
      INTR_ASSIGN_OR_RETURN_GRPC(
          *base_world_proto.mutable_objects(),
          AllWorldObjects(*base_world, request->base_world_id(), {},
                          request->view()));
    }
    {
      absl::MutexLock changed_world_lock(*changed_world_ptr->mtx);
      INTR_ASSIGN_OR_RETURN_GRPC(const auto changed_world,
                                 changed_world_ptr->GetObjectWorld());
      INTR_ASSIGN_OR_RETURN_GRPC(
          *changed_world_proto.mutable_objects(),
          AllWorldObjects(*changed_world, request->changed_world_id(), {},
                          request->view()));
    }
  }

  INTR_ASSIGN_OR_RETURN_GRPC(
      WorldComparator comparator,
      WorldComparator::Compare(base_world_proto, changed_world_proto,
                               request->unique_id_mode()));

  response->mutable_added()->Add(comparator.GetAdded().begin(),
                                 comparator.GetAdded().end());
  response->mutable_removed()->Add(comparator.GetRemoved().begin(),
                                   comparator.GetRemoved().end());
  for (const auto& [modified, msg] : comparator.GetModified()) {
    intrinsic_proto::world::CompareWorldsResponse::ModifiedObject* obj =
        response->mutable_modified()->Add();
    *obj->mutable_object() = modified;
    obj->set_modification(msg);
  }
  response->mutable_added_frames()->Add(comparator.GetAddedFrames().begin(),
                                        comparator.GetAddedFrames().end());
  response->mutable_removed_frames()->Add(comparator.GetRemovedFrames().begin(),
                                          comparator.GetRemovedFrames().end());
  for (const auto& [modified, msg] : comparator.GetModifiedFrames()) {
    intrinsic_proto::world::CompareWorldsResponse::ModifiedFrame* frame =
        response->mutable_modified_frames()->Add();
    *frame->mutable_frame() = modified;
    frame->set_modification(msg);
  }

  if (request->view() !=
      intrinsic_proto::world::ObjectView::OBJECT_VIEW_UNSPECIFIED) {
    *response->mutable_base_world_objects() =
        std::move(base_world_proto.objects());
    *response->mutable_changed_world_objects() =
        std::move(changed_world_proto.objects());
  }

  return grpc::Status::OK;
}

absl::Status ObjectWorldService::SyncFrameImpl(
    const ObjectWorld& from_world, ObjectWorld& to_world,
    const intrinsic_proto::world::FrameReference& frame_reference) {
  absl::StatusOr<const Frame*> from_frame_status =
      GetFrameByReference(from_world, frame_reference);
  if (absl::IsNotFound(from_frame_status.status())) {
    INTR_ASSIGN_OR_RETURN(Frame * to_frame,
                          GetFrameByReference(to_world, frame_reference));
    INTR_RETURN_IF_ERROR(to_frame->DeleteIfNoChildFrames())
        << "Cannot sync a deleted frame with children. Sync the children "
           "before syncing this frame.";
    return absl::OkStatus();
  } else if (!from_frame_status.ok()) {
    return from_frame_status.status();
  }

  INTR_RETURN_IF_ERROR(
      to_world.SyncFrameFromOtherWorld(*from_frame_status.value()));
  return absl::OkStatus();
}

absl::Status ObjectWorldService::SyncObjectImpl(
    const ObjectWorld& from_world, ObjectWorld& to_world,
    const ObjectReference& object_reference) {
  absl::StatusOr<const WorldObject*> from_object_status =
      GetObjectByReference(from_world, object_reference);
  if (absl::IsNotFound(from_object_status.status())) {
    INTR_ASSIGN_OR_RETURN(WorldObject * to_object,
                          GetObjectByReference(to_world, object_reference));
    INTR_RETURN_IF_ERROR(to_object->DeleteIfNoChildObjects())
        << "Cannot sync a deleted object with children. Sync the children "
           "before syncing this object.";
    return absl::OkStatus();
  } else if (!from_object_status.ok()) {
    return from_object_status.status();
  }

  INTR_RETURN_IF_ERROR_GRPC(
      to_world.SyncObjectFromOtherWorld(*from_object_status.value()));

  return absl::OkStatus();
}

grpc::Status ObjectWorldService::SyncObject(
    grpc::ServerContext* context,
    const intrinsic_proto::world::SyncObjectRequest* request,
    intrinsic_proto::world::SyncObjectResponse* response) {
  const stats::ScopedSpan span("ObjectWorldService/SyncObject", context);

  if (!request->has_object() && !request->has_frame()) {
    return ToGrpcStatus(
        absl::InvalidArgumentError("Object and Frame cannot be empty."));
  }
  if (request->from_world_id() == request->to_world_id()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "from_world_id and to_world_id cannot be equal."));
  }
  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> from_world_ptr,
                             WorldStore().GetWorld(request->from_world_id()));

  absl::MutexLock worlds_lock(WorldStore().worlds_mtx_);
  std::shared_ptr<WorldAndMutex> to_world_ptr;
  {
    auto maybe_to_world_ptr =
        WorldStore().GetWorldLocked(request->to_world_id());

    if (maybe_to_world_ptr.ok()) {
      // The target world exists so sync to it.
      to_world_ptr = *maybe_to_world_ptr;
    } else if (absl::IsNotFound(maybe_to_world_ptr.status())) {
      // The target world does not exist, so create a new empty world to sync
      // the object to.
      INTR_ASSIGN_OR_RETURN_GRPC(
          to_world_ptr, WorldStore().AddWorldLocked(
                            request->to_world_id(), World::CreateEmptyWorld(),
                            /*skip_compat_check=*/true));
    } else {
      // Other errors are raised to the caller.
      return ToGrpcStatus(maybe_to_world_ptr.status());
    }
  }

  // Acquire the world mutexes in ID order to avoid the potential for deadlock.
  // Unfortunately we don't have a readily available RAII type to manage the
  // ordered locking cases, so we manually lock and unlock.
  bool lock_to_first = request->from_world_id() > request->to_world_id();
  if (lock_to_first) {
    to_world_ptr->mtx->lock();
    from_world_ptr->mtx->lock();
  } else {
    from_world_ptr->mtx->lock();
    to_world_ptr->mtx->lock();
  }

  // The critical section where we use the world mutexes is contained in this
  // lambda to ensure we have a single exit.
  absl::Status sync_result = [&]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(
                                 WorldStore().worlds_mtx_, from_world_ptr->mtx,
                                 to_world_ptr->mtx) -> absl::Status {
    INTR_ASSIGN_OR_RETURN(const auto from_world,
                          from_world_ptr->GetObjectWorld());
    World cloned_entity_to_world = to_world_ptr->world.Clone();
    INTR_ASSIGN_OR_RETURN(std::unique_ptr<ObjectWorld> cloned_to_world,
                          ObjectWorld::CreateView(cloned_entity_to_world));

    absl::Status status;
    switch (request->resource_type_case()) {
      case intrinsic_proto::world::SyncObjectRequest::ResourceTypeCase::
          kObject: {
        status =
            SyncObjectImpl(*from_world, *cloned_to_world, request->object());
        break;
      }
      case intrinsic_proto::world::SyncObjectRequest::ResourceTypeCase::
          kFrame: {
        status = SyncFrameImpl(*from_world, *cloned_to_world, request->frame());
        break;
      }
      case intrinsic_proto::world::SyncObjectRequest::ResourceTypeCase::
          RESOURCE_TYPE_NOT_SET:
        return absl::InvalidArgumentError(
            "SyncObjectRequest.resource_type must be set.");
    }

    if (status.ok()) {
      // TODO(stoyang): Do we need to clone again here?
      auto new_world_ptr =
          std::make_shared<WorldAndMutex>(cloned_entity_to_world.Clone());
      {
        absl::MutexLock new_world_lock(*new_world_ptr->mtx);
        new_world_ptr->UpdateTimestamp();
        new_world_ptr->UpdateStructureHash();
        WorldStore().MarkWorldAsChanged(request->to_world_id(), new_world_ptr,
                                        /*has_state_change=*/false);
      }
      INTR_ASSIGN_OR_RETURN(auto _, WorldStore().ExchangeWorldLocked(
                                        request->to_world_id(), new_world_ptr,
                                        /*skip_compat_check=*/false));
    }

    return status;
  }();

  if (lock_to_first) {
    from_world_ptr->mtx->unlock();
    to_world_ptr->mtx->unlock();
  } else {
    to_world_ptr->mtx->unlock();
    from_world_ptr->mtx->unlock();
  }

  return ToGrpcStatus(sync_result);
}

absl::Status ObjectWorldService::CreatePublisherForWorld(
    absl::string_view world_id, const std::shared_ptr<WorldAndMutex>& world) {
  std::string topic = absl::StrCat("/world/", world_id, "/state");
  TopicConfig topic_config;

  INTR_ASSIGN_OR_RETURN(auto publisher,
                        pubsub_.CreatePublisher(topic, topic_config),
                        _ << "Failed to create publisher");

  world->publisher = std::make_unique<Publisher>(std::move(publisher));
  LOG(INFO) << "Started new publisher for world \"" << world_id
            << "\" on topic \"" << topic << "\".";
  return absl::OkStatus();
}

void ObjectWorldService::DeletePublisherForWorld(
    absl::string_view world_id, const std::shared_ptr<WorldAndMutex>& world) {
  LOG(INFO) << "Dropping publisher for world \"" << world_id << "\".";
  world->publisher.reset();
}

void ObjectWorldService::OnWorldChanged(
    absl::string_view world_id, const std::shared_ptr<WorldAndMutex>& world,
    bool has_state_change) {
  if (!has_state_change) {
    return;
  }

  auto status = PublishWorldStateOnce(world_id, world);
  if (!status.ok()) {
    LOG_EVERY_N_SEC(ERROR, 10) << "Failed to publish world state for world \""
                               << world_id << "\": " << status;
  }
}

absl::Status ObjectWorldService::PublishWorldStateOnce(
    absl::string_view world_id,
    const std::shared_ptr<WorldAndMutex>& world_ptr) {
  const stats::ScopedSpan span("ObjectWorldService/PublishWorldStateOnce");

  if (world_ptr->publisher == nullptr) {
    INTR_RETURN_IF_ERROR(CreatePublisherForWorld(world_id, world_ptr));
  }

  intrinsic_proto::world::GetWorldStateResponse msg;
  INTR_RETURN_IF_ERROR(
      PopulateGetWorldStateResponseExceptMetadata(world_ptr->world, &msg));

  INTR_ASSIGN_OR_RETURN(*msg.mutable_world_metadata(),
                        ToProtoLocked(*world_ptr, world_id));

  INTR_RETURN_IF_ERROR(
      world_ptr->publisher->Publish(msg, world_ptr->LastUpdate()));

  return absl::OkStatus();
}

ObjectWorldService::~ObjectWorldService() {
  // We always clear the callbacks without checking the flags, as we are the
  // only users and clearing an empty callback is a no-op.
  if (world_storage_) {
    world_storage_->ClearOnWorldChangeCallback();
    world_storage_->ClearOnDeleteWorldCallback();
  }
}

}  // namespace object_world
}  // namespace intrinsic
