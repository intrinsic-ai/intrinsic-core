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

#include "intrinsic/world/objects/frame_internal.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/util/aggregate_type.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/attachment_component.h"
#include "intrinsic/world/component/collections_component.h"
#include "intrinsic/world/component/collections_member_component.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/object_entity_filter.h"
#include "intrinsic/world/objects/object_world_creation_utils.h"
#include "intrinsic/world/objects/object_world_data.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/transform_node_internal.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/world.h"

namespace intrinsic {
namespace object_world {

namespace {

bool DetectIsAttachmentFrame(const World& world, WorldObject* parent,
                             AttachmentEntityId entity_id) {
  auto parent_collection_id = parent->GetCollectionEntity();
  if (!parent_collection_id.has_value()) {
    return false;
  }

  auto member_component =
      world.GetComponentByEntityId<CollectionsMemberComponent>(entity_id);
  if (!member_component.ok()) {
    return false;
  }

  auto types = member_component.value()->GetCollectionTypesByParentId(
      parent_collection_id.value());
  if (types.ok() &&
      types.value()->contains(CollectionsComponent::kAttachmentFrames)) {
    return true;
  }

  return false;
}

}  // namespace

using ::intrinsic::object_world::object_world_object_entity_filter_details::
    GetObjectEntitiesMatchingEntityFilter;

Frame::Frame(ObjectWorldResourceId id, FrameName name, WorldObject* parent,
             Frame* parent_frame, AttachmentEntityId entity_id,
             ObjectWorldData& data)
    : TransformNode(id, parent, data),
      name_(std::move(name)),
      parent_frame_(parent_frame),
      entity_id_(entity_id) {
  is_attachment_frame_ =
      DetectIsAttachmentFrame(data.GetEntityWorld(), parent, entity_id);
}

absl::Status Frame::SetName(const FrameName& name) {
  if (name_ == FlangeFrameName() || name_ == SensorFrameName()) {
    return absl::InvalidArgumentError(
        absl::Substitute("The frame with the reserved name \"$0\" is a special "
                         "frame and cannot be renamed.",
                         name_.value()));
  }

  INTR_RETURN_IF_ERROR(GetParent()->CheckFrameNameIsAvailable(name))
          .SetPrepend()
      << absl::Substitute(
             "Name of frame \"$0\" under object \"$1\" cannot be updated to "
             "\"$2\". ",
             name_.value(), GetParent()->GetName().value(), name.value());

  INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld());
  INTR_ASSIGN_OR_RETURN(WorldEntity * entity, world->GetEntityById(entity_id_));
  const std::string old_name = name_.value();
  INTR_RETURN_IF_ERROR(entity->SetLocalName(name.value()));
  name_ = name;
  if (GetParent() != nullptr) {
    GetParent()->UpdateFrameEntityLocalName(entity_id_, old_name, name.value());
  }

