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

#include "intrinsic/geometry/service/geometry_service_impl.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/timestamp.pb.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/config/proto/config.pb.h"
#include "intrinsic/geometry/api/apply_transform.h"
#include "intrinsic/geometry/api/fuse_geometries.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/geometry/api/renderable_generation.h"
#include "intrinsic/geometry/compatibility/io.h"
#include "intrinsic/geometry/processing/pipeline.h"
#include "intrinsic/geometry/processing/pipeline_builder.h"
#include "intrinsic/geometry/processing/pipeline_config.pb.h"
#include "intrinsic/geometry/proto/geometry.pb.h"
#include "intrinsic/geometry/proto/geometry_service.pb.h"
#include "intrinsic/geometry/proto/geometry_service_types.pb.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/renderable.pb.h"
#include "intrinsic/geometry/service/util/geometry_data.h"
#include "intrinsic/geometry/storage/geo_cache.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/longrunning/cc/operation_scheduler_interface.h"
#include "intrinsic/stats/tracing_utils.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_builder_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"

namespace intrinsic::geo {
GeometryServiceImpl::GeometryServiceImpl(
    std::shared_ptr<longrunning::OperationSchedulerInterface> scheduler,
    MakeGeoLib make_geo_lib, int cache_size)
    : scheduler_(std::move(scheduler)),
      geo_cache_(cache_size),
      make_geo_lib_(std::move(make_geo_lib)) {}

GeometryServiceImpl::GeometryServiceImpl(
    std::shared_ptr<longrunning::OperationSchedulerInterface> scheduler,
    MakeGeoLib make_geo_lib)
    : scheduler_(std::move(scheduler)),
      make_geo_lib_(std::move(make_geo_lib)) {}

::grpc::Status GeometryServiceImpl::GetGeometry(
    ::grpc::ServerContext* context,
    const intrinsic_proto::geometry::GetGeometryRequest* request,
    intrinsic_proto::geometry::GeometryWithMetadata* response) {
  auto span = stats::GetSpanFromServerContext(context);
  INTR_ASSIGN_OR_RETURN_GRPC((auto [geo, storage_refs]),
                             GetGeometryAndStoreForV1Refs(context, *request),
                             _.LogError());

  *response->mutable_geometry_storage_refs() = storage_refs;
  INTR_ASSIGN_OR_RETURN_GRPC(*response->mutable_geometry_v0(),
                             geometry_compatibility::ToProto(geo),
                             _.LogError());
  INTR_ASSIGN_OR_RETURN_GRPC(*response->mutable_inline_geometry(),
                             geometry_details::ToInlineGeometryProto(geo),
                             _.LogError());
  return grpc::Status::OK;
}

::grpc::Status GeometryServiceImpl::GetRenderable(
    ::grpc::ServerContext* context,
    const intrinsic_proto::geometry::GetRenderableRequest* request,
    intrinsic_proto::geometry::RenderableWithMetadata* response) {
  if (context->IsCancelled()) {
    return grpc::Status(grpc::StatusCode::CANCELLED, "Request cancelled.");
  }
  INTR_ASSIGN_OR_RETURN_GRPC((auto [geo, storage_refs]),
                             GetGeometryAndStoreForV1Refs(context, *request),
                             _.LogError());

  if (context->IsCancelled()) {
    return grpc::Status(grpc::StatusCode::CANCELLED, "Request cancelled.");
  }
  INTR_ASSIGN_OR_RETURN_GRPC(auto renderable, GetOrGenerateRenderable(geo),
                             _.LogError());
  *response->mutable_geometry_storage_refs() = storage_refs;
  INTR_ASSIGN_OR_RETURN_GRPC(*response->mutable_renderable_v0(),
                             geometry_compatibility::ToProto(*renderable),
                             _.LogError());
  INTR_ASSIGN_OR_RETURN_GRPC(*response->mutable_renderable(),
                             ToProto(*renderable), _.LogError());

  // TODO(stoyang): Actually support LOD levels
  response->set_lod_level(0);
  return grpc::Status::OK;
}

::grpc::Status GeometryServiceImpl::CreateGeometry(
    ::grpc::ServerContext* context,
    const intrinsic_proto::geometry::CreateGeometryRequest* request,
    intrinsic_proto::geometry::GeometryWithMetadata* response) {
  INTR_ASSIGN_OR_RETURN_GRPC(Geometry geo, ToGeometry(request->data()),
                             _.LogError());

  if (context->IsCancelled()) {
    return grpc::Status(grpc::StatusCode::CANCELLED, "Request cancelled.");
  }
  INTR_ASSIGN_OR_RETURN_GRPC(const auto geo_storage_refs,
                             SaveAndUploadGeometry(context, std::move(geo)),
                             _.LogError());
  if (context->IsCancelled()) {
    return grpc::Status(grpc::StatusCode::CANCELLED, "Request cancelled.");
  }

  *response->mutable_geometry_storage_refs() = geo_storage_refs;
  return grpc::Status::OK;
}

::grpc::Status GeometryServiceImpl::ProcessGeometry(
    ::grpc::ServerContext* context,
    const intrinsic_proto::geometry::ProcessGeometryRequest* request,
    google::longrunning::Operation* response) {
  if (scheduler_ == nullptr) {
    return intrinsic::UnimplementedErrorBuilderGrpc().LogError()
           << "No operation scheduler provided.";
  }

  INTR_ASSIGN_OR_RETURN_GRPC(const std::unique_ptr<GeometryLibrary> geo_lib,
                             MakeGeometryLibrary(context), _.LogError());
  if (context->IsCancelled()) {
    return grpc::Status(grpc::StatusCode::CANCELLED, "Request cancelled.");
  }

  Geometry geometry;
  if (request->has_geometry()) {
    INTR_ASSIGN_OR_RETURN_GRPC(
        geometry, ToGeometry(request->geometry(), &geo_lib->Deserializer()));
  } else if (request->has_geometry_storage_refs_v0()) {
    INTR_ASSIGN_OR_RETURN_GRPC(
        geometry,
        geo_lib->Deserializer().GetGeometry(request->geometry_storage_refs_v0(),
                                            GeometryOptions::Default()));
  }

  INTR_ASSIGN_OR_RETURN_GRPC(std::string fp, GenerateFingerprint(geometry));
  const std::string operation_name =
      absl::StrCat("ProcessGeometry/", fp, "/", absl::GetCurrentTimeNanos());

  intrinsic_proto::geometry::v1::Geometry geometry_proto = request->geometry();

  // Create the pipeline now that we have our geometry.
  INTR_ASSIGN_OR_RETURN_GRPC(
      std::shared_ptr<GeometryProcessingPipeline> pipeline,
      BuildGeometryPipeline(request->pipeline()), _.LogError());

  // Setup the status function that will return the current state.
  longrunning::OperationSchedulerInterface::StatusFuncT<
      intrinsic_proto::geometry::GeometryProcessingStatus>
      status_func = [geometry_proto = std::move(geometry_proto)](
                        absl::Time start_time,
                        std::optional<google::protobuf::Any> custom_data,
                        const longrunning::OperationContext& context)
      -> absl::StatusOr<intrinsic_proto::geometry::GeometryProcessingStatus> {
    intrinsic_proto::geometry::GeometryProcessingStatus status;
    *status.mutable_geometry() = geometry_proto;

    INTR_ASSIGN_OR_RETURN_GRPC(*status.mutable_create_time(),
                               intrinsic::FromAbslTime(start_time),
                               _.LogError());
    if (custom_data.has_value()) {
      *status.mutable_custom_data() = custom_data.value();
    }
    if (context.Progress().has_value()) {
      status.set_progress(context.Progress().value());
    }

    return status;
  };

  // Setup the operation function that will be run in the background.
  longrunning::OperationSchedulerInterface::OperationFuncT<
      intrinsic_proto::geometry::GeometryProcessingResult>
      operation_func = [geometry = geometry, pipeline = std::move(pipeline),
                        this](longrunning::OperationContext context)
      -> absl::StatusOr<intrinsic_proto::geometry::GeometryProcessingResult> {
    // Checks for cancellation before attempting to process the geometry (which
    // can be expensive).
    if (context.GetStopToken().stop_requested()) {
      return absl::CancelledError("Operation cancelled before processing.");
    }

    // Process the geometry using the configured pipeline.
    INTR_ASSIGN_OR_RETURN_GRPC(std::vector<Geometry> geometries,
                               pipeline->Process(context, geometry),
                               _.LogError());

    // Checks for cancellation before attempting to upload the geometry.
    if (context.GetStopToken().stop_requested()) {
      return absl::CancelledError("Operation cancelled before uploading.");
    }

    intrinsic_proto::geometry::GeometryProcessingResult result_proto;
    INTR_ASSIGN_OR_RETURN(const std::unique_ptr<GeometryLibrary> geo_lib,
                          MakeGeometryLibrary(/*context=*/nullptr));

    for (const auto& geo : geometries) {
      INTR_ASSIGN_OR_RETURN_GRPC(*result_proto.add_processed_geometries(),
                                 ToProto(geo, &geo_lib->Serializer()),
                                 _.LogError());
    }

    if (geometries.empty()) {
      return absl::InternalError("Pipeline returned no geometries.");
    }

    Geometry output_geometry;
    if (geometries.size() == 1) {
      output_geometry = geometries[0];
    } else {
      // FuseGeometries requires TransformedGeometry (Geometry + Pose).
      // We wrap each geometry with an identity transform to merge them
      // in their current coordinate system.
      // This merges the multiple pipeline outputs into a single geometry
      // to maintain backward compatibility for legacy clients that only
      // support a singular processed_geometry.
      std::vector<TransformedGeometry> transformed_geometries;
      transformed_geometries.reserve(geometries.size());
      for (const auto& geo : geometries) {
        transformed_geometries.push_back(TransformedGeometry(geo));
      }
      INTR_ASSIGN_OR_RETURN_GRPC(
          auto fused, FuseGeometries(transformed_geometries), _.LogError());
      // Defensively apply the pose even though FuseGeometries currently
      // returns an identity pose.
      INTR_ASSIGN_OR_RETURN_GRPC(
          output_geometry, ApplyTransform(fused.shape(), fused.ref_t_shape()),
          _.LogError());
    }

    if (geometries.size() == 1) {
      // Optimize by copying the already serialized geometry from the repeated
      // field to avoid duplicate serialization and database upload
      // side-effects.
      *result_proto.mutable_processed_geometry() =
          result_proto.processed_geometries(0);
    } else {
      INTR_ASSIGN_OR_RETURN_GRPC(
          *result_proto.mutable_processed_geometry(),
          ToProto(output_geometry, &geo_lib->Serializer()), _.LogError());
    }

    return result_proto;
  };

  std::optional<google::protobuf::Any> custom_data;
  if (request->has_custom_data()) {
    custom_data = request->custom_data();
  }

  INTR_ASSIGN_OR_RETURN_GRPC(
      *response,
      scheduler_->AddOperationWrapped(operation_name, custom_data, status_func,
                                      std::move(operation_func)),
      _.LogError());

  return grpc::Status::OK;
}

absl::StatusOr<std::unique_ptr<GeometryLibrary>>
GeometryServiceImpl::MakeGeometryLibrary(grpc::ServerContext* context) {
  if (!make_geo_lib_) {
    return absl::FailedPreconditionError(
        "Persistent geometry storage backend is not configured.");
  }

  INTR_ASSIGN_OR_RETURN(std::unique_ptr<GeometryLibrary> geo_lib,
                        make_geo_lib_(context, geo_cache_));
  if (geo_lib == nullptr) {
    return absl::InternalError(
        "Persistent geometry library factory returned null.");
  }

  return geo_lib;
}

absl::StatusOr<Geometry> GeometryServiceImpl::GetGeometry(
    grpc::ServerContext* context,
    const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs) {
  // TODO(b/459563840): Support cancellation of download.
  INTR_ASSIGN_OR_RETURN(const std::unique_ptr<GeometryLibrary> geo_lib,
                        MakeGeometryLibrary(context));
  return geo_lib->Deserializer().GetGeometry(geo_storage_refs,
                                             GeometryOptions::Default());
}

absl::StatusOr<Geometry> GeometryServiceImpl::GetGeometry(
    grpc::ServerContext* context,
    const intrinsic_proto::geometry::v1::GeometryStorageRefs&
        geo_storage_refs) {
  // TODO(b/459563840): Support cancellation of download.
  INTR_ASSIGN_OR_RETURN(const std::unique_ptr<GeometryLibrary> geo_lib,
                        MakeGeometryLibrary(context));
  return geo_lib->Deserializer().GetGeometry(geo_storage_refs, std::nullopt);
}

absl::StatusOr<intrinsic_proto::geometry::v1::GeometryStorageRefs>
GeometryServiceImpl::SaveAndUploadGeometry(grpc::ServerContext* context,
                                           Geometry&& geo) {
  // TODO(b/459563840): Support cancellation of upload.
  INTR_ASSIGN_OR_RETURN(const std::unique_ptr<GeometryLibrary> geo_lib,
                        MakeGeometryLibrary(context));
  return geo_lib->Serializer().SaveGeometryV1(std::move(geo), {});
}

}  // namespace intrinsic::geo
