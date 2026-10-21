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

#include "intrinsic/skills/internal/get_footprint_context_impl.h"

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/frame.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/world_object.h"

namespace intrinsic {
namespace skills {

absl::StatusOr<world::KinematicObject>
GetFootprintContextImpl::GetKinematicObjectForEquipment(
    absl::string_view equipment_name) {
  INTR_ASSIGN_OR_RETURN(const intrinsic_proto::resources::ResourceHandle handle,
                        equipment_.GetHandle(equipment_name));
  return object_world().GetKinematicObject(handle);
}

absl::StatusOr<world::WorldObject>
GetFootprintContextImpl::GetObjectForEquipment(
    absl::string_view equipment_name) {
  INTR_ASSIGN_OR_RETURN(const intrinsic_proto::resources::ResourceHandle handle,
                        equipment_.GetHandle(equipment_name));
  return object_world().GetObject(handle);
}

absl::StatusOr<world::Frame> GetFootprintContextImpl::GetFrameForEquipment(
    absl::string_view equipment_name, absl::string_view frame_name) {
  INTR_ASSIGN_OR_RETURN(const intrinsic_proto::resources::ResourceHandle handle,
                        equipment_.GetHandle(equipment_name));
  return object_world().GetFrame(handle, FrameName(frame_name));
}

}  // namespace skills
}  // namespace intrinsic