  return absl::OkStatus();
}

std::vector<Frame*> Frame::GetChildFrames() { return child_frames_; }

std::vector<const Frame*> Frame::GetChildFrames() const {
  return {child_frames_.begin(), child_frames_.end()};
}

std::vector<const Frame*> Frame::GetChildFramesSorted() const {
  std::vector<const Frame*> result{child_frames_.begin(), child_frames_.end()};
  absl::c_sort(result, [](const Frame* a, const Frame* b) {
    return a->GetName() < b->GetName();
  });
  return result;
}

absl::StatusOr<Frame*> Frame::CreateChildFrame(
    const FrameName& new_frame_name, const Pose3d& frame_t_new_frame) {
  return GetParent()->CreateEntityAndFrame(new_frame_name, GetEntityId(), this,
                                           frame_t_new_frame);
}

absl::StatusOr<std::vector<const WorldObject*>> Frame::GetChildObjects() const {
  std::vector<const WorldObject*> result;
  for (const WorldObject* parent_child : GetParent()->GetChildren()) {
    INTR_ASSIGN_OR_RETURN(AttachmentEntityId root_id,
                          parent_child->GetRootEntityId());
    INTR_ASSIGN_OR_RETURN(
        const AttachmentComponent* child_root_comp,
        GetEntityWorld().GetComponentByEntityId<AttachmentComponent>(root_id));
    if (child_root_comp->GetParentId() == GetEntityId()) {
      result.push_back(parent_child);
    }
  }
  return result;
}

namespace {

template <class T>
std::vector<T*> GetAllChildren(T& frame, bool include_self) {
  std::vector<T*> result;
  std::vector<T*> stack;
  absl::c_copy(frame.GetChildFrames(), std::back_inserter(stack));
  if (include_self) {
    result.push_back(&frame);
  }

  while (!stack.empty()) {
    T* current = stack.back();
    stack.pop_back();
    absl::c_copy(current->GetChildFrames(), std::back_inserter(stack));
    result.push_back(current);
  }

  return result;
}

}  // namespace

std::vector<Frame*> Frame::GetChildFramesRecursively() {
  return GetAllChildren(*this, /*include_self=*/false);
}

std::vector<const Frame*> Frame::GetChildFramesRecursively() const {
  return GetAllChildren(*this, /*include_self=*/false);
}

std::vector<const Frame*> Frame::GetChildFramesRecursivelySorted() const {
  std::vector<const Frame*> frames = GetChildFramesRecursively();
  absl::c_sort(frames, [](const Frame* a, const Frame* b) {
    return a->GetName() < b->GetName();
  });
  return frames;
}

absl::Status Frame::ReparentTo(WorldObject& new_parent) {
  return ReparentToImpl(/*new_parent_object=*/new_parent,
                        /*new_parent_frame=*/nullptr,
                        world::ObjectEntityFilter::BaseEntity());
}

absl::Status Frame::ReparentTo(WorldObject& new_parent,
                               const world::ObjectEntityFilter& filter) {
  return ReparentToImpl(/*new_parent_object=*/new_parent,
                        /*new_parent_frame=*/nullptr,
                        /*filter=*/filter);
}

absl::Status Frame::ReparentTo(Frame& new_parent) {
  return ReparentToImpl(/*new_parent_object=*/*new_parent.GetParent(),
                        /*new_parent_frame=*/&new_parent,
                        world::ObjectEntityFilter::BaseEntity());
}

absl::Status Frame::ReparentToFinalEntityOf(WorldObject& new_parent) {
  return ReparentToImpl(
      /*new_parent_object=*/new_parent,
      /*new_parent_frame=*/nullptr, world::ObjectEntityFilter::FinalEntity());
}

absl::Status Frame::ReparentToImpl(WorldObject& new_parent_object,
                                   Frame* new_parent_frame,
                                   const world::ObjectEntityFilter& filter) {
  std::vector<Frame*> self_and_children =
      GetAllChildren(*this, /*include_self*/ true);
  if (&new_parent_object != GetParent()) {
    for (Frame* child : self_and_children) {
      INTR_RETURN_IF_ERROR(
          new_parent_object.CheckFrameNameIsAvailable(child->GetName()))
              .SetPrepend()
          << absl::Substitute(
                 "Failed to reparent frame \"$0\" to object \"$1\": ",
                 GetName().value(), new_parent_object.GetName().value());
    }
  }

  AttachmentEntityId frame_entity_id = GetEntityId();
  AttachmentEntityId new_parent_entity_id;
  if (new_parent_frame) {
    new_parent_entity_id = new_parent_frame->GetEntityId();
  } else {
    INTR_ASSIGN_OR_RETURN(
        const auto parent_entity_ids,
        GetObjectEntitiesMatchingEntityFilter(new_parent_object, filter,
                                              /*expanded_list=*/false),
        _.SetPrepend() << absl::Substitute(
            "Frame \"$0\" could not find parent entity in object \"$1\": ",
            GetName().value(), new_parent_object.GetName().value()));
    if (parent_entity_ids.empty()) {
      return absl::NotFoundError(absl::Substitute(
          "Frame \"$0\" could not find parent entity within object \"$1\"",
          GetName().value(), new_parent_object.GetName().value()));
    }

    if (parent_entity_ids.size() != 1) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Cannot reparent frame \"$0\" - there were multiple matches ($1) for "
          "its parent entity within object \"$2\"",
          GetName().value(), parent_entity_ids.size(),
          new_parent_object.GetName().value()));
    }

    new_parent_entity_id = *parent_entity_ids.begin();

    // Check if the entity is a frame. If yes, set `new_parent_frame` so that
    // it can track its new child frame.
    for (Frame* frame : new_parent_object.GetFrames()) {
      if (frame->GetEntityId() == new_parent_entity_id) {
        new_parent_frame = frame;
        break;
      }
    }
  }

  // Stores the reference to child objects *before* the frames are re-parented.
  // Note that nested child objects (objects parented to other objects) are not
  // included. This is okay as the parent object for such objects is not
  // changed.
  std::vector<const WorldObject*> child_objects;
  for (Frame* f : self_and_children) {
    INTR_ASSIGN_OR_RETURN(
        const auto objects, f->GetChildObjects(),
        _.SetPrepend() << absl::Substitute("Failed to reparent frame \"$0\": ",
                                           GetName().value()));
    for (const WorldObject* object : objects) {
      child_objects.push_back(object);
    }
  }

  // Disallows reparenting to root if frame has child objects.
  if (new_parent_entity_id == kRootEntityId && !child_objects.empty()) {
    std::vector<std::string> child_object_names;
    child_object_names.reserve(child_objects.size());
    for (const auto& object : child_objects) {
      child_object_names.push_back(object->GetName().value());
    }

    // Sorts the names to make the error message deterministic.
    absl::c_sort(child_object_names);
    return absl::InvalidArgumentError(absl::Substitute(
        "Frame \"$0\" cannot be parented to root "
        "because it has child objects: [$1]. Reparent these objects to other "
        "frames or objects before parenting \"$0\" to root.",
        GetName().value(), absl::StrJoin(child_object_names, ", ")));
  }

  Pose3d new_parent_t_frame =
      GetEntityWorld().GetTransform(new_parent_entity_id, frame_entity_id);
  INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld());
  INTR_RETURN_IF_ERROR(world->ReparentEntity(new_parent_entity_id,
                                             frame_entity_id,
                                             new_parent_t_frame))
          .SetPrepend()
      << absl::Substitute("Failed to reparent frame \"$0\": ",
                          GetName().value());

  // Detach from parent frame if necessary.
  if (GetParentFrame()) {
    GetParentFrame()->RemoveChildFrameAsymmetric(this);
  }

  if (&new_parent_object != GetParent()) {
    // Move frame and all children to new parent object and transfer ownership.
    WorldObject& old_parent_object = *GetParent();
    for (Frame* child : self_and_children) {
      // Change `is_attachment_frame` to false if necessary before moving to the
      // new parent object. This is necessary because the new parent object may
      // not allow attachment frames due to lack of a collection entity.
      bool new_parent_allow_attachment_frame =
          new_parent_object.GetCollectionEntity().has_value();
      bool is_attachment_frame =
          child->IsAttachmentFrame() && new_parent_allow_attachment_frame;
      INTR_RETURN_IF_ERROR(child->SetIsAttachmentFrame(is_attachment_frame))
              .SetPrepend()
          << absl::Substitute("Failed to reparent frame \"$0\": ",
                              GetName().value());

      INTR_ASSIGN_OR_RETURN(
          std::unique_ptr<Frame> owned_child,
          old_parent_object.RemoveFrameAsymmetric(*child),
          _.SetPrepend() << absl::Substitute(
              "Failed to reparent frame \"$0\": ", GetName().value()));
      INTR_RETURN_IF_ERROR(
          new_parent_object.AddFrameAsymmetric(std::move(owned_child)))
              .SetPrepend()
          << absl::Substitute("Failed to reparent frame \"$0\": ",
                              GetName().value());
      child->SetParentAsymmetric(&new_parent_object);
      if (DetectIsAttachmentFrame(GetEntityWorld(), &new_parent_object,
                                  child->GetEntityId()) !=
          child->IsAttachmentFrame()) {
        return absl::InternalError(absl::Substitute(
            "After reparenting, frame '$0' is_attachment_frame is not "
            "consistent with the detection logic. This will cause data loss if "
            "a new view is created on the entity world.",
            child->GetName().value()));
      }
    }

    // Similar to frames, update the parent-child relationships in the object
    // view. Note that the base entities of these child objects are already
    // parented to the respective frame entities.
    for (auto& child_object : child_objects) {
      WorldObject* object = const_cast<WorldObject*>(child_object);
      INTR_RETURN_IF_ERROR(object->GetParent()->RemoveChildAsymmetric(*object))
              .SetPrepend()
          << absl::Substitute(
                 "Failed to reparent frame \"$0\" because of failure to remove "
                 "child object \"$1\" from parent \"$2\": ",
                 GetName().value(), object->GetName(),
                 object->GetParent()->GetName());
      object->SetParentAsymmetric(&new_parent_object);
      new_parent_object.AddChildAsymmetric(object);
    }
  }

  // Connect with new parent frame (or unset).
  parent_frame_ = new_parent_frame;
  if (new_parent_frame) {
    new_parent_frame->AddChildFrameAsymmetric(this);
  }

  return absl::OkStatus();
}

