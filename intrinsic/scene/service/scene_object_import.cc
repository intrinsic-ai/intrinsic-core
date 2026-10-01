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

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/log/die_if_null.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "google/type/color.pb.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/geometry/api/file_io.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/geometry/compatibility/io.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/geometry/proto/geometry_service.pb.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/material.pb.h"
#include "intrinsic/geometry/service/geometry_client.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_service_storage.h"
#include "intrinsic/kubernetes/acl/cc/client_context.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/longrunning/cc/operation_scheduler_interface.h"
#include "intrinsic/longrunning/cc/operations_proxy.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/instantiation/imported_scene_instantiator.h"
#include "intrinsic/scene/mesh/scene_object_from_mesh_file.h"
#include "intrinsic/scene/proto/v1/export.pb.h"
#include "intrinsic/scene/proto/v1/imported_scene.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_import.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_internal.pb.h"
#include "intrinsic/scene/sdf/scene_object_from_sdf.h"
#include "intrinsic/scene/sdf/scene_object_from_zipped_sdf.h"
#include "intrinsic/scene/sdf/scene_object_to_sdf.h"
#include "intrinsic/scene/sdf/scene_object_to_zipped_sdf.h"
#include "intrinsic/scene/sdf/sdf_path_resolver.h"
#include "intrinsic/scene/sdf/sim_spec_from_sdf.h"
#include "intrinsic/scene/service/generate_import_metadata.h"
#include "intrinsic/scene/service/geometry_client_with_cache.h"
#include "intrinsic/scene/service/import_cache.h"
#include "intrinsic/scene/service/post_process_imported_scene.h"
#include "intrinsic/scene/usd/scene_object_from_usd.h"
#include "intrinsic/scene/validate/scene_object_validate_geo.h"
#include "intrinsic/scene/validate/scene_object_validation.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/archive/unzip.h"
#include "intrinsic/util/proto/descriptors.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/proto/collections_component.pb.h"
#include "intrinsic/world/proto/world_fragment.pb.h"
#include "ortools/base/filesystem.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"
#include "ortools/base/path.h"
#include "ortools/base/temp_path.h"
#include "tiny_gltf.h"

