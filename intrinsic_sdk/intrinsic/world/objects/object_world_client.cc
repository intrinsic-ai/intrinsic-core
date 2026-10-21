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

#include "intrinsic/world/objects/object_world_client.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/wrappers.pb.h"
#include "grpcpp/client_context.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/equipment/equipment_utils.h"
#include "intrinsic/icon/equipment/icon_equipment.pb.h"
#include "intrinsic/icon/proto/cart_space_conversion.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.pb.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/pose.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/frame.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_entity_filter.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/transform_node.h"
#include "intrinsic/world/objects/world_object.h"
#include "intrinsic/world/proto/collision_settings.pb.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"
#include "intrinsic/world/robot_payload/robot_payload.h"

namespace intrinsic {
namespace world {

using ::intrinsic_proto::world::FrameReference;
using ::intrinsic_proto::world::Object;
using ::intrinsic_proto::world::ObjectReference;
using ::intrinsic_proto::world::ObjectView;
using ::intrinsic_proto::world::TransformNodeReference;
using ::intrinsic_proto::world::TransformNodeReferenceByName;
using ::intrinsic_proto::world::UpdateObjectNameRequest;

namespace {

absl::StatusOr<intrinsic_proto::world::Object> CallGetObjectUsingFullView(
    intrinsic_proto::world::GetObjectRequest request,
    intrinsic_proto::world::ObjectWorldService::StubInterface&
        object_world_service) {
  grpc::ClientContext ctx;
  intrinsic_proto::world::Object response;
  request.set_view(intrinsic_proto::world::ObjectView::FULL);
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(object_world_service.GetObject(&ctx, request, &response)));
  return response;
}

absl::StatusOr<WorldObject> GetObjectAsWorldObject(
    intrinsic_proto::world::GetObjectRequest request,
    intrinsic_proto::world::ObjectWorldService::StubInterface&
        object_world_service) {
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::world::Object proto,
      CallGetObjectUsingFullView(std::move(request), object_world_service));
  if (proto.type() == intrinsic_proto::world::ObjectType::KINEMATIC_OBJECT) {
    INTR_ASSIGN_OR_RETURN(KinematicObject kinematic_object,
                          KinematicObject::Create(std::move(proto)));
    return kinematic_object.AsWorldObject();
  }
  return WorldObject::Create(std::move(proto));
}

absl::StatusOr<Frame> CallGetFrame(
    const intrinsic_proto::world::GetFrameRequest& request,
    intrinsic_proto::world::ObjectWorldService::StubInterface&
        object_world_service) {
  grpc::ClientContext ctx;
  intrinsic_proto::world::Frame response;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(object_world_service.GetFrame(&ctx, request, &response)));
  return Frame::Create(std::move(response));
}

absl::Status CallUpdateTransform(
    ObjectWorldResourceId node_a_id,
    std::optional<ObjectEntityFilter> node_a_filter,
    ObjectWorldResourceId node_b_id,
    std::optional<ObjectEntityFilter> node_b_filter,
    std::optional<ObjectWorldResourceId> node_to_update_id, Pose3d a_t_b,
    const std::string& world_id,
    intrinsic_proto::world::ObjectWorldService::StubInterface&
        object_world_service) {
  grpc::ClientContext ctx;
  intrinsic_proto::world::UpdateTransformRequest request;
  request.set_world_id(world_id);
  request.mutable_node_a()->set_id(node_a_id.value());
  request.mutable_node_b()->set_id(node_b_id.value());
  if (node_a_filter.has_value()) {
    *request.mutable_node_a_filter() = node_a_filter->ToProto();
  }
  if (node_b_filter.has_value()) {
    *request.mutable_node_b_filter() = node_b_filter->ToProto();
  }
  if (node_to_update_id.has_value()) {
    request.mutable_node_to_update()->set_id(node_to_update_id->value());
  }

  *request.mutable_a_t_b() = ToProto(a_t_b);
  // Use minimalistic view since we are ignoring the response.
  request.set_view(intrinsic_proto::world::ObjectView::BASIC);
  intrinsic_proto::world::UpdateTransformResponse response;
  return ToAbslStatus(
      object_world_service.UpdateTransform(&ctx, request, &response));
}

absl::Status CallCreateFrame(
    intrinsic_proto::world::CreateFrameRequest request_with_parent,
    const FrameName& new_frame_name, const Pose3d& parent_t_new_frame,
    const std::string& world_id,
    intrinsic_proto::world::ObjectWorldService::StubInterface&
        object_world_service) {
  grpc::ClientContext ctx;
  request_with_parent.set_world_id(world_id);
  request_with_parent.set_new_frame_name(new_frame_name.value());
  *request_with_parent.mutable_parent_t_new_frame() =
      ToProto(parent_t_new_frame);
  intrinsic_proto::world::Frame response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service.CreateFrame(&ctx, request_with_parent, &response)));
  return absl::OkStatus();
}


