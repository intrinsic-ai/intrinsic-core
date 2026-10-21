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

#include "intrinsic/icon/skills/set_payload.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "grpcpp/support/time.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/icon/equipment/equipment_utils.h"
#include "intrinsic/icon/skills/set_payload.pb.h"
#include "intrinsic/math/proto/point.pb.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/cc/skill_utils.h"
#include "intrinsic/skills/internal/kinematic_object_for_position_part.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/robot_payload/robot_payload.h"

namespace intrinsic::skills {

using ::intrinsic_proto::skills::SetPayloadParams;

SetPayload::SetPayload(
    std::unique_ptr<icon::ChannelFactory> icon_channel_factory)
    : icon_channel_factory_(std::move(icon_channel_factory)) {}

std::unique_ptr<SkillInterface> SetPayload::CreateSkill() {
  return std::make_unique<SetPayload>(
      std::make_unique<icon::DefaultChannelFactory>());
}

absl::StatusOr<intrinsic_proto::skills::Footprint> SetPayload::GetFootprint(
    const GetFootprintRequest& request, GetFootprintContext& context) const {
  INTR_ASSIGN_OR_RETURN(world::KinematicObject robot,
                        context.GetKinematicObjectForEquipment(kEquipmentSlot));

  intrinsic_proto::skills::Footprint proto;
  ::intrinsic::skills::AddObjectReservation(
      robot.Name().value(),
      intrinsic_proto::skills::ObjectWorldReservation::WRITE, proto);
  ::intrinsic::skills::AddResourceReservation(
      kEquipmentSlot, intrinsic_proto::skills::ResourceReservation::WRITE,
      proto);
  return proto;
}

absl::StatusOr<std::unique_ptr<google::protobuf::Message>> SetPayload::Execute(
    const ExecuteRequest& request, ExecuteContext& context) {
  INTR_ASSIGN_OR_RETURN(const SetPayloadParams params,
                        request.params<SetPayloadParams>());

  INTR_ASSIGN_OR_RETURN(RobotPayload mounted_payload,
                        FromProto(params.mounted_payload()));

  const skills::EquipmentPack& equipment_pack = context.equipment();

  INTR_ASSIGN_OR_RETURN(
      icon::IconEquipment icon_equipment,
      icon::ConnectToIconEquipment(equipment_pack, kEquipmentSlot,
                                   *icon_channel_factory_),
      _.LogError());

  icon::Client icon_client(icon_equipment.channel);
  std::string position_part_name;
  if (icon_equipment.position_part_name.has_value()) {
    position_part_name = icon_equipment.position_part_name.value();
  } else {
    return absl::InvalidArgumentError(
        "No position part name found in the equipment.");
  }

  absl::Status status = icon_client.SetPayload(
      mounted_payload, position_part_name, "full_payload");
  if (!status.ok()) {
    if (status.code() == absl::StatusCode::kNotFound) {
      return absl::NotFoundError(absl::StrCat(
          "Cannot set the payload. Does the hardware module provide a "
          "payload_command interface?: ",
          status.message()));
    }
    return status;
  }

  world::ObjectWorldClient& world = context.object_world();
  INTR_ASSIGN_OR_RETURN(
      world::KinematicObject robot_object,
      KinematicObjectForPositionPart(kEquipmentSlot, equipment_pack, world));

  INTR_RETURN_IF_ERROR(
      world.UpdateMountedPayload(robot_object, mounted_payload));

  return nullptr;
}

absl::StatusOr<std::unique_ptr<::google::protobuf::Message>>
SetPayload::Preview(const PreviewRequest& request, PreviewContext& context) {
  INTR_ASSIGN_OR_RETURN(const SetPayloadParams params,
                        request.params<SetPayloadParams>());
  world::ObjectWorldClient& world = context.object_world();
  INTR_ASSIGN_OR_RETURN(world::KinematicObject robot_object,
                        context.GetKinematicObjectForEquipment(kEquipmentSlot));
  INTR_ASSIGN_OR_RETURN(RobotPayload mounted_payload,
                        FromProto(params.mounted_payload()));
  INTR_RETURN_IF_ERROR(
      world.UpdateMountedPayload(robot_object, mounted_payload));
  return nullptr;
}

}  // namespace intrinsic::skills
