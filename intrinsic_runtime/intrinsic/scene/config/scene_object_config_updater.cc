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

#include "intrinsic/scene/config/scene_object_config_updater.h"

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "intrinsic/scene/proto/v1/scene_object_config.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_updates.pb.h"

namespace intrinsic {
namespace scene_object {

using ::google::protobuf::Map;
using ::google::protobuf::RepeatedPtrField;
using ::intrinsic_proto::scene_object::v1::CartesianLimitsUpdate;
using ::intrinsic_proto::scene_object::v1::CreateFrameUpdate;
using ::intrinsic_proto::scene_object::v1::EntityPoseUpdate;
using ::intrinsic_proto::scene_object::v1::GeometryUpdate;
using ::intrinsic_proto::scene_object::v1::NamedConfiguration;
using ::intrinsic_proto::scene_object::v1::SceneObject;
using ::intrinsic_proto::scene_object::v1::SceneObjectConfig;
using ::intrinsic_proto::scene_object::v1::SceneObjectInstanceUpdate;
using ::intrinsic_proto::scene_object::v1::SceneObjectInstanceUpdates;
using ::intrinsic_proto::scene_object::v1::SetNamedConfigurationsUpdate;

namespace {

void MergeEntityPoseUpdate(const EntityPoseUpdate& update,
                           RepeatedPtrField<EntityPoseUpdate>* target) {
  for (EntityPoseUpdate& target_update : *target) {
    if (target_update.entity_name() == update.entity_name()) {
      target_update = update;
      return;
    }
  }

  *target->Add() = update;
}

void MergeCreateFrameUpdate(const CreateFrameUpdate& update,
                            RepeatedPtrField<CreateFrameUpdate>* target) {
  for (CreateFrameUpdate& target_update : *target) {
    if (target_update.new_frame_name() == update.new_frame_name()) {
      target_update = update;
      return;
    }
  }

  *target->Add() = update;
}

void MergeNamedJointConfigs(const SetNamedConfigurationsUpdate& update,
                            SetNamedConfigurationsUpdate& target) {
  if (update.clear_all_named_configurations()) {
    target = update;
    return;
  }

  // Remove requested configs from target's set list
  for (const std::string& name : update.named_configurations_to_remove()) {
    RepeatedPtrField<NamedConfiguration>* set_list =
        target.mutable_named_configurations_to_set();
    for (auto it = set_list->begin(); it != set_list->end();) {
      if (it->name() == name) {
        it = set_list->erase(it);
      } else {
        ++it;
      }
    }
    // Also add to remove list if not already there
    bool found = false;
    for (const std::string& rem_name :
         target.named_configurations_to_remove()) {
      if (rem_name == name) {
        found = true;
        break;
      }
    }
    if (!found) {
      target.add_named_configurations_to_remove(name);
    }
  }

  // Add or overwrite configs to set
  for (const NamedConfiguration& new_config :
       update.named_configurations_to_set()) {
    bool overwritten = false;
    for (NamedConfiguration& target_config :
         *target.mutable_named_configurations_to_set()) {
      if (target_config.name() == new_config.name()) {
        target_config = new_config;
        overwritten = true;
        break;
      }
    }
    if (!overwritten) {
      *target.add_named_configurations_to_set() = new_config;
    }
    // If it was in the remove list, we should remove it from there since we are
    // setting it now.
    RepeatedPtrField<std::string>* rem_list =
        target.mutable_named_configurations_to_remove();
    for (auto it = rem_list->begin(); it != rem_list->end();) {
      if (*it == new_config.name()) {
        it = rem_list->erase(it);
      } else {
        ++it;
      }
    }
  }
}

void MergeCartesianLimits(const CartesianLimitsUpdate& update,
                          CartesianLimitsUpdate& target) {
#define MERGE_REPEATED(field)                           \
  if (update.field##_size() > 0) {                      \
    target.mutable_##field()->CopyFrom(update.field()); \
  }

#define MERGE_OPTIONAL(field)           \
  if (update.has_##field()) {           \
    target.set_##field(update.field()); \
  }

  MERGE_REPEATED(min_translational_position);
  MERGE_REPEATED(max_translational_position);
  MERGE_REPEATED(min_translational_velocity);
  MERGE_REPEATED(max_translational_velocity);
  MERGE_REPEATED(min_translational_acceleration);
  MERGE_REPEATED(max_translational_acceleration);
  MERGE_REPEATED(min_translational_jerk);
  MERGE_REPEATED(max_translational_jerk);

  MERGE_OPTIONAL(max_rotational_velocity);
  MERGE_OPTIONAL(max_rotational_acceleration);
  MERGE_OPTIONAL(max_rotational_jerk);

#undef MERGE_REPEATED
#undef MERGE_OPTIONAL
}

void MergeJointLimitUpdate(const ::intrinsic_proto::JointLimitUpdate& update,
                           ::intrinsic_proto::JointLimitUpdate& target) {
#define MERGE_FIELD(field)                \
  do {                                    \
    if (update.has_##field()) {           \
      target.set_##field(update.field()); \
    }                                     \
  } while (0)

  MERGE_FIELD(min_position);
  MERGE_FIELD(max_position);
  MERGE_FIELD(max_velocity);
  MERGE_FIELD(max_acceleration);
  MERGE_FIELD(max_jerk);
  MERGE_FIELD(max_effort);
#undef MERGE_FIELD
}

bool MatchGeometry(const std::string& remove_name,
                   GeometryUpdate::GeometryType remove_type,
                   const std::string& target_name,
                   GeometryUpdate::GeometryType target_type) {
  const bool type_match =
      (remove_type == GeometryUpdate::GEOMETRY_TYPE_UNSPECIFIED ||
       remove_type == target_type);
  const bool name_match = (remove_name.empty() || remove_name == target_name);
  return type_match && name_match;
}

void RemoveMatchingSets(const std::string& name,
                        GeometryUpdate::GeometryType type,
                        RepeatedPtrField<GeometryUpdate::GeometryToSet>* sets) {
  auto it = sets->begin();
  while (it != sets->end()) {
    if (MatchGeometry(name, type, it->geometry_name(), it->type())) {
      it = sets->erase(it);
    } else {
      ++it;
    }
  }
}

void AddGeometryToRemove(const std::string& name,
                         GeometryUpdate::GeometryType type,
                         GeometryUpdate* target_update) {
  if (absl::c_none_of(target_update->geometries_to_remove(),
                      [&name, type](const auto& target_remove) {
                        return target_remove.type() == type &&
                               target_remove.geometry_name() == name;
                      })) {
    auto* remove = target_update->add_geometries_to_remove();
    remove->set_geometry_name(name);
    remove->set_type(type);
  }
}

void MergeGeometryUpdate(const GeometryUpdate& update,
                         Map<std::string, GeometryUpdate>* target) {
  const std::string& entity_name = update.entity_name();
  GeometryUpdate& target_update = (*target)[entity_name];

  // Merge geometries_to_remove
  for (const auto& remove : update.geometries_to_remove()) {
    AddGeometryToRemove(remove.geometry_name(), remove.type(), &target_update);
    RemoveMatchingSets(remove.geometry_name(), remove.type(),
                       target_update.mutable_geometries_to_set());
  }

  // Merge geometries_to_set
  for (const auto& set : update.geometries_to_set()) {
    if (!set.has_geometry()) {
      RemoveMatchingSets(set.geometry_name(), set.type(),
                         target_update.mutable_geometries_to_set());
      AddGeometryToRemove(set.geometry_name(), set.type(), &target_update);
    } else {
      // Standard set logic
      auto it = absl::c_find_if(*target_update.mutable_geometries_to_set(),
                                [&set](const auto& target_set) {
                                  return target_set.geometry_name() ==
                                             set.geometry_name() &&
                                         target_set.type() == set.type();
                                });
      if (it == target_update.mutable_geometries_to_set()->end()) {
        *target_update.add_geometries_to_set() = set;
      } else {
        *it = set;
      }
    }
  }
}

}  // namespace

absl::Status ApplyUpdatesToConfig(const SceneObject& object,
                                  const SceneObjectInstanceUpdates& updates,
                                  SceneObjectConfig* config) {
  for (const SceneObjectInstanceUpdate& update : updates.updates()) {
    switch (update.update_case()) {
      case ::intrinsic_proto::scene_object::v1::SceneObjectInstanceUpdate::
          kEntityPose: {
        const auto& pose_update = update.entity_pose();
        auto* new_frame_updates = config->mutable_new_frames();
        const auto new_frame_update = absl::c_find_if(
            *new_frame_updates, [&](const CreateFrameUpdate& u) {
              return u.new_frame_name() == pose_update.entity_name();
            });
        if (new_frame_update == new_frame_updates->end()) {
          MergeEntityPoseUpdate(pose_update,
                                config->mutable_entity_pose_updates());
        } else {
          *new_frame_update->mutable_parent_t_new_frame() =
              pose_update.parent_t_this();
        }
        break;
      }
      case ::intrinsic_proto::scene_object::v1::SceneObjectInstanceUpdate::
          kCreateFrame:
        MergeCreateFrameUpdate(update.create_frame(),
                               config->mutable_new_frames());
        break;
      case ::intrinsic_proto::scene_object::v1::SceneObjectInstanceUpdate::
          kSetNamedConfigurations:
        MergeNamedJointConfigs(update.set_named_configurations(),
                               *config->mutable_named_joint_configs());
        break;
      case ::intrinsic_proto::scene_object::v1::SceneObjectInstanceUpdate::
          kCartesianLimits:
        MergeCartesianLimits(update.cartesian_limits(),
                             *config->mutable_initial_cartesian_limits());
        break;
      case ::intrinsic_proto::scene_object::v1::SceneObjectInstanceUpdate::
          kUpdateJoints:
        if (update.update_joints().joint_positions_size() > 0) {
          LOG(WARNING) << "Unsupported joint positions in update_joints.";
          *config->mutable_updates()->add_updates() = update;
          break;
        }

        if (update.update_joints().parent_t_inboard_size() > 0) {
          LOG(WARNING) << "Unsupported parent_t_inboard in update_joints.";
          *config->mutable_updates()->add_updates() = update;
          break;
        }

        for (const auto& [name, limit_update] :
             update.update_joints().joint_system_limits()) {
          MergeJointLimitUpdate(limit_update,
                                (*config->mutable_initial_joint_settings()
                                      ->mutable_joint_system_limits())[name]);
        }

        for (const auto& [name, limit_update] :
             update.update_joints().joint_application_limits()) {
          MergeJointLimitUpdate(
              limit_update, (*config->mutable_initial_joint_settings()
                                  ->mutable_joint_application_limits())[name]);
        }
        break;
      case ::intrinsic_proto::scene_object::v1::SceneObjectInstanceUpdate::
          kUpdateSimulationProperties: {
        auto* sim_props = config->mutable_simulation_properties();
        if (update.update_simulation_properties().has_is_static()) {
          sim_props->set_is_static(
              update.update_simulation_properties().is_static());
        }
        if (update.update_simulation_properties().has_is_disabled()) {
          sim_props->set_is_disabled(
              update.update_simulation_properties().is_disabled());
        }
        break;
      }
      case ::intrinsic_proto::scene_object::v1::SceneObjectInstanceUpdate::
          kUpdateGeometry:
        if (update.update_geometry().entity_name().empty()) {
          LOG(WARNING) << "GeometryUpdate has empty entity_name, falling back "
                          "to deprecated list.";
          *config->mutable_updates()->add_updates() = update;
          break;
        }
        MergeGeometryUpdate(update.update_geometry(),
                            config->mutable_geometry_overrides());
        break;
      default:
        LOG(WARNING)
            << "Unsupported update type, falling back to deprecated list: "
            << update.update_case();
        *config->mutable_updates()->add_updates() = update;
        break;
    }
  }
  return absl::OkStatus();
}

}  // namespace scene_object
}  // namespace intrinsic
