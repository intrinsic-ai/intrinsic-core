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

#include "intrinsic/skills/internal/preview_context_impl.h"

#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/protobuf/timestamp.pb.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/skills/proto/prediction.pb.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/frame.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/world_object.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"

namespace intrinsic {
namespace skills {

absl::StatusOr<world::Frame> PreviewContextImpl::GetFrameForEquipment(
    absl::string_view equipment_name, absl::string_view frame_name) {
  INTR_ASSIGN_OR_RETURN(const intrinsic_proto::resources::ResourceHandle handle,
                        equipment_.GetHandle(equipment_name));
  return object_world().GetFrame(handle, FrameName(frame_name));
}

absl::StatusOr<world::KinematicObject>
PreviewContextImpl::GetKinematicObjectForEquipment(
    absl::string_view equipment_name) {
  INTR_ASSIGN_OR_RETURN(const intrinsic_proto::resources::ResourceHandle handle,
                        equipment_.GetHandle(equipment_name));
  return object_world().GetKinematicObject(handle);
}

absl::StatusOr<world::WorldObject> PreviewContextImpl::GetObjectForEquipment(
    absl::string_view equipment_name) {
  INTR_ASSIGN_OR_RETURN(const intrinsic_proto::resources::ResourceHandle handle,
                        equipment_.GetHandle(equipment_name));
  return object_world().GetObject(handle);
}

absl::Status PreviewContextImpl::RecordWorldUpdate(
    const intrinsic_proto::world::ObjectWorldUpdate& update,
    absl::Duration elapsed, absl::Duration duration) {
  if (elapsed < absl::ZeroDuration()) {
    return absl::InvalidArgumentError("`elapsed` must be non-negative.");
  }
  if (duration < absl::ZeroDuration()) {
    return absl::InvalidArgumentError("`duration` must be non-negative.");
  }

  intrinsic_proto::skills::TimedWorldUpdate timed_update;
  *timed_update.mutable_world_updates()->add_updates() = update;

  INTR_ASSIGN_OR_RETURN(
      absl::Time base_time,
      ToAbslTime(world_updates_.empty() ? google::protobuf::Timestamp()
                                        : world_updates_.back().start_time()));
  INTR_ASSIGN_OR_RETURN(*timed_update.mutable_start_time(),
                        FromAbslTime(base_time + elapsed));

  INTR_ASSIGN_OR_RETURN(*timed_update.mutable_time_until_update(),
                        FromAbslDuration(duration));

  world_updates_.push_back(std::move(timed_update));

  return absl::OkStatus();
}

}  // namespace skills
}  // namespace intrinsic
