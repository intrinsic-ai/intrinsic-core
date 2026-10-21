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

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/attachment_component.h"
#include "intrinsic/world/component/collections_component.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/frame_internal.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_creation_utils.h"
#include "intrinsic/world/objects/object_world_data.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/physical_object.h"
#include "intrinsic/world/objects/root_object.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/proto/collections_component.pb.h"
#include "intrinsic/world/proto/kinematics_component.pb.h"
#include "intrinsic/world/proto/world_updates.pb.h"
#include "intrinsic/world/util/walk_attachment_tree.h"
#include "intrinsic/world/world.h"

ABSL_FLAG(
    bool, sdf_to_object_world_authoring_hints, false,
    "Whether to add additional hints to object-world incompatibility "
    "error messages that can help fixing an input SDF from which the world "
    "was converted.");

namespace intrinsic {
namespace object_world {

class ObjectWorldCreationProcess {
  absl::StatusOr<AttachmentEntityId> GetObjectRootEntity(
      EntityId collection_entity_id,
      const WorldHashSet<AttachmentEntityId>& member_ids) {
    absl::StatusOr<AttachmentEntityId> collection_root_id =
        data_->GetEntityWorld().GetRootEntity(
            {member_ids.begin(), member_ids.end()});
    if (!collection_root_id.ok()) {
      INTR_ASSIGN_OR_RETURN(std::string collection_desc,
                            DescribeEntityForError(data_->GetEntityWorld(),
                                                   collection_entity_id));
      std::vector<std::string> members_desc;
      for (AttachmentEntityId member_id : member_ids) {
        INTR_ASSIGN_OR_RETURN(
            std::string member_desc,
            DescribeEntityForError(data_->GetEntityWorld(), member_id));
        members_desc.push_back(absl::StrCat("  (", member_desc, ")"));
      }

      std::string msg = absl::StrCat(
          "The entities {\n", absl::StrJoin(members_desc, ",\n"),
          "\n} in the collection represented by entity (", collection_desc,
          ") cannot be viewed as an object since they do not form one "
          "connected component in the attachment graph. ",
          collection_root_id.status().message());
      if (absl::GetFlag(FLAGS_sdf_to_object_world_authoring_hints)) {
        absl::StrAppend(
            &msg,
            "\nSDF authoring hint: All links, joints etc. in a model need to "
            "be attached to each other. Consider adding a fixed joint or "
            "splitting the affected model into multiple models. Note that "
            "nested models are separate models, i.e., if we have "
            "\"<model><model>A</model>B</model>\" the elements at A and B are "
            "part of different models. ");
      }
      return absl::InvalidArgumentError(msg);
    }
    return *collection_root_id;
  }

  absl::StatusOr<WorldObjectName> ObjectNameForErrorCollection(
      const World& world, EntityId collection_entity_id,
      const WorldHashSet<AttachmentEntityId>& member_ids) {
    // Build a nice error message.
    std::vector<std::string> collection_aliases;
    for (AttachmentEntityId member_id : member_ids) {
      INTR_ASSIGN_OR_RETURN(const WorldEntity* member_entity,
                            world.GetEntityById(member_id));
      if (!member_entity->GetAlias().empty()) {
        collection_aliases.emplace_back(member_entity->GetAlias());
      }
    }
    INTR_ASSIGN_OR_RETURN(std::string entity_desc,
                          DescribeEntityForError(world, collection_entity_id));

    std::string msg =
        absl::StrCat("Collection entity (", entity_desc,
                     ") is not compatible with object world naming convension."
                     "For globally unique name, the collection entity or the "
                     "root entity of the collection member entities "
                     "needs to have an alias representing this collection. If "
                     "an alias is not available, the collection entity needs "
                     "to have a unique local name among other frames and "
                     "object under the same namespace.");
    if (collection_aliases.empty()) {
      absl::StrAppend(&msg, " No collection member has an alias.");
    } else {
      absl::c_sort(collection_aliases);
      absl::StrAppend(
          &msg, " Found ", collection_aliases.size(),
          " collection members with aliases  (",
          absl::StrJoin(collection_aliases, ", ", absl::StreamFormatter()),
          ") but none of them is the root entity of the collection.");
    }
    INTR_ASSIGN_OR_RETURN(const WorldEntity* collection_entity,
                          world.GetEntityById(collection_entity_id));
    if (collection_entity->GetLocalName().empty()) {
      absl::StrAppend(&msg, " No local name for collection entity");
    }
    if (absl::GetFlag(FLAGS_sdf_to_object_world_authoring_hints)) {
      intrinsic_proto::world::WorldUpdates updates;
      auto* model_to_object = updates.add_updates()
                                  ->mutable_set_object_names()
                                  ->add_models_to_objects();
      // Collection entities created by world_from_sdf have exactly one label
      // which is equal to the corresponding SDF model name.
      model_to_object->set_sdf_model_name(
          collection_entity->GetLabels().begin()->value());
      model_to_object->set_object_name("my_object");
      absl::StrAppend(&msg,
                      "\nSDF authoring hint: Add a world update setting the "
                      "alias for the collections entity. Example:\n",
                      updates);
    }
    // Proceed with a random, unique name so that we can collect more
    // potential errors.
    non_critical_errors_.push_back(msg);
    return WorldObjectName(absl::StrCat(
        "obj_",
        absl::Uniform<uint64_t>(absl::BitGen())));  // NOLINT (cl/653534410)
  }

