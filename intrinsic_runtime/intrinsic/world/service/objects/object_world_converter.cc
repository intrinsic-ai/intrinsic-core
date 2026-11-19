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

#include "intrinsic/world/service/objects/object_world_converter.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/proto/cart_space_conversion.h"
#include "intrinsic/kinematics/types/joint_limits.pb.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/pose.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/attachment_component.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/component/kinematics_component.h"
#include "intrinsic/world/component/physics_component.h"
#include "intrinsic/world/component/ppr_component.h"
#include "intrinsic/world/component/sensor_component.h"
#include "intrinsic/world/component/simulation_component.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/defaulting_world_object_visitor.h"
#include "intrinsic/world/objects/frame_internal.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_entity_filter.h"
#include "intrinsic/world/objects/object_world_creation_utils.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/physical_object.h"
#include "intrinsic/world/objects/root_object.h"
#include "intrinsic/world/objects/simple_world_object_visitor.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"
#include "intrinsic/world/proto/ppr_component.pb.h"
#include "intrinsic/world/robot_payload/robot_payload.h"
#include "intrinsic/world/service/world_mutex.h"
#include "intrinsic/world/world.h"

namespace intrinsic {
namespace object_world {

namespace {

intrinsic_proto::world::ObjectFullPath ToProto(
    const std::vector<WorldObjectName>& full_path) {
  intrinsic_proto::world::ObjectFullPath result;
  result.mutable_object_names()->Reserve(full_path.size());
  for (const WorldObjectName& object_name : full_path) {
    result.add_object_names(object_name.value());
  }
  return result;
}

absl::StatusOr<intrinsic_proto::world::Frame> ToProto(
    const Frame& frame, absl::string_view world_id,
    intrinsic_proto::world::ObjectView view) {
  intrinsic_proto::world::Frame result;
  result.set_world_id(world_id);
  result.set_id(frame.GetId().value());
  result.set_name(frame.GetName().value());
  result.mutable_object()->set_id(frame.GetParent()->GetId().value());
  result.mutable_object()->set_name(frame.GetParent()->GetName().value());
  result.set_is_attachment_frame(frame.IsAttachmentFrame());

  if (frame.GetParentFrame()) {
    result.mutable_parent_frame()->set_id(
        frame.GetParentFrame()->GetId().value());
    result.mutable_parent_frame()->set_name(
        frame.GetParentFrame()->GetName().value());
  }
  *result.mutable_object_full_path() =
      ToProto(frame.GetParent()->GetFullPathName());

  for (const Frame* child_frame : frame.GetChildFrames()) {
    intrinsic_proto::world::IdAndName* id_and_name = result.add_child_frames();
    id_and_name->set_id(child_frame->GetId().value());
    id_and_name->set_name(child_frame->GetName().value());
  }
  if (view == intrinsic_proto::world::ObjectView::FULL) {
    INTR_ASSIGN_OR_RETURN(Pose parent_t_this, frame.GetParentTThis());
    *result.mutable_parent_t_this() = ToProto(parent_t_this);
  }
  return result;
}

class KinematicObjectComponentSettingVisitor
    : public DefaultingWorldObjectConstVisitor {
 public:
  explicit KinematicObjectComponentSettingVisitor(
      intrinsic_proto::world::Object* object)
      : object_(object) {}

  absl::Status DefaultVisit(const WorldObject& object) override {
    return absl::OkStatus();
  }

  absl::Status Visit(const KinematicObject& kinematic_object) override {
    INTR_ASSIGN_OR_RETURN(eigenmath::VectorXd joint_positions,
                          kinematic_object.GetJointPositions());
    INTR_ASSIGN_OR_RETURN(JointLimitsXd joint_system_limits,
                          kinematic_object.GetJointSystemLimits());
    INTR_ASSIGN_OR_RETURN(JointLimitsXd joint_application_limits,
                          kinematic_object.GetJointApplicationLimits());
    INTR_ASSIGN_OR_RETURN(std::vector<JointEntityId> joint_ids,
                          kinematic_object.GetJointEntityIds());
    INTR_ASSIGN_OR_RETURN(bool are_kinematics_updated,
                          kinematic_object.AreRobotKinematicsUpdated());

    intrinsic_proto::world::KinematicObjectComponent* component =
        object_->mutable_kinematic_object_component();
    VectorXdToRepeatedDouble(joint_positions,
                             component->mutable_joint_positions());
    *component->mutable_joint_system_limits() = ToProto(joint_system_limits);
    *component->mutable_joint_application_limits() =
        ToProto(joint_application_limits);
    component->set_are_kinematics_updated(are_kinematics_updated);

    component->mutable_joint_entity_ids()->Reserve(joint_ids.size());
    for (const auto& joint_id : joint_ids) {
      INTR_ASSIGN_OR_RETURN(
          AttachmentEntityId joint_attachment_id,
          kinematic_object.GetEntityWorld().ValidateEntity<AttachmentEntityId>(
              joint_id));
      *component->mutable_joint_entity_ids()->Add() =
          ObjectWorldResourceIdForEntity(joint_attachment_id).value();
    }

    for (const Frame* flange_frame : kinematic_object.GetIsoFlangeFrames()) {
      intrinsic_proto::world::IdAndName* id_and_name =
          component->add_iso_flange_frames();
      id_and_name->set_id(flange_frame->GetId().value());
      id_and_name->set_name(flange_frame->GetName().value());
    }

    WorldHashMap<std::string, eigenmath::VectorXd> stored_configs_map;
    INTR_ASSIGN_OR_RETURN(stored_configs_map,
                          kinematic_object.GetNamedJointConfigurations());
    std::vector<std::pair<std::string, eigenmath::VectorXd>> stored_configs(
        stored_configs_map.begin(), stored_configs_map.end());
    std::sort(stored_configs.begin(), stored_configs.end(),
              [](const std::pair<std::string, eigenmath::VectorXd>& config_a,
                 const std::pair<std::string, eigenmath::VectorXd>& config_b) {
                return config_a.first < config_b.first;
              });
    for (const auto& [name, config_positions] : stored_configs) {
      intrinsic_proto::world::NamedJointConfiguration* config =
          component->add_named_joint_configurations();
      config->set_name(name);
      VectorXdToRepeatedDouble(config_positions,
                               config->mutable_joint_positions());
    }

    INTR_ASSIGN_OR_RETURN(const auto cart_limits,
                          kinematic_object.GetCartesianLimits());
    *component->mutable_cartesian_limits() =
        intrinsic::icon::ToProto(cart_limits);

    INTR_ASSIGN_OR_RETURN(const auto payload,
                          kinematic_object.GetMountedPayload());
    if (payload.has_value()) {
      *component->mutable_mounted_payload() = ToProto(*payload);
    }

    INTR_ASSIGN_OR_RETURN(const auto ik_solver_map,
                          kinematic_object.GetIkSolvers());
    for (const auto& [base_id, tip_to_solver] : ik_solver_map) {
      for (const auto& [tip_id, solver_key] : tip_to_solver) {
        ::intrinsic_proto::world::KinematicObjectComponent::IkSolver*
            ik_solver = component->mutable_ik_solvers()->Add();
        ik_solver->set_base_entity_id(
            ObjectWorldResourceIdForEntity(base_id).value());
        ik_solver->set_tip_entity_id(
            ObjectWorldResourceIdForEntity(tip_id).value());
        ik_solver->set_kinematic_solver_key(solver_key);
      }
    }

    return absl::OkStatus();
  }

 private:
  intrinsic_proto::world::Object* object_;
};

}  // namespace

absl::StatusOr<intrinsic_proto::world::WorldMetadata> ToProto(
    const WorldAndMutex& world, absl::string_view world_id) {
  absl::ReaderMutexLock lock(*world.mtx);
  return ToProtoLocked(world, world_id);
}

absl::StatusOr<intrinsic_proto::world::WorldMetadata> ToProtoLocked(
    const WorldAndMutex& world, absl::string_view world_id) {
  intrinsic_proto::world::WorldMetadata result;
  result.set_id(world_id);
  result.set_world_structure_hash(world.world_structure_hash);
  result.set_user_tag(world.user_tag);
  INTR_RETURN_IF_ERROR(
      FromAbslTime(world.LastUpdate(), result.mutable_last_update()));
  return result;
}

absl::StatusOr<intrinsic_proto::world::Entity> ToProto(
    AttachmentEntityId entity_id, absl::string_view world_id,
    const WorldObject& object) {
  INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                        object.GetEntityWorld().GetEntityById(entity_id));

