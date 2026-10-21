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

#include "intrinsic/scene/service/scene_object_importer.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/map.h"
#include "intrinsic/geometry/api/file_io.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/mesh/scene_object_from_mesh_file.h"
#include "intrinsic/scene/proto/v1/imported_scene.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_import.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_internal.pb.h"
#include "intrinsic/scene/sdf/scene_object_from_sdf.h"
#include "intrinsic/scene/sdf/scene_object_from_zipped_sdf.h"
#include "intrinsic/scene/sdf/sdf_path_resolver.h"
#include "intrinsic/scene/service/generate_import_metadata.h"
#include "intrinsic/scene/service/geometry_client_with_cache.h"
#include "intrinsic/scene/service/import_cache.h"
#include "intrinsic/scene/service/post_process_imported_scene.h"
#include "intrinsic/scene/service/scene_file_discovery.h"
#include "intrinsic/scene/usd/scene_object_from_usd.h"
#include "intrinsic/scene/validate/scene_object_validate_geo.h"
#include "intrinsic/scene/validate/scene_object_validation.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/archive/unzip.h"
#include "intrinsic/util/status/status_macros.h"
#include "ortools/base/filesystem.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"
#include "ortools/base/path.h"
#include "ortools/base/temp_path.h"

namespace intrinsic {

namespace {

using ::intrinsic::internal::FindSceneFileToImportInDirectory;
using ::intrinsic::internal::SceneFileTypes;
using ::intrinsic::internal::SupportedSdfExtensions;
using ::intrinsic::longrunning::OperationContext;
using ::intrinsic::scene_object::SceneObjectFromMeshFile;
using ::intrinsic::scene_object::SceneObjectFromSdfFile;
using ::intrinsic::scene_object::SceneObjectFromSdfInDirectory;
using ::intrinsic::scene_object::ValidateReferencedGeos;
using ::intrinsic::scene_object::ValidateSceneObject;
using ::intrinsic::sdf::SdfPathResolver;
using ::intrinsic::usd::SceneObjectFromUsdFile;
using ::intrinsic::usd::SceneObjectFromUsdFileData;
using ::intrinsic::usd::SupportedUsdExtensions;
using ::intrinsic_proto::scene_object::v1::GeometryImportResolution;
using ::intrinsic_proto::scene_object::v1::IMPORT_STRUCTURE_MULTIPLE;
using ::intrinsic_proto::scene_object::v1::IMPORT_STRUCTURE_SINGLE;
using ::intrinsic_proto::scene_object::v1::ImportedScene;
using ::intrinsic_proto::scene_object::v1::ImportedSceneObjectInstance;
using ::intrinsic_proto::scene_object::v1::ImportSceneConfig;
using ::intrinsic_proto::scene_object::v1::ImportSceneObjectConfig;
using ::intrinsic_proto::scene_object::v1::ImportSceneObjectRequest;
using ::intrinsic_proto::scene_object::v1::ImportSceneObjectResponse;
using ::intrinsic_proto::scene_object::v1::ImportSceneRequest;
using ::intrinsic_proto::scene_object::v1::ImportSceneResult;
using ::intrinsic_proto::scene_object::v1::SceneFileData;
using ::intrinsic_proto::scene_object::v1::SceneObject;

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

bool IsSingleSceneObjectImport(const ImportSceneConfig& config) {
  return config.import_structure() != IMPORT_STRUCTURE_MULTIPLE;
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
ImportedScene CreateSingleObjectImportedScene(SceneObject&& scene_object) {
  ImportedScene scene;
  std::string object_name = scene_object.name();
  (*scene.mutable_scene_objects()->mutable_objects())[object_name] =
      std::move(scene_object);

  ImportedSceneObjectInstance& instance =
      (*scene.mutable_scene_object_instances()
            ->mutable_instances())[object_name];
  *instance.mutable_scene_object_id() = object_name;

  return scene;
}

// Applies the user data from the config to each scene object.
void ApplyUserData(
    const google::protobuf::Map<std::string, google::protobuf::Any>& user_data,
    google::protobuf::Map<std::string, SceneObject>* absl_nonnull
        scene_objects) {
  if (user_data.empty()) return;
  for (auto& [_, scene_object] : *scene_objects) {
    scene_object.mutable_user_data()->insert(user_data.begin(),
                                             user_data.end());
  }
}

}  // namespace

SceneObjectImporter::SceneObjectImporter(const Options& options)
    : options_(options),
      owned_import_cache_(options.raw_import_cache == nullptr
                              ? std::make_unique<ImportCache>()
                              : nullptr),
      cache_(options.raw_import_cache != nullptr ? *options.raw_import_cache
                                                 : *owned_import_cache_) {
  CHECK_NE(options_.geometry_library, nullptr);
  CHECK_NE(options_.geometry_client, nullptr);
}

ImportCache& SceneObjectImporter::raw_import_cache() { return cache_; }

absl::StatusOr<ImportSceneObjectResponse>
SceneObjectImporter::ImportSceneObject(
    const ImportSceneObjectRequest& request) {
  OperationContext default_context;
  return ImportSceneObject(request, default_context);
}

absl::StatusOr<ImportSceneObjectResponse>
SceneObjectImporter::ImportSceneObject(const ImportSceneObjectRequest& request,
                                       OperationContext& operation_context) {
  if (operation_context.GetStopSource().stop_requested()) {
    return absl::CancelledError("Cancelled");
  }
  if (!request.has_file()) {
    return absl::InvalidArgumentError(
        "Request does not contain file data for importing a scene object.");
  }
  const auto& file = request.file();
  stats::ScopedSpan span("ImportSceneObject");
  span.AddAttribute("scene_object_name", request.config().scene_object_name());

  // Convert ImportSceneObjectConfig to the internal ImportSceneConfig.
  ImportSceneConfig import_scene_config;
  import_scene_config.set_import_structure(IMPORT_STRUCTURE_SINGLE);
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
  ImportedScene imported_scene;
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
          cache_.GetSceneObject(file_hash);
      cached_scene_object.has_value() && !fine_cad_import) {
    imported_scene =
        CreateSingleObjectImportedScene(std::move(*cached_scene_object));
  } else {
    if (request.file().file_type() == SceneFileData::ZIP_BUNDLE ||
        request.file().file_type() == SceneFileData::UNSPECIFIED) {
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
          cache_.StoreSceneObject(file_hash, new_scene_object));
    } else {
      // Validate the imported scene objects explicitly when not caching.
      for (const auto& [_, scene_object] :
           imported_scene.scene_objects().objects()) {
        INTR_RETURN_IF_ERROR(ValidateSceneObject(scene_object));
        INTR_RETURN_IF_ERROR(ValidateReferencedGeos(
            scene_object, options_.geometry_library->Deserializer()));
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
                               import_scene_config, *options_.geometry_client));