  absl::StatusOr<WorldObjectName> ObjectNameFromCollection(
      const World& world, EntityId collection_entity_id,
      const WorldHashSet<AttachmentEntityId>& member_ids) {
    std::optional<std::string> name = world.TryGetObjectNameFromCollection(
        CollectionsEntityId(collection_entity_id.value()));
    if (name.has_value()) {
      INTR_RETURN_IF_ERROR(
          CheckNameIsCompatibleWithObjectView((*name), /*strict=*/false));
      return WorldObjectName(*name);
    }

    // Fallback to error collection logic
    INTR_ASSIGN_OR_RETURN(
        WorldObjectName result,
        ObjectNameForErrorCollection(world, collection_entity_id, member_ids));
    INTR_RETURN_IF_ERROR(
        CheckNameIsCompatibleWithObjectView(result.value(), /*strict=*/false));
    return result;
  }

  absl::StatusOr<WorldHashSet<AttachmentEntityId>> GetCollectionMembers(
      const World& world, CollectionsEntityId collection_entity_id,
      std::vector<intrinsic_proto::world::CollectionsComponent::CollectionType>
          collection_types) {
    INTR_ASSIGN_OR_RETURN(const CollectionsComponent* collections_component,
                          world.GetComponentByEntityId<CollectionsComponent>(
                              collection_entity_id));
    WorldHashSet<AttachmentEntityId> all_member_ids;
    for (const auto& type : collection_types) {
      for (const auto& m : collections_component->GetCollectionMembers(type)) {
        // Check for any misconfigured collections members
        switch (type) {
          case CollectionsComponent::kLinks:
            INTR_RETURN_IF_ERROR(
                world.ValidateEntity<LinkEntityId>(m).status());
            break;

          case CollectionsComponent::kSensors:
            INTR_RETURN_IF_ERROR(
                world.ValidateEntity<SensorEntityId>(m).status());
            break;

          case CollectionsComponent::kJoints:
            INTR_RETURN_IF_ERROR(
                world.ValidateEntity<JointEntityId>(m).status());
            break;

          default:
            // Do nothing
            break;
        }

        INTR_ASSIGN_OR_RETURN(AttachmentEntityId member_attachment_id,
                              world.ValidateEntity<AttachmentEntityId>(m));
        all_member_ids.insert(member_attachment_id);
      }
    }

    return all_member_ids;
  }

