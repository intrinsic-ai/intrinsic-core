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

#include "intrinsic/icon/server/auto_config_helpers.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/icon/control/parts/hal/adio_part/hal_adio_part.h"
#include "intrinsic/icon/control/parts/hal/adio_part/hal_adio_part_config.pb.h"
#include "intrinsic/icon/control/parts/hal/arm_part/hal_arm_part.h"
#include "intrinsic/icon/control/parts/hal/arm_part/hal_arm_part_config.pb.h"
#include "intrinsic/icon/control/parts/hal/force_torque_sensor_part/hal_force_torque_sensor_part.h"
#include "intrinsic/icon/control/parts/hal/force_torque_sensor_part/hal_force_torque_sensor_part_config.pb.h"
#include "intrinsic/icon/control/parts/hal/v1/hal_part_config.pb.h"
#include "intrinsic/icon/control/parts/proto/v1/realtime_part_config.pb.h"
#include "intrinsic/icon/hal/proto/hardware_module_config.pb.h"
#include "intrinsic/icon/hal/realtime_clock.h"
#include "intrinsic/icon/server/config/icon_main_config.pb.h"
#include "intrinsic/icon/server/config/realtime_control_config.pb.h"
#include "intrinsic/icon/server/config/services_config.pb.h"
#include "intrinsic/resources/client/resource_registry_client.h"
#include "intrinsic/resources/proto/resource_registry.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/world_object.h"
#include "intrinsic/world/proto/object_world_service.pb.h"

