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

#ifndef INTRINSIC_EXECUTIVE_ENGINE_CLIPS_WORLD_H_
#define INTRINSIC_EXECUTIVE_ENGINE_CLIPS_WORLD_H_

#include <optional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/executive/clips_cpp/function_facade.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/util/thread/thread_pool.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/service/updater/world_updater.grpc.pb.h"
#include "intrinsic/world/service/world_compatibility_service.grpc.pb.h"

namespace intrinsic {
namespace executive {

// Converts a Footprint protobuf into a CreateObjectRequest for
// ObjectWorldService.
//
// Creates a scene object named `local_name` attached to parent `root`:
// - Root link entity: `<local_name>_root` under parent `root`.
// - Child link entities: `volume_<local_name>_<index>` attached to
//   `<local_name>_root`, each carrying `Intrinsic_Collision` and
//   `Intrinsic_Visual` geometry components (via `named_geometries` for
//   TransformedGeometry or legacy `geometries` for GeometryStorageRefs).
//
// Requires all VolumeReservation entries to have volume_oneof set and have type
// WRITE. Returns an InvalidArgumentError if any reservation has a non-WRITE
// type or volume_oneof is not set.
absl::StatusOr<intrinsic_proto::world::CreateObjectRequest>
FootprintToCreateObjectRequest(
    absl::string_view world_id, absl::string_view local_name,
    const intrinsic_proto::skills::Footprint& footprint);

// Adds footprint volumes to the object world as a standalone scene object.
// - If the footprint contains no volumes, returns an empty string ("")
//   immediately.
// - Otherwise, creates the footprint scene object in ObjectWorldService and
//   returns the created object's name.
absl::StatusOr<std::string> AddFootprintVolumesToWorld(
    intrinsic_proto::world::ObjectWorldService::StubInterface&
        object_world_service_stub,
    absl::string_view world_id, absl::string_view action_id,
    const intrinsic_proto::skills::Footprint& footprint);

// Removes the footprint scene object from the object world.
// - If footprint_object_name is empty, returns OkStatus immediately.
// - Otherwise, deletes the object by name via ObjectWorldService::DeleteObject
//   with force = true.
absl::Status RemoveFootprintVolumesFromWorld(
    intrinsic_proto::world::ObjectWorldService::StubInterface&
        object_world_service_stub,
    absl::string_view world_id, absl::string_view footprint_object_name);

// Access world service from CLIPS.
namespace clips {
class EnvironmentAssertFacade;
}  // namespace clips

class ClipsWorld {
 public:
  ClipsWorld(
      clips::EnvironmentAssertFacade* assert_facade,
      clips::ProtobufManager* proto_manager,
      clips::TraceSpanManager* span_manager,
      intrinsic_proto::world::ObjectWorldService::StubInterface* object_stub,
      intrinsic_proto::world::WorldUpdater::StubInterface* world_updater_stub,
      intrinsic_proto::world::WorldCompatibilityService::StubInterface*
          compatibility_stub);

  // Must be called after construction.
  // Register the world functions with the CLIPS environment and creates the
  // thread bundle.
  absl::Status Init(clips::EnvironmentFunctionFacade* facade)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(facade->clips_mutex());

  // Must be called in the same parent thread as Init().
  absl::Status TearDown();

  // For testing.
  void WaitForFinishAll();

  absl::StatusOr<clips::ProtoMessageId> Query(
      absl::string_view world_id, clips::ProtoMessageId world_query_proto_id);

 private:
  // Downloads the most recent object world from the world service. This
  // function is called from within CLIPS.
  absl::StatusOr<clips::ProtoMessageId> DownloadObjectWorld(
      const std::string& world_id);

  // Clones a given world and returns the ID of the clone copy.
  // Give a clone_source to indicate what part of the code issued the clone.
  // Do not call directly. This function is called from within CLIPS.
  absl::StatusOr<std::string> CloneWorld(
      absl::string_view from_world_id, absl::string_view clone_source,
      clips::TraceSpanReferenceId trace_parent_span_id);

  // Deletes the given world ID from world service.
  // Do not call directly. This function is called from within CLIPS.
  absl::Status DeleteWorld(const std::string& world_id);

  // Pauses/Resumes the world updater.
  absl::Status PauseWorldUpdater();
  absl::Status ResumeWorldUpdater();

  // Checks the given footprint ID for conflicts with running actions.
  void CheckFootprintForConflict(absl::string_view world_id,
                                 clips::ProtoMessageId footprint_proto_id,
                                 absl::string_view action_uid,
                                 const clips::Values& running_action_uids,
                                 const clips::Values& running_footprints);

  clips::EnvironmentAssertFacade* assert_facade_;  // externally owned
  clips::ProtobufManager* proto_manager_;          // externally owned
  clips::TraceSpanManager* span_manager_;          // externally owned
  intrinsic_proto::world::ObjectWorldService::StubInterface*
      object_world_service_stub_;  // externally owned
  intrinsic_proto::world::WorldUpdater::StubInterface*
      world_updater_stub_;  // externally owned
  intrinsic_proto::world::WorldCompatibilityService::StubInterface*
      world_compatibility_service_stub_;  // externally owned
  std::optional<intrinsic::ThreadPool> bundle_;
};

}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_ENGINE_CLIPS_WORLD_H_
