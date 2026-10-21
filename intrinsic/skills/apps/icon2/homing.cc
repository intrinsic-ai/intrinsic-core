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

#include "intrinsic/skills/apps/icon2/homing.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "grpcpp/support/time.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/actions/homing_info.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/cc_client/client_utils.h"
#include "intrinsic/icon/cc_client/session.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/icon/equipment/equipment_utils.h"
#include "intrinsic/icon/equipment/icon_equipment.pb.h"
#include "intrinsic/icon/utils/arm_utils.h"
#include "intrinsic/math/proto/point.pb.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/apps/icon2/homing.pb.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/cc/skill_utils.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_world_client.h"

namespace intrinsic::skills {

using ::intrinsic_proto::skills::HomingParams;

// Gets the object and ICON part name for the arm being controlled.
//
// The user may provide this directly, but if they do not, we attempt to deduce
// it from the equipment. An error will be returned if the part is not provided
// directly and the information in the equipment does not specify a single arm
// position part.
absl::StatusOr<std::pair<intrinsic_proto::world::ObjectReference, std::string>>
GetIconObjectAndPositionPart(
    const intrinsic_proto::skills::HomingParams& parameters,
    const EquipmentPack& equipment, const world::ObjectWorldClient& world) {
  // We look into the resource data to find the part to control. If multiple
  // parts are specified, we error unless the user has provided us with a part
  // name to disambiguate.
  std::optional<intrinsic_proto::world::ObjectReference> icon_part_object;
  std::optional<std::string> icon_part_name;
  if (parameters.has_icon_part()) {
    icon_part_object = parameters.icon_part();
  }
  INTR_ASSIGN_OR_RETURN(
      auto position_part,
      equipment.Unpack<intrinsic_proto::icon::Icon2PositionPart>(
          Homing::kEquipmentSlot, icon::kIcon2PositionPartKey),
      _ << "Failed to find Icon2PositionPart in equipment data.");

  // If the icon part name was not given, then get it from the map if it is
  // unambiguous, i.e., only one icon part is provided.
  if (!icon_part_object.has_value()) {
    if (position_part.object_names().size() > 1) {
      return absl::InvalidArgumentError(
          "No icon_part was given, and the given ICON instance has "
          "multiple. Please specify 'icon_part' to select one part to "
          "use.");
    }
    if (position_part.object_names().empty()) {
      return absl::InvalidArgumentError(
          "No icon_part was given, and none were provided in the map.");
    }
    icon_part_object.emplace();
    icon_part_object->mutable_by_name()->set_object_name(
        position_part.object_names().begin()->second);
    icon_part_name = position_part.object_names().begin()->first;
  }
  if (!icon_part_object.has_value()) {
    // This shouldn't happen unless a coding mistake leaves icon_part unset.
    return absl::InternalError("Failed to deduce icon part name.");
  }

  // If we don't have the icon_part_name yet (because the user provided the
  // part as an object reference), try do the lookup.
  if (!icon_part_name) {
    // First, we turn the ObjectReference into a name in case it was somehow
    // provided as an id.
    INTR_ASSIGN_OR_RETURN(auto world_object,
                          world.GetObject(*icon_part_object));
    std::string object_name = world_object.Name().value();
    std::vector<absl::string_view> object_names;
    object_names.reserve(position_part.object_names().size());

    // Now do a look up against the position parts.
    for (const auto& [current_part_name, current_object_name] :
         position_part.object_names()) {
      object_names.push_back(current_object_name);
      if (current_object_name == object_name) {
        icon_part_name = current_part_name;
        break;
      }
    }

    // If we didn't find a match, the object the user provided is not a position
    // part and we error.
    if (!icon_part_name) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Could not find a position part corresponding to object with name '",
          object_name, "'. Please check you have the right object and ICON ",
          "instance selected. Objects corresponding to position parts for the ",
          "selected ICON instance are: ", absl::StrJoin(object_names, ", "),
          "."));
    }
  }

  // Both of these will be set at this point.
  if (!icon_part_name || !icon_part_object) {
    return absl::InternalError(
        "Internal error: could not deduce icon_part_name or icon_part_object. "
        "Please report this!");
  }

  return std::make_pair(*icon_part_object, *icon_part_name);
}

Homing::Homing(std::unique_ptr<icon::ChannelFactory> icon_channel_factory)
    : icon_channel_factory_(std::move(icon_channel_factory)) {}

std::unique_ptr<SkillInterface> Homing::CreateSkill() {
  return std::make_unique<Homing>(
      std::make_unique<icon::DefaultChannelFactory>());
}

absl::StatusOr<intrinsic_proto::skills::Footprint> Homing::GetFootprint(
    const GetFootprintRequest& request, GetFootprintContext& context) const {
  INTR_ASSIGN_OR_RETURN(
      const intrinsic_proto::skills::HomingParams params,
      request.params<intrinsic_proto::skills::HomingParams>());

  // If there is a part selected, we will use this one. Otherwise, we will
  // use the robot from the equipment slot. In execute, it is checked that there
  // is only one robot in the world in this case.
  if (params.has_icon_part()) {
    return ::intrinsic::skills::CreateObjectReservationFootprint(
        params.icon_part().by_name(),
        intrinsic_proto::skills::ObjectWorldReservation::WRITE);
  }

  INTR_ASSIGN_OR_RETURN(
      const world::KinematicObject robot,
      context.GetKinematicObjectForEquipment(Homing::kEquipmentSlot));
  return ::intrinsic::skills::CreateObjectReservationFootprint(
      robot.Name().value(),
      intrinsic_proto::skills::ObjectWorldReservation::WRITE);
}