  intrinsic_proto::world::Entity entity_proto;
  entity_proto.set_world_id(world_id);
  entity_proto.set_id(ObjectWorldResourceIdForEntity(entity_id).value());
  entity_proto.set_name(entity->GetLocalName());
  entity_proto.mutable_object()->set_id(object.GetId().value());
  entity_proto.mutable_object()->set_name(object.GetName().value());

  // Attachment component.
  INTR_ASSIGN_OR_RETURN(const AttachmentComponent* attachment,
                        entity->GetComponent<AttachmentComponent>());
  if (attachment->GetParentId() != kInvalidEntityId) {
    entity_proto.set_parent_id(
        ObjectWorldResourceIdForEntity(attachment->GetParentId()).value());
    *entity_proto.mutable_parent_t_this() =
        ToProto(attachment->GetParentTThis());
  }

  // Geometry component.
  if (entity->HasComponent<GeometryComponent>()) {
    INTR_ASSIGN_OR_RETURN(const GeometryComponent* geometry,
                          entity->GetComponent<GeometryComponent>());
    INTR_ASSIGN_OR_RETURN(*entity_proto.mutable_geometry_component(),
                          geometry->ToProto());
  }

  // Physics component.
  if (entity->HasComponent<PhysicsComponent>()) {
    INTR_ASSIGN_OR_RETURN(const PhysicsComponent* physics,
                          entity->GetComponent<PhysicsComponent>());
    INTR_ASSIGN_OR_RETURN(*entity_proto.mutable_physics_component(),
                          physics->ToProto());
  }