  absl::Status CheckEntityLocalNamesAreUnique(
      const WorldHashSet<AttachmentEntityId>& member_ids,
      const WorldObjectName& object_name) {
    WorldHashMap<std::string, AttachmentEntityId> seen_local_names;

    for (AttachmentEntityId member_id : member_ids) {
      INTR_ASSIGN_OR_RETURN(const WorldEntity* member_entity,
                            data_->GetEntityWorld().GetEntityById(member_id));
      const std::string local_name = member_entity->GetLocalName();

      if (local_name.empty()) {
        non_critical_errors_.push_back(absl::Substitute(
            "Entity names must be unique within an object. Object \"$0\" "
            "contains entity $1 which has no local name.",
            object_name.value(), member_id.value()));
        continue;
      }

      INTR_RETURN_IF_ERROR(
          CheckNameIsCompatibleWithObjectView(local_name, /*strict=*/true));

      const auto [it, success] =
          seen_local_names.insert({local_name, member_id});

      if (!success) {
        non_critical_errors_.push_back(absl::Substitute(
            "Entity names must be unique within an object. Object \"$0\" "
            "contains entity $1 and entity $2 which have the "
            "same local name \"$3\".",
            object_name.value(), it->second.value(), member_id.value(),
            local_name));
      }
    }

    return absl::OkStatus();
  }

  absl::Status AddObjectFromCollection(
      CollectionsEntityId collection_entity_id) {
    ObjectWorldResourceId object_id =
        ObjectWorldResourceIdForObject(collection_entity_id);

    const World& world = data_->GetEntityWorld();
    INTR_ASSIGN_OR_RETURN(
        WorldHashSet<AttachmentEntityId> member_ids,
        GetCollectionMembers(
            world, collection_entity_id,
            {CollectionsComponent::kLinks, CollectionsComponent::kJoints,
             CollectionsComponent::kSensors, CollectionsComponent::kProjectors,
             CollectionsComponent::kCoordinateFrames,
             CollectionsComponent::kAttachmentFrames}));
    if (member_ids.empty()) {
      // Ignores collection entity that represents an empty collection
      // as it can't be viewed as an object.
      return absl::OkStatus();
    }
    INTR_ASSIGN_OR_RETURN(
        WorldObjectName object_name,
        ObjectNameFromCollection(world, collection_entity_id, member_ids));
    INTR_ASSIGN_OR_RETURN(
        WorldHashSet<AttachmentEntityId> non_frame_member_ids,
        GetCollectionMembers(
            world, collection_entity_id,
            {CollectionsComponent::kLinks, CollectionsComponent::kJoints,
             CollectionsComponent::kSensors,
             CollectionsComponent::kProjectors}));
    INTR_RETURN_IF_ERROR(
        CheckEntityLocalNamesAreUnique(non_frame_member_ids, object_name));

    absl::StatusOr<RobotCollectionsEntityId> robot_collections_id =
        world.ValidateEntity<RobotCollectionsEntityId>(collection_entity_id);
    absl::StatusOr<std::vector<JointEntityId>> robot_dofs;
    if (robot_collections_id.ok()) {
      robot_dofs = world.GetRobotDofs(*robot_collections_id);
    }
    // Non-robot collections or collections with devices may have a
    // RobotComponent without any kinematic degrees of freedom. Such
    // collections represent physical objects rather than kinematic objects.
    if (robot_collections_id.ok() && robot_dofs.ok() && !robot_dofs->empty()) {
      INTR_RETURN_IF_ERROR(
          data_->InsertObject(std::make_unique<KinematicObject>(
              object_id, object_name, *robot_collections_id,
              std::move(member_ids), *data_)));
    } else {
      INTR_RETURN_IF_ERROR(data_->InsertObject(std::make_unique<PhysicalObject>(
          object_id, object_name, collection_entity_id, std::move(member_ids),
          *data_)));
    }

    return absl::OkStatus();
  }

