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

#include "intrinsic/simulation/gazebo/world_util.h"

#include <algorithm>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/simulation/world/world_to_sdf.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/util/download_world_util.h"
#include "intrinsic/world/world.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"
#include "ortools/base/path.h"

ABSL_FLAG(std::string, mesh_savepath, "",
          "Path to save meshes from the World.");

ABSL_FLAG(std::string, sdf_savepath, "",
          "Directory to save generated SDFs to for debugging.");

namespace intrinsic {
namespace simulation {
namespace details {

using ::intrinsic_proto::world::ObjectWorldService;

namespace {

const absl::Duration kGrpcClientConnectTimeout = absl::Seconds(300);
const absl::Duration kWorldUnavailableRetryBackoff = absl::Seconds(5);

// We check for both unavailable and unimplemented because the world service
// address could either be set to the cluster address or to ingress. If it is
// set to ingress, we will get an Unimplemented error if the world service is
// not yet running.
bool IsRetryableStatus(const absl::Status& status) {
  return absl::IsUnavailable(status) || absl::IsUnimplemented(status);
}

absl::StatusOr<World> FetchWorldOnce(ObjectWorldService::StubInterface& stub,
                                     absl::string_view world_id,
                                     const GeometryDeserializer& deserializer) {
  INTR_ASSIGN_OR_RETURN(
      World world,
      intrinsic::DownloadWorldFromObjectWorldService(world_id, stub));
  INTR_RETURN_IF_ERROR(
      intrinsic::DeserializeWorldGeometries(world, deserializer));
  return world;
}

}  // namespace

absl::StatusOr<World> DownloadWorldFromObjectWorldService(
    absl::string_view world_id,
    ObjectWorldService::StubInterface* object_world_service_stub,
    const GeometryDeserializer& geom_service_deserializer,
    int num_retries_if_unavailable) {
  if (object_world_service_stub == nullptr) {
    return absl::UnavailableError(
        "Object world service has not been initialized!");
  }

  absl::Status status;
  const int num_attempts = std::max(1, num_retries_if_unavailable + 1);
  for (int attempt_ind = 0; attempt_ind < num_attempts; ++attempt_ind) {
    LOG(INFO) << "Fetching world '" << world_id
              << "' from object world service.";

    absl::StatusOr<World> world = FetchWorldOnce(
        *object_world_service_stub, world_id, geom_service_deserializer);
    if (world.ok()) {
      return std::move(*world);
    }
    status = world.status();

    if (!IsRetryableStatus(status)) {
      return status;
    }

    if (attempt_ind + 1 < num_attempts) {
      LOG(INFO) << "Object world service is unavailable, attempt "
                << attempt_ind;
      absl::SleepFor(kWorldUnavailableRetryBackoff);
    }
  }
  // Return unavailable status.
  return status;
}

absl::StatusOr<std::unique_ptr<ObjectWorldService::Stub>>
GetObjectWorldServiceStub(absl::string_view object_world_service_address) {
  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<grpc::Channel> channel,
      connect::CreateClientChannel(
          object_world_service_address,
          /*deadline=*/absl::Now() + kGrpcClientConnectTimeout));
  return ObjectWorldService::NewStub(channel);
}

absl::StatusOr<std::unique_ptr<WorldSdfAdapter>> GetWorldAdapterAndSaveSdf(
    const World& world, WorldSdfAdapter::Options options) {
  auto adapter = WorldSdfAdapter::Create(world, options);

  std::string sdf_savepath = absl::GetFlag(FLAGS_sdf_savepath);
  if (!sdf_savepath.empty() && adapter.ok()) {
    auto sdf_filepath = file::JoinPath(
        sdf_savepath,
        absl::StrCat("world_to_sdf_", absl::FormatTime(absl::Now()), ".sdf"));
    absl::Status status =
        file::SetContents(sdf_filepath, (*adapter)->GetSDF(), file::Defaults());
    LOG_IF(WARNING, !status.ok())
        << "Failed to write generated SDF to file " << sdf_filepath;
  }

  return adapter;
}

}  // namespace details
}  // namespace simulation
}  // namespace intrinsic