  // Kinematics component.
  if (entity->HasComponent<KinematicsComponent>()) {
    INTR_ASSIGN_OR_RETURN(const KinematicsComponent* kinematics,
                          entity->GetComponent<KinematicsComponent>());
    INTR_ASSIGN_OR_RETURN(*entity_proto.mutable_kinematics_component(),
                          kinematics->ToProto());
  }

  // Sensor component.
  if (entity->HasComponent<SensorComponent>()) {
    INTR_ASSIGN_OR_RETURN(const SensorComponent* sensor,
                          entity->GetComponent<SensorComponent>());
    INTR_ASSIGN_OR_RETURN(*entity_proto.mutable_sensor_component(),
                          sensor->ToProto());
  }

  // PPR component.
  if (entity->HasComponent<PPRComponent>()) {
    INTR_ASSIGN_OR_RETURN(const PPRComponent* ppr,
                          entity->GetComponent<PPRComponent>());
    INTR_ASSIGN_OR_RETURN(*entity_proto.mutable_ppr_component(),
                          ppr->ToProto());
  }

  return entity_proto;
}

absl::StatusOr<intrinsic_proto::world::Object> ToProto(
    const WorldObject& object, absl::string_view world_id,
    intrinsic_proto::world::ObjectView view) {
  intrinsic_proto::world::Object result;
  result.set_world_id(world_id);
  result.set_id(object.GetId().value());
  result.set_name(object.GetName().value());
  INTR_ASSIGN_OR_RETURN(bool name_is_global_alias, object.NameIsGlobalAlias());
  result.set_name_is_global_alias(name_is_global_alias);

  SimpleWorldObjectConstVisitor type_setting_visitor(
      [&](const RootObject& root_object) {
        result.set_type(intrinsic_proto::world::ROOT);
        return absl::OkStatus();
      },
      [&](const PhysicalObject& physical_object) {
        result.set_type(intrinsic_proto::world::PHYSICAL_OBJECT);
        return absl::OkStatus();
      },
      [&](const KinematicObject& kinematic_object) {
        result.set_type(intrinsic_proto::world::KINEMATIC_OBJECT);
        return absl::OkStatus();
      });
  INTR_RETURN_IF_ERROR(object.Accept(type_setting_visitor));

  if (const WorldObject* parent = object.GetParent(); parent) {
    result.mutable_parent()->set_id(parent->GetId().value());
    result.mutable_parent()->set_name(parent->GetName().value());

    INTR_ASSIGN_OR_RETURN(const auto origin_id,
                          object.GetTransformOriginEntityId());
    INTR_ASSIGN_OR_RETURN(
        const auto* attachment,
        object.GetEntityWorld().GetComponentByEntityId<AttachmentComponent>(
            origin_id));
    result.mutable_parent_entity()->set_id(
        ObjectWorldResourceIdForEntity(attachment->GetParentId()).value());
  }
  *result.mutable_object_full_path() = ToProto(object.GetFullPathName());

  for (const WorldObject* child : object.GetChildren()) {
    intrinsic_proto::world::IdAndName* child_proto = result.add_children();
    child_proto->set_id(child->GetId().value());
    child_proto->set_name(child->GetName().value());
  }
  for (const Frame* frame : object.GetFramesSorted()) {
    intrinsic_proto::world::Frame* frame_proto = result.add_frames();
    INTR_ASSIGN_OR_RETURN(*frame_proto, ToProto(*frame, world_id, view));
  }

  // If we're not processing the root object, and we have a set view, set the
  // pose.
  if (object.GetId() != RootObjectId() &&
      view != intrinsic_proto::world::ObjectView::OBJECT_VIEW_UNSPECIFIED) {
    intrinsic_proto::world::ObjectComponent& object_component =
        *result.mutable_object_component();
    INTR_ASSIGN_OR_RETURN(Pose parent_t_this, object.GetParentTThis());
    *object_component.mutable_parent_t_this() = ToProto(parent_t_this);
  }

  // If we're not providing a full view, that's all we get
  if (view != intrinsic_proto::world::ObjectView::FULL) {
    return result;
  }

  // ---------------------------------------------------------------------------

  KinematicObjectComponentSettingVisitor visitor(&result);
  INTR_RETURN_IF_ERROR(object.Accept(visitor));

  INTR_ASSIGN_OR_RETURN(AttachmentEntityId root_id, object.GetRootEntityId());
  result.set_root_entity_id(ObjectWorldResourceIdForEntity(root_id).value());

  for (AttachmentEntityId entity_id : object.GetEntityIds()) {
    INTR_ASSIGN_OR_RETURN(intrinsic_proto::world::Entity entity_proto,
                          ToProto(entity_id, world_id, object));
    result.mutable_entities()->insert({entity_proto.id(), entity_proto});
  }

  // If we're processing the root object, then that's all we get.
  if (object.GetId() == RootObjectId()) {
    return result;
  }

  // ---------------------------------------------------------------------------

  intrinsic_proto::world::ObjectComponent& object_component =
      *result.mutable_object_component();

  // PPR component.
  INTR_ASSIGN_OR_RETURN(std::optional<std::string> resource_name,
                        object.GetResourceName());
  if (resource_name) {
    object_component.mutable_ppr_component()->set_resource_name(*resource_name);
  }

  // Simulation component.
  absl::StatusOr<const SimulationComponent*> simulation_component =
      object.GetSimulationComponent();
  if (simulation_component.ok()) {
    INTR_ASSIGN_OR_RETURN(*object_component.mutable_simulation_component(),
                          (*simulation_component)->ToProto());
  } else if (!absl::IsNotFound(simulation_component.status())) {
    INTR_RETURN_IF_ERROR(simulation_component.status());
  }

  // User data
  if (auto user_data_protos = object.GetUserDataProtos();
      user_data_protos.ok()) {
    for (const auto& [key, value] : **user_data_protos) {
      (*object_component.mutable_user_data())[key] = value;
    }
  }

  return result;
}

absl::StatusOr<intrinsic_proto::world::Objects> ToProto(
    const std::vector<const WorldObject*>& objects, absl::string_view world_id,
    intrinsic_proto::world::ObjectView view) {
  intrinsic_proto::world::Objects result;
  for (const WorldObject* object : objects) {
    CHECK(object != nullptr)
        << "nullptr WorldObject found while converting WorldObjects to proto";
    INTR_ASSIGN_OR_RETURN(*result.add_objects(),
                          ToProto(*object, world_id, view));
  }
  return result;
}

absl::StatusOr<intrinsic_proto::world::Frame> ToProto(
    const Frame& frame, absl::string_view world_id) {
  return ToProto(frame, world_id, intrinsic_proto::world::ObjectView::FULL);
}

world::ObjectEntityFilter FromProto(
    const ::intrinsic_proto::world::ObjectEntityFilter& entity_filter) {
  return world::ObjectEntityFilter::FromProto(entity_filter);
}

}  // namespace object_world
}  // namespace intrinsic