intrinsic_proto::world::CreateObjectRequest BaseCreateObjectRequest(
    const absl::string_view world_id, const WorldObjectName& name,
    const ObjectWorldClient::CreateObjectOptions& options) {
  intrinsic_proto::world::CreateObjectRequest request;
  request.set_name(name.value());
  request.set_name_is_global_alias(true);
  request.set_world_id(world_id);
  Pose3d object_t_parent = Pose3d::Identity();
  if (options.parent.has_value()) {
    if (auto parnet_object = WorldObject::FromTransformNode(*options.parent);
        parnet_object.has_value()) {
      *request.mutable_parent_object()
           ->mutable_reference()
           ->mutable_by_name()
           ->mutable_object_name() = parnet_object->Name().value();
      request.mutable_parent_object()
          ->mutable_entity_filter()
          ->set_include_final_entity(true);
    } else if (auto parent_frame = Frame::FromTransformNode(*options.parent);
               parent_frame.has_value()) {
      *request.mutable_parent_object()
           ->mutable_reference()
           ->mutable_by_name()
           ->mutable_object_name() = parent_frame->ObjectName().value();
      *request.mutable_parent_object()
           ->mutable_entity_filter()
           ->add_entity_names() = parent_frame->Name().value();
      object_t_parent = parent_frame->ParentTThis();
    }
  } else {
    request.mutable_parent_object()->mutable_reference()->set_id(
        RootObjectId().value());
  }
  if (options.parent_t_created_object.has_value()) {
    *request.mutable_parent_object_t_created_object() =
        ToProto(object_t_parent * options.parent_t_created_object.value());
  } else {
    *request.mutable_parent_object_t_created_object() =
        ToProto(object_t_parent);
  }
  return request;
}


}  // namespace

ObjectWorldClient::ObjectWorldClient(
    absl::string_view world_id,
    std::shared_ptr<ObjectWorldService::StubInterface> object_world_service)
    : ObjectWorldClient(world_id, std::move(object_world_service),
                        /*owns_world=*/false) {}

ObjectWorldClient::ObjectWorldClient(
    absl::string_view world_id,
    std::shared_ptr<ObjectWorldService::StubInterface> object_world_service,
    bool owns_world)
    : world_id_(world_id),
      object_world_service_(std::move(object_world_service)),
      owns_world_(owns_world) {}

ObjectWorldClient::~ObjectWorldClient() { DeleteOwnedWorld(); }

ObjectWorldClient::ObjectWorldClient(ObjectWorldClient&& other) noexcept
    : world_id_(std::move(other.world_id_)),
      object_world_service_(std::move(other.object_world_service_)),
      owns_world_(other.owns_world_) {
  other.owns_world_ = false;
}

ObjectWorldClient& ObjectWorldClient::operator=(
    ObjectWorldClient&& other) noexcept {
  if (this != &other) {
    DeleteOwnedWorld();
    world_id_ = std::move(other.world_id_);
    object_world_service_ = std::move(other.object_world_service_);
    owns_world_ = other.owns_world_;
    other.owns_world_ = false;
  }
  return *this;
}

absl::StatusOr<ObjectWorldClient> ObjectWorldClient::Clone() const {
  grpc::ClientContext ctx;
  intrinsic_proto::world::CloneWorldRequest request;
  request.set_world_id(world_id_);
  intrinsic_proto::world::WorldMetadata response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_->CloneWorld(&ctx, request, &response)));
  return ObjectWorldClient(response.id(), object_world_service_,
                           /*owns_world=*/true);
}

void ObjectWorldClient::DeleteOwnedWorld() {
  if (!owns_world_ || object_world_service_ == nullptr || world_id_.empty()) {
    return;
  }
  grpc::ClientContext ctx;
  intrinsic_proto::world::DeleteWorldRequest request;
  request.set_world_id(world_id_);
  google::protobuf::Empty response;
  if (absl::Status status = ToAbslStatus(
          object_world_service_->DeleteWorld(&ctx, request, &response));
      !status.ok()) {
    LOG(WARNING) << "Failed to delete cloned world '" << world_id_
                 << "': " << status;
  }
  owns_world_ = false;
}