  // Validate the modified scene objects.
  // This might not be required in the long run but it's a nice to have at
  // this time to ensure we catch errors in the import config processing.
  for (const auto& [_, scene_object] :
       imported_scene.scene_objects().objects()) {
    INTR_RETURN_IF_ERROR(ValidateSceneObject(scene_object));
    INTR_RETURN_IF_ERROR(ValidateReferencedGeos(
        scene_object, options_.geometry_library->Deserializer()));
  }

  INTR_ASSIGN_OR_RETURN(
      auto result_metadata,
      GenerateImportMetadata(imported_scene,
                             options_.geometry_library->Deserializer()));

  if (imported_scene.scene_objects().objects().empty()) {
    return absl::InternalError("Imported scene contains no objects.");
  }

  ImportSceneObjectResponse response;
  *response.mutable_scene_object() =
      std::move(imported_scene.mutable_scene_objects()
                    ->mutable_objects()
                    ->begin()
                    ->second);
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

absl::StatusOr<ImportSceneResult> SceneObjectImporter::ImportScene(
    const ImportSceneRequest& request) {
  OperationContext default_context;
  return ImportScene(request, default_context);
}

absl::StatusOr<ImportSceneResult> SceneObjectImporter::ImportScene(
    const ImportSceneRequest& request, OperationContext& operation_context) {
  stats::ScopedSpan span("ImportSceneOperation");
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
  ImportedScene imported_scene;

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
              cache_.GetImportedScene(file_hash);
          cached_scene.has_value()) {
        imported_scene = std::move(*cached_scene);
      }
    } else {
      if (std::optional<SceneObject> cached_scene_object =
              cache_.GetSceneObject(file_hash);
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
    if (!request.has_scene_file_data()) {
      return absl::InvalidArgumentError(
          "Request does not contain valid scene file data for import into a "
          "scene object.");
    }
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
            cache_.StoreImportedScene(file_hash, imported_scene));
      } else {
        if (imported_scene.scene_objects().objects().empty()) {
          return absl::InternalError("Imported scene contains no objects.");
        }
        INTR_RETURN_IF_ERROR(cache_.StoreSceneObject(
            file_hash,
            imported_scene.scene_objects().objects().begin()->second));
      }
    } else {
      // Validate the imported scene objects explicitly when not caching.
      for (const auto& [_, scene_object] :
           imported_scene.scene_objects().objects()) {
        INTR_RETURN_IF_ERROR(ValidateSceneObject(scene_object));
        INTR_RETURN_IF_ERROR(ValidateReferencedGeos(
            scene_object, options_.geometry_library->Deserializer()));
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
    INTR_RETURN_IF_ERROR(ValidateSceneObject(scene_object));
    INTR_RETURN_IF_ERROR(ValidateReferencedGeos(
        scene_object, options_.geometry_library->Deserializer()));
  }

  operation_context.AddProgress(0.5);

  ImportSceneResult result;
  if (request.has_import_config()) {
    INTR_RETURN_IF_ERROR(PostProcessImportedScene(
        operation_context, imported_scene, request.import_config(),
        *options_.geometry_client));

    // Validate the modified scene objects.
    // This might not be required in the long run but it's a nice to have at
    // this time to ensure we catch errors in the import config processing.
    for (const auto& [_, scene_object] :
         imported_scene.scene_objects().objects()) {
      INTR_RETURN_IF_ERROR(ValidateSceneObject(scene_object));
      INTR_RETURN_IF_ERROR(ValidateReferencedGeos(
          scene_object, options_.geometry_library->Deserializer()));
    }
  }

  INTR_ASSIGN_OR_RETURN(
      *result.mutable_result_metadata(),
      GenerateImportMetadata(imported_scene,
                             options_.geometry_library->Deserializer()));
  *result.mutable_scene() = std::move(imported_scene);
  result.mutable_result_metadata()->set_file_token(file_hash);

  operation_context.AddProgress(0.5);
  return result;
}

