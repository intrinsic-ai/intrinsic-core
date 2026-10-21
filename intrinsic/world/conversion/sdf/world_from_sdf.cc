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

#include "intrinsic/world/conversion/sdf/world_from_sdf.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/substitute.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/hal/proto/v1/digital_input_output.pb.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/point.pb.h"
#include "intrinsic/scene/sdf/custom_tags.h"
#include "intrinsic/scene/sdf/sdf_path_resolver.h"
#include "intrinsic/scene/sdf/sdf_util.h"
#include "intrinsic/scene/sdf/xml_utils.h"
#include "intrinsic/scene/user_data_keys.h"
#include "intrinsic/simulation/world/sim_plugins.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/attachment_component.h"
#include "intrinsic/world/component/collections_component.h"
#include "intrinsic/world/component/collections_member_component.h"
#include "intrinsic/world/component/collision_component.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/component/kinematics_component.h"
#include "intrinsic/world/component/physics_component.h"
#include "intrinsic/world/component/projector_component.h"
#include "intrinsic/world/component/robot_component.h"
#include "intrinsic/world/component/sensor_component.h"
#include "intrinsic/world/component/simulation_component.h"
#include "intrinsic/world/component/user_data_component.h"
#include "intrinsic/world/conversion/sdf/collision_component_from_sdf.h"
#include "intrinsic/world/conversion/sdf/geometry_component_from_sdf.h"
#include "intrinsic/world/conversion/sdf/joint_conversion.h"
#include "intrinsic/world/conversion/sdf/physics_component_from_sdf.h"
#include "intrinsic/world/conversion/sdf/sensor_conversion.h"
#include "intrinsic/world/dof_kinematic_view.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/grouping.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/labels.h"
#include "intrinsic/world/proto/kinematics_component.pb.h"
#include "intrinsic/world/proto/physics_component.pb.h"
#include "intrinsic/world/proto/robot_component.pb.h"
#include "intrinsic/world/proto/sensor_component.pb.h"
#include "intrinsic/world/world.h"
#include "ortools/base/filesystem.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"
#include "ortools/base/path.h"
#include "sdf/Element.hh"
#include "sdf/Frame.hh"
#include "sdf/Joint.hh"
#include "sdf/Link.hh"
#include "sdf/Model.hh"
#include "sdf/Projector.hh"
#include "sdf/Root.hh"
#include "sdf/SDFImpl.hh"
#include "sdf/Sensor.hh"
#include "sdf/Types.hh"
#include "sdf/World.hh"

