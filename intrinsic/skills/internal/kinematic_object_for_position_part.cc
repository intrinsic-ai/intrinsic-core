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

#include "intrinsic/skills/internal/kinematic_object_for_position_part.h"

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/equipment/equipment_utils.h"
#include "intrinsic/icon/equipment/icon_equipment.pb.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"

namespace intrinsic::skills {

absl::StatusOr<world::KinematicObject> KinematicObjectForPositionPart(
    absl::string_view equipment_name, const EquipmentPack& equipment_pack,
    world::ObjectWorldClient& object_world) {
  INTR_ASSIGN_OR_RETURN(const intrinsic_proto::resources::ResourceHandle handle,
                        equipment_pack.GetHandle(equipment_name));
  auto icon_pos_part_it =
      handle.resource_data().find(icon::kIcon2PositionPartKey);
  if (icon_pos_part_it == handle.resource_data().end()) {
    LOG(INFO) << "No kinematic model found for Icon2PositionPart. Returning "
                 "kinematic model for "
              << handle.name() << ".";
    return object_world.GetKinematicObject(handle);
  }

  intrinsic_proto::icon::Icon2PositionPart icon_position_part;
  if (!icon_pos_part_it->second.contents().UnpackTo(&icon_position_part)) {
    return absl::NotFoundError(
        absl::StrCat("Resource handle ", handle.name(),
                     " does not have any Icon2PositionPart information."));
  }
  if (icon_position_part.object_names().size() == 1) {
    const auto& object_name = icon_position_part.object_names().begin()->second;
    LOG(INFO) << "Using kinematic object from position part object_names: "
              << object_name;
    return object_world.GetKinematicObject(WorldObjectName(object_name));
  }
  if (icon_position_part.world_robot_collection_name().empty()) {
    LOG(INFO) << "Icon2PositionPart for " << icon_position_part.part_name()
              << " does not specify a kinematic model. Returning kinematic "
                 "model for "
              << handle.name();
    return object_world.GetKinematicObject(handle);
  }
  LOG(INFO) << "Using kinematic object from world robot collection: "
            << icon_position_part.world_robot_collection_name();
  return object_world.GetKinematicObject(
      WorldObjectName(icon_position_part.world_robot_collection_name()));
}

}  // namespace intrinsic::skills