// Import scene object located from a directory. The directory could contain
// arbitrary scene files that needs resolution.
absl::StatusOr<ImportedScene> SceneObjectImporter::ImportSceneFromDirectory(
    absl::string_view scene_file_directory,
    const ImportSceneConfig& import_config) {
  INTR_ASSIGN_OR_RETURN(const auto find_scene_file_result,
                        FindSceneFileToImportInDirectory(scene_file_directory));
  const auto [scene_file, scene_file_type] = find_scene_file_result;

  switch (scene_file_type) {
    case SceneFileTypes::SDF: {
      INTR_ASSIGN_OR_RETURN(
          auto sdf_object,
          SceneObjectFromSdfInDirectory(
              scene_file_directory, options_.geometry_library->Serializer()));
      return CreateSingleObjectImportedScene(std::move(sdf_object));
    }
    case SceneFileTypes::USD: {
      INTR_ASSIGN_OR_RETURN(
          auto scene_object,
          SceneObjectFromUsdFile(scene_file,
                                 options_.geometry_library->Serializer()));
      return CreateSingleObjectImportedScene(std::move(scene_object));
    }
    case SceneFileTypes::CAD: {
        return absl::UnimplementedError("CAD scene import is not supported.");
    }
    case SceneFileTypes::MESH: {
      INTR_ASSIGN_OR_RETURN(
          auto scene_object,
          SceneObjectFromMeshFile(scene_file, std::nullopt,
                                  options_.geometry_library->Serializer()));
      return CreateSingleObjectImportedScene(std::move(scene_object));
    }
  }

  return absl::InternalError("Failed to import scene from directory");
}

// Imports scene object from file.
absl::StatusOr<ImportedScene> SceneObjectImporter::ImportSceneFromFile(
    absl::string_view scene_file_path, const ImportSceneConfig& import_config) {
  INTR_RETURN_IF_ERROR(file::Exists(scene_file_path, file::Defaults()));
  const std::string ext =
      absl::AsciiStrToLower(file::Extension(scene_file_path));
  if (geo::SupportedMeshExtensions().contains(ext)) {
    INTR_ASSIGN_OR_RETURN(
        auto scene_object,
        SceneObjectFromMeshFile(scene_file_path, std::nullopt,
                                options_.geometry_library->Serializer()));
    return CreateSingleObjectImportedScene(std::move(scene_object));
  } else if (SupportedSdfExtensions().contains(ext)) {
    INTR_ASSIGN_OR_RETURN(
        auto scene_object,
        SceneObjectFromSdfFile(scene_file_path, SdfPathResolver,
                               options_.geometry_library->Serializer()));
    return CreateSingleObjectImportedScene(std::move(scene_object));
  } else if (SupportedUsdExtensions().contains(ext)) {
    std::string file_contents;
    INTR_RETURN_IF_ERROR(
        file::GetContents(scene_file_path, &file_contents, file::Defaults()));
    INTR_ASSIGN_OR_RETURN(
        auto scene_object,
        SceneObjectFromUsdFileData(scene_file_path, file_contents,
                                   options_.geometry_library->Serializer()));
    return CreateSingleObjectImportedScene(std::move(scene_object));
  }

  return absl::UnimplementedError(
      absl::Substitute("Cannot import scene object from unsupported file $0",
                       file::Basename(scene_file_path)));
}

}  // namespace intrinsic
