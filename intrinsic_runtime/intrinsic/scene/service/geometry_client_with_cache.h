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

#ifndef INTRINSIC_SCENE_SERVICE_GEOMETRY_CLIENT_WITH_CACHE_H_
#define INTRINSIC_SCENE_SERVICE_GEOMETRY_CLIENT_WITH_CACHE_H_

#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/geometry/service/geometry_client.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/util/lru_cache.h"
#include "intrinsic/util/proto/pb_hash.h"

namespace intrinsic {

// A wrapper around GeometryClient that caches the result of ProcessGeometry
// calls to avoid duplicate processing of the same geometry with the same
// pipeline config.
// Used for SceneObject import.
struct GeometryClientWithProcessGeometryCache {
  std::unique_ptr<GeometryClient> c;

  mutable absl::Mutex cache_mutex;
  intrinsic::LruCache<intrinsic_proto::geometry::ProcessGeometryRequest,
                      intrinsic_proto::geometry::GeometryProcessingResult,
                      intrinsic::pb_hash, intrinsic::pb_equals>
      process_geometry_cache ABSL_GUARDED_BY(cache_mutex);

  explicit GeometryClientWithProcessGeometryCache(
      std::unique_ptr<GeometryClient> client);

  ~GeometryClientWithProcessGeometryCache();

  absl::StatusOr<intrinsic_proto::geometry::GeometryStorageRefs>
  ProcessGeometry(
      longrunning::OperationContext& operation_context,
      const intrinsic_proto::geometry::GeometryStorageRefs& storage_refs,
      const intrinsic_proto::geometry::PipelineConfiguration& pipeline);

  absl::StatusOr<std::vector<intrinsic_proto::geometry::v1::Geometry>>
  ProcessGeometry(
      longrunning::OperationContext& operation_context,
      const intrinsic_proto::geometry::v1::Geometry& geometry,
      const intrinsic_proto::geometry::PipelineConfiguration& pipeline);
};

}  // namespace intrinsic
#endif  // INTRINSIC_SCENE_SERVICE_GEOMETRY_CLIENT_WITH_CACHE_H_
