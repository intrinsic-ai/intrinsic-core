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

#include "intrinsic/icon/equipment/testing/equipment_test_utils.h"

#include <optional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/icon/equipment/equipment_utils.h"
#include "intrinsic/icon/equipment/force_control_settings.pb.h"
#include "intrinsic/icon/equipment/icon_equipment.pb.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/skills/proto/skills.pb.h"

namespace intrinsic {
namespace icon {

Icon2EquipmentHandleBuilder::Icon2EquipmentHandleBuilder(
    absl::string_view robot_id, absl::string_view target)
    : robot_id_(robot_id), target_(target) {}

Icon2EquipmentHandleBuilder&
Icon2EquipmentHandleBuilder::WithPositionControlledPart(
    absl::string_view part_name, absl::string_view hardware_object_name) {
  position_parts_.push_back(
      {.part_name = std::string(part_name),
       .hardware_object_name = std::string(hardware_object_name)});
  return *this;
}

Icon2EquipmentHandleBuilder& Icon2EquipmentHandleBuilder::WithGripperPart(
    absl::string_view part_name) {
  gripper_part_ = part_name;
  return *this;
}

Icon2EquipmentHandleBuilder& Icon2EquipmentHandleBuilder::WithAdioPart(
    absl::string_view part_name) {
  adio_parts_.push_back(std::string(part_name));
  return *this;
}

Icon2EquipmentHandleBuilder&
Icon2EquipmentHandleBuilder::WithForceTorqueSensorPart(
    absl::string_view part_name,
    const std::optional<intrinsic_proto::icon::ForceControlSettings>&
        force_control_settings) {
  force_torque_sensor_part_ =
      intrinsic_proto::icon::Icon2ForceTorqueSensorPart();
  force_torque_sensor_part_->set_part_name(part_name);
  if (force_control_settings.has_value()) {
    // Write the new and the deprecated force control settings field.
    *force_torque_sensor_part_->mutable_force_control_settings() =
        *force_control_settings;
    force_torque_sensor_part_->mutable_settings()->insert(
        {std::string(part_name), *force_control_settings});
  }

  return *this;
}

Icon2EquipmentHandleBuilder& Icon2EquipmentHandleBuilder::WithRangefinderPart(
    absl::string_view part_name) {
  rangefinder_part_ = part_name;
  return *this;
}

intrinsic_proto::resources::ResourceHandle Icon2EquipmentHandleBuilder::Build()
    const {
  intrinsic_proto::resources::ResourceHandle handle;
  handle.set_name(robot_id_);
  handle.mutable_connection_info()->mutable_grpc()->set_address(target_);
  handle.mutable_connection_info()->mutable_grpc()->set_server_instance(
      robot_id_);

  handle.mutable_resource_data()->insert(
      {kIcon2ConnectionKey,
       intrinsic_proto::resources::ResourceHandle::ResourceData{}});

  if (!position_parts_.empty()) {
    intrinsic_proto::resources::ResourceHandle::ResourceData data;
    intrinsic_proto::icon::Icon2PositionPart part;

    for (const auto& position_part : position_parts_) {
      std::string object_name = position_part.hardware_object_name.empty()
                                    ? robot_id_
                                    : position_part.hardware_object_name;
      if (part.part_name().empty()) {
        part.set_part_name(position_part.part_name);
        part.set_world_robot_collection_name(object_name);
      }
      part.mutable_object_names()->insert(
          {position_part.part_name, object_name});
    }

    CHECK(data.mutable_contents()->PackFrom(part));
    handle.mutable_resource_data()->insert({kIcon2PositionPartKey, data});
  }
  if (!gripper_part_.empty()) {
    intrinsic_proto::resources::ResourceHandle::ResourceData data;
    intrinsic_proto::icon::Icon2GripperPart part;
    part.set_part_name(gripper_part_);
    part.add_part_names(gripper_part_);
    CHECK(data.mutable_contents()->PackFrom(part));

    handle.mutable_resource_data()->insert({kIcon2GripperPartKey, data});
  }
  if (!adio_parts_.empty()) {
    intrinsic_proto::resources::ResourceHandle::ResourceData data;
    intrinsic_proto::icon::Icon2AdioPart part;
    part.mutable_icon_target()->set_part_name(adio_parts_[0]);
    for (const auto& adio_part : adio_parts_) {
      part.add_icon_parts(adio_part);
    }
    CHECK(data.mutable_contents()->PackFrom(part));

    handle.mutable_resource_data()->insert({kIcon2AdioPartKey, data});
  }
  if (force_torque_sensor_part_.has_value()) {
    intrinsic_proto::resources::ResourceHandle::ResourceData data;
    CHECK(data.mutable_contents()->PackFrom(*force_torque_sensor_part_));

    handle.mutable_resource_data()->insert(
        {kIcon2ForceTorqueSensorPartKey, data});
  }
  if (!rangefinder_part_.empty()) {
    intrinsic_proto::resources::ResourceHandle::ResourceData data;
    intrinsic_proto::icon::Icon2RangefinderPart part;
    part.set_part_name(rangefinder_part_);
    part.add_part_names(rangefinder_part_);
    CHECK(data.mutable_contents()->PackFrom(part));

    handle.mutable_resource_data()->insert({kIcon2RangefinderPartKey, data});
  }

  return handle;
}

absl::flat_hash_map<std::string, intrinsic_proto::resources::ResourceHandle>
Icon2EquipmentHandleBuilder::BuildMapForEquipmentSlot(
    absl::string_view equipment_slot) const {
  return {{std::string(equipment_slot), Build()}};
}

Icon2SkillInstanceBuilder::Icon2SkillInstanceBuilder(
    absl::string_view skill_id, absl::string_view target,
    absl::string_view equipment_slot, absl::string_view robot_id)
    : skill_id_(skill_id),
      equipment_slot_(equipment_slot),
      handle_builder_(robot_id, target) {}

Icon2SkillInstanceBuilder&
Icon2SkillInstanceBuilder::WithPositionControlledPart(
    absl::string_view part_name, absl::string_view hardware_object_name) {
  handle_builder_.WithPositionControlledPart(part_name, hardware_object_name);
  return *this;
}

Icon2SkillInstanceBuilder& Icon2SkillInstanceBuilder::WithGripperPart(
    absl::string_view part_name) {
  handle_builder_.WithGripperPart(part_name);
  return *this;
}

Icon2SkillInstanceBuilder& Icon2SkillInstanceBuilder::WithAdioPart(
    absl::string_view part_name) {
  handle_builder_.WithAdioPart(part_name);
  return *this;
}

Icon2SkillInstanceBuilder& Icon2SkillInstanceBuilder::WithForceTorqueSensorPart(
    absl::string_view part_name,
    const std::optional<intrinsic_proto::icon::ForceControlSettings>&
        force_control_settings) {
  handle_builder_.WithForceTorqueSensorPart(part_name, force_control_settings);
  return *this;
}

Icon2SkillInstanceBuilder& Icon2SkillInstanceBuilder::WithRangefinderPart(
    absl::string_view part_name) {
  handle_builder_.WithRangefinderPart(part_name);
  return *this;
}

intrinsic_proto::skills::SkillInstance Icon2SkillInstanceBuilder::Build()
    const {
  intrinsic_proto::skills::SkillInstance skill_instance;
  skill_instance.set_id_version(skill_id_ + ".0.0.1");
  skill_instance.set_instance_name("test_instance." + skill_id_);
  skill_instance.mutable_resource_handles()->insert(
      {equipment_slot_, handle_builder_.Build()});
  return skill_instance;
}

}  // namespace icon
}  // namespace intrinsic