absl::StatusOr<std::unique_ptr<google::protobuf::Message>> Homing::Execute(
    const ExecuteRequest& request, ExecuteContext& context) {
  INTR_ASSIGN_OR_RETURN(const HomingParams params,
                        request.params<HomingParams>());
  world::ObjectWorldClient& world = context.object_world();

  std::string drive_name = params.drive_name();
  if (drive_name.empty()) {
    return absl::InvalidArgumentError(
        "Parameter `drive_name` must not be empty.");
  }

  if (!params.has_timeout()) {
    return absl::InvalidArgumentError("Parameter `timeout` must be set.");
  }
  INTR_ASSIGN_OR_RETURN(const absl::Duration timeout,
                        ToAbslDuration(params.timeout()));
  if (timeout > absl::Seconds(180)) {
    return absl::InvalidArgumentError(
        "Parameter `timeout` must be less than the skill execution timeout of "
        "3 minutes.");
  }
  INTR_ASSIGN_OR_RETURN(auto handle,
                        context.equipment().GetHandle(kEquipmentSlot));
  INTR_ASSIGN_OR_RETURN(const auto connection_config,
                        skills::GetConnectionParamsFromHandle(handle));
  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<ChannelInterface> icon_channel,
      icon_channel_factory_->MakeChannel(
          connection_config, connect::kGrpcClientConnectDefaultTimeout));
  if (icon_channel == nullptr) {
    return absl::InternalError("Failed to create ICON channel.");
  }
  icon::Client icon_client(icon_channel);

  // We look into the resource data to find the parts to control. If multiple
  // parts are specified, we error unless the user has provided us with a part
  // name to disambiguate.
  //
  // Long term we would replace this with talking to icon directly to reduce the
  // amount of skill specific information carried by the assets framework.
  INTR_ASSIGN_OR_RETURN(
      (auto [icon_part_object, icon_part_name]),
      GetIconObjectAndPositionPart(params, context.equipment(), world));

  // Find the right object in the world.
  INTR_ASSIGN_OR_RETURN(world::KinematicObject kinematic_object,
                        world.GetKinematicObject(icon_part_object));

  // This Cleanup will ensure the joint position of the robot is updated in the
  // world no matter how we exit.
  absl::Cleanup update_robot_on_exit = [icon_part_name, &icon_client,
                                        &kinematic_object, &world]() {
    auto part_status_or = icon_client.GetSinglePartStatus(icon_part_name);
    if (!part_status_or.ok()) {
      LOG(WARNING) << "Failed to get part status upon finishing Execute.";
      return;
    }
    eigenmath::VectorXd dof_values =
        icon::GetSensedJointPosition(*part_status_or);
    auto status = world.UpdateJointPositions(kinematic_object, dof_values);
    if (!status.ok()) {
      LOG(WARNING) << "Failed to update world with robot position upon "
                      "finishing Execute.";
    }
  };

  // Create an ICON session with an instance of the homing action.
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<icon::Session> session,
      icon::Session::Start(icon_channel, {std::string(icon_part_name)},
                           context.logging_context().data_logger_context));

  icon::HomingInfo::FixedParams homing_params;
  homing_params.set_drive_name(drive_name);
  if (params.has_homing_method()) {
    homing_params.set_homing_method(params.homing_method());
  }
  if (params.has_offset()) {
    homing_params.set_offset(params.offset());
  }
  if (params.has_search_speed()) {
    homing_params.set_search_speed(params.search_speed());
  }
  if (params.has_creep_speed()) {
    homing_params.set_creep_speed(params.creep_speed());
  }
  if (params.has_acceleration()) {
    homing_params.set_acceleration(params.acceleration());
  }
  if (params.has_ignore_is_settled_condition()) {
    homing_params.set_ignore_is_settled_condition(
        params.ignore_is_settled_condition());
  }

  // Actually set up the action.
  icon::ReactionHandle homing_attained(1);
  icon::ActionDescriptor homing_descriptor =
      icon::ActionDescriptor(icon::HomingInfo::kActionTypeName,
                             icon::ActionInstanceId(1), icon_part_name)
          .WithFixedParams(homing_params)
          .WithReaction(icon::ReactionDescriptor(icon::IsDone())
                            .WithHandle(homing_attained));

  INTR_ASSIGN_OR_RETURN(icon::Action action,
                        session->AddAction(homing_descriptor), _.LogError());
  INTR_RETURN_IF_ERROR(session->StartAction(action)) << "While starting Action";

  // Wait until the action is done.
  const absl::Time deadline = absl::Now() + timeout;

  if (absl::Status status =
          session->RunWatcherLoopUntilReaction(homing_attained, deadline);
      !status.ok()) {
    LOG(ERROR) << "Failed to run watcher loop: " << status;
    return status;
  }
  if (absl::Status status = session->End(); !status.ok()) {
    LOG(ERROR) << "Failed to end session: " << status;
    return status;
  }
  LOG(INFO) << "Homing completed successfully.";
  return nullptr;
}

absl::StatusOr<std::unique_ptr<::google::protobuf::Message>> Homing::Preview(
    const PreviewRequest& request, PreviewContext& context) {
  // NO-OP, assume that the current homing position is valid.
  return nullptr;
}

}  // namespace intrinsic::skills