  // Tries to find the parent object for the object that we intend to create
  // from the entities in 'member_ids'. This is achieved by looking for an
  // object that contains the parent entity of the common root of the entities
  // in 'member_ids'.
  absl::StatusOr<WorldObject*> GetParentObject(const WorldObject& object) {
    INTR_ASSIGN_OR_RETURN(AttachmentEntityId collection_root_id,
                          GetObjectRootEntity(*object.GetCollectionEntity(),
                                              object.GetEntityIds()));
    INTR_ASSIGN_OR_RETURN(
        const AttachmentComponent* root_attachment,
        data_->GetEntityWorld().GetComponentByEntityId<AttachmentComponent>(
            collection_root_id));
    AttachmentEntityId parent_id = root_attachment->GetParentId();

    const WorldHashMap<AttachmentEntityId, WorldObject*>& objects_by_entity_id =
        data_->GetObjectsByEntityId();
    const auto& it = objects_by_entity_id.find(parent_id);
    if (it != objects_by_entity_id.end()) {
      return it->second;
    }

    INTR_ASSIGN_OR_RETURN(std::string collection_desc,
                          DescribeEntityForError(data_->GetEntityWorld(),
                                                 object.GetCollectionEntity()));
    INTR_ASSIGN_OR_RETURN(
        std::string root_desc,
        DescribeEntityForError(data_->GetEntityWorld(), collection_root_id));
    INTR_ASSIGN_OR_RETURN(
        std::string parent_desc,
        DescribeEntityForError(data_->GetEntityWorld(), parent_id));
    return absl::InvalidArgumentError(
        absl::StrCat("Could not determine parent object for the object "
                     "corresponding to the collection entity (",
                     collection_desc, "). The collection's root entity (",
                     root_desc, ") is a child of the entity (", parent_desc,
                     ") which is not a member of another collection that "
                     "corresponds to an object."));
  }

  absl::Status FindAndAddFrames() {
    INTR_ASSIGN_OR_RETURN(AttachmentGraph attachment_graph,
                          MakeAttachmentGraph(data_->GetEntityWorld()));

    for (auto& [_, object] : data_->GetObjectsById()) {
      INTR_RETURN_IF_ERROR(
          object->FindAndAddFrames(&non_critical_errors_, &attachment_graph));
    }

    return absl::OkStatus();
  }

  // Raises an error if not every attachment entity is represented by at least
  // one object or frame. This is a sanity check for extra safety to ensure that
  // the object-world view does not produce any undesired effects. This could
  // possibly be relaxed if we find a use case.
  absl::Status VerifyAllAttachmentEntitiesAreRepresented() {
    absl::flat_hash_set<AttachmentEntityId> represented_by_objects;
    absl::flat_hash_set<AttachmentEntityId> represented_by_frames;

    for (const auto& [object_id, object] : data_->GetObjectsById()) {
      represented_by_objects.insert(object->GetEntityIds().begin(),
                                    object->GetEntityIds().end());
      for (const Frame* frame : object->GetFrames()) {
        represented_by_objects.insert(frame->GetEntityId());
      }
    }

    for (AttachmentEntityId entity_id :
         data_->GetEntityWorld().GetTypedEntityIds<AttachmentEntityId>()) {
      if (represented_by_objects.contains(entity_id) ||
          represented_by_frames.contains(entity_id)) {
        continue;
      }

      INTR_ASSIGN_OR_RETURN(
          std::string entity_desc,
          DescribeEntityForError(data_->GetEntityWorld(), entity_id));
      std::string msg =
          absl::StrCat("Attachment entity (", entity_desc,
                       ") is not represented by any object or frame.");

      INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                            data_->GetEntityWorld().GetEntityById(entity_id));
      INTR_RETURN_IF_ERROR(
          IsFrameEntity(entity, [&msg](
                                    absl::string_view not_a_frame_explanation) {
            absl::StrAppend(&msg, " It was not classified as a frame because: ",
                            not_a_frame_explanation, ".");
          }).status());

      non_critical_errors_.push_back(std::move(msg));
    }

    return absl::OkStatus();
  }

  absl::Status SetObjectParentsAndChildren() {
    for (auto& [_, object] : data_->GetObjectsById()) {
      if (object->GetId() == RootObjectId()) continue;
      INTR_ASSIGN_OR_RETURN(WorldObject * parent_object,
                            GetParentObject(*object));
      object->SetParentAsymmetric(parent_object);
      parent_object->AddChildAsymmetric(object.get());
    }

    return absl::OkStatus();
  }