namespace intrinsic {
namespace sdf {

using ::intrinsic::eigenmath::Vector2d;
using ::intrinsic::eigenmath::Vector3d;
using ::intrinsic::simulation::PluginSdfTrait;
using ::sdf::ElementConstPtr;
using IconSimPluginSpec =
    intrinsic_proto::world::RobotComponent::IconSimPluginSpec;
using IconSimDevice = intrinsic_proto::world::RobotComponent::IconSimDevice;
using MultiCameraPluginSpec =
    intrinsic_proto::world::RobotComponent::MultiCameraPluginSpec;
using GenericActionPluginSpec =
    intrinsic_proto::world::generic_action::GenericActionPluginSpec;

using DigitalInputOutput = intrinsic_proto::icon::v1::DigitalInputOutput;
WorldFromSdf::WorldFromSdf() : uri_resolver_(SdfPathResolver) {
  link_id_to_original_world_t_link_[kRootEntityId] = Pose3d();
}

WorldFromSdf& WorldFromSdf::SetUriResolver(UriResolver uri_resolver) {
  uri_resolver_ = std::move(uri_resolver);
  return *this;
}

WorldFromSdf& WorldFromSdf::SetGeometryParsing(bool parse_geometry) {
  skip_geometry_parsing_ = !parse_geometry;
  return *this;
}

WorldFromSdf& WorldFromSdf::SetGroupIdGeneration(bool group_id_generation) {
  generate_group_ids_ = group_id_generation;
  return *this;
}

absl::Status WorldFromSdf::Parse(const ::sdf::Root& sdf_root) {
  link_name_to_entity_id_.clear();
  world_ = std::make_unique<World>(World::CreateEmptyWorld());

  if (sdf_root.WorldCount() == 1) {
    INTR_RETURN_IF_ERROR(ParseWorld(*sdf_root.WorldByIndex(0)));
  } else if (sdf_root.WorldCount() == 0) {
    // If there are no <world> elements, try to parse the top-level <model>
    // element as if they are parented to the world root. This clause allows us
    // to load model-only SDFs, which might happen if someone wants to view a
    // robot SDF that's referenced in a workcell SDF via <include>.
    // Note: Although in sdf 1.9 spec multiple models are allowed under root,
    // libsdformat implementation only allow one <model> under <sdf> for
    // <include> disambiguity, see
    // https://github.com/gazebosim/sdformat/issues/395
    if (const auto* model = sdf_root.Model()) {
      INTR_ASSIGN_OR_RETURN(auto model_link_map,
                            ParseModel(*model, LabelId(""), Pose3d()));
      INTR_RETURN_IF_ERROR(MergeNestedLinkNameToIdMap(
          model_link_map, "", &link_name_to_entity_id_));
    } else {
      return absl::UnimplementedError(
          "parsing non <model> or <world> sdf as world are not yet supported");
    }
  } else {
    return absl::UnimplementedError(
        "multiple <world> elements are not yet supported");
  }
  return absl::OkStatus();
}

absl::Status WorldFromSdf::Parse(const std::string& sdf_text) {
  // Override the SDF library's URI resolver with our own.
  ::sdf::setFindCallback([this](const std::string& uri) -> std::string {
    auto resolved_path_or = uri_resolver_(uri);
    if (resolved_path_or.ok()) {
      return std::move(resolved_path_or).value();
    }
    LOG(ERROR) << "Failed to resolve URI '" << uri
               << "': " << resolved_path_or.status();
    return "";
  });
  absl::Cleanup reset_find_callback = [] { ::sdf::setFindCallback({}); };

  // Try to parse sdf_text and clear the URI resolver.
  sdf_root_ = std::make_unique<::sdf::Root>();
  ::sdf::Errors errors = sdf_root_->LoadSdfString(sdf_text);
  if (!errors.empty()) {
    sdf_root_ = nullptr;
    auto error_builder = DataLossErrorBuilder().LogError();
    error_builder << "Failed to parse SDF from root with " << errors.size()
                  << " errors.";
    for (const auto& error : errors) {
      error_builder << "\n" << error;
    }
    return error_builder;
  }
  return Parse(*sdf_root_);
}

absl::StatusOr<std::unique_ptr<World>> WorldFromSdf::GetWorld() {
  if (!world_) {
    return absl::NotFoundError(
        "GetWorld() can only be called once after each Parse() call");
  }
  return std::move(world_);
}

const WorldHashMap<std::string, LinkEntityId>&
WorldFromSdf::GetLinkNameToEntityIdMap() const {
  return link_name_to_entity_id_;
}

absl::Status WorldFromSdf::MergeNestedLinkNameToIdMap(
    const WorldHashMap<std::string, LinkEntityId>& nested_model_map,
    const std::string& outer_model_name,
    WorldHashMap<std::string, LinkEntityId>* destination_map) {
  for (const auto& [link_name, link_id] : nested_model_map) {
    // Prepend nested links with the name of the outer model (see documentation
    // of FindLinkId() in world_from_sdf.h).
    std::string decorated_name;
    if (outer_model_name.empty()) {
      decorated_name = link_name;
    } else {
      decorated_name =
          absl::StrCat(outer_model_name, kNestedModelSeparator, link_name);
    }
    if (!destination_map->emplace(decorated_name, link_id).second) {
      return AlreadyExistsErrorBuilder()
             << "found multiple links named \"" << decorated_name << "\"";
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<LinkEntityId> WorldFromSdf::FindLinkId(
    const std::string& model_name,
    const WorldHashMap<std::string, LinkEntityId>& link_name_to_id_map,
    const std::string& link_name) {
  // Only search for fully qualified name when model_name is empty. In this case
  // we are searching for a nested link name under the world
  if (model_name.empty()) {
    if (!absl::StrContains(link_name, kNestedModelSeparator)) {
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "link with name " << link_name
             << " without nested scope shouldn't be searched under world scope";
    }
    const auto iter = link_name_to_id_map.find(link_name);
    if (iter != link_name_to_id_map.end()) {
      return iter->second;
    }
    return intrinsic::NotFoundErrorBuilder()
           << "Could not find link with name \"" << link_name << "\""
           << "from world root";
  }
  const auto iter = link_name_to_id_map.find(
      absl::StrCat(model_name, kNestedModelSeparator, link_name));
  if (iter != link_name_to_id_map.end()) {
    return iter->second;
  }
  return intrinsic::NotFoundErrorBuilder()
         << "Could not find link with name '" << link_name << "' in model '"
         << model_name << "' Options are: {"
         << absl::StrJoin(std::views::keys(link_name_to_id_map), ", ") << "}";
}

absl::StatusOr<AttachmentEntityId> WorldFromSdf::FindParentId(
    const std::string& model_name,
    const WorldHashMap<std::string, LinkEntityId>& link_name_to_id_map,
    const WorldHashMap<std::string, AttachmentEntityId>& frame_name_to_id_map,
    const std::string& parent_name) {
  if (auto parent_id = FindLinkId(model_name, link_name_to_id_map, parent_name);
      parent_id.ok()) {
    return *parent_id;
  }

  if (const auto iter = frame_name_to_id_map.find(parent_name);
      iter != frame_name_to_id_map.end()) {
    return iter->second;
  }

  return intrinsic::NotFoundErrorBuilder()
         << "Could not find parent with name '" << parent_name << "' in model '"
         << model_name << "' Options are: {"
         << absl::StrJoin(std::views::keys(link_name_to_id_map), ", ")
         << absl::StrJoin(std::views::keys(frame_name_to_id_map), ", ") << "}";
}

absl::Status WorldFromSdf::ParseWorld(const ::sdf::World& sdf_world) {
  const auto model_count = sdf_world.ModelCount();
  for (auto i = 0; i < model_count; ++i) {
    const ::sdf::Model* model = sdf_world.ModelByIndex(i);
    INTR_RET_CHECK_NE(model, nullptr);
    INTR_ASSIGN_OR_RETURN(auto model_link_map,
                          ParseModel(*model, LabelId(""), Pose3d()));
    INTR_RETURN_IF_ERROR(MergeNestedLinkNameToIdMap(model_link_map, "",
                                                    &link_name_to_entity_id_));
  }

  // Process world-level <frame> elements.
  WorldHashMap<std::string, AttachmentEntityId> frame_name_to_id_map;
  for (int i = 0; i < sdf_world.FrameCount(); ++i) {
    const ::sdf::Frame* frame = sdf_world.FrameByIndex(i);
    INTR_RET_CHECK_NE(frame, nullptr);

    INTR_RETURN_IF_ERROR(ParseFrame(*frame, "", Pose3d(), LabelId(""),
                                    frame_name_to_id_map,
                                    link_name_to_entity_id_)
                             .status());
  }

  // Process optional <light> elements.
  INTR_ASSIGN_OR_RETURN(auto* root_entity,
                        world_->GetEntityById(kRootEntityId));
  INTR_ASSIGN_OR_RETURN(auto* user_data,
                        root_entity->GetOrCreateComponent<UserDataComponent>());

  for (const auto& light : GetChildrenByTag(sdf_world.Element(), "light")) {
    INTR_ASSIGN_OR_RETURN(std::string light_xml, GetCompactXml(light));
    user_data->MutableUserDataMap()[kSdfLights] += light_xml;
  }

  return absl::OkStatus();
}

absl::StatusOr<WorldHashMap<std::string, LinkEntityId>>
WorldFromSdf::ParseModel(const ::sdf::Model& model,
                         const LabelId& parent_model_label,
                         const Pose3d& world_t_parent_model) {
  const std::string name = model.Name();
  if (absl::StrContains(name, kNestedModelSeparator)) {
    return absl::InvalidArgumentError(
        absl::Substitute("<model> name containing $0 is not allowed for nested "
                         "frame disambiguity, please rename model with name $1",
                         kNestedModelSeparator, name));
  }
  INTR_ASSIGN_OR_RETURN(const Pose3d parent_model_t_this,
                        ParseSemanticPose(model.SemanticPose()));
  const Pose3d world_t_this = world_t_parent_model * parent_model_t_this;

  // Create the label we'll apply to entities created directly by this model.
  // Nested models also use this label as a basis for their own labels.
  LabelId model_label;
  if (parent_model_label.empty()) {
    model_label = LabelId(name);
  } else {
    model_label = LabelId(
        absl::StrCat(parent_model_label.value(), kNestedModelSeparator, name));
  }

  // Process any nested <model>s before this one.
  WorldHashMap<std::string, LinkEntityId> link_name_to_id_map;
  const auto nested_model_count = model.ModelCount();
  for (auto i = 0; i < nested_model_count; ++i) {
    const auto* nested_model = model.ModelByIndex(i);
    INTR_RET_CHECK_NE(nested_model, nullptr);
    // Recursively call ParseModel.
    INTR_ASSIGN_OR_RETURN(auto nested_link_map,
                          ParseModel(*nested_model, model_label, world_t_this));
    INTR_RETURN_IF_ERROR(MergeNestedLinkNameToIdMap(nested_link_map, name,
                                                    &link_name_to_id_map));
  }

  // TODO(b/171736860): Remove this Grouping aspect code once all clients have
  // been migrated to use RobotComponents and/or LabelIds. Prospectively create
  // the model's group because it has to be done before any entities use the
  // corresponding label.
  GroupId robot_group_id;
  bool created_robot_group = false;
  if (generate_group_ids_) {
    robot_group_id = GroupId(model_label.value());
    if (!world_->GetGroupIds().contains(robot_group_id)) {
      INTR_RETURN_IF_ERROR(world_->AddGroupId(robot_group_id));
      created_robot_group = true;
    }
  }

  // Create a collections entity for this <model>.
  auto collections_id =
      world_->CreateEntityWithComponentTypes<CollectionsEntityId>();
  INTR_ASSIGN_OR_RETURN(WorldEntity * collections_ent,
                        world_->GetEntityById(collections_id));
  INTR_RETURN_IF_ERROR(collections_ent->SetLocalName(name));
  INTR_RETURN_IF_ERROR(collections_ent->AddLabels({model_label}));
  INTR_ASSIGN_OR_RETURN(auto* collections_component,
                        collections_ent->GetComponent<CollectionsComponent>());

  // Process <link> children.
  std::vector<CollectionsMemberEntityId> link_ids;
  std::vector<CollectionsMemberEntityId> sensor_ids;
  std::vector<CollectionsMemberEntityId> projector_ids;
  const auto link_count = model.LinkCount();
  for (auto i = 0; i < link_count; ++i) {
    const auto* link = model.LinkByIndex(i);
    INTR_RET_CHECK_NE(link, nullptr);
    INTR_ASSIGN_OR_RETURN(
        ParseLinkResult parsed_link,
        ParseLink(*link, world_t_this, model_label, collections_id));

    // Prepend link names with their model's name (see
    // http://gazebosim.org/tutorials?tut=nested_model).
    std::string decorated_link_name =
        absl::StrCat(name, kNestedModelSeparator, link->Name());
    if (link_name_to_id_map.contains(decorated_link_name)) {
      return AlreadyExistsErrorBuilder()
             << "found multiple links named \"" << decorated_link_name
             << "\" while processing model \"" << name << "\"";
    }
    link_name_to_id_map[decorated_link_name] = parsed_link.link_id;

    // Add collections_id as a link collection parent.
    INTR_ASSIGN_OR_RETURN(auto* link_ent,
                          world_->GetEntityById(parsed_link.link_id));
    INTR_ASSIGN_OR_RETURN(auto* link_collections_member,
                          link_ent->GetComponent<CollectionsMemberComponent>());
    INTR_RETURN_IF_ERROR(link_collections_member->AddParentCollection(
        collections_id, CollectionsComponent::kLinks));
    link_ids.emplace_back(parsed_link.link_id);
    for (const SensorEntityId& sensor_id : parsed_link.sensor_ids) {
      sensor_ids.emplace_back(sensor_id.value());
    }
    for (const ProjectorEntityId& projector_id : parsed_link.projector_ids) {
      projector_ids.emplace_back(projector_id.value());
    }
  }
  // Set the link collection's members to complete the cross-references.
  INTR_RETURN_IF_ERROR(collections_component->SetCollectionMembers(
      CollectionsComponent::kLinks, link_ids));

  // TODO(b/171736860): Remove this Grouping aspect code once all clients have
  // been migrated to use labels. If it turned out the model didn't have any
  // links, remove the group we created earlier.
  if (link_ids.empty() && nested_model_count == 0) {
    if (created_robot_group) {
      INTR_RETURN_IF_ERROR(world_->RemoveGroupId(robot_group_id));
    }
    robot_group_id = GroupId();
  }

  // process optional <static> element.
  if (model.Static()) {
    INTR_ASSIGN_OR_RETURN(
        SimulationComponent * simulation_component,
        collections_ent->GetOrCreateComponent<SimulationComponent>());
    simulation_component->SetIsStatic(true);
  }

  // Process <joint> children.
  std::vector<CollectionsMemberEntityId> joint_ids;
  bool has_non_fixed_joint = false;

  std::vector<JointEntityId> joints_to_process_in_order;
  const auto joint_count = model.JointCount();
  for (auto i = 0; i < joint_count; ++i) {
    const auto* joint = model.JointByIndex(i);
    INTR_RET_CHECK_NE(joint, nullptr);
    INTR_ASSIGN_OR_RETURN(
        CreateJointEntitiesResult joint_entities,
        CreateJointEntities(*joint, world_t_this, name, model_label,
                            link_name_to_id_map, collections_id));

    INTR_ASSIGN_OR_RETURN(const auto* joint_kinematics,
                          world_->GetComponentByEntityId<KinematicsComponent>(
                              joint_entities.joint_id));
    has_non_fixed_joint |=
        joint_kinematics->GetMotionType() !=
        intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED;

    joints_to_process_in_order.push_back(joint_entities.joint_id);
    for (const SensorEntityId& sensor_id : joint_entities.sensor_ids) {
      sensor_ids.emplace_back(sensor_id.value());
    }
  }

  WorldHashMap<std::string, JointEntityId> joint_local_name_to_id;
  for (const auto& joint_id : joints_to_process_in_order) {
    // If we bypass this joint, delete the entity and go to the next <joint>
    // element.
    if (BypassFixedCrossModelJoint(joint_id).ok()) {
      INTR_RETURN_IF_ERROR(world_->RemoveEntity(joint_id));
      continue;
    }

    // If we only have fixed joints then we can inline them here.
    if (!has_non_fixed_joint && BypassFixedInModelJoint(joint_id).ok()) {
      INTR_RETURN_IF_ERROR(world_->RemoveEntity(joint_id));
      continue;
    }

    joint_ids.emplace_back(joint_id);
    INTR_ASSIGN_OR_RETURN(const auto joint_world_entity,
                          world_->GetEntityById(joint_id));
    const auto joint_local_name = joint_world_entity->GetLocalName();
    joint_local_name_to_id.insert({joint_local_name, joint_id});

    // Add collections_id as a joint collection parent.
    INTR_ASSIGN_OR_RETURN(
        auto* joint_collections_member,
        world_->GetComponentByEntityId<CollectionsMemberComponent>(joint_id));
    INTR_RETURN_IF_ERROR(joint_collections_member->AddParentCollection(
        collections_id, CollectionsComponent::kJoints));
  }

  // Set the joint collection's members to complete the cross-references.
  INTR_RETURN_IF_ERROR(collections_component->SetCollectionMembers(
      CollectionsComponent::kJoints, joint_ids));
  INTR_RETURN_IF_ERROR(collections_component->SetCollectionMembers(
      CollectionsComponent::kSensors, sensor_ids));
  INTR_RETURN_IF_ERROR(collections_component->SetCollectionMembers(
      CollectionsComponent::kProjectors, projector_ids));

  // Process <frame> children.
  WorldHashMap<std::string, AttachmentEntityId> frame_name_to_id_map;
  for (int i = 0; i < model.FrameCount(); ++i) {
    const ::sdf::Frame* frame = model.FrameByIndex(i);
    INTR_RET_CHECK_NE(frame, nullptr);

    INTR_ASSIGN_OR_RETURN(
        AttachmentEntityId frame_id,
        ParseFrame(*frame, name, world_t_this, model_label,
                   frame_name_to_id_map, link_name_to_id_map));

    if (frame_id == kInvalidEntityId) {
      continue;
    }

    // This frame may have been parented to an entity that's not in the current
    // model, so we should use the same collections component as its parent
    // link.
    INTR_ASSIGN_OR_RETURN(
        const AttachmentComponent* frame_attachment,
        world_->GetComponentByEntityId<AttachmentComponent>(frame_id));

    absl::StatusOr<CollectionsMemberEntityId>
        frame_parent_collections_member_id =
            world_->ValidateEntity<CollectionsMemberEntityId>(
                frame_attachment->GetParentId());

    // Similarly, frames in a model could have been specified as world frames.
    if (!frame_parent_collections_member_id.ok()) {
      continue;
    }

    // Assume the parent only belongs to a single collection for now.
    INTR_ASSIGN_OR_RETURN(
        const CollectionsMemberComponent* frame_parent_collections_member,
        world_->GetComponentByEntityId<CollectionsMemberComponent>(
            *frame_parent_collections_member_id));
    auto parent_collections_to_frame_parent_map =
        frame_parent_collections_member->GetParentCollectionsIdToTypesMap();

    if (parent_collections_to_frame_parent_map.size() > 1) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Frame '$0' parent belongs to more than one collections entity. "
          "Unable to determine proper collection for frame.",
          name));
    }
    CollectionsEntityId frame_parent_collection_id =
        parent_collections_to_frame_parent_map.begin()->first;

    // Add frame parent collection to frame's membership component.
    INTR_ASSIGN_OR_RETURN(
        CollectionsMemberComponent * collections_member,
        world_->GetOrCreateComponentByEntityId<CollectionsMemberComponent>(
            frame_id));

    const bool create_attachment_entity =
        GetAttributeAsBool(frame->Element(),
                           std::string(kCreateAttachmentEntityCustomAttribute))
            .value_or(false);

    auto collection_type = CollectionsComponent::kCoordinateFrames;
    if (create_attachment_entity) {
      collection_type = CollectionsComponent::kAttachmentFrames;
    }

    INTR_RETURN_IF_ERROR(collections_member->AddParentCollection(
        frame_parent_collection_id, collection_type));

    // Add frame to frame parent collection's list of coordinate frames.
    INTR_ASSIGN_OR_RETURN(CollectionsComponent * frame_parent_collection,
                          world_->GetComponentByEntityId<CollectionsComponent>(
                              frame_parent_collection_id));
    std::vector<CollectionsMemberEntityId> new_collection_members(
        frame_parent_collection->GetCollectionMembers(collection_type));
    INTR_ASSIGN_OR_RETURN(
        CollectionsMemberEntityId frame_member_id,
        world_->ValidateEntity<CollectionsMemberEntityId>(frame_id));
    new_collection_members.push_back(frame_member_id);
    INTR_RETURN_IF_ERROR(frame_parent_collection->SetCollectionMembers(
        collection_type, new_collection_members));
  }

  // Remove the collections entity and return if the collection is empty and it
  // doesn't carry any additional userdata (such as plugin info).
  //
  // Note that we cannot do this check earlier (and avoid creating the
  // collections entity) since we inline some fixed joints. I.e., only after
  // going through all <joint> elements we can say whether we actually have
  // created a joint entity.
  if (collections_component->GetAllCollectionMembers().empty() &&
      !model.Element()->HasElement("plugin")) {
    INTR_RETURN_IF_ERROR(world_->RemoveEntity(collections_id));
    return link_name_to_id_map;
  }

  if (has_non_fixed_joint) {
    // When there is at least one DoF joint. Create add a RobotComponent to the
    // collections entity and set a dummy solvable frame (necessary for the
    // World V1 Robots aspect API to work).
    INTR_ASSIGN_OR_RETURN(
        RobotComponent * robot_component,
        collections_ent->GetOrCreateComponent<RobotComponent>());
    INTR_RETURN_IF_ERROR(robot_component->AddSolvableFrames(
        AttachmentEntityId(kInvalidEntityId),
        AttachmentEntityId(kInvalidEntityId), kDefaultKinematicSolverKey));

    // Update joint DOFs to ensure that they are within limits. This cannot be
    // done earlier since frame poses might be set incorrectly.
    for (auto& [_, joint_id] : joint_local_name_to_id) {
      // In case joint value zero lies outside limits, set joint value to
      // mid-range instead.
      INTR_ASSIGN_OR_RETURN(WorldEntity * joint_entity,
                            world_->GetEntityById(joint_id));
      INTR_ASSIGN_OR_RETURN(auto* joint_kinematics,
                            joint_entity->GetComponent<KinematicsComponent>());
      if (!world_->CheckDofRawValue(joint_id, joint_kinematics->GetRawValue())
               .ok()) {
        double mid_range =
            (joint_kinematics->GetApplicationRawValueFixedLimits().first +
             joint_kinematics->GetApplicationRawValueFixedLimits().second) /
            2;
        LOG(INFO) << "Setting initial joint value of \""
                  << joint_entity->GetLocalName() << "\" to " << mid_range;
        INTR_RETURN_IF_ERROR(world_->SetDofRawValue(joint_id, mid_range,
                                                    /*enforce_limits=*/true));
      }
    }
  }

  // Process <plugin> children.
  std::vector<IconSimDevice> icon_sim_devices;
  std::vector<std::string> extra_plugins_for_user_data;
  for (const ElementConstPtr& plugin :
       GetChildrenByTag(model.Element(), "plugin")) {
    INTR_ASSIGN_OR_RETURN(std::string xml, GetCompactXml(plugin));
    INTR_ASSIGN_OR_RETURN(std::string filename,
                          GetAttributeAsString(plugin, "filename"));
    if (simulation::FilenameMatchesPluginSpec<IconSimPluginSpec>(filename)) {
      auto robot_component_or = collections_ent->GetComponent<RobotComponent>();
      if (robot_component_or.ok()) {
        INTR_ASSIGN_OR_RETURN(auto spec,
                              PluginSdfTrait<IconSimPluginSpec>::Parse(xml));
        (*robot_component_or)->SetIconSimPluginSpec(spec);
      } else {
        return intrinsic::InternalErrorBuilder()
               << "Plugin with filename '" << filename
               << "' ignored under <model> '" << model_label.value()
               << "' without DoF.";
      }
    } else if (simulation::FilenameMatchesPluginSpec<IconSimDevice>(filename)) {
      INTR_RETURN_IF_ERROR(
          PluginSdfTrait<IconSimDevice>::Parse(xml, &icon_sim_devices));

    } else if (simulation::FilenameMatchesPluginSpec<GenericActionPluginSpec>(
                   filename)) {
      if (collections_ent->HasComponent<RobotComponent>()) {
        INTR_ASSIGN_OR_RETURN(auto* robot_component,
                              collections_ent->GetComponent<RobotComponent>());
        INTR_ASSIGN_OR_RETURN(
            auto spec, PluginSdfTrait<GenericActionPluginSpec>::Parse(xml));
        robot_component->SetGenericActionPluginSpec(spec);
        if (!spec.has_action_configs() ||
            spec.action_configs().actions().empty()) {
          return absl::NotFoundError(
              absl::Substitute("GenericActionPlugin has invalid configuration. "
                               "No actions found."));
        }
      } else {
        return absl::NotFoundError(absl::Substitute(
            "No robot component found on collection entity, $0, that the "
            "GenericActionPlugin is attached to.",
            collections_ent->GetAlias()));
      }
    } else if (simulation::FilenameMatchesPluginSpec<DigitalInputOutput>(
                   filename)) {
      INTR_ASSIGN_OR_RETURN(
          auto* user_data_component,
          collections_ent->GetOrCreateComponent<UserDataComponent>());
      const WorldHashMap<std::string, ::google::protobuf::Any>& user_data_map =
          user_data_component->UserDataProtos();

      DigitalInputOutput dio_data;
      // If there's already DIO data for this entity, we want to append to it.
      auto itr = user_data_map.find(sdf::kDioData);
      if (itr != user_data_map.end()) {
        if (!itr->second.UnpackTo(&dio_data)) {
          // Try to parse the type URL from the Any into a full name.
          std::string actual_user_data_type_name;
          if (!::google::protobuf::Any::ParseAnyTypeUrl(
                  itr->second.type_url(), &actual_user_data_type_name)) {
            actual_user_data_type_name = itr->second.type_url();
          }
          return absl::InvalidArgumentError(absl::StrCat(
              "The user data proto map for entity ",
              collections_ent->GetAlias(),
              " has an entry for digital inputs and outputs (DIOs), but that "
              "entry has the wrong type. Should be '",
              DigitalInputOutput::GetDescriptor()->full_name(), "', but is '",
              actual_user_data_type_name, "'"));
        }
      }
      INTR_RETURN_IF_ERROR(
          PluginSdfTrait<DigitalInputOutput>::Parse(xml, &dio_data));
      // Save the updated DIO data to the user data map
      ::google::protobuf::Any packed_dio_data;
      if (!packed_dio_data.PackFrom(dio_data)) {
        return absl::InternalError(
            absl::StrCat("Failed to pack DIO data for entity '",
                         collections_ent->GetAlias(), "' into an Any proto"));
      }
      user_data_component->MutableUserDataProtos().emplace(
          sdf::kDioData, std::move(packed_dio_data));
    } else {
      LOG(WARNING)
          << "Plugin with filename '" << filename << "' under model '"
          << model_label.value()
          << "' is not natively handled by world. Adding it to user data.";

      extra_plugins_for_user_data.push_back(std::move(xml));
    }
  }

  if (!extra_plugins_for_user_data.empty()) {
    INTR_ASSIGN_OR_RETURN(
        auto* collections_user_data,
        collections_ent->GetOrCreateComponent<UserDataComponent>());
    google::protobuf::StringValue str_val;
    str_val.set_value(absl::StrJoin(extra_plugins_for_user_data, "\n"));
    static_cast<void>(
        collections_user_data->MutableUserDataProtos()[kGazeboPlugins].PackFrom(
            str_val));  // TODO: Handle return value
  }

  if (!icon_sim_devices.empty()) {
    INTR_ASSIGN_OR_RETURN(
        RobotComponent * robot_component,
        collections_ent->GetOrCreateComponent<RobotComponent>());
    // Set joint angles based on ICON sim devices' <initial> tag. Users can
    // override these angles with ObjectWorldUpdates (see
    // http://intrinsic/apps/bluebird_caw/workcells/common/robot_updates.pbtxt;l=5;rcl=512753131
    // for an example).
    for (const auto& sim_device : icon_sim_devices) {
      if (sim_device.has_joint() && sim_device.has_initial()) {
        auto joint_name_and_entity =
            joint_local_name_to_id.find(sim_device.joint());
        if (joint_name_and_entity == joint_local_name_to_id.end()) {
          return absl::NotFoundError(absl::Substitute(
              "ICON sim device '$0' references joint '$1', which does not "
              "exist. Available joints: [$2]",
              sim_device.name(), sim_device.joint(),
              absl::StrJoin(std::views::keys(joint_local_name_to_id), ", ")));
        }
        INTR_RETURN_IF_ERROR(world_->SetDofRawValue(
            /*joint_handle=*/joint_name_and_entity->second,
            /*raw_value=*/sim_device.initial(),
            /*enforce_limits=*/true))
            << "Check the <initial> tag for the simulated joint device '"
            << sim_device.name() << "' in your SDF.";
      }
    }
    // Save sim device information in RobotComponent
    robot_component->SetIconSimDevices(icon_sim_devices);
  }

  // TODO(b/332530262): Move robot level config to scene object creation.
  // Parse optional <intrinsic:cartesian_limits>
  if (model.Element()->HasElement(std::string(kCartesianLimitsCustomElement))) {
    // We should have created a robot component before this. If there isn't,
    // error out to warn user about this.
    INTR_ASSIGN_OR_RETURN(
        RobotComponent * robot_component,
        collections_ent->GetComponent<RobotComponent>(),
        _ << "Found sdf tag <" << kCartesianLimitsCustomElement
          << "> under non robot <model> '" << model_label.value() << "'");
    auto cartesian_limits_element =
        model.Element()->GetElement(std::string(kCartesianLimitsCustomElement));
    INTR_ASSIGN_OR_RETURN(auto cartesian_limits,
                          ParseCartesianLimits(cartesian_limits_element));
    INTR_RETURN_IF_ERROR(robot_component->SetCartesianLimits(cartesian_limits));
  }

  if (model.Element()->HasElement(std::string(kIkSolverCustomElement))) {
    const auto ik_solver_elements = sdf::GetChildrenByTag(
        model.Element(), std::string(sdf::kIkSolverCustomElement));

    std::vector<std::pair<std::string, std::optional<std::string>>> ik_solvers;
    for (const auto& ik_solver_element : ik_solver_elements) {
      INTR_ASSIGN_OR_RETURN(auto solver_and_tip_link_name,
                            sdf::ParseIkSolver(ik_solver_element));
      ik_solvers.push_back(solver_and_tip_link_name);
    }
    // TODO(b/343309271): Support multiple ik_solvers.
    if (ik_solvers.size() > 1) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Multiple ik solvers found for model '", model.Name(),
          "'. Only one should be provided. Found [",
          absl::StrJoin(ik_solvers, ", ",
                        [](std::string* out, const auto& element) {
                          absl::StrAppend(
                              out, element.first, "(",
                              element.second.value_or("std::nullopt"), ")");
                        }),
          "]."));
    }