namespace intrinsic {

using internal::FindFilesWithExtensionInDirectory;
using internal::FindFilesWithExtensionsInDirectory;
using internal::FindSceneFileToImportInDirectory;
using internal::FindTopmostDirectoryWithContent;
using internal::GetAllSupportedExtensions;
using internal::SceneFileTypes;

namespace {

using ::intrinsic::scene_object::SceneObjectToZippedSdf;
using ::intrinsic::sdf::SceneObjectToSdf;
using ::intrinsic::sdf::SceneObjectToSdfOptions;
using ::intrinsic_proto::geometry::GeometryService;
using ::intrinsic_proto::scene_object::v1::ExportRequest;
using ::intrinsic_proto::scene_object::v1::ExportResponse;
using ::intrinsic_proto::scene_object::v1::GeometryImportResolution;
using ::intrinsic_proto::scene_object::v1::ImportedScene;
using ::intrinsic_proto::scene_object::v1::ImportedSceneObjectInstance;
using ::intrinsic_proto::scene_object::v1::ImportResultMetadata;
using ::intrinsic_proto::scene_object::v1::ImportSceneConfig;
using ::intrinsic_proto::scene_object::v1::ImportSceneObjectMetadata;
using ::intrinsic_proto::scene_object::v1::ImportSceneObjectRequest;
using ::intrinsic_proto::scene_object::v1::ImportSceneObjectResponse;
using ::intrinsic_proto::scene_object::v1::ImportSceneRequest;
using ::intrinsic_proto::scene_object::v1::ImportSceneResult;
using ::intrinsic_proto::scene_object::v1::ImportSceneStatus;
using ::intrinsic_proto::scene_object::v1::InstantiateImportedSceneRequest;
using ::intrinsic_proto::scene_object::v1::InstantiateImportedSceneResponse;
using ::intrinsic_proto::scene_object::v1::InstantiateImportedSceneStatus;
using ::intrinsic_proto::scene_object::v1::SceneFileData;
using ::intrinsic_proto::scene_object::v1::SceneObject;

constexpr absl::string_view kImportObjectDocumentation =
    "https://flowstate.intrinsic.ai/docs/assets/create_new_assets/"
    "create_scene_objects/#upload-new-objects-from-file";

// Some reasonable upper bound on the max size to avoid OOM errors.
constexpr uint64_t kMaxUncompressedArchiveSizeBytes =
    2ULL * 1024 * 1024 * 1024;  // 2 GB

// Limit the depth of directories in the archive to OOM errors.
constexpr int kMaxDirectoryDepth = 20;

const absl::flat_hash_map<SceneFileData::Type, std::string> kFileTypeToExt = {
    // clang-format off
  {SceneFileData::UNSPECIFIED, "zip"},
  {SceneFileData::ZIP_BUNDLE, "zip"},
  {SceneFileData::OBJ, "obj"},
  {SceneFileData::STL, "stl"},
  {SceneFileData::GLTF, "gltf"},
  {SceneFileData::GLB, "glb"},
  {SceneFileData::STEP, "step"},
  {SceneFileData::PARASOLID, "x_t"},
  {SceneFileData::SDF, "sdf"},
  {SceneFileData::USDZ, "usdz"},
  {SceneFileData::USDA, "usda"},
  {SceneFileData::USDC, "usdc"},
  {SceneFileData::USD, "usd"},
    // clang-format on
};

std::string GetExtensionFromFileType(SceneFileData::Type file_type) {
  if (kFileTypeToExt.contains(file_type)) {
    return kFileTypeToExt.at(file_type);
  }
  return "unsupported";
}

absl::flat_hash_set<std::string> SupportedSdfExtensions() {
  return absl::flat_hash_set<std::string>({"sdf"});
}

bool IsSingleSceneObjectImport(const ImportSceneConfig& config) {
  return config.import_structure() !=
         intrinsic_proto::scene_object::v1::IMPORT_STRUCTURE_MULTIPLE;
}

// Returns true for CAD file types whose tessellation depends on the requested
// import resolution and that were requested at FINE resolution.
bool IsFineResolutionCadImport(
    SceneFileData::Type file_type,
    const GeometryImportResolution& geometry_import_resolution) {
  return false;
}

// Creates an ImportedScene with one instance and no updates from a single
// SceneObject.
intrinsic_proto::scene_object::v1::ImportedScene
CreateSingleObjectImportedScene(SceneObject&& scene_object) {
  intrinsic_proto::scene_object::v1::ImportedScene scene;
  std::string object_name = scene_object.name();
  (*scene.mutable_scene_objects()->mutable_objects())[object_name] =
      std::move(scene_object);

  intrinsic_proto::scene_object::v1::ImportedSceneObjectInstance& instance =
      (*scene.mutable_scene_object_instances()
            ->mutable_instances())[object_name];

  *instance.mutable_scene_object_id() = object_name;

  return scene;
}

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

// Applies the user data from the config to each scene object.
void ApplyUserData(
    const ::google::protobuf::Map<std::string, google::protobuf::Any>&
        user_data,
    ::google::protobuf::Map<std::string, SceneObject>* absl_nonnull
        scene_objects) {
  if (user_data.empty()) return;
  for (auto& [_, scene_object] : *scene_objects) {
    scene_object.mutable_user_data()->insert(user_data.begin(),
                                             user_data.end());
  }
}

}  // namespace

// Implementation detail for SceneObjectInternal that handles scheduling
// longrunning operations for different file types.
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

  // Imports scene object located from a directory. The directory could
  // contain arbitrary scene files that needs resolution.
  absl::StatusOr<intrinsic_proto::scene_object::v1::ImportedScene>
  ImportSceneFromDirectory(absl::string_view scene_file_directory,
                           const ImportSceneConfig& import_config);

  // Imports scene from file.
  absl::StatusOr<intrinsic_proto::scene_object::v1::ImportedScene>
  ImportSceneFromFile(absl::string_view scene_file_path,
                      const ImportSceneConfig& import_config);

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

  // Scheduler used for long-running operations
  std::shared_ptr<longrunning::OperationSchedulerInterface> scheduler_;