namespace {

absl::StatusOr<TransformNode> GetTransformNodeById(
    const ObjectWorldResourceId& id, const ObjectWorldClient& client) {
  absl::StatusOr<WorldObject> object = client.GetObject(id);
  if (object.ok()) {
    return object->AsTransformNode();
  } else if (absl::IsNotFound(object.status())) {
    INTR_ASSIGN_OR_RETURN(Frame frame, client.GetFrame(id));
    return frame.AsTransformNode();
  } else {
    return object.status();
  }
}

absl::StatusOr<TransformNode> GetTransformNodeByNameReference(
    const TransformNodeReferenceByName& reference,
    const ObjectWorldClient& client) {
  switch (reference.transform_node_reference_by_name_case()) {
    case TransformNodeReferenceByName::kObject: {
      INTR_ASSIGN_OR_RETURN(
          WorldObject object,
          client.GetObject(WorldObjectName(reference.object().object_name())));
      return object.AsTransformNode();
    }
    case TransformNodeReferenceByName::kFrame: {
      INTR_ASSIGN_OR_RETURN(
          Frame frame,
          client.GetFrame(WorldObjectName(reference.frame().object_name()),
                          FrameName(reference.frame().frame_name())));
      return frame.AsTransformNode();
    }
    case TransformNodeReferenceByName::TRANSFORM_NODE_REFERENCE_BY_NAME_NOT_SET:
      return absl::InvalidArgumentError(
          "The oneof "
          "'TransformNodeReferenceByName.transform_node_reference_by_name' "
          "must be set.");
  }
}

}  // namespace

absl::StatusOr<TransformNode> ObjectWorldClient::GetTransformNode(
    const TransformNodeReference& reference) const {
  switch (reference.transform_node_reference_case()) {
    case TransformNodeReference::kId:
      return GetTransformNodeById(ObjectWorldResourceId(reference.id()), *this);
    case TransformNodeReference::kByName:
      return GetTransformNodeByNameReference(reference.by_name(), *this);
    case TransformNodeReference::TRANSFORM_NODE_REFERENCE_NOT_SET:
      return absl::InvalidArgumentError(
          "The oneof 'TransformNodeReference.transform_node_reference' must be "
          "set.");
  }
}

absl::StatusOr<WorldObject> ObjectWorldClient::GetRootObject() const {
  return GetObject(RootObjectId());
}

absl::StatusOr<WorldObject> ObjectWorldClient::GetObject(
    const ObjectReference& reference) const {
  switch (reference.object_reference_case()) {
    case ObjectReference::kId:
      return GetObject(ObjectWorldResourceId(reference.id()));
    case ObjectReference::kByName:
      return GetObject(WorldObjectName(reference.by_name().object_name()));
    case ObjectReference::OBJECT_REFERENCE_NOT_SET:
      return absl::InvalidArgumentError(
          "The oneof 'ObjectReference.object_reference' must be set.");
  }
}

absl::StatusOr<WorldObject> ObjectWorldClient::GetObject(
    const ObjectWorldResourceId& id) const {
  intrinsic_proto::world::GetObjectRequest request;
  request.set_world_id(world_id_);
  request.mutable_object()->set_id(id.value());
  return GetObjectAsWorldObject(std::move(request), *object_world_service_);
}

absl::StatusOr<WorldObject> ObjectWorldClient::GetObject(
    const WorldObjectName& name) const {
  intrinsic_proto::world::GetObjectRequest request;
  request.set_world_id(world_id_);
  request.mutable_object()->mutable_by_name()->set_object_name(name.value());
  return GetObjectAsWorldObject(std::move(request), *object_world_service_);
}

absl::StatusOr<WorldObject> ObjectWorldClient::GetObject(
    const intrinsic_proto::resources::ResourceHandle& resource_handle) const {
  intrinsic_proto::world::GetObjectRequest request;
  request.set_world_id(world_id_);
  request.set_resource_handle_name(resource_handle.name());
  return GetObjectAsWorldObject(std::move(request), *object_world_service_);
}

absl::StatusOr<WorldObject> ObjectWorldClient::GetObject(
    const WorldObject& object) const {
  return GetObject(object.Id());
}

absl::StatusOr<std::vector<WorldObject>> ObjectWorldClient::ListObjects(
    std::vector<intrinsic_proto::world::ObjectReference> query_objects) const {
  intrinsic_proto::world::ListObjectsRequest request;
  request.set_world_id(world_id_);
  request.set_view(intrinsic_proto::world::ObjectView::FULL);

  for (auto& query_object : query_objects) {
    *request.add_query_objects() = std::move(query_object);
  }

  grpc::ClientContext ctx;
  intrinsic_proto::world::ListObjectsResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_->ListObjects(&ctx, request, &response)));
  std::vector<WorldObject> objects;
  objects.reserve(response.objects_size());
  for (auto&& object_proto : *response.mutable_objects()) {
    INTR_ASSIGN_OR_RETURN(WorldObject object,
                          WorldObject::Create(std::move(object_proto)));
    objects.push_back(std::move(object));
  }
  return objects;
}