    if (!ik_solvers.empty()) {
      const auto& [solver_name, tip_link_name] = ik_solvers[0];

      // We should have created a robot component before this. If there isn't,
      // error out to warn user about this.
      INTR_ASSIGN_OR_RETURN(RobotComponent * robot_component,
                            collections_ent->GetComponent<RobotComponent>(),
                            _ << "Found sdf tag <" << kIkSolverCustomElement
                              << "> under non robot <model> '"
                              << model_label.value() << "'");

      INTR_ASSIGN_OR_RETURN(auto final_links,
                            world_->GetFinalEntitiesOfRobot(collections_id));
      if (final_links.empty()) {
        return absl::InvalidArgumentError(absl::StrCat(
            "No final links found for model '", model.Name(), "'."));
      }

      AttachmentEntityId tip_link_id(kInvalidEntityId);
      if (final_links.size() == 1) {
        tip_link_id = *final_links.begin();
        if (tip_link_name.has_value() &&
            *tip_link_name != world_->GetLocalNameForEntityById(tip_link_id)) {
          return absl::InvalidArgumentError(absl::StrCat(
              "Tip link name '", *tip_link_name,
              "' does not match the final link name '",
              world_->GetLocalNameForEntityById(tip_link_id), "'."));
        }
      } else {
        if (!tip_link_name.has_value()) {
          return absl::InvalidArgumentError(absl::StrCat(
              "Invalid number of final links for model '", model.Name(),
              "'. Expected 1, got ", final_links.size(), "."));
        }

        std::vector<std::string> final_link_names;
        for (const auto& final_link : final_links) {
          auto final_link_name = world_->GetLocalNameForEntityById(final_link);
          final_link_names.push_back(final_link_name);
          if (*tip_link_name == final_link_name) {
            tip_link_id = final_link;
            break;
          }
        }

        if (tip_link_id == kInvalidEntityId) {
          return absl::InvalidArgumentError(absl::StrCat(
              "Tip link name '", *tip_link_name, "' for model '", model.Name(),
              "' does not match any of the final link names [",
              absl::StrJoin(final_link_names, ", "), "]."));
        }
      }

      // Set the ik solver key with the robot
      LOG(INFO) << "Setting ik solver key '" << solver_name << "' for model '"
                << model.Name() << "' using tip link[" << tip_link_id << "]["
                << world_->GetLocalNameForEntityById(tip_link_id)
                << "] which was" << (!tip_link_name.has_value() ? " not" : "")
                << " specified directly.";
      INTR_RETURN_IF_ERROR(robot_component->AddSolvableFrames(
          AttachmentEntityId(kInvalidEntityId), tip_link_id, solver_name));
      INTR_RETURN_IF_ERROR(robot_component->RemoveSolvableFrames(
          AttachmentEntityId(kInvalidEntityId),
          AttachmentEntityId(kInvalidEntityId)));
    }
  }

  // If there are only links and fixed joints, we can just return now after
  // plugins are processed.
  if (!has_non_fixed_joint) {
    return link_name_to_id_map;
  }

  // TODO(b/148814696): Remove this robot GroupId-related code once all clients
  // have been migrated to entity-based functions. If there's a robot group, add
  // the newly-created robot entity to its list.
  INTR_ASSIGN_OR_RETURN(
      auto robot_id,
      world_->ValidateEntity<RobotCollectionsEntityId>(collections_id));
  if (!robot_group_id.empty()) {
    std::vector<RobotCollectionsEntityId> group_robot_ids;
    auto old_group_robot_ids_or =
        world_->GetRobotCollectionsEntityIdsForRobotGroupId(robot_group_id);
    if (old_group_robot_ids_or.ok()) {
      group_robot_ids = *old_group_robot_ids_or.value();
    }
    group_robot_ids.push_back(robot_id);
    world_->SetRobotCollectionsEntityIdsForRobotGroupId(robot_group_id,
                                                        group_robot_ids);
  }

  INTR_RETURN_IF_ERROR(world_->SortRobotLinkAndJointLists(robot_id));
  return link_name_to_id_map;
  // NOLINTNEXTLINE(readability/fn_size)
}