absl::Status Frame::DeleteIfNoChildFrames() {
  return Delete(/*delete_child_frames=*/false);
}

absl::Status Frame::DeleteIncludingChildFrames() {
  return Delete(/*delete_child_frames=*/true);
}

absl::Status Frame::Delete(bool delete_child_frames) {
  std::vector<Frame*> child_frames = GetChildFrames();

  // If we have any child frames, and we've not requested to delete them, then
  // simply error out.
  if (!child_frames.empty() && !delete_child_frames) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Frame \"$0\" under object \"$1\" could not be "
        "deleted since force-delete was not requested and it "
        "has the following child frames: $2.",
        GetName().value(), GetParent()->GetName().value(),
        absl::StrJoin(child_frames, ", ",
                      [](std::string* out, const Frame* frame) {
                        out->append(frame->GetName().value());
                      })));
  }

  // If we have any entities that are descendants of this frame, but are not
  // frames themselves, then we cannot delete the frame.
  WorldHashSet<AttachmentEntityId> child_frame_ids;
  std::vector<const Frame*> child_frame_stack{this};
  while (!child_frame_stack.empty()) {
    const Frame* frame = child_frame_stack.back();
    child_frame_stack.pop_back();
    if (frame->CheckIsMovable().ok()) {
      child_frame_ids.insert(frame->GetEntityId());
      for (const Frame* child : frame->GetChildFrames()) {
        child_frame_stack.push_back(child);
      }
    }
    INTR_ASSIGN_OR_RETURN(auto child_objects, frame->GetChildObjects());
    if (!child_objects.empty()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Frame \"$0\" under object \"$1\" could not be "
          "deleted since $2 \"$3\" has child objects: [$4].",
          GetName().value(), GetParent()->GetName().value(),
          (frame == this ? "this frame" : "one of its child frames"),
          frame->GetName().value(),
          absl::StrJoin(child_objects, ", ",
                        [](std::string* out, const WorldObject* object) {
                          out->append(object->GetName().value());
                        })));
    }
  }

  // Check actual collection entities, since we may have deleted some of the
  // parent's entity_id_ list as part of deleting the object.
  WorldHashSet<AttachmentEntityId> entity_ids;
  if (auto collection_entity_id = GetParent()->GetCollectionEntity();
      collection_entity_id.has_value()) {
    INTR_ASSIGN_OR_RETURN(
        const CollectionsComponent* collection,
        GetEntityWorld().GetComponentByEntityId<CollectionsComponent>(
            *collection_entity_id));
    for (auto member_id : collection->GetAllCollectionMembers()) {
      if (auto attachment_entity_id =
              GetEntityWorld().ValidateEntity<AttachmentEntityId>(member_id);
          attachment_entity_id.ok()) {
        entity_ids.insert(*attachment_entity_id);
      }
    }
  } else {
    entity_ids = GetParent()->GetEntityIds();
  }

  for (const AttachmentEntityId entity_id : entity_ids) {
    // Skip frames.
    if (child_frame_ids.contains(entity_id)) {
      continue;
    }
    INTR_ASSIGN_OR_RETURN(
        const AttachmentComponent* attachment,
        GetEntityWorld().GetComponentByEntityId<AttachmentComponent>(
            entity_id));
    if (child_frame_ids.contains(attachment->GetParentId())) {
      INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                            GetEntityWorld().GetEntityById(entity_id));
      return absl::InvalidArgumentError(
          absl::Substitute("Frame \"$0\" under object \"$1\" could not be "
                           "deleted since it has non-frame child entity \"$2\"",
                           GetName().value(), GetParent()->GetName().value(),
                           entity->GetLocalName()));
    }
  };

  for (Frame* child_frame : GetChildFrames()) {
    INTR_RETURN_IF_ERROR(child_frame->Delete(/*delete_child_frames*/ true));
  }

  // If frame is movable that means that it's not a "virtual" frame that's
  // mirroring some other entity (like a sensor), and can be deleted from the
  // entity world.
  const bool delete_from_world = CheckIsMovable().ok();

  if (GetParentFrame()) {
    GetParentFrame()->RemoveChildFrameAsymmetric(this);
  }
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<Frame> self,
                        GetParent()->RemoveFrameAsymmetric(*this));

  if (delete_from_world) {
    INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld());
    INTR_RETURN_IF_ERROR(world->SafelyRemoveEntity(GetEntityId()));
  }

  return absl::OkStatus();
}

