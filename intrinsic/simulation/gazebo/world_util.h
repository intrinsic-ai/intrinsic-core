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

#ifndef INTRINSIC_SIMULATION_GAZEBO_WORLD_UTIL_H_
#define INTRINSIC_SIMULATION_GAZEBO_WORLD_UTIL_H_

#include <memory>
#include <string>

#include "absl/flags/declare.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/simulation/world/world_to_sdf.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/world.h"

ABSL_DECLARE_FLAG(std::string, mesh_savepath);
ABSL_DECLARE_FLAG(std::string, sdf_savepath);

namespace intrinsic {
namespace simulation {
namespace details {

absl::StatusOr<World> DownloadWorldFromObjectWorldService(
    absl::string_view world_id,
    intrinsic_proto::world::ObjectWorldService::StubInterface*
        object_world_service_stub,
    const GeometryDeserializer& geom_service_deserializer,
    int num_retries_if_unavailable);

absl::StatusOr<
    std::unique_ptr<intrinsic_proto::world::ObjectWorldService::Stub>>
GetObjectWorldServiceStub(absl::string_view object_world_service_address);

// To save meshes to a disk location specified with a cmd-line flag, you can use
// WorldSdfAdapter::Options options;
// options.mesh_savepath = absl::GetFlag(FLAGS_mesh_savepath);
// auto adapter_or = GetWorldAdapterAndSaveSdf(world, options);
absl::StatusOr<std::unique_ptr<WorldSdfAdapter>> GetWorldAdapterAndSaveSdf(
    const World& world, WorldSdfAdapter::Options options);

}  // namespace details
}  // namespace simulation
}  // namespace intrinsic

#endif  // INTRINSIC_SIMULATION_GAZEBO_WORLD_UTIL_H_