absl::StatusOr<WorldFromSdf::ParseLinkResult> WorldFromSdf::ParseLink(
    const ::sdf::Link& link, const Pose3d& world_t_parent_model,
    const LabelId& model_label, CollectionsEntityId collections_id) {
  const std::string name = link.Name();
  if (absl::StrContains(name, kNestedModelSeparator)) {
    return absl::InvalidArgumentError(
        absl::Substitute("<link> name containing $0 is not allowed for nested "
                         "frame disambiguity, please rename model with name $1",
                         kNestedModelSeparator, name));
  }

  auto link_id = world_->CreateEntityWithComponentTypes<LinkEntityId>();
  INTR_ASSIGN_OR_RETURN(WorldEntity * link_ent, world_->GetEntityById(link_id));
  INTR_RETURN_IF_ERROR(link_ent->SetLocalName(name));
  INTR_RETURN_IF_ERROR(link_ent->AddLabels({model_label}));

  // Process <pose>. Note that links are created as children of the world root.
  // They will be reparented later if they are the child link of a <joint>.
  INTR_ASSIGN_OR_RETURN(AttachmentComponent * link_attachment,
                        link_ent->GetComponent<AttachmentComponent>());
  INTR_ASSIGN_OR_RETURN(const Pose3d parent_model_t_this,
                        ParseSemanticPose(link.SemanticPose()));
  link_attachment->SetParentId(kRootEntityId);
  link_attachment->SetParentTThis(world_t_parent_model * parent_model_t_this);
  // Cache the link's original world-relative pose. It is used if this link is
  // reparented in ParseJoint().
  link_id_to_original_world_t_link_[link_id] =
      world_->GetTransform(kRootEntityId, link_id);

  if (!skip_geometry_parsing_) {
    INTR_ASSIGN_OR_RETURN(GeometryComponent * link_geometry,
                          link_ent->GetComponent<GeometryComponent>());

    // Process <collision> elements.
    NamedGeometrySet collision_geo_set;
    const auto collision_count = link.CollisionCount();
    for (auto i = 0; i < collision_count; ++i) {
      const auto* collision = link.CollisionByIndex(i);
      INTR_RET_CHECK_NE(collision, nullptr);
      INTR_ASSIGN_OR_RETURN(auto collision_geo, GeometryFromSdfCollision(
                                                    *collision, uri_resolver_));
      if (collision_geo == std::nullopt) {
        continue;
      }
      collision_geo_set.emplace(collision->Name(),
                                std::move(collision_geo).value());
    }
    if (!collision_geo_set.empty()) {
      // TODO(b/180671784): We used to merge the collision shapes here into a
      // single mesh. In order to make sure primitive shapes can be output in
      // its original representation we are no longer doing the merge. The
      // reason being primitive shapes cannot be merged to a primitive shape.
      // Revisit this decision when appropriate.
      link_geometry->SetGeometry(kKindCollisionGeometry, collision_geo_set);
    }

    // Process <visual> elements.
    NamedGeometrySet visual_geo_set;
    const auto visual_count = link.VisualCount();
    for (auto i = 0; i < visual_count; ++i) {
      const auto* visual = link.VisualByIndex(i);
      INTR_RET_CHECK_NE(visual, nullptr);
      INTR_ASSIGN_OR_RETURN(auto visual_shape,
                            GeometryFromSdfVisual(*visual, uri_resolver_));
      if (visual_shape == std::nullopt) {
        continue;
      }
      visual_geo_set.emplace(visual->Name(), std::move(visual_shape).value());
    }
    if (!visual_geo_set.empty()) {
      link_geometry->SetGeometry(kKindVisualGeometry, visual_geo_set);
    }
  }

  // Construct a new physics component.
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<PhysicsComponent> physics_component,
                        PhysicsComponentFromSdfLink(link));
  INTR_RETURN_IF_ERROR(link_ent->SetComponent(std::move(physics_component)));

  // Construct a new collision component.
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<CollisionComponent> collision_component,
                        CollisionComponentFromSdfLink(link));
  INTR_RETURN_IF_ERROR(link_ent->SetComponent(std::move(collision_component)));

  ParseLinkResult result;
  result.link_id = link_id;

  // Process optional <sensor> elements.
  const uint64_t sensor_count = link.SensorCount();
  for (uint64_t i = 0; i < sensor_count; ++i) {
    INTR_ASSIGN_OR_RETURN(
        auto sensor_id,
        CreateSensorEntity(*link.SensorByIndex(i), link_id, collections_id));
    INTR_ASSIGN_OR_RETURN(WorldEntity * sensor_ent,
                          world_->GetEntityById(sensor_id));
    INTR_RETURN_IF_ERROR(sensor_ent->AddLabels({model_label}));
    result.sensor_ids.push_back(sensor_id);
  }

  // Process optional <light> elements.
  for (const ElementConstPtr& light :
       GetChildrenByTag(link.Element(), "light")) {
    LOG(WARNING) << "Light '" << light->GetName()
                 << "' is ignored; <light> under <link> not supported.";
  }

  // Process optional <projector> elements.
  const auto projector_count = link.ProjectorCount();
  for (auto i = 0; i < projector_count; ++i) {
    const auto& projector = *link.ProjectorByIndex(i);
    INTR_ASSIGN_OR_RETURN(auto projector_id,
                          ParseProjector(projector, link_id, collections_id));
    INTR_ASSIGN_OR_RETURN(WorldEntity * projector_ent,
                          world_->GetEntityById(projector_id));
    INTR_RETURN_IF_ERROR(projector_ent->AddLabels({model_label}));
    result.projector_ids.push_back(projector_id);
  }

  return result;
}