absl::StatusOr<Pose3d> Frame::GetParentTThis() const {
  AttachmentEntityId parent_id;
  if (GetParentFrame()) {
    INTR_ASSIGN_OR_RETURN(parent_id,
                          GetParentFrame()->GetTransformOriginEntityId());
  } else {
    INTR_ASSIGN_OR_RETURN(parent_id, GetParent()->GetTransformOriginEntityId());
  }
  return GetEntityWorld().GetTransform(parent_id, entity_id_);
}

absl::StatusOr<AttachmentEntityId> Frame::GetTransformOriginEntityId() const {
  return entity_id_;
}

absl::StatusOr<AttachmentEntityId> Frame::GetTransformEntityId(
    const world::ObjectEntityFilter& filter) const {
  if (!filter.EntityIds().empty() && filter.EntityIds() != std::set{GetId()}) {
    return absl::InvalidArgumentError(
        "Frame entity filter contains IDs that do not match the frame ID.");
  }

  if (!filter.EntityNames().empty()) {
    INTR_ASSIGN_OR_RETURN(
        const WorldEntity* frame_entity,
        GetObjectWorldData().GetEntityWorld().GetEntityById(entity_id_));
    if (filter.EntityNames() != std::set{frame_entity->GetLocalName()}) {
      return absl::InvalidArgumentError(
          "Frame entity filter contains names that do not match the frame "
          "name.");
    }
  }

  return entity_id_;
}