  // Cache of raw imports without any config applied keyed by the imported
  // file's hash.
  ImportCache raw_import_cache_;
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
  if (request->format() != ExportRequest::EXPORT_FORMAT_SDF) {
    return absl::UnimplementedError("Only SDF format is currently supported.");
  }

  if (!request->has_scene_object()) {
    return absl::InvalidArgumentError("Request does not contain SceneObject.");
  }

  google::protobuf::FileDescriptorSet fds;
  if (request->include_user_data()) {
    // Some SceneObject assets may not have fds set correctly. So always merge
    // fds for `Struct` (a known type) to prevent avoidable serialization
    // errors.
    fds = request->fds();
    MergeFileDescriptorSet<google::protobuf::Struct>(fds);
  }
  SceneObjectToSdfOptions sdf_options = {
      .serialize_user_data = request->include_user_data(),
      .user_data_fds = fds,
  };

  const auto& scene_object = request->scene_object().proto();
  if (request->include_geometries()) {
    if (geometry_library_ == nullptr) {
      return absl::InternalError("Geometry library is not initialized.");
    }
    sdf_options.geometry_deserializer = &geometry_library_->Deserializer();
    ExportResponse response;
    INTR_ASSIGN_OR_RETURN(*response.mutable_zip_archive(),
                          SceneObjectToZippedSdf(scene_object, sdf_options));
    return response;
  }

  INTR_ASSIGN_OR_RETURN(std::string sdf_content,
                        SceneObjectToSdf(scene_object, sdf_options));
  ExportResponse response;
  response.set_sdf(std::move(sdf_content));
  return response;
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
  if (operation_context.GetStopSource().stop_requested()) {
    return absl::CancelledError("Cancelled");
  }
  const auto& file = request.file();
  intrinsic::stats::ScopedSpan span("ImportSceneObject");
  span.AddAttribute("scene_object_name", request.config().scene_object_name());

  // Convert ImportSceneObjectConfig to the internal ImportSceneConfig.
  ImportSceneConfig import_scene_config;
  import_scene_config.set_import_structure(
      intrinsic_proto::scene_object::v1::IMPORT_STRUCTURE_SINGLE);
  if (request.has_config()) {
    if (request.config().has_length_unit_conversion()) {
      *import_scene_config.mutable_length_unit_conversion() =
          request.config().length_unit_conversion();
    }
    if (request.config().has_geometry_operations()) {
      *import_scene_config.mutable_geometry_operations() =
          request.config().geometry_operations();
    }
    if (request.config().has_transform_scene_object()) {
      const auto& transform = request.config().transform_scene_object();
      if (transform.has_uniform_scale()) {
        *import_scene_config.mutable_transform_scene()->mutable_scale() =
            ToVectorProto(eigenmath::Vector3d::Ones() *
                          transform.uniform_scale());
      }
      if (transform.has_rotation()) {
        *import_scene_config.mutable_transform_scene()->mutable_rotation() =
            transform.rotation();
      }
      if (transform.has_translation()) {
        *import_scene_config.mutable_transform_scene()->mutable_translation() =
            transform.translation();
      }
    }
    if (request.config().has_geometry_import_resolution()) {
      *import_scene_config.mutable_geometry_import_resolution() =
          request.config().geometry_import_resolution();
    }
    if (request.config().has_material_properties()) {
      *import_scene_config.mutable_material_properties() =
          request.config().material_properties();
    }
    if (request.config().has_scene_object_name()) {
      import_scene_config.set_scene_object_name(
          request.config().scene_object_name());
    }
  }
  TempPath temp_path("/tmp/");

  auto remove_path_from_error = [path_to_remove =
                                     temp_path.path()](StatusBuilder builder) {
    absl::Status status = builder;
    absl::Status status_with_path_removed = absl::Status(
        status.code(),
        absl::StrReplaceAll(status.message(),
                            {{absl::StrCat(path_to_remove, "/"), ""}}));
    return StatusBuilder(std::move(status_with_path_removed));
  };
  intrinsic_proto::scene_object::v1::ImportedScene imported_scene;
  const std::string file_hash =
      absl::StrCat(absl::Hex(absl::HashOf(file.data())));

  bool fine_cad_import =
      IsFineResolutionCadImport(request.file().file_type(),
                                request.config().geometry_import_resolution());

