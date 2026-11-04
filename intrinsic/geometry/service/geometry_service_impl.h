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

#ifndef INTRINSIC_GEOMETRY_SERVICE_GEOMETRY_SERVICE_IMPL_H_
#define INTRINSIC_GEOMETRY_SERVICE_GEOMETRY_SERVICE_IMPL_H_

#include <memory>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "google/longrunning/operations.pb.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/geometry/proto/geometry_service.pb.h"
#include "intrinsic/geometry/proto/geometry_service_types.pb.h"
#include "intrinsic/geometry/storage/geo_cache.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/longrunning/cc/operation_scheduler_interface.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
// A geometry service implementation that is backed by a GeometryLibrary
// instance
class GeometryServiceImpl
    : public intrinsic_proto::geometry::GeometryService::Service {
 public:
  using MakeGeoLib =
      absl::AnyInvocable<absl::StatusOr<std::unique_ptr<GeometryLibrary>>(
          grpc::ServerContext*, GeometryFingerprintCache&)>;

  GeometryServiceImpl(
      std::shared_ptr<longrunning::OperationSchedulerInterface> scheduler,
      MakeGeoLib make_geo_lib, int cache_size);

  GeometryServiceImpl(
      std::shared_ptr<longrunning::OperationSchedulerInterface> scheduler,
      MakeGeoLib make_geo_lib);

  ::grpc::Status GetGeometry(
      ::grpc::ServerContext* context,
      const intrinsic_proto::geometry::GetGeometryRequest* request,
      intrinsic_proto::geometry::GeometryWithMetadata* response) override;

  ::grpc::Status GetRenderable(
      ::grpc::ServerContext* context,
      const intrinsic_proto::geometry::GetRenderableRequest* request,
      intrinsic_proto::geometry::RenderableWithMetadata* response) override;

  ::grpc::Status CreateGeometry(
      ::grpc::ServerContext* context,
      const intrinsic_proto::geometry::CreateGeometryRequest* request,
      intrinsic_proto::geometry::GeometryWithMetadata* response) override;

  ::grpc::Status ProcessGeometry(
      ::grpc::ServerContext* context,
      const intrinsic_proto::geometry::ProcessGeometryRequest* request,
      google::longrunning::Operation* response) override;

 protected:
  // Scheduler used for the ProcessGeometry requests. Shared_ptr because this is
  // shared with the OperationsService in practice.
  std::shared_ptr<longrunning::OperationSchedulerInterface> scheduler_;

 private:
  absl::StatusOr<Geometry> GetGeometry(
      grpc::ServerContext* context,
      const intrinsic_proto::geometry::GeometryStorageRefs& refs);

  absl::StatusOr<Geometry> GetGeometry(
      grpc::ServerContext* context,
      const intrinsic_proto::geometry::v1::GeometryStorageRefs& refs);

  // Helper function to get geometry from the request based on the geometry
  // storage ref type and store it for v1 geometry storage refs.
  template <typename Request>
  absl::StatusOr<
      std::pair<Geometry, intrinsic_proto::geometry::v1::GeometryStorageRefs>>
  GetGeometryAndStoreForV1Refs(grpc::ServerContext* context,
                               const Request& request) {
    if (request.has_geometry_storage_refs()) {
      const auto& storage_refs_v1 = request.geometry_storage_refs();
      if (storage_refs_v1.exact_geometry_ref().empty()) {
        return absl::InvalidArgumentError(
            "Missing exact geometry storage refs.");
      }
      INTR_ASSIGN_OR_RETURN(Geometry geo,
                            GetGeometry(context, storage_refs_v1));

      return std::make_pair(geo, storage_refs_v1);
    } else if (request.has_geometry_storage_refs_v0()) {
      const auto& storage_refs_v0 = request.geometry_storage_refs_v0();
      if (storage_refs_v0.geometry_ref().empty() &&
          storage_refs_v0.fingerprint().empty()) {
        return absl::InvalidArgumentError("Missing geometry storage refs.");
      }
      INTR_ASSIGN_OR_RETURN(Geometry geo,
                            GetGeometry(context, storage_refs_v0));
      INTR_ASSIGN_OR_RETURN(const auto storage_refs_v1,
                            SaveAndUploadGeometry(context, Geometry(geo)),
                            _.LogError());
      return std::make_pair(geo, storage_refs_v1);
    }
    return absl::InvalidArgumentError("Missing valid geometry storage refs.");
  }

  // Saves the given geometry to using the GeometryLibrary. Returns the id of
  // the geometry.
  absl::StatusOr<intrinsic_proto::geometry::v1::GeometryStorageRefs>
  SaveAndUploadGeometry(grpc::ServerContext* context, Geometry&& geo);

  // Creates a GeometryLibrary instance using `make_geo_lib_`, returning an
  // error if the factory is unset, fails, or returns null.
  absl::StatusOr<std::unique_ptr<GeometryLibrary>> MakeGeometryLibrary(
      grpc::ServerContext* context);

  GeometryFingerprintCache geo_cache_;

  // The geometry library to use when reading and writing geometry.
  MakeGeoLib make_geo_lib_;
};

}  // namespace intrinsic::geo

namespace intrinsic {
using ::intrinsic::geo::GeometryServiceImpl;
}  // namespace intrinsic

#endif  // INTRINSIC_GEOMETRY_SERVICE_GEOMETRY_SERVICE_IMPL_H_
