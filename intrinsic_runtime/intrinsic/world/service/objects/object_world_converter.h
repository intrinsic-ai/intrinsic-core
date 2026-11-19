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

#ifndef INTRINSIC_WORLD_SERVICE_OBJECTS_OBJECT_WORLD_CONVERTER_H_
#define INTRINSIC_WORLD_SERVICE_OBJECTS_OBJECT_WORLD_CONVERTER_H_

#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/objects/frame_internal.h"
#include "intrinsic/world/objects/object_entity_filter.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/service/world_mutex.h"

namespace intrinsic {
namespace object_world {

// Converts the given WorldAndMutex to its proto representation.
absl::StatusOr<intrinsic_proto::world::WorldMetadata> ToProto(
    const WorldAndMutex& world, absl::string_view world_id)
    ABSL_LOCKS_EXCLUDED(world.mtx);

// Converts the given WorldAndMutex to its proto representation. A shared lock
// on 'world.mtx' must be held by the caller.
absl::StatusOr<intrinsic_proto::world::WorldMetadata> ToProtoLocked(
    const WorldAndMutex& world, absl::string_view world_id)
    ABSL_SHARED_LOCKS_REQUIRED(world.mtx);

// Converts the internal entity with the given id to its external/public proto
// representation.
absl::StatusOr<intrinsic_proto::world::Entity> ToProto(
    AttachmentEntityId entity_id, absl::string_view world_id,
    const WorldObject& object);

// Converts the given WorldObject to its proto representation. The returned
// Object message including Frame messages nested within is populated according
// to the given ObjectView.
absl::StatusOr<intrinsic_proto::world::Object> ToProto(
    const WorldObject& object, absl::string_view world_id,
    intrinsic_proto::world::ObjectView view);

// Converts the given vector of WorldObjects to their Objects proto
// representation.
absl::StatusOr<intrinsic_proto::world::Objects> ToProto(
    const std::vector<const WorldObject*>& objects, absl::string_view world_id,
    intrinsic_proto::world::ObjectView view);

// Converts the given Frame to its proto representation. Returns a fully
// populated Frame message.
// Note that there is no "FrameView" parameter. Frame messages are only
// populated partially when inlined into an Object message (see ToProto( const
// WorldObject&, ...) above).
absl::StatusOr<intrinsic_proto::world::Frame> ToProto(
    const Frame& frame, absl::string_view world_id);

world::ObjectEntityFilter FromProto(
    const ::intrinsic_proto::world::ObjectEntityFilter& entity_filter);

}  // namespace object_world
}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_SERVICE_OBJECTS_OBJECT_WORLD_CONVERTER_H_