absl::StatusOr<WorldFromSdf::CreateJointEntitiesResult>
WorldFromSdf::CreateJointEntities(
    const ::sdf::Joint& joint, const Pose3d& world_t_parent_model,
    const std::string& model_name, const LabelId& model_label,
    const WorldHashMap<std::string, LinkEntityId>& link_name_to_id_map,
    std::optional<CollectionsEntityId> collections_id) {
  // Create the joint entity.
  auto joint_id = world_->CreateEntityWithComponentTypes<JointEntityId>();
  INTR_ASSIGN_OR_RETURN(WorldEntity * joint_ent,
                        world_->GetEntityById(joint_id));

  // World independent data parsed from the joint.
  INTR_ASSIGN_OR_RETURN(sdf::ParseJointResult parse_joint_result,
                        sdf::ParseJoint(joint));

  const std::string& name = parse_joint_result.name;
  INTR_RETURN_IF_ERROR(joint_ent->SetLocalName(name));
  INTR_RETURN_IF_ERROR(joint_ent->AddLabels({model_label}));

  // Override the existing kinematics component
  INTR_RETURN_IF_ERROR(joint_ent->SetComponent<KinematicsComponent>(
      std::move(parse_joint_result.kinematics_component)));
  INTR_ASSIGN_OR_RETURN(KinematicsComponent * joint_kinematics,
                        joint_ent->GetComponent<KinematicsComponent>());

  // This replicates the DofId assigned by original implementation in
  // Robots::AddRobot().
  world_->SetDofIdForEntityId(
      DofId(absl::StrCat(joint_id.value(), kNestedModelSeparator, model_name,
                         kNestedModelSeparator, name)),
      joint_id);

  // Create attachment component for joint entity and attach to parent
  INTR_ASSIGN_OR_RETURN(AttachmentComponent * joint_attachment,
                        joint_ent->GetComponent<AttachmentComponent>());

  // Find attachment component of joint child and attach that to joint entity.
  const std::string& child_link_name = parse_joint_result.child_name;
  INTR_ASSIGN_OR_RETURN(
      LinkEntityId child_link_id,
      FindLinkId(model_name, link_name_to_id_map, child_link_name),
      _ << "while parsing <child> of <joint> '" << name << "'");
  INTR_ASSIGN_OR_RETURN(WorldEntity * child_link_entity,
                        world_->GetEntityById(child_link_id));
  INTR_ASSIGN_OR_RETURN(AttachmentComponent * child_link_attachment,
                        child_link_entity->GetComponent<AttachmentComponent>());

  if (child_link_attachment->GetParentId() != kRootEntityId) {
    // TODO(b/300357844): Handle this case or have this unsupported behavior
    // properly documented in public documentation for sdf support.
    INTR_ASSIGN_OR_RETURN(
        WorldEntity * child_existing_parent,
        world_->GetEntityById(child_link_attachment->GetParentId()));
    return AlreadyExistsErrorBuilder()
           << " Joint named '" << name << "' has child link '"
           << child_link_entity->GetLocalName()
           << "' which already has a parent named '"
           << child_existing_parent->GetLocalName() << "'.";
  }
  child_link_attachment->SetParentId(joint_id);
  child_link_attachment->SetParentTThis(Pose3d());

  const std::string& parent_link_name = parse_joint_result.parent_name;
  AttachmentEntityId parent_link_id = kRootEntityId;
  if (parent_link_name != "world") {
    INTR_ASSIGN_OR_RETURN(
        parent_link_id,
        FindLinkId(model_name, link_name_to_id_map, parent_link_name),
        _ << "while parsing <parent> of <joint> '" << name << "'");
    joint_attachment->SetParentId(parent_link_id);
    INTR_RET_CHECK(parse_joint_result.parent_t_child.has_value());
    joint_attachment->SetParentTThis(*parse_joint_result.parent_t_child);
  } else {
    auto it = link_id_to_original_world_t_link_.find(child_link_id);
    INTR_RET_CHECK(it != link_id_to_original_world_t_link_.end())
        << "Could not find link id " << child_link_id;
    joint_attachment->SetParentId(parent_link_id);
    joint_attachment->SetParentTThis(it->second);
    joint_kinematics->SetParentTInboard(it->second *
                                        parse_joint_result.child_t_joint);
  }

  if (parent_link_id == kRootEntityId &&
      joint_kinematics->GetMotionType() ==
          intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED) {
    // TODO(b/231642607) Don't use fixed_in_root
    child_link_attachment->MarkFixedInRoot(true);
  }

  // Exclude the parent and child links from colliding with each other.
  auto parent_link_phys_ent_id_or =
      world_->ValidateEntity<PhysicalEntityId>(parent_link_id);
  if (parent_link_phys_ent_id_or.ok()) {
    INTR_ASSIGN_OR_RETURN(
        auto* parent_link_collision,
        world_->GetComponentByEntityId<CollisionComponent>(parent_link_id));
    INTR_ASSIGN_OR_RETURN(
        auto* child_link_collision,
        child_link_entity->GetComponent<CollisionComponent>());
    parent_link_collision->AddExclusionId(child_link_id);
    child_link_collision->AddExclusionId(parent_link_phys_ent_id_or.value());
  }

  // Process optional <physics>.
  auto physics_or = GetChildWithTag(joint.Element(), "physics");
  if (physics_or.ok()) {
    INTR_ASSIGN_OR_RETURN(std::string xml, GetCompactXml(physics_or.value()));
    INTR_ASSIGN_OR_RETURN(UserDataComponent * joint_user_data,
                          joint_ent->GetOrCreateComponent<UserDataComponent>());
    joint_user_data->MutableUserDataMap().emplace(kGazeboJointPhysics, xml);
  }

  CreateJointEntitiesResult result;
  result.joint_id = joint_id;

  // Process optional <sensor> elements.
  const uint64_t sensor_count = joint.SensorCount();
  for (uint64_t i = 0; i < sensor_count; ++i) {
    INTR_ASSIGN_OR_RETURN(
        auto sensor_id,
        CreateSensorEntity(*joint.SensorByIndex(i), joint_id, collections_id));
    INTR_ASSIGN_OR_RETURN(WorldEntity * sensor_ent,
                          world_->GetEntityById(sensor_id));
    INTR_RETURN_IF_ERROR(sensor_ent->AddLabels({model_label}));
    result.sensor_ids.push_back(sensor_id);
  }

  return result;
}

