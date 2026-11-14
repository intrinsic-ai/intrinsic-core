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

#include "intrinsic/simulation/world/world_service_updater.h"

#include <memory>
#include <string>

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "grpcpp/client_context.h"
#include "intrinsic/simulation/world/world_updater.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/service/world_service.pb.h"

namespace intrinsic {
namespace simulation {

WorldServiceUpdater::WorldServiceUpdater(
    absl::string_view target_world_id,
    ObjectWorldServiceInterface* object_world_service,
    UpdateGenerator* generator, absl::Duration time_between_updates)
    : WorldUpdater(generator, time_between_updates),
      object_world_service_(object_world_service),
      target_world_id_(target_world_id) {}

absl::StatusOr<std::unique_ptr<WorldServiceUpdater>>
WorldServiceUpdater::Create(absl::string_view target_world_id,
                            ObjectWorldServiceInterface* object_world_service,
                            UpdateGenerator* generator,
                            absl::Duration time_between_updates) {
  const absl::Duration kMinimumTimeBetweenUpdates = absl::Milliseconds(1);
  if (time_between_updates < kMinimumTimeBetweenUpdates) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "Cannot start a world service updater with too frequent updates "
           << "(got " << time_between_updates
           << " which is less than the minimum " << kMinimumTimeBetweenUpdates
           << ").";
  }

  // Send RPC to object world service to get the root object. This serves as
  // a sanity check that the world with the given id exists.
  grpc::ClientContext ctx;
  ConfigureClientContext(&ctx);

  intrinsic_proto::world::GetObjectRequest request;
  request.set_world_id(target_world_id);
  request.mutable_object()->set_id("root");
  intrinsic_proto::world::Object dummy_response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service->GetObject(&ctx, request, &dummy_response)));

  return absl::WrapUnique(new WorldServiceUpdater(
      target_world_id, object_world_service, generator, time_between_updates));
}

void WorldServiceUpdater::SetTargetWorldId(absl::string_view id) {
  absl::WriterMutexLock l(target_world_id_mutex_);
  target_world_id_ = std::string(id);
}

std::string WorldServiceUpdater::GetTargetWorldId() {
  absl::ReaderMutexLock l(target_world_id_mutex_);
  return target_world_id_;
}

void WorldServiceUpdater::UpdateObjectWorldService(
    const ObjectWorldUpdates& updates) {
  if (updates.updates().empty()) {
    // Nothing to do here.
    return;
  }

  // Send RPC to object world service with all object updates
  grpc::ClientContext obj_ctx;
  ConfigureClientContext(&obj_ctx);

  intrinsic_proto::world::UpdateWorldResourcesRequest request;
  intrinsic_proto::world::UpdateWorldResourcesResponse response;

  request.set_world_id(GetTargetWorldId());
  *request.mutable_world_updates() = updates;

  auto result =
      object_world_service_->UpdateWorldResources(&obj_ctx, request, &response);
  if (!result.ok()) {
    LOG_EVERY_N_SEC(ERROR, 5)
        << "Error updating world: " << ToAbslStatus(result);
  }
}

void WorldServiceUpdater::OnWorldUpdates(const ObjectWorldUpdates& updates) {
  UpdateObjectWorldService(updates);
}

}  // namespace simulation
}  // namespace intrinsic