void Frame::AddChildFrameAsymmetric(Frame* child) {
  child_frames_.push_back(child);
}

void Frame::RemoveChildFrameAsymmetric(const Frame* child) {
  child_frames_.erase(absl::c_find(child_frames_, child));
}

absl::Status Frame::CheckIsMovable(
    const std::optional<world::ObjectEntityFilter>& filter) const {
  if (!GetParent()) {
    return absl::OkStatus();
  }
  for (AttachmentEntityId object_entity_id : GetParent()->GetEntityIds()) {
    if (GetEntityId() == object_entity_id) {
      INTR_ASSIGN_OR_RETURN(const WorldEntity* frame_entity,
                            GetObjectWorldData().GetEntityWorld().GetEntityById(
                                object_entity_id));
      std::string explanation;
      auto store_explanation = [&explanation](absl::string_view e) {
        explanation = e;
      };
      INTR_ASSIGN_OR_RETURN(bool is_frame_entity,
                            IsFrameEntity(frame_entity, store_explanation));
      if (!is_frame_entity) {
        // This frame points to a non-frame entity of the parent object and is
        // not explicitly represented with a dedicated entity. Modifying this
        // frame would modify the object itself, which we currently do not
        // allow. Adjust this if necessary. E.g., we could add a frame entity on
        // the fly.
        return absl::InvalidArgumentError(absl::Substitute(
            "Frame \"$0\" cannot be moved since it is virtual and was "
            "automatically inferred from the parent object: $1",
            GetName().value(), explanation));
      }
    }
  }

  return absl::OkStatus();
}