  // Check if the raw import is cached first.
  // Note for CAD files, since the raw file could produce different results
  // based on the import resolution, we do not rely on the raw import cache when
  // the import resolution is specified as fine.
  if (std::optional<SceneObject> cached_scene_object =
          raw_import_cache_.GetSceneObject(file_hash);
      cached_scene_object.has_value() && !fine_cad_import) {
    imported_scene =
        CreateSingleObjectImportedScene(std::move(*cached_scene_object));
  } else {
    if (request.file().file_type() == SceneFileData::ZIP_BUNDLE ||
        SceneFileData::UNSPECIFIED) {
      INTR_RETURN_IF_ERROR(UnzipData(
          file.data(), temp_path.path(),
          {.strip_common_prefix_directories = true,
           .max_uncompressed_size_bytes = kMaxUncompressedArchiveSizeBytes,
           .max_directory_depth = kMaxDirectoryDepth}))
          << "Failed to unzip scene file data ";
      INTR_ASSIGN_OR_RETURN(
          imported_scene,
          ImportSceneFromDirectory(temp_path.path(), import_scene_config),
          _.With(remove_path_from_error)
              << "This ZIP file cannot be imported.");
    } else {
      const std::string stem = request.config().scene_object_name().empty()
                                   ? "imported_scene_object"
                                   : request.config().scene_object_name();
      const std::string file_name = absl::StrCat(
          stem, ".", GetExtensionFromFileType(request.file().file_type()));
      const std::string temp_file = file::JoinPath(temp_path.path(), file_name);
      INTR_RETURN_IF_ERROR(
          file::SetContents(temp_file, file.data(), file::Defaults()));
      INTR_ASSIGN_OR_RETURN(imported_scene,
                            ImportSceneFromFile(temp_file, import_scene_config),
                            _.With(remove_path_from_error));
    }

    // Validate that only one scene object is imported.
    if (imported_scene.scene_objects().objects().size() != 1) {
      return absl::InternalError(
          absl::StrCat("Expected exactly one scene object to be imported, but "
                       "got ",
                       imported_scene.scene_objects().objects().size()));
    }

    // Cache the newly imported scene object.
    if (!fine_cad_import) {
      const SceneObject& new_scene_object =
          imported_scene.scene_objects().objects().begin()->second;
      INTR_RETURN_IF_ERROR(
          raw_import_cache_.StoreSceneObject(file_hash, new_scene_object));
    } else {
      // Validate the imported scene objects explicitly when not caching.
      for (const auto& [_, scene_object] :
           imported_scene.scene_objects().objects()) {
        INTR_RETURN_IF_ERROR(scene_object::ValidateSceneObject(scene_object));
        INTR_RETURN_IF_ERROR(scene_object::ValidateReferencedGeos(
            scene_object, geometry_library_->Deserializer()));
      }
    }
  }

  // Applies user_data if provided.
  if (!request.config().user_data().empty() &&
      !imported_scene.scene_objects().objects().empty()) {
    ApplyUserData(request.config().user_data(),
                  imported_scene.mutable_scene_objects()->mutable_objects());
  }

  if (operation_context.GetStopSource().stop_requested()) {
    return absl::CancelledError("Cancelled");
  }
  operation_context.AddProgress(0.5);

  INTR_RETURN_IF_ERROR(
      PostProcessImportedScene(operation_context, imported_scene,
                               import_scene_config, *geometry_client_));

  // Validate the modified scene objects.
  // This might not be required in the long run but it's a nice to have at
  // this time to ensure we catch errors in the import config processing.
  for (const auto& [_, scene_object] :
       imported_scene.scene_objects().objects()) {
    INTR_RETURN_IF_ERROR(scene_object::ValidateSceneObject(scene_object));
    INTR_RETURN_IF_ERROR(scene_object::ValidateReferencedGeos(
        scene_object, geometry_library_->Deserializer()));
  }

  INTR_ASSIGN_OR_RETURN(ImportResultMetadata result_metadata,
                        GenerateImportMetadata(
                            imported_scene, geometry_library_->Deserializer()));