absl::StatusOr<AttachmentEntityId> WorldFromSdf::ParseFrame(
    const ::sdf::Frame& frame, const std::string& model_name,
    const Pose3d& world_t_parent_model, const LabelId& model_label,
    WorldHashMap<std::string, AttachmentEntityId>& frame_name_to_id_map,
    const WorldHashMap<std::string, LinkEntityId>& link_name_to_id_map) {
  // Ignore <frame>s by default and only create an entity if explicitly
  // requested through a special attribute.
  // Background: <frame>s are a helper mechanism for authoring SDF and are not
  // explicitly represented in any form when loaded in Gazebo. E.g., <frame>s
  // can be referenced in the 'relative_to' attributes of <pose>s, and <include
  // merge="true"> statements result in the automatic generation of <frame>s
  // named "_merged__xyz__model__".
  const bool create_entity =
      GetAttributeAsBool(frame.Element(),
                         std::string(kCreateEntityCustomAttribute))
          .value_or(false);
  const bool create_attachment_entity =
      GetAttributeAsBool(frame.Element(),
                         std::string(kCreateAttachmentEntityCustomAttribute))
          .value_or(false);
  if (!create_entity && !create_attachment_entity) {
    return AttachmentEntityId(kInvalidEntityId);
  }

  // Create attachment entity for the frame.
  AttachmentEntityId frame_id =
      world_->CreateEntityWithComponentTypes<AttachmentEntityId>();
  INTR_ASSIGN_OR_RETURN(WorldEntity * frame_ent,
                        world_->GetEntityById(frame_id));
  INTR_ASSIGN_OR_RETURN(AttachmentComponent * frame_attachment,
                        frame_ent->GetComponent<AttachmentComponent>());

  // Process 'name' attribute.
  INTR_RETURN_IF_ERROR(frame_ent->SetLocalName(frame.Name()));
  INTR_RETURN_IF_ERROR(frame_ent->AddLabels({model_label}));

  // Find parent entity from 'attached_to' attribute or by defaulting to root.
  AttachmentEntityId frame_parent_id;
  if (frame.AttachedTo().empty()) {
    if (model_name.empty()) {
      // <frame> is under <world> and does not have a an 'attached_to' attribute
      // -> create a frame under the root entity.
      frame_parent_id = kRootEntityId;
    } else {
      return absl::NotFoundError(absl::Substitute(
          "Attribute 'attached_to' not found while parsing <frame name=\"$0\"> "
          "under model \"$1\". Frames under a <model> are only supported if "
          "they are attached to a link.",
          frame.Name(), model_name));
    }
  } else {
    // <frame> is under <world> or under a <model> and has an 'attached_to'
    // attribute -> create a frame under the referenced link/frame entity.
    INTR_ASSIGN_OR_RETURN(
        frame_parent_id,
        FindParentId(model_name, link_name_to_id_map, frame_name_to_id_map,
                     frame.AttachedTo()),
        _ << "while parsing <frame name=\"" << frame.Name()
          << "\">. Only frames with an 'attached_to' attribute "
             "pointing to a <link> or <frame> are currently supported.");
  }
  frame_attachment->SetParentId(frame_parent_id);

  // Calculate and set attachment pose.
  INTR_ASSIGN_OR_RETURN(const Pose3d parent_model_t_frame,
                        ParseSemanticPose(frame.SemanticPose()));
  const Pose3d world_t_frame = world_t_parent_model * parent_model_t_frame;
  INTR_RETURN_IF_ERROR(world_->MarkTransformInaccuracy(frame_parent_id,
                                                       frame_id,
                                                       /*inaccurate=*/true));
  INTR_RETURN_IF_ERROR(
      world_->UpdateIndirectTransform(kRootEntityId, frame_id, world_t_frame)
          .status());

  frame_name_to_id_map[frame.Name()] = frame_id;
  return frame_id;
}

