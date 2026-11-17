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

#include "intrinsic/scene/service/geometry_client_with_cache.h"

#include "absl/status/status.h"
#include "absl/strings/substitute.h"
#include "grpcpp/client_context.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

GeometryClientWithProcessGeometryCache::GeometryClientWithProcessGeometryCache(
    std::unique_ptr<GeometryClient> client)
    : c(std::move(client)), process_geometry_cache(1000) {}

GeometryClientWithProcessGeometryCache::
    ~GeometryClientWithProcessGeometryCache() {
  absl::MutexLock lock(&cache_mutex);
  process_geometry_cache.removeAll();
  process_geometry_cache.clear();
}

absl::StatusOr<intrinsic_proto::geometry::GeometryStorageRefs>
GeometryClientWithProcessGeometryCache::ProcessGeometry(
    longrunning::OperationContext& operation_context,
    const intrinsic_proto::geometry::GeometryStorageRefs& storage_refs,
    const intrinsic_proto::geometry::PipelineConfiguration& pipeline) {
  intrinsic_proto::geometry::GeometryProcessingResult result;
  intrinsic_proto::geometry::ProcessGeometryRequest request;
  *request.mutable_geometry_storage_refs_v0() = storage_refs;
  *request.mutable_pipeline() = pipeline;
  {
    absl::MutexLock lock(&cache_mutex);
    if (auto* cached_result = process_geometry_cache.lookup(request);
        cached_result != nullptr) {
      result = *cached_result;
      process_geometry_cache.release(request, cached_result);
      if (!result.has_processed_geometry_storage_refs_v0()) {
        return absl::InternalError(
            absl::Substitute("Cached result for geometry processing $0 does "
                             "not contain processed geometry storage refs.",
                             request));
      }
      return result.processed_geometry_storage_refs_v0();
    }
  }
  grpc::ClientContext context;
  INTR_ASSIGN_OR_RETURN(
      result,
      c->ProcessGeometry(context, operation_context.GetStopToken(), request));
  if (!result.has_processed_geometry_storage_refs_v0()) {
    return absl::InternalError(
        absl::Substitute("Result for geometry processing $0 does not contain "
                         "processed geometry storage refs.",
                         request));
  }
  {
    absl::MutexLock lock(&cache_mutex);
    process_geometry_cache.insert(
        request,
        new intrinsic_proto::geometry::GeometryProcessingResult(result), 1);
  }
  return result.processed_geometry_storage_refs_v0();
}

absl::StatusOr<std::vector<intrinsic_proto::geometry::v1::Geometry>>
GeometryClientWithProcessGeometryCache::ProcessGeometry(
    longrunning::OperationContext& operation_context,
    const intrinsic_proto::geometry::v1::Geometry& geometry,
    const intrinsic_proto::geometry::PipelineConfiguration& pipeline) {
  intrinsic_proto::geometry::GeometryProcessingResult result;
  intrinsic_proto::geometry::ProcessGeometryRequest request;
  *request.mutable_geometry() = geometry;
  *request.mutable_pipeline() = pipeline;
  {
    absl::MutexLock lock(&cache_mutex);
    if (auto* cached_result = process_geometry_cache.lookup(request);
        cached_result != nullptr) {
      result = *cached_result;
      process_geometry_cache.release(request, cached_result);
      // Until we remove assignment of processed_geometry we must check for its
      // value
      if (!result.has_processed_geometry()) {
        return absl::InternalError(
            absl::Substitute("Cached result for geometry processing $0 does "
                             "not contain processed geometry.",
                             request));
      }
      std::vector<intrinsic_proto::geometry::v1::Geometry> geometries;
      geometries.reserve(result.processed_geometries_size());
      for (auto&& g : *result.mutable_processed_geometries()) {
        geometries.push_back(std::move(g));
      }
      return geometries;
    }
  }
  grpc::ClientContext context;
  INTR_ASSIGN_OR_RETURN(
      result,
      c->ProcessGeometry(context, operation_context.GetStopToken(), request));
  if (!result.has_processed_geometry()) {
    return absl::InternalError(
        absl::Substitute("Result for geometry processing $0 does "
                         "not contain processed geometry.",
                         request));
  }
  {
    absl::MutexLock lock(&cache_mutex);
    process_geometry_cache.insert(
        request,
        new intrinsic_proto::geometry::GeometryProcessingResult(result), 1);
  }
  std::vector<intrinsic_proto::geometry::v1::Geometry> geometries;
  geometries.reserve(result.processed_geometries_size());
  for (auto&& g : *result.mutable_processed_geometries()) {
    geometries.push_back(std::move(g));
  }
  return geometries;
}

}  // namespace intrinsic