  absl::Status CreateViewAccumulatingNonCriticalErrors() {
    // Add root object corresponding to the root entity, which is always
    // there.
    INTR_RETURN_IF_ERROR(
        data_->InsertObject(std::make_unique<RootObject>(*data_)));

    // Create one object for every collection in the given world.
    //
    // We aggregate the error to make it easier for users to fix the problem
    // without needing to rebuild the world many times.
    std::vector<std::string> collection_errors;
    for (const CollectionsEntityId& collection_entity_id :
         data_->GetEntityWorld()
             .GetTypedEntityIds<CollectionsComponentType>()) {
      INTR_RETURN_IF_ERROR(AddObjectFromCollection(collection_entity_id));
    }

    // The collections in the input world can have an arbitrary order. Only
    // after all WorldObjects have been created can we set their parent/child
    // pointers.
    INTR_RETURN_IF_ERROR(SetObjectParentsAndChildren());

    INTR_RETURN_IF_ERROR(FindAndAddFrames());

    INTR_RETURN_IF_ERROR(VerifyAllAttachmentEntitiesAreRepresented());

    WorldHashSet<std::string> resource_names;
    WorldHashSet<WorldObjectName> object_names;
    for (const auto& [_, object] : data_->GetObjectsById()) {
      // Perform check that there are no duplicate object names.
      INTR_ASSIGN_OR_RETURN(bool name_is_global_alias,
                            object->NameIsGlobalAlias());
      if (name_is_global_alias) {
        INTR_RET_CHECK(object_names.emplace(object->GetName()).second)
            << "Duplicate global object name " << object->GetName();
      }

      // Make sure there are no objects with the same resource name.
      INTR_ASSIGN_OR_RETURN(std::optional<std::string> resource_name,
                            object->GetResourceName());
      if (resource_name) {
        INTR_RET_CHECK(resource_names.emplace(*resource_name).second)
            << "Duplicate resource name " << *resource_name;
      }
    }

    return absl::OkStatus();
  }

 public:
  absl::StatusOr<std::unique_ptr<ObjectWorldData>> CreateView() {
    const stats::ScopedSpan span("ObjectWorldCreationProcess/CreateView");

    absl::Status status = CreateViewAccumulatingNonCriticalErrors();

    if (status.ok() && non_critical_errors_.empty()) {
      return std::move(data_);
    } else if (!status.ok() && !absl::IsInvalidArgument(status)) {
      // Error not related to the input, return normally.
      return status;
    } else {
      // There are one or more invalid argument errors, return one Status object
      // with all error messages concatenated.
      intrinsic::StatusBuilder error =
          status.ok() ? intrinsic::StatusBuilder(absl::InvalidArgumentError(""))
                      : intrinsic::StatusBuilder(status);
      return error.SetPrepend()
             << "Given world is not compatible with the object-world "
                "view because of the following reason(s):\n\n"
             << absl::StrJoin(non_critical_errors_, "\n\n")
             << (!status.ok() && !non_critical_errors_.empty() ? "\n\n" : "");
    }
  }

  explicit ObjectWorldCreationProcess(std::unique_ptr<ObjectWorldData> data)
      : data_(std::move(data)) {}

 private:
  std::vector<std::string> non_critical_errors_;
  std::unique_ptr<ObjectWorldData> data_;
};

absl::Status ObjectWorld::VerifyWorldIsCompatible(const World& world) {
  return CreateView(world).status();
}

absl::Status ObjectWorld::VerifyWorldCouldBeCompatible(const World& world) {
  World world_clone = world.Clone();
  return CreateView(world_clone).status();
}

absl::StatusOr<std::unique_ptr<const ObjectWorld>> ObjectWorld::CreateView(
    const World& world) {
  ObjectWorldCreationProcess process(std::make_unique<ObjectWorldData>(world));
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<ObjectWorldData> data,
                        process.CreateView());
  return absl::WrapUnique(new ObjectWorld(std::move(data)));
}

absl::StatusOr<std::unique_ptr<ObjectWorld>> ObjectWorld::CreateView(
    World& world) {
  ObjectWorldCreationProcess process(
      std::make_unique<MutableObjectWorldData>(world));
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<ObjectWorldData> data,
                        process.CreateView());
  return absl::WrapUnique(new ObjectWorld(std::move(data)));
}

}  // namespace object_world
}  // namespace intrinsic