  ImportSceneObjectResponse response;
  *response.mutable_scene_object() =
      std::move(imported_scene.scene_objects().objects().begin()->second);
  if (result_metadata.scene_object_to_visual_stats().contains(
          response.scene_object().name())) {
    *response.mutable_visual_stats() =
        result_metadata.scene_object_to_visual_stats().at(
            response.scene_object().name());
  }
  if (result_metadata.scene_object_to_collision_stats().contains(
          response.scene_object().name())) {
    *response.mutable_collision_stats() =
        result_metadata.scene_object_to_collision_stats().at(
            response.scene_object().name());
  }

  operation_context.AddProgress(0.5);
  return response;
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
  intrinsic::stats::ScopedSpan span("ImportSceneOperation");
  span.AddAttribute("display_name", request.import_config().display_name());
  TempPath temp_path("/tmp/");

  auto remove_path_from_error = [path_to_remove =
                                     temp_path.path()](StatusBuilder builder) {
    absl::Status status = builder;
    absl::Status status_with_path_removed = absl::Status(
        status.code(),
        absl::StrReplaceAll(status.message(),
                            {{absl::StrCat(path_to_remove, "/"), ""}}));
    return StatusBuilder(std::move(status_with_path_removed));
  };
  intrinsic_proto::scene_object::v1::ImportedScene imported_scene;

  bool fine_cad_import = IsFineResolutionCadImport(
      request.scene_file_data().file_type(),
      request.import_config().geometry_import_resolution());
  std::string file_hash;
  switch (request.to_be_imported_case()) {
    case ImportSceneRequest::kSceneFileData: {
      file_hash = absl::StrCat(
          absl::Hex(absl::HashOf(request.scene_file_data().data())));
    } break;
    case ImportSceneRequest::kFileToken: {
      file_hash = request.file_token();
    } break;
    default:
      return absl::InvalidArgumentError(
          "ImportSceneRequest has unsupported to_be_imported case.");
  }

  const bool import_multiple_objects =
      !IsSingleSceneObjectImport(request.import_config());
  // First check if the raw import is cached.
  // Note for CAD files, since the raw file could produce different results
  // based on the import resolution, we do not rely on the raw import cache
  // when the import resolution is specified as fine.
  if (!fine_cad_import) {
    if (import_multiple_objects) {
      if (std::optional<ImportedScene> cached_scene =
              raw_import_cache_.GetImportedScene(file_hash);
          cached_scene.has_value()) {
        imported_scene = std::move(*cached_scene);
      }
    } else {
      if (std::optional<SceneObject> cached_scene_object =
              raw_import_cache_.GetSceneObject(file_hash);
          cached_scene_object.has_value()) {
        imported_scene =
            CreateSingleObjectImportedScene(std::move(*cached_scene_object));
      }
    }
  }

  if (request.has_file_token() &&
      imported_scene.scene_objects().objects().empty()) {
    return absl::NotFoundError(
        "File token provided but no existing import result found. Please "
        "import with scene_file_data instead.");
  }

  // Import actual file if not cached.
  if (imported_scene.scene_objects().objects().empty()) {
    if (request.scene_file_data().file_type() == SceneFileData::ZIP_BUNDLE ||
        request.scene_file_data().file_type() == SceneFileData::UNSPECIFIED) {
      INTR_RETURN_IF_ERROR(UnzipData(
          request.scene_file_data().data(), temp_path.path(),
          {.strip_common_prefix_directories = true,
           .max_uncompressed_size_bytes = kMaxUncompressedArchiveSizeBytes,
           .max_directory_depth = kMaxDirectoryDepth}))
          << "Failed to unzip scene file data ";
      INTR_ASSIGN_OR_RETURN(
          imported_scene,
          ImportSceneFromDirectory(temp_path.path(), request.import_config()),
          _.With(remove_path_from_error)
              << "This ZIP file cannot be imported.");
    } else {
      const std::string stem =
          request.import_config().scene_object_name().empty()
              ? "imported_scene_object"
              : request.import_config().scene_object_name();
      const std::string file_name = absl::StrCat(
          stem, ".",
          GetExtensionFromFileType(request.scene_file_data().file_type()));
      const std::string temp_file = file::JoinPath(temp_path.path(), file_name);
      INTR_RETURN_IF_ERROR(file::SetContents(
          temp_file, request.scene_file_data().data(), file::Defaults()));
      INTR_ASSIGN_OR_RETURN(
          imported_scene,
          ImportSceneFromFile(temp_file, request.import_config()),
          _.With(remove_path_from_error));
    }
    // Cache based on the import structure.
    if (!fine_cad_import) {
      if (import_multiple_objects) {
        INTR_RETURN_IF_ERROR(
            raw_import_cache_.StoreImportedScene(file_hash, imported_scene));
      } else {
        INTR_RETURN_IF_ERROR(raw_import_cache_.StoreSceneObject(
            file_hash,
            imported_scene.scene_objects().objects().begin()->second));
      }
    } else {
      // Validate the imported scene objects explicitly when not caching.
      for (const auto& [_, scene_object] :
           imported_scene.scene_objects().objects()) {
        INTR_RETURN_IF_ERROR(scene_object::ValidateSceneObject(scene_object));
        INTR_RETURN_IF_ERROR(scene_object::ValidateReferencedGeos(
            scene_object, geometry_library_->Deserializer()));
      }
    }
  }

