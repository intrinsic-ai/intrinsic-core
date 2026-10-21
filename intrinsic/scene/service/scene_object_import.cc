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

#include "intrinsic/scene/service/scene_object_import.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/log/die_if_null.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/any.pb.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/geometry/service/geometry_client.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_service_storage.h"
#include "intrinsic/kubernetes/acl/cc/client_context.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/longrunning/cc/operation_scheduler_interface.h"
#include "intrinsic/longrunning/cc/operations_proxy.h"
#include "intrinsic/scene/instantiation/imported_scene_instantiator.h"
#include "intrinsic/scene/proto/v1/export.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_import.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_internal.pb.h"
#include "intrinsic/scene/service/geometry_client_with_cache.h"
#include "intrinsic/scene/service/scene_object_exporter.h"
#include "intrinsic/scene/service/scene_object_importer.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"

namespace intrinsic {

namespace {

using ::intrinsic_proto::geometry::GeometryService;
using ::intrinsic_proto::scene_object::v1::ExportRequest;
using ::intrinsic_proto::scene_object::v1::ExportResponse;
using ::intrinsic_proto::scene_object::v1::ImportSceneObjectMetadata;
using ::intrinsic_proto::scene_object::v1::ImportSceneObjectRequest;
using ::intrinsic_proto::scene_object::v1::ImportSceneObjectResponse;
using ::intrinsic_proto::scene_object::v1::ImportSceneRequest;
using ::intrinsic_proto::scene_object::v1::ImportSceneResult;
using ::intrinsic_proto::scene_object::v1::ImportSceneStatus;
using ::intrinsic_proto::scene_object::v1::InstantiateImportedSceneRequest;
using ::intrinsic_proto::scene_object::v1::InstantiateImportedSceneResponse;
using ::intrinsic_proto::scene_object::v1::InstantiateImportedSceneStatus;

template <typename StatusType>
absl::StatusOr<StatusType> ReportImportStatus(
    absl::Time start_time, std::optional<google::protobuf::Any> custom_data,
    const longrunning::OperationContext& context) {
  StatusType status;
  INTR_ASSIGN_OR_RETURN(*status.mutable_create_time(),
                        FromAbslTime(start_time));
  if (custom_data.has_value()) {
    *status.mutable_custom_data() = custom_data.value();
  }
  if (context.Progress().has_value()) {
    status.set_progress(context.Progress().value());
  }
  return status;
}

absl::StatusOr<ImportSceneStatus> ReportImportSceneStatus(
    absl::Time start_time, std::optional<google::protobuf::Any> custom_data,
    const longrunning::OperationContext& context) {
  return ReportImportStatus<ImportSceneStatus>(start_time, custom_data,
                                               context);
}

absl::StatusOr<ImportSceneObjectMetadata> ReportImportSceneObjectStatus(
    absl::Time start_time, std::optional<google::protobuf::Any> custom_data,
    const longrunning::OperationContext& context) {
  return ImportSceneObjectMetadata();
}

}  // namespace

// Implementation detail for SceneObjectInternal, SceneObjectImport, and
// SceneObjectExport that handles scheduling longrunning operations and
// delegating business logic to SceneObjectImporter and ExportSceneObject.
class SceneObjectImportImpl {
 public:
  SceneObjectImportImpl(
      std::unique_ptr<GeometryService::StubInterface> geometry_service_stub,
      std::shared_ptr<longrunning::OperationsProxy> geometry_operations_proxy,
      std::unique_ptr<ImportedSceneInstantiator> imported_scene_instantiator,
      std::shared_ptr<longrunning::OperationSchedulerInterface> scheduler);

  absl::StatusOr<google::longrunning::Operation> ImportSceneObject(
      grpc::ServerContext* absl_nonnull context,
      const ImportSceneObjectRequest* request);

  absl::StatusOr<google::longrunning::Operation> ImportScene(
      grpc::ServerContext* absl_nonnull context,
      const ImportSceneRequest* request);

  absl::StatusOr<google::longrunning::Operation> InstantiateImportedScene(
      grpc::ServerContext* absl_nonnull context,
      const InstantiateImportedSceneRequest* request);

  absl::StatusOr<ExportResponse> Export(
      grpc::ServerContext* absl_nonnull context, const ExportRequest* request);

  absl::StatusOr<google::longrunning::Operation> ScheduleImportSceneObject(
      grpc::ServerContext* absl_nonnull context,
      const ImportSceneObjectRequest& request);

  absl::StatusOr<ImportSceneObjectResponse> ImportSceneObjectOperation(
      const ImportSceneObjectRequest& request,
      longrunning::OperationContext& operation_context);