absl::StatusOr<SensorEntityId> WorldFromSdf::CreateSensorEntity(
    const ::sdf::Sensor& sensor, AttachmentEntityId parent_id,
    std::optional<CollectionsEntityId> collections_id) {
  SensorEntityId sensor_id =
      world_->CreateEntityWithComponentTypes<AttachmentComponentType,
                                             SensorComponentType>();
  INTR_ASSIGN_OR_RETURN(WorldEntity * sensor_ent,
                        world_->GetEntityById(sensor_id));

  if (collections_id.has_value()) {
    // Add collections_id as a sensor collection parent.
    INTR_ASSIGN_OR_RETURN(
        CollectionsMemberComponent * collections_member,
        sensor_ent->GetOrCreateComponent<CollectionsMemberComponent>());
    INTR_RETURN_IF_ERROR(collections_member->AddParentCollection(
        *collections_id, CollectionsComponent::kSensors));
  }

  INTR_ASSIGN_OR_RETURN(auto* sensor_attachment,
                        sensor_ent->GetComponent<AttachmentComponent>());

  INTR_ASSIGN_OR_RETURN(ParseSensorResult parse_sensor_result,
                        sdf::ParseSensor(sensor));

  INTR_RETURN_IF_ERROR(sensor_ent->SetLocalName(parse_sensor_result.name));

  sensor_attachment->SetParentTThis(parse_sensor_result.parent_t_sensor);
  sensor_attachment->SetParentId(parent_id);

  INTR_RETURN_IF_ERROR(sensor_ent->SetComponent<SensorComponent>(
      std::move(parse_sensor_result.sensor_component)));

  return sensor_id;
}