  // Applies user_data if provided.
  if (!request.import_config().user_data().empty() &&
      !imported_scene.scene_objects().objects().empty()) {
    ApplyUserData(request.import_config().user_data(),
                  imported_scene.mutable_scene_objects()->mutable_objects());
  }

  // Validate the imported scene objects.
  for (const auto& [_, scene_object] :
       imported_scene.scene_objects().objects()) {
    INTR_RETURN_IF_ERROR(scene_object::ValidateSceneObject(scene_object));
    INTR_RETURN_IF_ERROR(scene_object::ValidateReferencedGeos(
        scene_object, geometry_library_->Deserializer()));
  }

  operation_context.AddProgress(0.5);

  ImportSceneResult result;
  if (request.has_import_config()) {
    INTR_RETURN_IF_ERROR(
        PostProcessImportedScene(operation_context, imported_scene,
                                 request.import_config(), *geometry_client_));

    // Validate the modified scene objects.
    // This might not be required in the long run but it's a nice to have at
    // this time to ensure we catch errors in the import config processing.
    for (const auto& [_, scene_object] :
         imported_scene.scene_objects().objects()) {
      INTR_RETURN_IF_ERROR(scene_object::ValidateSceneObject(scene_object));
      INTR_RETURN_IF_ERROR(scene_object::ValidateReferencedGeos(
          scene_object, geometry_library_->Deserializer()));
    }
  }

  INTR_ASSIGN_OR_RETURN(*result.mutable_result_metadata(),
                        GenerateImportMetadata(
                            imported_scene, geometry_library_->Deserializer()));
  *result.mutable_scene() = std::move(imported_scene);
  result.mutable_result_metadata()->set_file_token(file_hash);

  operation_context.AddProgress(0.5);
  return result;
}

// Import scene object located from a directory. The directory could contain
// arbitrary scene files that needs resolution.
absl::StatusOr<intrinsic_proto::scene_object::v1::ImportedScene>
SceneObjectImportImpl::ImportSceneFromDirectory(
    absl::string_view scene_file_directory,
    const ImportSceneConfig& import_config) {
  INTR_ASSIGN_OR_RETURN(const auto find_scene_file_result,
                        FindSceneFileToImportInDirectory(scene_file_directory));
  const auto [scene_file, scene_file_type] = find_scene_file_result;

  switch (scene_file_type) {
    case SceneFileTypes::SDF: {
      INTR_ASSIGN_OR_RETURN(
          auto sdf_object,
          scene_object::SceneObjectFromSdfInDirectory(
              scene_file_directory, geometry_library_->Serializer()));
      return CreateSingleObjectImportedScene(std::move(sdf_object));
    }
    case SceneFileTypes::USD: {
      INTR_ASSIGN_OR_RETURN(auto scene_object,
                            usd::SceneObjectFromUsdFile(
                                scene_file, geometry_library_->Serializer()));
      return CreateSingleObjectImportedScene(std::move(scene_object));
    }
    case SceneFileTypes::CAD: {
        return absl::UnimplementedError("CAD scene import is not supported.");
    }
    case SceneFileTypes::MESH: {
      INTR_ASSIGN_OR_RETURN(
          auto scene_object,
          scene_object::SceneObjectFromMeshFile(
              scene_file, std::nullopt, geometry_library_->Serializer()));
      return CreateSingleObjectImportedScene(std::move(scene_object));
    }
  }

  return absl::InternalError("Failed to import scene from directory");
}