namespace intrinsic::icon {

namespace {

constexpr char kJointPositionState[] = "intrinsic_fbs.JointPositionState";
constexpr char kJointVelocityState[] = "intrinsic_fbs.JointVelocityState";
constexpr char kJointAccelerationState[] =
    "intrinsic_fbs.JointAccelerationState";
constexpr char kJointTorqueState[] = "intrinsic_fbs.JointTorqueState";
constexpr char kJointPositionCommand[] = "intrinsic_fbs.JointPositionCommand";
constexpr char kJointCommandedPosition[] =
    "intrinsic_fbs.JointCommandedPosition";
constexpr char kJointVelocityCommand[] = "intrinsic_fbs.JointVelocityCommand";
constexpr char kJointTorqueCommand[] = "intrinsic_fbs.JointTorqueCommand";
constexpr char kJointSystemLimits[] = "intrinsic_fbs.JointLimits";
constexpr char kControlModeStatus[] = "intrinsic_fbs.ControlModeStatus";
constexpr char kDigitalInputStatus[] = "intrinsic_fbs.DigitalInputStatus";
constexpr char kDigitalOutputCommand[] = "intrinsic_fbs.DigitalOutputCommand";
constexpr char kAnalogInputStatus[] = "intrinsic_fbs.AnalogInputStatus";
constexpr char kAnalogOutputCommand[] = "intrinsic_fbs.AnalogOutputCommand";
constexpr char kForceTorqueCommand[] = "intrinsic_fbs.ForceTorqueCommand";
constexpr char kForceTorqueStatus[] = "intrinsic_fbs.ForceTorqueStatus";
constexpr char kHomingCommand[] = "intrinsic_fbs.HomeCommand";
constexpr char kHomingStatus[] = "intrinsic_fbs.HomingStatus";
constexpr char kPayloadCommand[] = "intrinsic_fbs.PayloadCommand";
constexpr char kPayloadState[] = "intrinsic_fbs.PayloadState";

// See intrinsic::ati::AtiForceTorqueBusDevice::kStatusInterfaceName.
constexpr absl::string_view kForceTorqueStatusInterfaceSuffix =
    "_force_torque_status";

struct PartConfigAndResourceData {
  intrinsic_proto::v1::RealtimePartConfig config;
  // Specifies the world object id of the resource that the part is attached to
  // if any.
  std::string object_id = "";
  // Specifies the names of the hardware modules that are used by this part.
  absl::flat_hash_set<std::string> hardware_module_names{};
};

std::optional<std::string> FindHardwareModuleThatDrivesClock(
    const absl::flat_hash_map<std::string,
                              absl::flat_hash_map<std::string, std::string>>&
        modules_and_interfaces) {
  // Return the first HWM that exposes the realtime clock lockstep interface.
  for (const auto& [module_name, interfaces] : modules_and_interfaces) {
    if (auto it = std::find_if(
            interfaces.begin(), interfaces.end(),
            [](const std::pair<std::string, std::string>& interface_and_type) {
              return interface_and_type.first ==
                     kRealtimeClockLockstepInterfaceName;
            });
        it != interfaces.end()) {
      return module_name;
    }
  }
  return std::nullopt;
}

absl::StatusOr<double> FindControlFrequencyFromResourceRegistryOrWorld(
    absl::string_view robot_resource_name,
    const resources::ResourceRegistryClient& resource_registry_client,
    world::ObjectWorldClient& object_world_client) {
  // Try the config from the resource registry first. If present, this overrides
  // any value in the World.
  //
  // TODO(b/389912730): Remove World handling code once the migration to control
  // frequency in config is complete
  INTR_ASSIGN_OR_RETURN(
      ::intrinsic_proto::resources::ResourceInstance resource,
      resource_registry_client.GetResource(robot_resource_name));

  if (intrinsic_proto::icon::HardwareModuleConfig hwm_config;
      resource.configuration().UnpackTo(&hwm_config)) {
    switch (hwm_config.control_rate_case()) {
      case intrinsic_proto::icon::HardwareModuleConfig::kControlFrequencyHz:
        return hwm_config.control_frequency_hz();
      case intrinsic_proto::icon::HardwareModuleConfig::kControlPeriodNs:
        return absl::FDivDuration(
            absl::Seconds(1),
            absl::Nanoseconds(hwm_config.control_period_ns()));
      default:
        LOG(WARNING) << "Robot arm object '" << robot_resource_name
                     << "' does not have a control frequency in its "
                        "configuration. This will become an error soon.";
        break;
    }
  }

  INTR_ASSIGN_OR_RETURN(auto object, object_world_client.GetObject(
                                         WorldObjectName(robot_resource_name)));
  if (!object.Proto().has_kinematic_object_component()) {
    return absl::NotFoundError(
        absl::StrCat("The queried object `", robot_resource_name,
                     "` doesn't contain a kinematic object component."));
  }
  if (!object.Proto().kinematic_object_component().has_control_frequency_hz()) {
    return absl::NotFoundError(
        "The queried object's configuration does not contain a control "
        "frequency, and the object's kinematic component doesn't either. "
        "Please update the HardwareModuleConfig (not the World model) to "
        "include either control_frequency_hz or control_period_ns");
  }
  return object.Proto().kinematic_object_component().control_frequency_hz();
}

absl::StatusOr<bool> HasIKSolver(
    absl::string_view robot_resource_name,
    world::ObjectWorldClient& object_world_client) {
  INTR_ASSIGN_OR_RETURN(auto object, object_world_client.GetObject(
                                         WorldObjectName(robot_resource_name)));
  if (!object.Proto().has_kinematic_object_component()) {
    return absl::NotFoundError(
        absl::StrCat("The queried object `", robot_resource_name,
                     "` doesn't contain a kinematic object component."));
  }
  if (object.Proto().kinematic_object_component().ik_solvers_size() > 0) {
    return true;
  }
  return false;
}

absl::string_view FindFtSensorLinkName(const world::WorldObject& object) {
  for (const auto& entity : object.Proto().entities()) {
    if (entity.second.has_sensor_component() &&
        entity.second.sensor_component().has_force_torque()) {
      return entity.second.name();
    }
  }
  if (auto it = std::find_if(
          object.Proto().entities().begin(), object.Proto().entities().end(),
          [](const std::pair<std::string, intrinsic_proto::world::Entity>&
                 entity) {
            return entity.second.has_sensor_component() &&
                   entity.second.sensor_component().has_force_torque();
          });
      it != object.Proto().entities().end()) {
    return it->second.name();
  }
  return "";
}

absl::StatusOr<PartConfigAndResourceData> MakeArmPartConfig(
    const absl::flat_hash_map<std::string,
                              absl::flat_hash_map<std::string, std::string>>&
        modules_and_interfaces,
    const absl::flat_hash_map<std::string, std::string>&
        module_name_to_resource_name) {
  PartConfigAndResourceData output;
  intrinsic_proto::v1::RealtimePartConfig part_config;

  for (const auto& module : modules_and_interfaces) {
    intrinsic_proto::icon::HalArmPartConfig arm_config;
    // We try to fill in the interfaces. If there are multiple interfaces of the
    // same type we overwrite.
    std::string module_name = module.first;
    if (!module_name_to_resource_name.contains(module_name)) {
      return absl::NotFoundError(
          absl::StrCat("No resource name found for module: ",
                       arm_config.joint_position_state().module_name()));
    }
    for (const auto& interface : module.second) {
      std::string interface_name = interface.first;
      if (interface.second == kJointPositionState) {
        arm_config.mutable_joint_position_state()->set_module_name(module_name);
        arm_config.mutable_joint_position_state()->set_interface_name(
            interface_name);
      }
      if (interface.second == kJointVelocityState) {
        arm_config.mutable_joint_velocity_state()->set_module_name(module_name);
        arm_config.mutable_joint_velocity_state()->set_interface_name(
            interface_name);
      }
      if (interface.second == kJointAccelerationState) {
        arm_config.mutable_joint_acceleration_state()->set_module_name(
            module_name);
        arm_config.mutable_joint_acceleration_state()->set_interface_name(
            interface_name);
      }
      if (interface.second == kJointTorqueState) {
        arm_config.mutable_joint_torque_state()->set_module_name(module_name);
        arm_config.mutable_joint_torque_state()->set_interface_name(
            interface_name);
      }
      if (interface.second == kJointPositionCommand) {
        arm_config.mutable_joint_position_command()->set_module_name(
            module_name);
        arm_config.mutable_joint_position_command()->set_interface_name(
            interface_name);
      }
      if (interface.second == kJointCommandedPosition) {
        arm_config.mutable_joint_commanded_position()->set_module_name(
            module_name);
        arm_config.mutable_joint_commanded_position()->set_interface_name(
            interface_name);
      }
      if (interface.second == kJointVelocityCommand) {
        arm_config.mutable_joint_velocity_command()->set_module_name(
            module_name);
        arm_config.mutable_joint_velocity_command()->set_interface_name(
            interface_name);
      }
      if (interface.second == kJointTorqueCommand) {
        arm_config.mutable_joint_torque_command()->set_module_name(module_name);
        arm_config.mutable_joint_torque_command()->set_interface_name(
            interface_name);
      }
      if (interface.second == kControlModeStatus) {
        arm_config.mutable_control_mode_state()->set_module_name(module_name);
        arm_config.mutable_control_mode_state()->set_interface_name(
            interface_name);
      }
      if (interface.second == kJointSystemLimits) {
        arm_config.mutable_joint_system_limits()->set_module_name(module_name);
        arm_config.mutable_joint_system_limits()->set_interface_name(
            interface_name);
      }
      if (interface.second == kPayloadCommand) {
        arm_config.mutable_payload_command()->set_module_name(module_name);
        arm_config.mutable_payload_command()->set_interface_name(
            interface_name);
      }
      if (interface.second == kPayloadState) {
        arm_config.mutable_payload_state()->set_module_name(module_name);
        arm_config.mutable_payload_state()->set_interface_name(interface_name);
      }
      if (interface.second == kHomingCommand) {
        // Cut the suffix off to get the drive name.
        auto drive_name = interface_name.substr(
            0, interface_name.rfind('_', interface_name.rfind('_') - 1));
        auto homing_command =
            (*arm_config.mutable_homing())[drive_name].mutable_command();
        homing_command->set_module_name(module_name);
        homing_command->set_interface_name(interface_name);
      }
      if (interface.second == kHomingStatus) {
        // Cut the suffix off to get the drive name.
        auto drive_name = interface_name.substr(
            0, interface_name.rfind('_', interface_name.rfind('_') - 1));
        auto homing_state =
            (*arm_config.mutable_homing())[drive_name].mutable_state();
        homing_state->set_module_name(module_name);
        homing_state->set_interface_name(interface_name);
      }
    }

    if (!arm_config.has_joint_position_command() ||
        arm_config.joint_position_command().interface_name().empty() ||
        !arm_config.has_joint_position_state() ||
        arm_config.joint_position_state().interface_name().empty()) {
      continue;
    } else if (!arm_config.has_joint_velocity_state() ||
               arm_config.joint_velocity_state().interface_name().empty()) {
      arm_config.set_calculate_velocity_state_from_position(true);
    }

    if (!part_config.mutable_config()->PackFrom(arm_config)) {
      return absl::InternalError("Failed to pack arm config.");
    }
    part_config.set_part_type_name(HalArmPart::kPartTypeName);
    part_config.set_safety_action_type_name("intrinsic.stop");
    std::string arm_resource_name =
        module_name_to_resource_name.at(module_name);
    part_config.set_hardware_resource_name(arm_resource_name);
    output.config = part_config;
    output.object_id = arm_resource_name;
    output.hardware_module_names.insert(module_name);
    // Currently we only support one arm part. If we have reached this point we
    // don't try with remaining modules.
    return output;
  }
  return absl::NotFoundError("No arm part was found");
}

absl::StatusOr<PartConfigAndResourceData> MakeADIOPartConfig(
    const absl::flat_hash_map<std::string,
                              absl::flat_hash_map<std::string, std::string>>&
        modules_and_interfaces) {
  PartConfigAndResourceData output;
  intrinsic_proto::icon::HalADIOPartConfig adio_config;

  // We add all interfaces to a single part.
  for (const auto& module : modules_and_interfaces) {
    std::string module_name = module.first;
    for (const auto& interface : module.second) {
      std::string interface_name = interface.first;
      intrinsic_proto::icon::HalADIOPartConfig::HalBlockConfig block;
      if (interface.second == kDigitalInputStatus) {
        block.mutable_interface()->set_module_name(module_name);
        block.mutable_interface()->set_interface_name(interface_name);
        adio_config.mutable_digital_inputs()->Add(std::move(block));
        output.hardware_module_names.insert(module_name);
      } else if (interface.second == kDigitalOutputCommand) {
        block.mutable_interface()->set_module_name(module_name);
        block.mutable_interface()->set_interface_name(interface_name);
        adio_config.mutable_digital_outputs()->Add(std::move(block));
        output.hardware_module_names.insert(module_name);
      } else if (interface.second == kAnalogInputStatus) {
        block.mutable_interface()->set_module_name(module_name);
        block.mutable_interface()->set_interface_name(interface_name);
        adio_config.mutable_analog_inputs()->Add(std::move(block));
        output.hardware_module_names.insert(module_name);
      } else if (interface.second == kAnalogOutputCommand) {
        block.mutable_interface()->set_module_name(module_name);
        block.mutable_interface()->set_interface_name(interface_name);
        adio_config.mutable_analog_outputs()->Add(std::move(block));
        output.hardware_module_names.insert(module_name);
      }
    }
  }
  if (adio_config.digital_inputs().empty() &&
      adio_config.digital_outputs().empty() &&
      adio_config.analog_inputs().empty() &&
      adio_config.analog_outputs().empty()) {
    return absl::NotFoundError("No ADIO interfaces were found");
  }

  if (!output.config.mutable_config()->PackFrom(adio_config)) {
    return absl::InternalError("Failed to pack adio config.");
  }
  output.config.set_part_type_name(HalADIOPart::kPartTypeName);
  output.config.set_safety_action_type_name("intrinsic.empty");
  return output;
}

absl::StatusOr<PartConfigAndResourceData> MakeForceTorqueSensorPartConfig(
    const absl::flat_hash_map<std::string,
                              absl::flat_hash_map<std::string, std::string>>&
        modules_and_interfaces,
    const absl::flat_hash_map<std::string, std::string>&
        module_name_to_resource_name,
    world::ObjectWorldClient& object_world_client,
    absl::string_view robot_resource_name = "") {
  PartConfigAndResourceData output;
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig sensor_config;
  std::string ft_module_name = "";
  std::string ft_resource_name = "";

  // We search through the modules to see if we can find the required force
  // torque sensor interfaces. Some robots have integrated FT sensors, such as
  // URs, but we give external FT sensors precedence over integrated ones.
  for (const auto& module : modules_and_interfaces) {
    std::string module_name = module.first;
    intrinsic_proto::icon::HalForceTorqueSensorPartConfig temp_sensor_config;
    for (const auto& interface : module.second) {
      std::string interface_name = interface.first;
      if (interface.second == kForceTorqueCommand) {
        temp_sensor_config.mutable_force_torque_command()->set_module_name(
            module_name);
        temp_sensor_config.mutable_force_torque_command()->set_interface_name(
            interface_name);
      } else if (interface.second == kForceTorqueStatus) {
        temp_sensor_config.mutable_force_torque_state()->set_module_name(
            module_name);
        temp_sensor_config.mutable_force_torque_state()->set_interface_name(
            interface_name);
      }
    }

    // Make sure we found both command and state interfaces.
    if (!temp_sensor_config.has_force_torque_command() ||
        temp_sensor_config.force_torque_command().interface_name().empty() ||
        !temp_sensor_config.has_force_torque_state() ||
        temp_sensor_config.force_torque_state().interface_name().empty()) {
      continue;
    }
    // We have a valid FT sensor, so add this to the configuration.
    ft_module_name = module_name;
    if (!module_name_to_resource_name.contains(ft_module_name)) {
      return absl::NotFoundError(
          absl::StrCat("No resource name found for module: ", ft_module_name));
    }
    ft_resource_name = module_name_to_resource_name.at(ft_module_name);
    sensor_config = temp_sensor_config;
    // Stop searching if we found a non-integrated FT sensor.
    if (ft_resource_name != robot_resource_name) break;
  }

  if (ft_module_name.empty() || ft_resource_name.empty()) {
    return absl::NotFoundError("No FT sensor was found");
  }

  // We try to configure the FT sensor considering its attachment to a robot
  // arm (if present).
  absl::StatusOr<world::WorldObject> object_with_ft_status =
      object_world_client.GetObject(WorldObjectName(ft_resource_name));
  std::string attempted_resource_names = ft_resource_name;

  if (!object_with_ft_status.ok() &&
      absl::IsNotFound(object_with_ft_status.status())) {
    // Fallback: try to derive resource name from interface name.
    // EtherCAT Fieldbus Devices will have their asset name prefixed to the
    // hardware interface names.
    const auto& ft_state_interface_name =
        sensor_config.force_torque_state().interface_name();
    const absl::string_view extracted_resource_name = absl::StripSuffix(
        ft_state_interface_name, kForceTorqueStatusInterfaceSuffix);
    attempted_resource_names =
        absl::StrCat(attempted_resource_names, " or ", extracted_resource_name);
    object_with_ft_status =
        object_world_client.GetObject(WorldObjectName(extracted_resource_name));
    if (object_with_ft_status.ok()) {
      ft_resource_name = std::string(extracted_resource_name);
    }
  }

  if (!object_with_ft_status.ok()) {
    return absl::NotFoundError(absl::StrCat(
        "Failed to find world object with resource name(s): ",
        attempted_resource_names, ": ", object_with_ft_status.status()));
  }

  world::WorldObject object_with_ft = object_with_ft_status.value();
  if (object_with_ft.Proto().has_kinematic_object_component() &&
      object_with_ft.Proto().name() == robot_resource_name) {
    // This case assumes the FT is integrated in the robot.
    for (const auto& interface : modules_and_interfaces.at(ft_module_name)) {
      if (interface.second == kJointPositionState) {
        sensor_config.mutable_joint_position_state()->set_module_name(
            ft_module_name);
        sensor_config.mutable_joint_position_state()->set_interface_name(
            interface.first);
      }
    }
    sensor_config.set_world_robot_collection_name(robot_resource_name);
    sensor_config.set_ft_sensor_link_name(FindFtSensorLinkName(object_with_ft));
    sensor_config.set_target_link_name(sensor_config.ft_sensor_link_name());
    sensor_config.set_support_mass(0);
    sensor_config.add_ft_t_cog(0);
    sensor_config.add_ft_t_cog(0);
    sensor_config.add_ft_t_cog(0);
  } else if (!robot_resource_name.empty()) {
    world::WorldObject object = object_with_ft;
    while (object.Proto().has_parent()) {
      INTR_ASSIGN_OR_RETURN(
          object, object_world_client.GetObject(
                      ObjectWorldResourceId(object.Proto().parent().id())));
      std::string object_module_name = "";
      for (const auto& [module_name, resource_name] :
           module_name_to_resource_name) {
        if (object.Proto().name() == resource_name) {
          object_module_name = module_name;
          break;
        }
      }
      if (object.Proto().name() == robot_resource_name &&
          object.Proto().has_kinematic_object_component() &&
          modules_and_interfaces.contains(object_module_name)) {
        for (const auto& robot_interface :
             modules_and_interfaces.at(object_module_name)) {
          if (robot_interface.second == kJointPositionState) {
            sensor_config.mutable_joint_position_state()->set_module_name(
                object_module_name);
            sensor_config.mutable_joint_position_state()->set_interface_name(
                robot_interface.first);
          }
        }
        sensor_config.set_world_robot_collection_name(robot_resource_name);

        // Now we need to find the target link. We prioritize the tip link for
        // IK solvers if present.
        if (object.Proto().kinematic_object_component().ik_solvers_size() > 0) {
          std::string target_link_id = object.Proto()
                                           .kinematic_object_component()
                                           .ik_solvers(0)
                                           .tip_entity_id();
          sensor_config.set_target_link_name(
              object.Proto().entities().at(target_link_id).name());
        } else {
          absl::flat_hash_set<std::string> edge_link_ids{};
          for (const auto& entity : object.Proto().entities()) {
            edge_link_ids.insert(entity.first);
          }
          // If any link has a parent, remove the parent from the set.
          for (const auto& entity : object.Proto().entities()) {
            if (!entity.second.parent_id().empty()) {
              edge_link_ids.erase(entity.second.parent_id());
            }
          }
          if (edge_link_ids.begin() == edge_link_ids.end()) {
            return absl::NotFoundError(
                "No tip node from an IK solver and no edge links were "
                "found.");
          }
          if (edge_link_ids.size() > 1) {
            LOG(WARNING)
                << "More than one leaf node was found so using the first.";
          }
          sensor_config.set_target_link_name(
              object.Proto().entities().at(*edge_link_ids.begin()).name());
        }
      }
      sensor_config.set_ft_sensor_link_name(
          FindFtSensorLinkName(object_with_ft));
    }
    // Set default values for the FT payload.
    sensor_config.set_support_mass(0);
    sensor_config.add_ft_t_cog(0);
    sensor_config.add_ft_t_cog(0);
    sensor_config.add_ft_t_cog(0);
  }
  if (!output.config.mutable_config()->PackFrom(sensor_config)) {
    return absl::InternalError("Failed to pack sensor config.");
  }
  output.config.set_part_type_name(HalForceTorqueSensorPart::kPartTypeName);
  output.config.set_safety_action_type_name("intrinsic.empty");
  output.hardware_module_names.insert(ft_module_name);
  if (sensor_config.has_joint_position_state()) {
    output.hardware_module_names.insert(
        sensor_config.joint_position_state().module_name());
  }
  output.object_id = object_with_ft.Proto().name();
  return output;
}

}  // namespace

absl::StatusOr<intrinsic_proto::icon::IconMainConfig> AutoGenerateIconConfig(
    const absl::flat_hash_map<std::string,
                              absl::flat_hash_map<std::string, std::string>>&
        modules_and_interfaces,
    const absl::flat_hash_map<std::string, std::string>&
        module_name_to_resource_name,
    const resources::ResourceRegistryClient& resource_registry_client,
    world::ObjectWorldClient& object_world_client) {
  if (modules_and_interfaces.empty()) {
    return absl::InvalidArgumentError("No hardware interfaces were provided.");
  }
  if (module_name_to_resource_name.size() != modules_and_interfaces.size()) {
    return absl::InvalidArgumentError(
        "`modules_and_interfaces` and `module_name_to_resource_name` have "
        "different sizes. Please ensure all hardware modules are initialized "
        "and expose hardware interfaces.");
  }
  for (const auto& module : modules_and_interfaces) {
    if (!module_name_to_resource_name.contains(module.first)) {
      return absl::InvalidArgumentError(
          "`modules_and_interfaces` and `module_name_to_resource_name` do not "
          "contain the same modules. Please ensure all hardware modules are "
          "initialized and expose hardware interfaces.");
    }
  }

  intrinsic_proto::icon::IconMainConfig config;

  // Find a hardware module that ticks the clock
  if (auto hardware_module_that_drives_clock =
          FindHardwareModuleThatDrivesClock(modules_and_interfaces);
      hardware_module_that_drives_clock.has_value()) {
    config.set_hardware_module_that_drives_clock(
        hardware_module_that_drives_clock.value());
  }

  absl::flat_hash_set<std::string> used_hardware_modules{};

  auto maybe_arm_part =
      MakeArmPartConfig(modules_and_interfaces, module_name_to_resource_name);
  std::string robot_arm_name = "";
  if (maybe_arm_part.ok()) {
    config.mutable_realtime_control_config()->mutable_parts_by_name()->insert(
        {"arm", maybe_arm_part.value().config});
    INTR_ASSIGN_OR_RETURN(double control_frequency,
                          FindControlFrequencyFromResourceRegistryOrWorld(
                              maybe_arm_part.value().object_id,
                              resource_registry_client, object_world_client));
    robot_arm_name = maybe_arm_part.value().object_id;
    config.set_control_frequency_hz(control_frequency);

    config.mutable_services()->mutable_world_service_from_grpc()->set_world_id(
        object_world_client.GetWorldID());
    // Only add kinematics and assembly services if the arm object has an IK
    // solver.
    INTR_ASSIGN_OR_RETURN(
        bool has_ik_solver,
        HasIKSolver(maybe_arm_part.value().object_id, object_world_client));
    if (has_ik_solver) {
      config.mutable_services()->set_kinematics_from_world_service(true);
      config.mutable_services()->set_assembly_from_world_service(true);
    }
    used_hardware_modules.insert(
        maybe_arm_part.value().hardware_module_names.begin(),
        maybe_arm_part.value().hardware_module_names.end());
  }

  auto maybe_adio_part = MakeADIOPartConfig(modules_and_interfaces);
  if (maybe_adio_part.ok()) {
    config.mutable_realtime_control_config()->mutable_parts_by_name()->insert(
        {"adio", maybe_adio_part.value().config});
    used_hardware_modules.insert(
        maybe_adio_part.value().hardware_module_names.begin(),
        maybe_adio_part.value().hardware_module_names.end());
  }

  auto maybe_force_torque_sensor_part = MakeForceTorqueSensorPartConfig(
      modules_and_interfaces, module_name_to_resource_name, object_world_client,
      robot_arm_name);
  if (maybe_force_torque_sensor_part.ok()) {
    config.mutable_realtime_control_config()->mutable_parts_by_name()->insert(
        {"ft_sensor", maybe_force_torque_sensor_part.value().config});
    used_hardware_modules.insert(
        maybe_force_torque_sensor_part.value().hardware_module_names.begin(),
        maybe_force_torque_sensor_part.value().hardware_module_names.end());
  }

  // We connect to the hardware modules that are used by the parts.
  for (const auto& module : used_hardware_modules) {
    config.add_hardware_module_names(module);
  }

  config.set_hardware_module_read_write_timeout_seconds(1);
  return config;
}

//

}  // namespace intrinsic::icon