absl::Status WorldFromSdf::BypassFixedCrossModelJoint(JointEntityId joint_id) {
  // Verify the joint has an attachment component, a kinematics component, and
  // fixed motion type.
  INTR_ASSIGN_OR_RETURN(auto* joint_ent, world_->GetEntityById(joint_id));
  INTR_ASSIGN_OR_RETURN(auto* joint_kinematics,
                        joint_ent->GetComponent<KinematicsComponent>());
  if (joint_kinematics->GetMotionType() !=
      intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "joint (ID " << joint_id.value() << ") is not fixed";
  }

  // Verify the joint has exactly 1 child.
  auto children = world_->GetChildrenOf(joint_id);
  if (children.size() != 1) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "joint (ID " << joint_id.value() << ") has " << children.size()
           << " children";
  }
  INTR_ASSIGN_OR_RETURN(auto* child_ent, world_->GetEntityById(children[0]));

  // Verify that the parent and child entities have no labels in common.
  INTR_ASSIGN_OR_RETURN(auto* joint_attachment,
                        joint_ent->GetComponent<AttachmentComponent>());
  AttachmentEntityId parent_id = joint_attachment->GetParentId();
  INTR_ASSIGN_OR_RETURN(const auto* parent_ent,
                        world_->GetEntityById(parent_id));
  const auto& parent_labels = parent_ent->GetLabels();
  const auto& child_labels = child_ent->GetLabels();
  if (std::any_of(child_labels.begin(), child_labels.end(),
                  [&parent_labels](const LabelId& label) {
                    return parent_labels.find(label) != parent_labels.end();
                  })) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "children of joint (ID " << joint_id.value()
           << ") have common labels";
  }

  // Update the child's attachment component to bypass the fixed joint.
  INTR_ASSIGN_OR_RETURN(auto* child_attachment,
                        child_ent->GetComponent<AttachmentComponent>());
  child_attachment->SetParentId(parent_id);
  child_attachment->SetParentTThis(joint_attachment->GetParentTThis() *
                                   child_attachment->GetParentTThis());
  return absl::OkStatus();
}

absl::Status WorldFromSdf::BypassFixedInModelJoint(JointEntityId joint_id) {
  // Verify the joint has an attachment component, a kinematics component, and
  // fixed motion type.
  INTR_ASSIGN_OR_RETURN(auto* joint_ent, world_->GetEntityById(joint_id));
  INTR_ASSIGN_OR_RETURN(auto* joint_kinematics,
                        joint_ent->GetComponent<KinematicsComponent>());
  if (joint_kinematics->GetMotionType() !=
      intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "joint (ID " << joint_id.value() << ") is not fixed";
  }

  // Verify the joint has exactly 1 child.
  auto children = world_->GetChildrenOf(joint_id);
  if (children.size() != 1) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "joint (ID " << joint_id.value() << ") has " << children.size()
           << " children";
  }

  // Ensure we don't accidentally remove useful information from the world.
  if (joint_ent->HasComponent<SensorComponent>() ||
      joint_ent->HasComponent<UserDataComponent>()) {
    return absl::FailedPreconditionError(
        "Joint entity has extra components and cannot be removed");
  }

  // Verify that the parent and child entities have all labels in common.
  INTR_ASSIGN_OR_RETURN(auto* joint_attachment,
                        joint_ent->GetComponent<AttachmentComponent>());

  // Update the child's attachment component to bypass the fixed joint.
  INTR_ASSIGN_OR_RETURN(
      auto* child_attachment,
      world_->GetComponentByEntityId<AttachmentComponent>(children[0]));
  child_attachment->SetParentId(joint_attachment->GetParentId());
  child_attachment->SetParentTThis(joint_attachment->GetParentTThis() *
                                   child_attachment->GetParentTThis());
  return absl::OkStatus();
}

absl::StatusOr<ProjectorEntityId> WorldFromSdf::ParseProjector(
    const ::sdf::Projector& projector, AttachmentEntityId parent_id,
    std::optional<CollectionsEntityId> collections_id) {
  ProjectorEntityId projector_id =
      world_->CreateEntityWithComponentTypes<AttachmentComponentType,
                                             ProjectorComponentType>();
  INTR_ASSIGN_OR_RETURN(WorldEntity * projector_ent,
                        world_->GetEntityById(projector_id));

  if (collections_id.has_value()) {
    // Add collections_id as a projector collection parent.
    INTR_ASSIGN_OR_RETURN(
        CollectionsMemberComponent * collections_member,
        projector_ent->GetOrCreateComponent<CollectionsMemberComponent>());
    INTR_RETURN_IF_ERROR(collections_member->AddParentCollection(
        *collections_id, CollectionsComponent::kProjectors));
  }

  INTR_RETURN_IF_ERROR(projector_ent->SetLocalName(projector.Name()));

  INTR_ASSIGN_OR_RETURN(const Pose3d parent_t_this,
                        ParseSemanticPose(projector.SemanticPose()));
  INTR_ASSIGN_OR_RETURN(auto* projector_attachment,
                        projector_ent->GetComponent<AttachmentComponent>());
  projector_attachment->SetParentId(parent_id);
  projector_attachment->SetParentTThis(parent_t_this);

  INTR_ASSIGN_OR_RETURN(auto* projector_component,
                        projector_ent->GetComponent<ProjectorComponent>());
  projector_component->SetHorizontalFov(projector.HorizontalFov().Radian());
  projector_component->SetNearClip(projector.NearClip());
  projector_component->SetFarClip(projector.FarClip());
  projector_component->SetVisibilityFlags(projector.VisibilityFlags());
  INTR_ASSIGN_OR_RETURN(std::string filename,
                        uri_resolver_(projector.Texture()));
  if (!file::Exists(filename, file::Defaults()).ok()) {
    return intrinsic::NotFoundErrorBuilder()
           << "Texture file doesn't exist: " << filename;
  }
  std::string texture_str;
  CHECK_OK(file::GetContents(filename, &texture_str, file::Defaults()));
  std::string extension(file::Extension(filename));
  Texture texture = {.data = std::move(texture_str),
                     .format = std::move(extension)};
  projector_component->SetTexture(texture);

  return projector_id;
}

}  // namespace sdf
}  // namespace intrinsic