absl::StatusOr<std::vector<WorldObjectName>>
ObjectWorldClient::ListObjectNames() const {
  intrinsic_proto::world::ListObjectsRequest request;
  request.set_world_id(world_id_);
  request.set_view(intrinsic_proto::world::ObjectView::BASIC);
  grpc::ClientContext ctx;
  intrinsic_proto::world::ListObjectsResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_->ListObjects(&ctx, request, &response)));
  std::vector<WorldObjectName> objects;
  objects.reserve(response.objects_size());
  for (const auto& object_proto : response.objects()) {
    objects.emplace_back(object_proto.name());
  }
  return objects;
}

absl::Status ObjectWorldClient::DeleteObject(
    const WorldObject& object, const DeleteObjectWithChildrenOption& option) {
  intrinsic_proto::world::DeleteObjectRequest request;
  request.set_world_id(world_id_);
  request.mutable_object()->set_id(object.Id().value());
  if (option == DeleteObjectWithChildrenOption::kDeleteChildren) {
    request.set_force(true);
  } else if (option == DeleteObjectWithChildrenOption::kReparentChildren) {
    request.set_reparent_child_objects(true);
  }

  grpc::ClientContext ctx;
  google::protobuf::Empty response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_->DeleteObject(&ctx, request, &response)));
  return absl::OkStatus();
}

absl::StatusOr<KinematicObject> ObjectWorldClient::GetKinematicObject(
    const ObjectReference& reference) const {
  switch (reference.object_reference_case()) {
    case ObjectReference::kId:
      return GetKinematicObject(ObjectWorldResourceId(reference.id()));
    case ObjectReference::kByName:
      return GetKinematicObject(
          WorldObjectName(reference.by_name().object_name()));
    case ObjectReference::OBJECT_REFERENCE_NOT_SET:
      return absl::InvalidArgumentError("");
  }
}

absl::StatusOr<KinematicObject> ObjectWorldClient::GetKinematicObject(
    const ObjectWorldResourceId& id) const {
  intrinsic_proto::world::GetObjectRequest request;
  request.set_world_id(world_id_);
  request.mutable_object()->set_id(id.value());
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::world::Object proto,
      CallGetObjectUsingFullView(std::move(request), *object_world_service_));
  if (proto.type() != intrinsic_proto::world::ObjectType::KINEMATIC_OBJECT) {
    return absl::InvalidArgumentError(
        absl::StrCat("The object with id \"", id.value(),
                     "\" exists but it is not a kinematic object."));
  }
  return KinematicObject::Create(std::move(proto));
}

absl::StatusOr<KinematicObject> ObjectWorldClient::GetKinematicObject(
    const WorldObjectName& name) const {
  intrinsic_proto::world::GetObjectRequest request;
  request.set_world_id(world_id_);
  request.mutable_object()->mutable_by_name()->set_object_name(name.value());
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::world::Object proto,
      CallGetObjectUsingFullView(std::move(request), *object_world_service_));
  if (proto.type() != intrinsic_proto::world::ObjectType::KINEMATIC_OBJECT) {
    return absl::InvalidArgumentError(
        absl::StrCat("The object with name \"", name.value(),
                     "\" exists but it is not a kinematic object."));
  }
  return KinematicObject::Create(std::move(proto));
}

absl::StatusOr<KinematicObject> ObjectWorldClient::GetKinematicObject(
    const intrinsic_proto::resources::ResourceHandle& resource_handle) const {
  intrinsic_proto::world::GetObjectRequest request;
  request.set_world_id(world_id_);
  // TODO(b/321881202): Remove Icon part logic below.
  auto icon_pos_part_it =
      resource_handle.resource_data().find(icon::kIcon2PositionPartKey);

  request.set_resource_handle_name(resource_handle.name());
  if (icon_pos_part_it != resource_handle.resource_data().end()) {
    intrinsic_proto::icon::Icon2PositionPart icon_position_part;
    if (!icon_pos_part_it->second.contents().UnpackTo(&icon_position_part)) {
      return absl::NotFoundError(
          absl::StrCat("Resource handle ", resource_handle.name(),
                       " does not have any Icon2PositionPart information."));
    }
    if (icon_position_part.object_names().size() == 1) {
      const auto& object_name =
          icon_position_part.object_names().begin()->second;
      LOG(INFO) << "Using kinematic object from position part object_names: "
                << object_name;
      return GetKinematicObject(WorldObjectName(object_name));
    }
    if (!icon_position_part.world_robot_collection_name().empty()) {
      LOG(INFO) << "Using kinematic object from world robot collection: "
                << icon_position_part.world_robot_collection_name();
      request.set_resource_handle_name(
          icon_position_part.world_robot_collection_name());
    }
  }
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::world::Object proto,
      CallGetObjectUsingFullView(std::move(request), *object_world_service_));
  if (proto.type() != intrinsic_proto::world::ObjectType::KINEMATIC_OBJECT) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The object associated with the resource handle name \"",
        resource_handle.name(), "\" exists but it is not a kinematic object."));
  }
  return KinematicObject::Create(std::move(proto));
}