absl::Status Frame::SetIsAttachmentFrame(bool is_attachment_frame) {
  if (is_attachment_frame_ == is_attachment_frame) {
    return absl::OkStatus();
  }

  auto parent_collection_id = GetParent()->GetCollectionEntity();
  if (!parent_collection_id.has_value()) {
    return absl::InvalidArgumentError(
        "Frame parent is not a collection, cannot change attachment frame "
        "designation");
  }

  INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld());
  INTR_ASSIGN_OR_RETURN(auto parent_collection,
                        world->GetComponentByEntityId<CollectionsComponent>(
                            parent_collection_id.value()));

  if (is_attachment_frame) {
    INTR_ASSIGN_OR_RETURN(
        auto member_component,
        world->GetOrCreateComponentByEntityId<CollectionsMemberComponent>(
            GetEntityId()));

    INTR_RETURN_IF_ERROR(member_component->AddParentCollection(
        parent_collection_id.value(), CollectionsComponent::kAttachmentFrames));

    // Update the list of members in the parent collection.
    auto members = parent_collection->GetCollectionMembers(
        CollectionsComponent::kAttachmentFrames);
    members.emplace_back(GetEntityId().value());
    INTR_RETURN_IF_ERROR(parent_collection->SetCollectionMembers(
        CollectionsComponent::kAttachmentFrames, members));
  } else {  // Remove the designation
    INTR_ASSIGN_OR_RETURN(auto entity, world->GetEntityById(GetEntityId()));
    INTR_ASSIGN_OR_RETURN(auto member_component,
                          entity->GetComponent<CollectionsMemberComponent>());

    INTR_RETURN_IF_ERROR(member_component->DeleteParentCollection(
        parent_collection_id.value(), CollectionsComponent::kAttachmentFrames));

    // Update the list of members in the parent collection.
    auto members = parent_collection->GetCollectionMembers(
        CollectionsComponent::kAttachmentFrames);
    members.erase(std::remove(members.begin(), members.end(), GetEntityId()),
                  members.end());
    INTR_RETURN_IF_ERROR(parent_collection->SetCollectionMembers(
        CollectionsComponent::kAttachmentFrames, members));

    // If this was the last collection this entity is part of, we can remove the
    // whole component.
    auto all_mappings = member_component->GetParentCollectionsIdToTypesMap();
    if (all_mappings.empty()) {
      INTR_RETURN_IF_ERROR(
          entity->RemoveComponent<CollectionsMemberComponent>());
    }
  }

  is_attachment_frame_ = is_attachment_frame;
  return absl::OkStatus();
}

}  // namespace object_world
}  // namespace intrinsic