// Imports scene object from file.
absl::StatusOr<intrinsic_proto::scene_object::v1::ImportedScene>
SceneObjectImportImpl::ImportSceneFromFile(
    absl::string_view scene_file_path, const ImportSceneConfig& import_config) {
  INTR_RETURN_IF_ERROR(file::Exists(scene_file_path, file::Defaults()));
  const std::string ext =
      absl::AsciiStrToLower(file::Extension(scene_file_path));
  if (geo::SupportedMeshExtensions().contains(ext)) {
    INTR_ASSIGN_OR_RETURN(
        auto scene_object,
        scene_object::SceneObjectFromMeshFile(scene_file_path, std::nullopt,
                                              geometry_library_->Serializer()));
    return CreateSingleObjectImportedScene(std::move(scene_object));
  } else if (SupportedSdfExtensions().contains(ext)) {
    INTR_ASSIGN_OR_RETURN(auto scene_object,
                          scene_object::SceneObjectFromSdfFile(
                              scene_file_path, sdf::SdfPathResolver,
                              geometry_library_->Serializer()));
    return CreateSingleObjectImportedScene(std::move(scene_object));
  } else if (usd::SupportedUsdExtensions().contains(ext)) {
    std::string file_contents;
    INTR_RETURN_IF_ERROR(
        file::GetContents(scene_file_path, &file_contents, file::Defaults()));
    INTR_ASSIGN_OR_RETURN(
        auto scene_object,
        usd::SceneObjectFromUsdFileData(scene_file_path, file_contents,
                                        geometry_library_->Serializer()));
    return CreateSingleObjectImportedScene(std::move(scene_object));
  }

  return absl::UnimplementedError(
      absl::Substitute("Cannot import scene object from unsupported file $0",
                       file::Basename(scene_file_path)));
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
    std::unique_ptr<intrinsic_proto::geometry::GeometryService::StubInterface>
        geometry_service_stub,
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
    grpc::ServerContext* context,
    const intrinsic_proto::scene_object::v1::ImportSceneObjectRequest* request,
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

namespace internal {

absl::StatusOr<std::vector<std::string>> FindFilesWithExtensionInDirectory(
    absl::string_view directory, absl::string_view ext) {
  std::vector<std::string> mesh_files;
  INTR_RETURN_IF_ERROR(file::Match(
      file::JoinPath(directory, absl::StrCat("*.", absl::AsciiStrToLower(ext))),
      &mesh_files, file::Defaults()));
  INTR_RETURN_IF_ERROR(file::Match(
      file::JoinPath(directory, absl::StrCat("*.", absl::AsciiStrToUpper(ext))),
      &mesh_files, file::Defaults()));
  // Sort, to keep the list deterministic
  std::sort(mesh_files.begin(), mesh_files.end());
  return mesh_files;
}

absl::StatusOr<std::vector<std::string>> FindFilesWithExtensionsInDirectory(
    absl::string_view directory, const absl::flat_hash_set<std::string>& exts) {
  std::vector<std::string> all_matches;
  for (const std::string& ext : exts) {
    INTR_ASSIGN_OR_RETURN(auto files,
                          FindFilesWithExtensionInDirectory(directory, ext));
    all_matches.insert(all_matches.end(), files.begin(), files.end());
  }
  // Sort, to keep the list deterministic
  std::sort(all_matches.begin(), all_matches.end());
  return all_matches;
}

absl::StatusOr<std::string> FindTopmostDirectoryWithContent(
    absl::string_view scene_file_directory) {
  std::string root_dir = std::string(scene_file_directory);
  while (true) {
    std::vector<std::string> relevant_entries;
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(root_dir, ec)) {
      if (ec) break;
      const std::string filename = entry.path().filename().string();
      if (filename == ".DS_Store" || filename == "__MACOSX") {
        continue;
      }
      relevant_entries.push_back(entry.path().string());
    }
    if (ec) {
      return absl::InternalError(
          absl::StrCat("Error iterating directory: ", root_dir));
    }

    if (relevant_entries.size() == 1 &&
        std::filesystem::is_directory(relevant_entries[0], ec) && !ec) {
      root_dir = relevant_entries[0];
    } else {
      break;
    }
  }
  return root_dir;
}

absl::StatusOr<std::pair<std::string, SceneFileTypes>>
FindSceneFileToImportInDirectory(absl::string_view scene_file_directory) {
  // We look for a scene file that we can import, somewhere within the
  // directory. When there are multiple files that we can import, we pick one
  // according to these rules:
  // 1. We only consider files at the topmost dir (ignoring any empty parent
  //    dirs, which are common in zip-files)
  // 2. The priority order when there are multiple file-types is: SDF, CAD, USD,
  //    then MESH type files.
  // 3. When there are multiple of the same file-type (like two SDF files), we
  //    either pick one (in the case of USD) or return with an error.

  // 1. Find the root/topmost directory for import
  INTR_ASSIGN_OR_RETURN(std::string root_dir,
                        FindTopmostDirectoryWithContent(scene_file_directory));

  // 2 - Look for files to import, in priority order.

  // SDF
  INTR_ASSIGN_OR_RETURN(
      std::vector<std::string> sdf_files,
      FindFilesWithExtensionsInDirectory(root_dir, SupportedSdfExtensions()));
  if (!sdf_files.empty()) {
    if (sdf_files.size() > 1) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Found multiple SDF files in the directory: $0. We only support "
          "importing one file at a time. Please remove the others.",
          absl::StrJoin(sdf_files, ", ")));
    }
    return std::make_pair(sdf_files[0], SceneFileTypes::SDF);
  }
  // USD
  INTR_ASSIGN_OR_RETURN(std::vector<std::string> usd_files,
                        FindFilesWithExtensionsInDirectory(
                            root_dir, usd::SupportedUsdExtensions()));
  if (!usd_files.empty()) {
    // If there are multiple USD files, we log a warning and choose the first
    // one, in sorted order. This is usually the correct choice. For example,
    // "robot.usd" will be chosen before "robot_variantb.usd".
    if (usd_files.size() > 1) {
      LOG(WARNING)
          << "Found multiple USD files at the root of the folder. Choosing "
          << usd_files[0]
          << ". If you would like a certain file chosen, please remove the "
             "others. Found: "
          << absl::StrJoin(usd_files, ", ");
    }
    return std::make_pair(usd_files[0], SceneFileTypes::USD);
  }
  // MESH
  INTR_ASSIGN_OR_RETURN(std::vector<std::string> mesh_files,
                        FindFilesWithExtensionsInDirectory(
                            root_dir, geo::SupportedMeshExtensions()));
  if (!mesh_files.empty()) {
    if (mesh_files.size() > 1) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Found multiple mesh files in the directory: $0. We only support "
          "importing one file at a time. Please remove the others.",
          absl::StrJoin(mesh_files, ", ")));
    }
    return std::make_pair(mesh_files[0], SceneFileTypes::MESH);
  }

  return absl::NotFoundError(absl::Substitute(
      "Could not find a scene file to import in the folder. Was "
      "expecting a file with one of these extensions, placed at the "
      "folder root: $0. See $1 for more information.",
      absl::StrJoin(GetAllSupportedExtensions(), ", "),
      kImportObjectDocumentation));
}

std::vector<std::string> GetAllSupportedExtensions() {
  std::vector<std::string> all_supported_extensions;
  all_supported_extensions.insert(all_supported_extensions.end(),
                                  SupportedSdfExtensions().begin(),
                                  SupportedSdfExtensions().end());
  all_supported_extensions.insert(all_supported_extensions.end(),
                                  usd::SupportedUsdExtensions().begin(),
                                  usd::SupportedUsdExtensions().end());
  all_supported_extensions.insert(all_supported_extensions.end(),
                                  geo::SupportedMeshExtensions().begin(),
                                  geo::SupportedMeshExtensions().end());
  std::sort(all_supported_extensions.begin(), all_supported_extensions.end());
  return all_supported_extensions;
}

}  // namespace internal

}  // namespace intrinsic