absl::StatusOr<KinematicObject> ObjectWorldClient::GetKinematicObject(
    const KinematicObject& object) const {
  return GetKinematicObject(object.Id());
}

absl::StatusOr<Frame> ObjectWorldClient::GetFrame(
    const FrameReference& reference) const {
  switch (reference.frame_reference_case()) {
    case FrameReference::kId:
      return GetFrame(ObjectWorldResourceId(reference.id()));
    case FrameReference::kByName:
      return GetFrame(WorldObjectName(reference.by_name().object_name()),
                      FrameName(reference.by_name().frame_name()));
    case FrameReference::FRAME_REFERENCE_NOT_SET:
      return absl::InvalidArgumentError("");
  }
}

absl::StatusOr<Frame> ObjectWorldClient::GetFrame(
    const ObjectWorldResourceId& id) const {
  intrinsic_proto::world::GetFrameRequest request;
  request.set_world_id(world_id_);
  request.mutable_frame()->set_id(id.value());
  return CallGetFrame(request, *object_world_service_);
}

absl::StatusOr<Frame> ObjectWorldClient::GetFrame(
    const WorldObjectName& object_name, const FrameName& frame_name) const {
  intrinsic_proto::world::GetFrameRequest request;
  request.set_world_id(world_id_);
  request.mutable_frame()->mutable_by_name()->set_object_name(
      object_name.value());
  request.mutable_frame()->mutable_by_name()->set_frame_name(
      frame_name.value());
  return CallGetFrame(request, *object_world_service_);
}

absl::StatusOr<Frame> ObjectWorldClient::GetFrame(
    const intrinsic_proto::resources::ResourceHandle& object_resource_handle,
    const FrameName& frame_name) const {
  // This could also be supported directly by the backend so that we would not
  // have to request the entire object with all of its frames.
  INTR_ASSIGN_OR_RETURN(WorldObject object, GetObject(object_resource_handle));
  return object.GetFrame(frame_name);
}

absl::StatusOr<Frame> ObjectWorldClient::GetFrame(const Frame& frame) const {
  return GetFrame(frame.Id());
}

absl::Status ObjectWorldClient::CreateFrame(const FrameName& new_frame_name,
                                            const WorldObject& parent_object,
                                            const Pose3d& parent_t_new_frame) {
  intrinsic_proto::world::CreateFrameRequest request;
  request.mutable_parent_object_with_filter()->mutable_reference()->set_id(
      parent_object.Id().value());
  return CallCreateFrame(std::move(request), new_frame_name, parent_t_new_frame,
                         world_id_, *object_world_service_);
}

absl::Status ObjectWorldClient::CreateFrame(const FrameName& new_frame_name,
                                            const Frame& parent_frame,
                                            const Pose3d& parent_t_new_frame) {
  intrinsic_proto::world::CreateFrameRequest request;
  request.mutable_parent_frame()->set_id(parent_frame.Id().value());
  return CallCreateFrame(std::move(request), new_frame_name, parent_t_new_frame,
                         world_id_, *object_world_service_);
}

absl::Status ObjectWorldClient::UpdateObjectName(
    const WorldObject& object, const WorldObjectName& new_name) {
  grpc::ClientContext ctx;
  UpdateObjectNameRequest request;
  request.set_world_id(world_id_);
  request.mutable_object()->set_id(object.Id().value());
  request.set_name(new_name.value());
  // Use minimalistic view since we are ignoring the response.
  request.set_view(ObjectView::BASIC);
  request.set_name_is_global_alias(true);
  Object response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_->UpdateObjectName(&ctx, request, &response)));
  return absl::OkStatus();
}

absl::Status ObjectWorldClient::UpdateObjectName(
    const WorldObject& object, const WorldObjectName& new_name,
    WorldObjectNameType /*name_type*/) {
  return UpdateObjectName(object, new_name);
}

absl::Status ObjectWorldClient::UpdateFrameName(const Frame& frame,
                                                const FrameName& new_name) {
  grpc::ClientContext ctx;
  intrinsic_proto::world::UpdateFrameNameRequest request;
  request.set_world_id(world_id_);
  request.mutable_frame()->set_id(frame.Id().value());
  request.set_name(new_name.value());
  intrinsic_proto::world::Frame response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_->UpdateFrameName(&ctx, request, &response)));
  return absl::OkStatus();
}

absl::StatusOr<Pose3d> ObjectWorldClient::GetTransform(
    const TransformNode& node_a, const TransformNode& node_b) const {
  return GetTransform(node_a, std::nullopt, node_b, std::nullopt);
}

absl::StatusOr<Pose3d> ObjectWorldClient::GetTransform(
    const TransformNode& node) const {
  return GetTransform(RootObjectId(), std::nullopt, node.Id(), std::nullopt);
}