  absl::StatusOr<google::longrunning::Operation> ScheduleImportScene(
      grpc::ServerContext* absl_nonnull context,
      const ImportSceneRequest& request);

  absl::StatusOr<ImportSceneResult> ImportSceneOperation(
      const ImportSceneRequest& request,
      longrunning::OperationContext& operation_context);

  absl::StatusOr<google::longrunning::Operation>
  ScheduleInstantiateImportedScene(
      grpc::ServerContext* absl_nonnull context,
      const InstantiateImportedSceneRequest& request,
      std::unique_ptr<acl::User> identity);

  absl::StatusOr<InstantiateImportedSceneResponse>
  InstantiateImportedSceneOperation(
      const InstantiateImportedSceneRequest& request, const acl::User& identity,
      std::shared_ptr<InstantiateImportedSceneStatus> status);

  std::unique_ptr<GeometryService::StubInterface> geometry_service_stub_;
  std::unique_ptr<GeometryLibrary> geometry_library_;
  std::unique_ptr<GeometryClientWithProcessGeometryCache> geometry_client_;
  std::unique_ptr<ImportedSceneInstantiator> imported_scene_instantiator_;

  std::shared_ptr<longrunning::OperationSchedulerInterface> scheduler_;
  std::unique_ptr<SceneObjectImporter> importer_;
};

SceneObjectImportImpl::SceneObjectImportImpl(
    std::unique_ptr<GeometryService::StubInterface> geometry_service_stub,
    std::shared_ptr<longrunning::OperationsProxy> geometry_operations_proxy,
    std::unique_ptr<ImportedSceneInstantiator> imported_scene_instantiator,
    std::shared_ptr<longrunning::OperationSchedulerInterface> scheduler)
    : geometry_service_stub_(
          std::move(ABSL_DIE_IF_NULL(geometry_service_stub))),
      imported_scene_instantiator_(
          std::move(ABSL_DIE_IF_NULL(imported_scene_instantiator))),
      scheduler_(std::move(ABSL_DIE_IF_NULL(scheduler))) {
  geometry_library_ =
      GetGeometryServiceGeometryLibrary(geometry_service_stub_.get());
  auto geometry_client = GeometryClient::Create(geometry_service_stub_.get(),
                                                geometry_operations_proxy);
  CHECK_OK(geometry_client);
  CHECK_NE(*geometry_client, nullptr);
  geometry_client_ = std::make_unique<GeometryClientWithProcessGeometryCache>(
      *std::move(geometry_client));

  SceneObjectImporter::Options importer_options;
  importer_options.geometry_library = geometry_library_.get();
  importer_options.geometry_client = geometry_client_.get();
  importer_ = std::make_unique<SceneObjectImporter>(importer_options);
}

absl::StatusOr<google::longrunning::Operation>
SceneObjectImportImpl::ImportSceneObject(
    grpc::ServerContext* absl_nonnull context,
    const ImportSceneObjectRequest* request) {
  if (!request->has_file()) {
    return absl::InvalidArgumentError(
        "Request does not contain file data for importing a scene object.");
  }
  return ScheduleImportSceneObject(context, *request);
}

absl::StatusOr<google::longrunning::Operation>
SceneObjectImportImpl::ImportScene(grpc::ServerContext* absl_nonnull context,
                                   const ImportSceneRequest* request) {
  if (request->to_be_imported_case() ==
      ImportSceneRequest::TO_BE_IMPORTED_NOT_SET) {
    return absl::InvalidArgumentError(
        "Request does not contain valid scene file data for import into a "
        "scene object.");
  }
  return ScheduleImportScene(context, *request);
}

absl::StatusOr<google::longrunning::Operation>
SceneObjectImportImpl::InstantiateImportedScene(
    grpc::ServerContext* absl_nonnull context,
    const InstantiateImportedSceneRequest* request) {
  if (!request->has_imported_scene()) {
    return absl::InvalidArgumentError(
        "Request does not contain valid imported scene data for "
        "instantiation.");
  }
  std::unique_ptr<acl::User> identity =
      acl::CreateGrpcMetadataUserFromServerContext(*context);
  return ScheduleInstantiateImportedScene(context, *request,
                                          std::move(identity));
}

absl::StatusOr<ExportResponse> SceneObjectImportImpl::Export(
    grpc::ServerContext* absl_nonnull context, const ExportRequest* request) {
  if (geometry_library_ == nullptr) {
    return absl::InternalError("Geometry library is not initialized.");
  }
  return ExportSceneObject(*request, *geometry_library_);
}

absl::StatusOr<google::longrunning::Operation>
SceneObjectImportImpl::ScheduleImportSceneObject(
    grpc::ServerContext* absl_nonnull context,
    const ImportSceneObjectRequest& request) {
  // Function to return the status of the operation.
  longrunning::OperationSchedulerInterface::StatusFuncT<
      ImportSceneObjectMetadata>
      status_func = ReportImportSceneObjectStatus;

  // Function to import the mesh data.
  longrunning::OperationSchedulerInterface::OperationFuncT<
      ImportSceneObjectResponse>
      operation_func = [this, request](longrunning::OperationContext context) {
        auto result = this->ImportSceneObjectOperation(request, context);
        if (!result.ok()) {
          LOG(WARNING) << "Failed to import scene object: "
                       << result.status().message();
        }
        return result;
      };

  // Set up and start the operation.
  const uint64_t file_hash = absl::HashOf(request.file().data());
  const std::string operation_name =
      absl::StrCat("ImportSceneObject/", absl::Hex(file_hash), "/",
                   absl::GetCurrentTimeNanos());

  // Checks for cancellation before scheduling the operation.
  if (context->IsCancelled()) {
    return absl::CancelledError("Cancelled");
  }

  return scheduler_->AddOperationWrapped(
      operation_name, google::protobuf::Any(), status_func, operation_func);
}

absl::StatusOr<ImportSceneObjectResponse>
SceneObjectImportImpl::ImportSceneObjectOperation(
    const ImportSceneObjectRequest& request,
    longrunning::OperationContext& operation_context) {
  return importer_->ImportSceneObject(request, operation_context);
}

absl::StatusOr<google::longrunning::Operation>
SceneObjectImportImpl::ScheduleImportScene(
    grpc::ServerContext* absl_nonnull context,
    const ImportSceneRequest& request) {
  std::optional<google::protobuf::Any> custom_data;
  if (request.has_custom_data()) {
    custom_data = request.custom_data();
  }

  // Function to return the status of the operation.
  longrunning::OperationSchedulerInterface::StatusFuncT<ImportSceneStatus>
      status_func = ReportImportSceneStatus;

  // Function to import the mesh data.
  longrunning::OperationSchedulerInterface::OperationFuncT<ImportSceneResult>
      operation_func = [this, request](longrunning::OperationContext context) {
        return this->ImportSceneOperation(request, context);
      };

  // Set up and start the operation.
  const uint64_t file_hash = absl::HashOf(request.scene_file_data().data());
  const std::string operation_name =
      absl::StrCat("ImportSceneObject/", absl::Hex(file_hash), "/",
                   absl::GetCurrentTimeNanos());

  // Checks for cancellation before scheduling the operation.
  if (context->IsCancelled()) {
    return absl::CancelledError("Cancelled");
  }

  return scheduler_->AddOperationWrapped(operation_name, custom_data,
                                         status_func, operation_func);
}

absl::StatusOr<ImportSceneResult> SceneObjectImportImpl::ImportSceneOperation(
    const ImportSceneRequest& request,
    longrunning::OperationContext& operation_context) {
  return importer_->ImportScene(request, operation_context);
}

absl::StatusOr<google::longrunning::Operation>
SceneObjectImportImpl::ScheduleInstantiateImportedScene(
    grpc::ServerContext* absl_nonnull context,
    const InstantiateImportedSceneRequest& request,
    std::unique_ptr<acl::User> identity) {
  std::shared_ptr<acl::User> shared_identity(std::move(identity));

  // A status that is shared between the status reporting func and operation
  // func.
  auto status = std::make_shared<InstantiateImportedSceneStatus>();
  std::optional<google::protobuf::Any> custom_data = std::nullopt;
  if (request.has_custom_data()) {
    *status->mutable_custom_data() = request.custom_data();
    custom_data = request.custom_data();
  }

  // Function to return the status of the operation.
  longrunning::OperationSchedulerInterface::StatusFuncT<
      InstantiateImportedSceneStatus>
      status_func = [status](absl::Time start_time,
                             std::optional<google::protobuf::Any> custom_data,
                             const longrunning::OperationContext& context) {
        return *status;
      };

  // Function to instantiate the scene data.
  longrunning::OperationSchedulerInterface::OperationFuncT<
      InstantiateImportedSceneResponse>
      operation_func =
          [this, request, status = std::move(status),
           shared_identity = std::move(shared_identity)](
              [[maybe_unused]] longrunning::OperationContext /*context*/) {
            return this->InstantiateImportedSceneOperation(
                request, *shared_identity, std::move(status));
          };

  // Set up and start the operation.
  const uint64_t request_hash = absl::HashOf(request.SerializeAsString());
  const std::string operation_name =
      absl::StrCat("InstantiateImportedScene/", absl::Hex(request_hash), "/",
                   absl::GetCurrentTimeNanos());

  // Checks for cancellation before scheduling the operation.
  if (context->IsCancelled()) {
    return absl::CancelledError("Cancelled");
  }

  return scheduler_->AddOperationWrapped(operation_name, custom_data,
                                         status_func, operation_func);
}

absl::StatusOr<InstantiateImportedSceneResponse>
SceneObjectImportImpl::InstantiateImportedSceneOperation(
    const InstantiateImportedSceneRequest& request, const acl::User& identity,
    std::shared_ptr<InstantiateImportedSceneStatus> status) {
  return imported_scene_instantiator_->InstantiateImportedScene(
      request, identity, std::move(status));
}

absl::StatusOr<SceneObjectImportServices> CreateSceneObjectImportServices(
    std::unique_ptr<GeometryService::StubInterface> geometry_service_stub,
    std::shared_ptr<longrunning::OperationsProxy> geometry_operations_proxy,
    std::unique_ptr<ImportedSceneInstantiator> imported_scene_instantiator,
    std::shared_ptr<longrunning::OperationSchedulerInterface> scheduler) {
  auto pimpl = std::make_shared<SceneObjectImportImpl>(
      std::move(geometry_service_stub), std::move(geometry_operations_proxy),
      std::move(imported_scene_instantiator),
      std::move(scheduler));
  return SceneObjectImportServices{
      .soii_service = std::make_unique<SceneObjectInternal>(pimpl),
      .soi_service = std::make_unique<SceneObjectImport>(pimpl),
      .soe_service = std::make_unique<SceneObjectExport>(pimpl),
  };
}

SceneObjectInternal::SceneObjectInternal(
    std::shared_ptr<SceneObjectImportImpl> pimpl)
    : pimpl_(std::move(pimpl)) {}

SceneObjectInternal::~SceneObjectInternal() = default;

grpc::Status SceneObjectInternal::ImportScene(
    grpc::ServerContext* context, const ImportSceneRequest* request,
    google::longrunning::Operation* response) {
  const stats::ScopedSpan span("SceneObjectInternal/ImportScene", context);

  INTR_ASSIGN_OR_RETURN_GRPC(*response, pimpl_->ImportScene(context, request));
  return grpc::Status::OK;
}

grpc::Status SceneObjectInternal::InstantiateImportedScene(
    grpc::ServerContext* context,
    const InstantiateImportedSceneRequest* request,
    google::longrunning::Operation* response) {
  const stats::ScopedSpan span("SceneObjectInternal/InstantiateImportedScene",
                               context);

  INTR_ASSIGN_OR_RETURN_GRPC(
      *response, pimpl_->InstantiateImportedScene(context, request));
  return grpc::Status::OK;
}

grpc::Status SceneObjectInternal::Export(grpc::ServerContext* context,
                                         const ExportRequest* request,
                                         ExportResponse* response) {
  const stats::ScopedSpan span("SceneObjectInternal/Export", context);

  INTR_ASSIGN_OR_RETURN_GRPC(*response, pimpl_->Export(context, request));
  return grpc::Status::OK;
}

SceneObjectImport::SceneObjectImport(
    std::shared_ptr<SceneObjectImportImpl> pimpl)
    : pimpl_(std::move(pimpl)) {}

grpc::Status SceneObjectImport::ImportSceneObject(
    grpc::ServerContext* context, const ImportSceneObjectRequest* request,
    google::longrunning::Operation* response) {
  const stats::ScopedSpan span("SceneObjectImport/ImportSceneObject", context);

  INTR_ASSIGN_OR_RETURN_GRPC(*response,
                             pimpl_->ImportSceneObject(context, request));
  return grpc::Status::OK;
}

SceneObjectExport::SceneObjectExport(
    std::shared_ptr<SceneObjectImportImpl> pimpl)
    : pimpl_(std::move(pimpl)) {}

grpc::Status SceneObjectExport::Export(grpc::ServerContext* context,
                                       const ExportRequest* request,
                                       ExportResponse* response) {
  const stats::ScopedSpan span("SceneObjectExport/Export", context);

  INTR_ASSIGN_OR_RETURN_GRPC(*response, pimpl_->Export(context, request));
  return grpc::Status::OK;
}

}  // namespace intrinsic
