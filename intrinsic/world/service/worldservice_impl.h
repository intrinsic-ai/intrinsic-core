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

#ifndef INTRINSIC_WORLD_SERVICE_WORLDSERVICE_IMPL_H_
#define INTRINSIC_WORLD_SERVICE_WORLDSERVICE_IMPL_H_

#include <future>  // NOLINT(build/c++11)
#include <memory>
#include <optional>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/service/world_service.grpc.pb.h"
#include "intrinsic/world/service/world_service.pb.h"
#include "intrinsic/world/service/world_storage.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

class WorldServiceImpl final
    : public intrinsic_proto::world::internal::WorldService::Service {
 public:
  // Creates a new instance.
  static absl::StatusOr<std::unique_ptr<WorldServiceImpl>> CreateService(
      WorldStorage& world_storage,
      std::optional<ObjectWorldCompatibilityFunc> compat_func,
      GeometryLibrary& geo_lib);

  // Creates a new instance, allowing for deferred initialization for dependent
  // services via futures.
  static absl::StatusOr<std::unique_ptr<WorldServiceImpl>> CreateService(
      std::future<WorldStorage*> world_storage,
      std::optional<ObjectWorldCompatibilityFunc> compat_func,
      std::future<GeometryLibrary*> geo_lib);

  ~WorldServiceImpl() override = default;
  // Disable copy constructors.
  WorldServiceImpl(const WorldServiceImpl&) = delete;
  const WorldServiceImpl& operator=(const WorldServiceImpl&) = delete;

  grpc::Status GetIkSolution(
      grpc::ServerContext* context,
      const intrinsic_proto::world::internal::GetIkSolutionRequest* request,
      intrinsic_proto::world::internal::SingleIKSolution* response) override;

  // Returns the entity matching the given search criteria.
  // This exists primarily for testing purposes.
  absl::StatusOr<intrinsic_proto::world::internal::EntityWithMetadata>
  GetEntity(absl::string_view world_id,
            const intrinsic_proto::world::EntitySearchCriteria& entity);

 private:
  WorldServiceImpl(std::future<WorldStorage*> world_storage,
                   std::optional<ObjectWorldCompatibilityFunc> compat_func,
                   std::future<GeometryLibrary*> geo_lib)
      : world_storage_future_(std::move(world_storage)),
        compat_func_(compat_func),
        geo_lib_future_(std::move(geo_lib)) {}

  // Converts the given world id, entity id, and entity into a
  // EntityWithMetadata proto.
  absl::StatusOr<intrinsic_proto::world::internal::EntityWithMetadata>
  EntityToProto(absl::string_view world_id, EntityId entity_id,
                const WorldEntity& entity);

  absl::Mutex world_storage_mutex_;
  WorldStorage* world_storage_ ABSL_GUARDED_BY(world_storage_mutex_) = nullptr;
  std::future<WorldStorage*> world_storage_future_
      ABSL_GUARDED_BY(world_storage_mutex_);
  WorldStorage* WorldStore() ABSL_LOCKS_EXCLUDED(world_storage_mutex_);

  // If set, we will try to conform to the object world invariants when
  // performing world operations or error on unsupported operations.
  std::optional<ObjectWorldCompatibilityFunc> compat_func_;

  // The geometry storage to use when reading or writing protos.
  absl::Mutex geo_lib_mutex_;
  GeometryLibrary* geo_lib_ ABSL_GUARDED_BY(geo_lib_mutex_) = nullptr;
  std::future<GeometryLibrary*> geo_lib_future_ ABSL_GUARDED_BY(geo_lib_mutex_);
  GeometryLibrary* GeoLib() ABSL_LOCKS_EXCLUDED(geo_lib_mutex_);
};

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_SERVICE_WORLDSERVICE_IMPL_H_