absl::StatusOr<Pose3d> ObjectWorldClient::GetTransform(
    const TransformNode& node_a,
    std::optional<ObjectEntityFilter> node_a_filter,
    const TransformNode& node_b,
    std::optional<ObjectEntityFilter> node_b_filter) const {
  return GetTransform(node_a.Id(), node_a_filter, node_b.Id(), node_b_filter);
}

absl::StatusOr<Pose3d> ObjectWorldClient::GetTransform(
    const ObjectWorldResourceId& node_a_id,
    std::optional<ObjectEntityFilter> node_a_filter,
    const ObjectWorldResourceId& node_b_id,
    std::optional<ObjectEntityFilter> node_b_filter) const {
  grpc::ClientContext ctx;
  intrinsic_proto::world::GetTransformRequest request;
  request.set_world_id(world_id_);
  request.mutable_node_a()->set_id(node_a_id.value());
  request.mutable_node_b()->set_id(node_b_id.value());

  if (node_a_filter.has_value()) {
    *request.mutable_node_a_filter() = node_a_filter->ToProto();
  }
  if (node_b_filter.has_value()) {
    *request.mutable_node_b_filter() = node_b_filter->ToProto();
  }

  intrinsic_proto::world::GetTransformResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_->GetTransform(&ctx, request, &response)));
  return intrinsic_proto::FromProto(response.a_t_b());
}

absl::Status ObjectWorldClient::UpdateTransform(const TransformNode& node_a,
                                                const TransformNode& node_b,
                                                const Pose3d& a_t_b) {
  return CallUpdateTransform(node_a.Id(), std::nullopt, node_b.Id(),
                             std::nullopt, std::nullopt, a_t_b, world_id_,
                             *object_world_service_);
}

absl::Status ObjectWorldClient::UpdateTransform(
    const TransformNode& node_a, const TransformNode& node_b,
    const TransformNode& node_to_update, const Pose3d& a_t_b) {
  return CallUpdateTransform(node_a.Id(), std::nullopt, node_b.Id(),
                             std::nullopt, node_to_update.Id(), a_t_b,
                             world_id_, *object_world_service_);
}

absl::Status ObjectWorldClient::UpdateTransform(
    const TransformNode& node_a, const ObjectEntityFilter& node_a_filter,
    const TransformNode& node_b, const ObjectEntityFilter& node_b_filter,
    const TransformNode& node_to_update, const Pose3d& a_t_b) {
  return CallUpdateTransform(node_a.Id(), node_a_filter, node_b.Id(),
                             node_b_filter, node_to_update.Id(), a_t_b,
                             world_id_, *object_world_service_);
}

absl::Status ObjectWorldClient::UpdateJointPositions(
    const KinematicObject& kinematic_object,
    const eigenmath::VectorXd& joint_positions) {
  grpc::ClientContext ctx;
  intrinsic_proto::world::UpdateObjectJointsRequest request;
  request.set_world_id(world_id_);
  request.mutable_object()->set_id(kinematic_object.Id().value());
  VectorXdToRepeatedDouble(joint_positions, request.mutable_joint_positions());
  // Use minimalistic view since we are ignoring the response.
  request.set_view(intrinsic_proto::world::ObjectView::BASIC);
  intrinsic_proto::world::Object response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_->UpdateObjectJoints(&ctx, request, &response)));
  return absl::OkStatus();
}

absl::Status ObjectWorldClient::UpdateJointApplicationLimits(
    const KinematicObject& kinematic_object,
    const JointLimitsXd& joint_limits) {
  grpc::ClientContext ctx;
  intrinsic_proto::world::UpdateObjectJointsRequest request;
  request.set_world_id(world_id_);
  request.mutable_object()->set_id(kinematic_object.Id().value());
  *request.mutable_joint_application_limits() =
      ToJointLimitsUpdate(joint_limits);
  // Use minimalistic view since we are ignoring the response.
  request.set_view(intrinsic_proto::world::ObjectView::BASIC);
  intrinsic_proto::world::Object response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_->UpdateObjectJoints(&ctx, request, &response)));
  return absl::OkStatus();
}

absl::Status ObjectWorldClient::UpdateJointSystemLimits(
    const KinematicObject& kinematic_object,
    const JointLimitsXd& joint_limits) {
  grpc::ClientContext ctx;
  intrinsic_proto::world::UpdateObjectJointsRequest request;
  request.set_world_id(world_id_);
  request.mutable_object()->set_id(kinematic_object.Id().value());
  *request.mutable_joint_system_limits() = ToJointLimitsUpdate(joint_limits);
  // Use minimalistic view since we are ignoring the response.
  request.set_view(intrinsic_proto::world::ObjectView::BASIC);
  intrinsic_proto::world::Object response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_->UpdateObjectJoints(&ctx, request, &response)));
  return absl::OkStatus();
}

