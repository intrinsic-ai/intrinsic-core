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

#ifndef INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_IMPORT_H_
#define INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_IMPORT_H_

#include <memory>

#include "absl/status/statusor.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/any.pb.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/geometry/proto/geometry_service.pb.h"
#include "intrinsic/longrunning/cc/operation_scheduler_interface.h"
#include "intrinsic/longrunning/cc/operations_proxy.h"
#include "intrinsic/scene/instantiation/imported_scene_instantiator.h"
#include "intrinsic/scene/proto/v1/export.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_export.grpc.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_export.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_import.grpc.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_import.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_internal.grpc.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_internal.pb.h"

namespace intrinsic {

class SceneObjectImportImpl;
class SceneObjectImport;
class SceneObjectInternal;
class SceneObjectExport;

struct SceneObjectImportServices {
  std::unique_ptr<SceneObjectInternal> soii_service;
  std::unique_ptr<SceneObjectImport> soi_service;
  std::unique_ptr<SceneObjectExport> soe_service;
};

// Creates both the internal and external SceneObjectImport services in a single
// struct.
absl::StatusOr<SceneObjectImportServices> CreateSceneObjectImportServices(
    std::unique_ptr<intrinsic_proto::geometry::GeometryService::StubInterface>
        geometry_service_stub,
    std::shared_ptr<longrunning::OperationsProxy> geometry_operations_proxy,
    std::unique_ptr<ImportedSceneInstantiator> imported_scene_instantiator,
    std::shared_ptr<longrunning::OperationSchedulerInterface> scheduler);

class SceneObjectInternal
    : public intrinsic_proto::scene_object::v1::SceneObjectInternal::Service {
 public:
  SceneObjectInternal() = delete;
  explicit SceneObjectInternal(std::shared_ptr<SceneObjectImportImpl> pimpl);

  ~SceneObjectInternal() override;

  grpc::Status ImportScene(
      grpc::ServerContext* context,
      const intrinsic_proto::scene_object::v1::ImportSceneRequest* request,
      google::longrunning::Operation* response) override;

  grpc::Status InstantiateImportedScene(
      grpc::ServerContext* context,
      const intrinsic_proto::scene_object::v1::InstantiateImportedSceneRequest*
          request,
      google::longrunning::Operation* response) override;

  grpc::Status Export(
      grpc::ServerContext* context,
      const intrinsic_proto::scene_object::v1::ExportRequest* request,
      intrinsic_proto::scene_object::v1::ExportResponse* response) override;

 private:
  std::shared_ptr<SceneObjectImportImpl> pimpl_;
};

class SceneObjectImport
    : public intrinsic_proto::scene_object::v1::SceneObjectImport::Service {
 public:
  SceneObjectImport() = delete;
  explicit SceneObjectImport(std::shared_ptr<SceneObjectImportImpl> pimpl);

  ~SceneObjectImport() override = default;

  grpc::Status ImportSceneObject(
      grpc::ServerContext* context,
      const intrinsic_proto::scene_object::v1::ImportSceneObjectRequest*
          request,
      google::longrunning::Operation* response) override;

 private:
  std::shared_ptr<SceneObjectImportImpl> pimpl_;
};

class SceneObjectExport
    : public intrinsic_proto::scene_object::v1::SceneObjectExport::Service {
 public:
  SceneObjectExport() = delete;
  explicit SceneObjectExport(std::shared_ptr<SceneObjectImportImpl> pimpl);

  ~SceneObjectExport() override = default;

  grpc::Status Export(
      grpc::ServerContext* context,
      const intrinsic_proto::scene_object::v1::ExportRequest* request,
      intrinsic_proto::scene_object::v1::ExportResponse* response) override;

 private:
  std::shared_ptr<SceneObjectImportImpl> pimpl_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_IMPORT_H_
