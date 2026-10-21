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

#ifndef INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_IMPORTER_H_
#define INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_IMPORTER_H_

#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/scene/proto/v1/imported_scene.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_import.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_internal.pb.h"
#include "intrinsic/scene/service/geometry_client_with_cache.h"
#include "intrinsic/scene/service/import_cache.h"

namespace intrinsic {

// Pure C++ domain logic for importing scene files (SDF, USD, CAD, OBJ, STL,
// GLTF, GLB, etc.) into SceneObject and ImportedScene proto structures.
class SceneObjectImporter {
 public:
  struct Options {
    // Geometry library used to serialize and deserialize geometry data.
    // Required.
    GeometryLibrary* geometry_library = nullptr;

    // Client used to post-process imported geometry. Required.
    GeometryClientWithProcessGeometryCache* geometry_client = nullptr;

    // Cache of raw imports keyed by file hash. Optional. If nullptr, an
    // internal cache will be created and used.
    ImportCache* raw_import_cache = nullptr;
  };

  explicit SceneObjectImporter(const Options& options);

  ~SceneObjectImporter() = default;

  // Imports a scene object from raw file data (or zip bundle).
  // Validates that exactly one scene object is imported.
  absl::StatusOr<intrinsic_proto::scene_object::v1::ImportSceneObjectResponse>
  ImportSceneObject(
      const intrinsic_proto::scene_object::v1::ImportSceneObjectRequest&
          request,
      longrunning::OperationContext& operation_context);

  // Overload of ImportSceneObject using a default OperationContext.
  absl::StatusOr<intrinsic_proto::scene_object::v1::ImportSceneObjectResponse>
  ImportSceneObject(
      const intrinsic_proto::scene_object::v1::ImportSceneObjectRequest&
          request);

  // Imports a scene (single or multiple objects) from SceneFileData or file
  // token.
  absl::StatusOr<intrinsic_proto::scene_object::v1::ImportSceneResult>
  ImportScene(
      const intrinsic_proto::scene_object::v1::ImportSceneRequest& request,
      longrunning::OperationContext& operation_context);

  // Overload of ImportScene using a default OperationContext.
  absl::StatusOr<intrinsic_proto::scene_object::v1::ImportSceneResult>
  ImportScene(
      const intrinsic_proto::scene_object::v1::ImportSceneRequest& request);

  // Imports a scene from a directory on disk.
  absl::StatusOr<intrinsic_proto::scene_object::v1::ImportedScene>
  ImportSceneFromDirectory(
      absl::string_view scene_file_directory,
      const intrinsic_proto::scene_object::v1::ImportSceneConfig&
          import_config);

  // Imports a scene from a file on disk.
  absl::StatusOr<intrinsic_proto::scene_object::v1::ImportedScene>
  ImportSceneFromFile(
      absl::string_view scene_file_path,
      const intrinsic_proto::scene_object::v1::ImportSceneConfig&
          import_config);

  // Returns the active import cache.
  ImportCache& raw_import_cache();

 private:
  Options options_;
  std::unique_ptr<ImportCache> owned_import_cache_;
  ImportCache& cache_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_IMPORTER_H_