absl::Status ObjectWorldClient::UpdateJointLimits(
    const KinematicObject& kinematic_object,
    const JointLimitsXd& joint_application_limits,
    const JointLimitsXd& joint_system_limits) {
  grpc::ClientContext ctx;
  intrinsic_proto::world::UpdateObjectJointsRequest request;
  request.set_world_id(world_id_);
  request.mutable_object()->set_id(kinematic_object.Id().value());
  *request.mutable_joint_application_limits() =
      ToJointLimitsUpdate(joint_application_limits);
  *request.mutable_joint_system_limits() =
      ToJointLimitsUpdate(joint_system_limits);
  // Use minimalistic view since we are ignoring the response.
  request.set_view(intrinsic_proto::world::ObjectView::BASIC);
  intrinsic_proto::world::Object response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_->UpdateObjectJoints(&ctx, request, &response)));
  return absl::OkStatus();
}

absl::Status ObjectWorldClient::UpdateCartesianLimits(
    const KinematicObject& kinematic_object,
    const CartesianLimits& cartesian_limits) {
  grpc::ClientContext ctx;
  intrinsic_proto::world::UpdateKinematicObjectPropertiesRequest request;
  request.set_world_id(world_id_);
  request.mutable_object()->set_id(kinematic_object.Id().value());
  *request.mutable_cartesian_limits() =
      intrinsic::icon::ToProto(cartesian_limits);
  // Use minimalistic view since we are ignoring the response.
  request.set_view(intrinsic_proto::world::ObjectView::BASIC);
  intrinsic_proto::world::Object response;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(object_world_service_->UpdateKinematicObjectProperties(
          &ctx, request, &response)));
  return absl::OkStatus();
}

absl::Status ObjectWorldClient::UpdateMountedPayload(
    const KinematicObject& kinematic_object, const RobotPayload& payload) {
  grpc::ClientContext ctx;
  intrinsic_proto::world::UpdateKinematicObjectPropertiesRequest request;
  request.set_world_id(world_id_);
  request.mutable_object()->set_id(kinematic_object.Id().value());
  *request.mutable_mounted_payload() = ToProto(payload);
  // Use minimalistic view since we are ignoring the response.
  request.set_view(intrinsic_proto::world::ObjectView::BASIC);
  intrinsic_proto::world::Object response;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(object_world_service_->UpdateKinematicObjectProperties(
          &ctx, request, &response)));
  return absl::OkStatus();
}

absl::Status ObjectWorldClient::BatchUpdate(
    const ::intrinsic_proto::world::ObjectWorldUpdates& updates) {
  grpc::ClientContext ctx;
  intrinsic_proto::world::UpdateWorldResourcesRequest request;
  request.set_world_id(world_id_);
  // Use minimalistic view since we are ignoring the response.
  request.set_view(intrinsic_proto::world::ObjectView::BASIC);
  *request.mutable_world_updates() = updates;
  intrinsic_proto::world::UpdateWorldResourcesResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_->UpdateWorldResources(&ctx, request, &response)));
  return absl::OkStatus();
}

namespace {

absl::Status CallReparentObject(
    const WorldObject& object, const WorldObject& new_parent,
    intrinsic_proto::world::ObjectEntityFilter new_parent_entity_filter,
    const std::string& world_id,
    intrinsic_proto::world::ObjectWorldService::StubInterface&
        object_world_service) {
  grpc::ClientContext ctx;
  intrinsic_proto::world::ReparentObjectRequest request;
  request.set_world_id(world_id);
  request.mutable_object()->set_id(object.Id().value());
  request.mutable_new_parent()->mutable_reference()->set_id(
      new_parent.Id().value());
  *request.mutable_new_parent()->mutable_entity_filter() =
      std::move(new_parent_entity_filter);
  // Use minimalistic view since we are ignoring the response.
  request.set_view(intrinsic_proto::world::ObjectView::BASIC);
  intrinsic_proto::world::Object response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service.ReparentObject(&ctx, request, &response)));
  return absl::OkStatus();
}

}  // namespace

absl::Status ObjectWorldClient::ReparentObject(const WorldObject& object,
                                               const WorldObject& new_parent) {
  return ReparentObject(object, new_parent,
                        ObjectEntityFilter().IncludeBaseEntity());
}

absl::Status ObjectWorldClient::ReparentObject(
    const WorldObject& object, const WorldObject& new_parent,
    const ObjectEntityFilter& filter) {
  return CallReparentObject(object, new_parent, filter.ToProto(), world_id_,
                            *object_world_service_);
}

absl::Status ObjectWorldClient::ReparentObjectToFinalEntity(
    const WorldObject& object, const KinematicObject& new_parent) {
  return ReparentObject(object, new_parent,
                        ObjectEntityFilter().IncludeFinalEntity());
}

absl::StatusOr<intrinsic_proto::world::CollisionSettings>
ObjectWorldClient::GetCollisionSettings() const {
  grpc::ClientContext ctx;
  intrinsic_proto::world::GetCollisionSettingsRequest request;
  request.set_world_id(world_id_);
  intrinsic_proto::world::CollisionSettings response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_->GetCollisionSettings(&ctx, request, &response)));
  return response;
}

namespace {

absl::Status CallToggleCollisions(
    const WorldObject& object_a, const ObjectEntityFilter& entity_filter_a,
    const WorldObject& object_b, const ObjectEntityFilter& entity_filter_b,
    intrinsic_proto::world::ToggleMode toggle_mode, const std::string& world_id,
    intrinsic_proto::world::ObjectWorldService::StubInterface&
        object_world_service) {
  grpc::ClientContext ctx;
  intrinsic_proto::world::ToggleCollisionsRequest request;
  request.set_world_id(world_id);
  request.set_toggle_mode(toggle_mode);
  request.mutable_object_a()->mutable_reference()->set_id(
      object_a.Id().value());
  request.mutable_object_b()->mutable_reference()->set_id(
      object_b.Id().value());
  *request.mutable_object_a()->mutable_entity_filter() =
      entity_filter_a.ToProto();
  *request.mutable_object_b()->mutable_entity_filter() =
      entity_filter_b.ToProto();
  // Use minimalistic view since we are ignoring the response.
  request.set_view(intrinsic_proto::world::ObjectView::BASIC);

  intrinsic_proto::world::Objects response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service.ToggleCollisions(&ctx, request, &response)));
  return absl::OkStatus();
}

}  // namespace

absl::Status ObjectWorldClient::DisableCollisions(const WorldObject& object_a,
                                                  const WorldObject& object_b) {
  return CallToggleCollisions(
      object_a, ObjectEntityFilter().IncludeAllEntities(), object_b,
      ObjectEntityFilter().IncludeAllEntities(),
      intrinsic_proto::world::TOGGLE_MODE_DISABLE, world_id_,
      *object_world_service_);
}

absl::Status ObjectWorldClient::DisableCollisions(
    const WorldObject& object_a, const ObjectEntityFilter& entity_filter_a,
    const WorldObject& object_b, const ObjectEntityFilter& entity_filter_b) {
  return CallToggleCollisions(object_a, entity_filter_a, object_b,
                              entity_filter_b,
                              intrinsic_proto::world::TOGGLE_MODE_DISABLE,
                              world_id_, *object_world_service_);
}

absl::Status ObjectWorldClient::EnableCollisions(const WorldObject& object_a,
                                                 const WorldObject& object_b) {
  return CallToggleCollisions(
      object_a, ObjectEntityFilter().IncludeAllEntities(), object_b,
      ObjectEntityFilter().IncludeAllEntities(),
      intrinsic_proto::world::TOGGLE_MODE_ENABLE, world_id_,
      *object_world_service_);
}

absl::Status ObjectWorldClient::EnableCollisions(
    const WorldObject& object_a, const ObjectEntityFilter& entity_filter_a,
    const WorldObject& object_b, const ObjectEntityFilter& entity_filter_b) {
  return CallToggleCollisions(object_a, entity_filter_a, object_b,
                              entity_filter_b,
                              intrinsic_proto::world::TOGGLE_MODE_ENABLE,
                              world_id_, *object_world_service_);
}



absl::Status ObjectWorldClient::CreateObjectFromSceneObject(
    const intrinsic_proto::scene_object::v1::SceneObject& scene_object,
    const WorldObjectName& name,
    const ObjectWorldClient::CreateObjectOptions& options) {
  grpc::ClientContext ctx;
  intrinsic_proto::world::CreateObjectRequest request =
      BaseCreateObjectRequest(world_id_, name, options);

  *request.mutable_create_from_scene_object()->mutable_scene_object() =
      scene_object;
  for (const auto& [key, value] : options.user_data) {
    request.mutable_user_data()->insert({key, value});
  }

  intrinsic_proto::world::Object response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_->CreateObject(&ctx, request, &response)));
  return absl::OkStatus();
}

absl::Status ObjectWorldClient::CreateSingleEntityObject(
    const WorldObjectName& name,
    const intrinsic_proto::world::GeometryComponent& geometry_component,
    const CreateObjectOptions& options) {
  grpc::ClientContext ctx;
  intrinsic_proto::world::CreateObjectRequest request =
      BaseCreateObjectRequest(world_id_, name, options);

  *request.mutable_create_single_entity_object()->mutable_geometry_component() =
      geometry_component;

  intrinsic_proto::world::Object response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_->CreateObject(&ctx, request, &response)));
  return absl::OkStatus();
}



}  // namespace world
}  // namespace intrinsic
