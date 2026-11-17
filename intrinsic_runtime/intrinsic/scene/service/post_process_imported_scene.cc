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

#include "intrinsic/scene/service/post_process_imported_scene.h"

#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/time/time.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "grpcpp/client_context.h"
#include "intrinsic/geometry/api/apply_material_properties.h"
#include "intrinsic/geometry/api/renderable_generation.h"
#include "intrinsic/geometry/compatibility/io.h"
#include "intrinsic/geometry/processing/pipeline_config.pb.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/renderable.pb.h"
#include "intrinsic/geometry/proto/v1/geometry.pb.h"
#include "intrinsic/geometry/proto/v1/material.pb.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/scene/processing/merge_scene_object_links.h"
#include "intrinsic/scene/processing/scale_imported_scene.h"
#include "intrinsic/scene/processing/transform_imported_scene.h"
#include "intrinsic/scene/service/geometry_client_with_cache.h"
#include "intrinsic/util/proto/pb_hash.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/world/geometry_types.h"

namespace intrinsic {
namespace {

using ::intrinsic_proto::geometry::v1::MaterialProperties;
using ::intrinsic_proto::scene_object::v1::GeometryOperations;
using ::intrinsic_proto::scene_object::v1::ImportedScene;
using ::intrinsic_proto::scene_object::v1::ImportedSceneObjectInstance;
using ::intrinsic_proto::scene_object::v1::ImportSceneConfig;
using ::intrinsic_proto::scene_object::v1::SceneObject;

bool IsSingleSceneObjectImport(const ImportSceneConfig& config) {
  return config.import_structure() !=
         intrinsic_proto::scene_object::v1::IMPORT_STRUCTURE_MULTIPLE;
}

absl::Status RemoveGeometryTypesFromSceneObject(
    SceneObject& scene_object,
    const google::protobuf::RepeatedPtrField<std::string>& remove_types) {
  if (remove_types.empty()) {
    return absl::OkStatus();
  }
  for (auto& entity : *scene_object.mutable_entities()) {
    if (!entity.has_link()) continue;
    if (!entity.link().has_geometry_component()) continue;
    for (const std::string& remove_type : remove_types) {
      entity.mutable_link()
          ->mutable_geometry_component()
          ->mutable_named_geometries()
          ->erase(remove_type);
    }
  }
  return absl::OkStatus();
}

// Applies length unit conversion to the imported scene by replacing the
// length properties in the scene object with the given scale factor. Only very
// simple Scenes with single Link Geometries are supported. Returns an error if
// the imported scene has instance updates or multiple entity SceneObjects.
absl::Status ApplyLengthUnitConversion(
    longrunning::OperationContext& operation_context,
    ImportedScene& imported_scene, const double scale_factor,
    GeometryClientWithProcessGeometryCache& geometry_client) {
  if (operation_context.GetStopToken().stop_requested()) {
    return absl::CancelledError("Cancelled");
  }

  // First do some validations on whether we can apply the length unit
  // conversion.
  if (imported_scene.has_instance_updates()) {
    return absl::InvalidArgumentError(
        "Length unit conversion does not support complex imported scenes.");
  }
  for (const auto& [_, scene_object] :
       imported_scene.scene_objects().objects()) {
    if (scene_object.entities_size() != 1) {
      return absl::InvalidArgumentError(
          "Length unit conversion does not support multiple entity "
          "SceneObject.");
    }
  }

  intrinsic_proto::geometry::PipelineConfiguration scale_pipeline;
  scale_pipeline.mutable_steps()->Add()->mutable_scale()->set_scale_factor(
      scale_factor);

  for (auto& [_, scene_object] :
       *imported_scene.mutable_scene_objects()->mutable_objects()) {
    auto& entity = (*scene_object.mutable_entities())[0];
    if (!entity.has_link()) {
      return absl::InvalidArgumentError(
          "Length unit conversion does not support non-link entities.");
    }
    if (!entity.link().has_geometry_component()) continue;
    for (auto& [_, geo_set] : *entity.mutable_link()
                                   ->mutable_geometry_component()
                                   ->mutable_named_geometries()) {
      for (auto& geometry : *geo_set.mutable_geometries()) {
        if (!geometry.has_geometry_storage_refs()) continue;
        INTR_ASSIGN_OR_RETURN(
            *geometry.mutable_geometry_storage_refs(),
            geometry_client.ProcessGeometry(operation_context,
                                            geometry.geometry_storage_refs(),
                                            scale_pipeline));
      }
      for (auto& [_, transformed_geometry] :
           *geo_set.mutable_named_geometries()) {
        INTR_ASSIGN_OR_RETURN(
            std::vector<intrinsic_proto::geometry::v1::Geometry>
                processed_geoms,
            geometry_client.ProcessGeometry(operation_context,
                                            transformed_geometry.geometry(),
                                            scale_pipeline));
        if (processed_geoms.size() != 1) {
          return absl::InternalError(absl::Substitute(
              "Scale pipeline returned $0 geometries, expected exactly 1",
              processed_geoms.size()));
        }
        *transformed_geometry.mutable_geometry() =
            std::move(processed_geoms[0]);
      }
    }
  }

  return absl::OkStatus();
}

// Processes geometries in `scene_object` in place with named type
// `geometry_type`, by configuration `pipeline` with `geometry_client`.
// Returns an error combining all geometry processing errors.
absl::Status ProcessGeometriesWithGeometryService(
    longrunning::OperationContext& operation_context, SceneObject& scene_object,
    absl::string_view geometry_type,
    const intrinsic_proto::geometry::PipelineConfiguration& pipeline,
    GeometryClientWithProcessGeometryCache& geometry_client) {
  // Collect all the unique geometry storage refs we need to process.
  absl::flat_hash_set<intrinsic_proto::geometry::GeometryStorageRefs,
                      intrinsic::pb_hash, intrinsic::pb_equals>
      processing_inputs;

  // Collect all the unique v1 geometry protos we need to process.
  absl::flat_hash_set<intrinsic_proto::geometry::v1::Geometry,
                      intrinsic::pb_hash, intrinsic::pb_equals>
      processing_inputs_v1;

  for (const auto& entity : scene_object.entities()) {
    if (!entity.has_link()) continue;
    const auto& named_geo =
        entity.link().geometry_component().named_geometries();
    if (!named_geo.contains(geometry_type)) continue;

    auto& geometry_set = named_geo.at(geometry_type);
    for (const auto& geometry : geometry_set.geometries()) {
      if (geometry.has_geometry_storage_refs()) {
        processing_inputs.insert(geometry.geometry_storage_refs());
      }
    }
    for (const auto& [_, transformed_geometry] :
         geometry_set.named_geometries()) {
      if (transformed_geometry.has_geometry()) {
        processing_inputs_v1.insert(transformed_geometry.geometry());
      }
    }
  }

  // Checks for cancellation before processing geometries.
  if (operation_context.GetStopToken().stop_requested()) {
    return absl::CancelledError("Cancelled");
  }

  absl::flat_hash_map<
      intrinsic_proto::geometry::GeometryStorageRefs,
      absl::StatusOr<intrinsic_proto::geometry::GeometryStorageRefs>,
      intrinsic::pb_hash, intrinsic::pb_equals>
      processing_outputs;

  absl::flat_hash_map<
      intrinsic_proto::geometry::v1::Geometry,
      absl::StatusOr<std::vector<intrinsic_proto::geometry::v1::Geometry>>,
      intrinsic::pb_hash, intrinsic::pb_equals>
      processing_outputs_v1;

  // Processes each of the inputs and store the resulting StatusOr<GeometryRefs>
  // into the map for inspection further below.
  {
    // Processes v0 geometries first.
    std::vector<Thread> workers;
    absl::Mutex result_mutex;
    for (const auto& geometry_refs : processing_inputs) {
      workers.emplace_back([&operation_context, geometry_refs, &geometry_client,
                            &pipeline, &result_mutex, &processing_outputs]() {
        if (operation_context.GetStopSource().stop_requested()) {
          return;
        }

        auto process_result = geometry_client.ProcessGeometry(
            operation_context, geometry_refs, pipeline);
        absl::MutexLock lock(result_mutex);
        processing_outputs[geometry_refs] = std::move(process_result);
      });
    }

    for (auto& worker : workers) {
      worker.join();
    }
  }

  if (operation_context.GetStopToken().stop_requested()) {
    return absl::CancelledError("Cancelled");
  }

  {
    // Processes v1 geometries next. This should be done after processing v0 so
    // as to re-use the cached results from processing geometries above.
    std::vector<Thread> workers;
    absl::Mutex result_mutex;
    for (const auto& geometry : processing_inputs_v1) {
      workers.emplace_back([&operation_context, geometry, &geometry_client,
                            &pipeline, &result_mutex,
                            &processing_outputs_v1]() {
        if (operation_context.GetStopToken().stop_requested()) {
          return;
        }

        auto processed_geometry = geometry_client.ProcessGeometry(
            operation_context, geometry, pipeline);
        absl::MutexLock lock(result_mutex);
        processing_outputs_v1[geometry] = std::move(processed_geometry);
      });
    }
    for (auto& worker : workers) {
      worker.join();
    }
  }

  // Checks for cancellation before processing errors. This is important because
  // operations could have been cancelled and we don't want to return misleading
  // errors in that case.
  if (operation_context.GetStopToken().stop_requested()) {
    return absl::CancelledError("Cancelled");
  }

  // Update all of the geo within the scene object that was processed.
  // Collecting any errors we find.
  std::vector<absl::Status> processing_errors;
  for (auto& entity : *scene_object.mutable_entities()) {
    if (!entity.has_link()) continue;
    if (!entity.link().geometry_component().named_geometries().contains(
            geometry_type))
      continue;

    auto& geometry_set = entity.mutable_link()
                             ->mutable_geometry_component()
                             ->mutable_named_geometries()
                             ->at(geometry_type);
    for (auto& geometry : *geometry_set.mutable_geometries()) {
      if (!geometry.has_geometry_storage_refs()) continue;
      if (!processing_outputs.contains(geometry.geometry_storage_refs())) {
        return absl::InternalError(
            absl::Substitute("Geometry storage refs $0, from entity '$1' not "
                             "found in processing outputs",
                             geometry.geometry_storage_refs(), entity.name()));
      }

      auto process_result =
          processing_outputs[geometry.geometry_storage_refs()];
      if (process_result.ok()) {
        *geometry.mutable_geometry_storage_refs() = process_result.value();
      } else {
        processing_errors.push_back(absl::InvalidArgumentError(absl::Substitute(
            "Failed to process geometry in entity '$0' with status: $1",
            entity.name(), process_result.status().message())));
      }
    }

    std::vector<std::string> keys_to_remove;
    std::vector<std::pair<std::string,
                          intrinsic_proto::geometry::v1::TransformedGeometry>>
        entries_to_add;

    // Helper to check if a candidate name is already taken within the geometry
    // set, taking into account pending additions.
    auto is_name_taken = [](const std::string& candidate_name,
                            const auto& named_geometries,
                            const auto& entries_to_add) {
      if (named_geometries.contains(candidate_name)) {
        return true;
      }
      for (const auto& [added_name, _] : entries_to_add) {
        if (added_name == candidate_name) {
          return true;
        }
      }
      return false;
    };

    for (auto& [name, transformed_geometry] :
         *geometry_set.mutable_named_geometries()) {
      if (!transformed_geometry.has_geometry()) continue;
      const auto processed_geometry_it =
          processing_outputs_v1.find(transformed_geometry.geometry());
      if (processed_geometry_it == processing_outputs_v1.end()) {
        return absl::InternalError(
            absl::Substitute("Geometry $0, from entity '$1' not "
                             "found in processing outputs",
                             transformed_geometry.geometry(), entity.name()));
      }
      const auto& processed_geometry = processed_geometry_it->second;
      if (processed_geometry.ok()) {
        const std::vector<intrinsic_proto::geometry::v1::Geometry>&
            output_geometries = processed_geometry.value();
        if (output_geometries.empty()) {
          return absl::InternalError(absl::Substitute(
              "Expected at least 1 geometry from processing for entity '$0', "
              "got 0",
              entity.name()));
        }
        if (output_geometries.size() == 1) {
          *transformed_geometry.mutable_geometry() = output_geometries[0];
        } else if (output_geometries.size() > 1) {
          // If the geometry was split into multiple parts (e.g. by convex
          // decomposition), the original geometry is replaced with the new
          // parts. Unique names are generated for each part using the pattern:
          // "<original_name>_<index>". If a collision occurs, a counter suffix
          // is appended.
          keys_to_remove.push_back(name);
          const std::string& base_name = name;
          for (size_t i = 0; i < output_geometries.size(); ++i) {
            std::string candidate_name = absl::StrCat(base_name, "_", i);
            int counter = 0;
            while (is_name_taken(candidate_name,
                                 geometry_set.named_geometries(),
                                 entries_to_add)) {
              candidate_name = absl::StrCat(base_name, "_", i, "_", counter++);
            }
            // Copy the original transformed geometry to retain the pose.
            // The split parts are defined in the local frame of the original
            // geometry, so they must share the same pose to remain correctly
            // positioned relative to each other.
            intrinsic_proto::geometry::v1::TransformedGeometry
                new_transformed_geometry = transformed_geometry;
            *new_transformed_geometry.mutable_geometry() = output_geometries[i];
            entries_to_add.push_back({std::move(candidate_name),
                                      std::move(new_transformed_geometry)});
          }
        }
      } else {
        processing_errors.push_back(absl::InvalidArgumentError(absl::Substitute(
            "Failed to process geometry in entity '$0' with status: $1",
            entity.name(), processed_geometry.status().message())));
      }
    }

    for (const auto& key : keys_to_remove) {
      geometry_set.mutable_named_geometries()->erase(key);
    }
    for (auto& [new_name, new_transformed_geometry] : entries_to_add) {
      (*geometry_set.mutable_named_geometries())[new_name] =
          std::move(new_transformed_geometry);
    }
  }

  if (!processing_errors.empty()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Failed to process geometries for geometry type $0 with errors: $1",
        geometry_type, absl::StrJoin(processing_errors, ", ")));
  }
  return absl::OkStatus();
}

// Replace material properties to geometries in the scene object in place.
absl::Status ReplaceSceneObjectMaterialProperties(
    longrunning::OperationContext& operation_context, SceneObject& scene_object,
    const MaterialProperties& material_properties,
    GeometryClientWithProcessGeometryCache& geometry_client) {
  INTR_RETURN_IF_ERROR(ValidateMaterialProperties(material_properties));

  // Collect all the unique geometry storage refs we need to process.
  // If we need to support this in insrc we can use a similar mechanism to:
  // https://github.com/intrinsic-ai/intrinsic-core/blob/main/google3/intrinsic/geometry/storage/shared_storage.h#L28
  absl::flat_hash_set<intrinsic_proto::geometry::GeometryStorageRefs,
                      intrinsic::pb_hash, intrinsic::pb_equals>
      original_geometry_refs;

  for (const auto& entity : scene_object.entities()) {
    if (!entity.has_link()) continue;
    const auto& named_geo =
        entity.link().geometry_component().named_geometries();

    for (const auto& [geometry_type, geometry_set] : named_geo) {
      // Skip collision geometry to save some processing time.
      if (geometry_type == kKindCollisionGeometry) continue;
      for (const auto& geometry : geometry_set.geometries()) {
        if (geometry.has_geometry_storage_refs()) {
          original_geometry_refs.insert(geometry.geometry_storage_refs());
        }
      }
    }
  }

  absl::flat_hash_map<intrinsic_proto::geometry::GeometryStorageRefs,
                      intrinsic_proto::geometry::GeometryStorageRefs,
                      intrinsic::pb_hash, intrinsic::pb_equals>
      original_to_updated_geometry_refs;

  for (const auto& geometry_refs : original_geometry_refs) {
    if (operation_context.GetStopToken().stop_requested()) {
      return absl::CancelledError("Cancelled");
    }
    grpc::ClientContext context;
    INTR_ASSIGN_OR_RETURN(
        auto geometry_with_metadata,
        geometry_client.c->GetGeometry(context, geometry_refs));
    INTR_ASSIGN_OR_RETURN(auto geometry,
                          geometry_compatibility::ToGeometry(
                              geometry_with_metadata.geometry_v0()));
    INTR_ASSIGN_OR_RETURN(auto renderable, GetOrGenerateRenderable(geometry));
    INTR_ASSIGN_OR_RETURN(auto updated_glb,
                          ApplyMaterialPropertiesToGlb(
                              renderable->GetGLBString(), material_properties));

    geometry_with_metadata.mutable_geometry_v0()
        ->mutable_renderable()
        ->set_gltf_string(updated_glb);
    // TODO(b/417296237): Don't create a new geometry, but update the existing
    // one once the geometry data model supports it.
    grpc::ClientContext context_create_geo;
    INTR_ASSIGN_OR_RETURN(
        auto updated_geometry,
        geometry_client.c->CreateGeometry(
            context_create_geo, geometry_with_metadata.geometry_v0()));
    original_to_updated_geometry_refs[geometry_refs] =
        updated_geometry.geometry_storage_refs_v0();
  }

  // Apply the material properties to the geometries by
  // - Replacing the v0 geometry storage refs with the updated ones.
  // - Saving the material properties as overrides to v1 geometries.
  for (auto& entity : *scene_object.mutable_entities()) {
    if (!entity.has_link()) continue;
    auto& named_geo = *entity.mutable_link()
                           ->mutable_geometry_component()
                           ->mutable_named_geometries();

    for (auto& [geometry_type, geometry_set] : named_geo) {
      for (auto& geometry : *geometry_set.mutable_geometries()) {
        const auto original_refs = geometry.geometry_storage_refs();
        if (original_to_updated_geometry_refs.contains(original_refs)) {
          *geometry.mutable_geometry_storage_refs() =
              original_to_updated_geometry_refs[original_refs];
        }
      }
      for (auto& [_, transformed_geometry] :
           *geometry_set.mutable_named_geometries()) {
        *transformed_geometry.mutable_geometry()->mutable_material_overrides() =
            material_properties;
      }
    }
  }

  return absl::OkStatus();
}

// Processes geometries in `scene_object` in place with geometry operations
// defined in `operations` where the key is the geometry name in named
// geometries of geometry components. Handles geometry processing with given
// `geometry_client`.
absl::Status ProcessGeometries(
    longrunning::OperationContext& operation_context, SceneObject& scene_object,
    const google::protobuf::Map<
        std::string, intrinsic_proto::geometry::PipelineConfiguration>&
        operations,
    GeometryClientWithProcessGeometryCache& geometry_client) {
  for (const auto& [geometry_type, pipeline] : operations) {
    INTR_RETURN_IF_ERROR(ProcessGeometriesWithGeometryService(
        operation_context, scene_object, geometry_type, pipeline,
        geometry_client));
  }

  return absl::OkStatus();
}

// Applies geometry operations to geometries in the scene object in place.
absl::Status ApplyGeometryOperations(
    longrunning::OperationContext& operation_context, SceneObject& scene_object,
    const GeometryOperations& geometry_operations,
    GeometryClientWithProcessGeometryCache& geometry_client) {
  INTR_RETURN_IF_ERROR(RemoveGeometryTypesFromSceneObject(
      scene_object, geometry_operations.remove_types()));

  INTR_RETURN_IF_ERROR(ProcessGeometries(operation_context, scene_object,
                                         geometry_operations.operations(),
                                         geometry_client));
  return absl::OkStatus();
}

// Applies geometry operations to geometries in the imported scene in place.
absl::Status ApplyGeometryOperations(
    longrunning::OperationContext& operation_context,
    intrinsic_proto::scene_object::v1::ImportedScene& imported_scene,
    const GeometryOperations& geometry_operations,
    GeometryClientWithProcessGeometryCache& geometry_client) {
  if (operation_context.GetStopToken().stop_requested()) {
    return absl::CancelledError("Cancelled");
  }

  for (auto& scene_object : std::views::values(
           *imported_scene.mutable_scene_objects()->mutable_objects())) {
    INTR_RETURN_IF_ERROR(ApplyGeometryOperations(
        operation_context, scene_object, geometry_operations, geometry_client));
  }
  return absl::OkStatus();
}

// Replace material properties to geometries in the imported scene in place.
absl::Status ReplaceMaterialProperties(
    longrunning::OperationContext& operation_context,
    ImportedScene& imported_scene,
    const MaterialProperties& material_properties,
    GeometryClientWithProcessGeometryCache& geometry_client) {
  if (operation_context.GetStopToken().stop_requested()) {
    return absl::CancelledError("Cancelled");
  }

  for (auto& scene_object : std::views::values(
           *imported_scene.mutable_scene_objects()->mutable_objects())) {
    INTR_RETURN_IF_ERROR(ReplaceSceneObjectMaterialProperties(
        operation_context, scene_object, material_properties, geometry_client));
  }
  return absl::OkStatus();
}

absl::Status RenameSingleObjectScene(ImportedScene& scene,
                                     absl::string_view name) {
  INTR_RET_CHECK_EQ(scene.scene_objects().objects_size(), 1);
  INTR_RET_CHECK_EQ(scene.scene_object_instances().instances_size(), 1);
  INTR_RET_CHECK(scene.instance_updates().updates().empty());
  SceneObject so = scene.scene_objects().objects().begin()->second;
  so.set_name(name);
  scene.mutable_scene_objects()->mutable_objects()->clear();
  scene.mutable_scene_objects()->mutable_objects()->insert(
      {std::string(name), std::move(so)});
  ImportedSceneObjectInstance instance;
  instance.set_scene_object_id(name);
  scene.mutable_scene_object_instances()->mutable_instances()->clear();
  scene.mutable_scene_object_instances()->mutable_instances()->insert(
      {std::string(name), std::move(instance)});
  LOG(INFO) << "Renamed scene: to " << name;
  return absl::OkStatus();
}

}  // namespace

// Processes `imported_scene` in place based on `config`. Uses
// `geometry_service_stub` for processing geometry and store processed
// geometry references. Returns an error if application of any configuration
// fails.
absl::Status PostProcessImportedScene(
    longrunning::OperationContext& operation_context,
    intrinsic_proto::scene_object::v1::ImportedScene& imported_scene,
    const ImportSceneConfig& config,
    GeometryClientWithProcessGeometryCache& geometry_client) {
  if (operation_context.GetStopToken().stop_requested()) {
    return absl::CancelledError("Cancelled");
  }
  if (IsSingleSceneObjectImport(config) && config.has_scene_object_name()) {
    for (auto& [_, so] :
         *imported_scene.mutable_scene_objects()->mutable_objects()) {
      LOG(INFO) << "Setting scene object name to "
                << config.scene_object_name();
      so.set_name(config.scene_object_name());
    }
  }

  if (config.has_length_unit_conversion()) {
    INTR_RETURN_IF_ERROR(ApplyLengthUnitConversion(
        operation_context, imported_scene,
        config.length_unit_conversion().scale_factor(), geometry_client));
  }

  if (config.import_structure() ==
      intrinsic_proto::scene_object::v1::IMPORT_STRUCTURE_SINGLE_LINK) {
    for (auto& [_, so] :
         *imported_scene.mutable_scene_objects()->mutable_objects()) {
      INTR_ASSIGN_OR_RETURN(so, scene_object::MergeSceneObjectLinks(so));
    }
  }

  if (config.has_geometry_operations()) {
    INTR_RETURN_IF_ERROR(
        ApplyGeometryOperations(operation_context, imported_scene,
                                config.geometry_operations(), geometry_client));
  }
  if (config.has_material_properties()) {
    INTR_RETURN_IF_ERROR(ReplaceMaterialProperties(
        operation_context, imported_scene, config.material_properties(),
        geometry_client));
  }
  if (config.has_transform_scene()) {
    if (config.transform_scene().has_scale()) {
      INTR_ASSIGN_OR_RETURN(
          imported_scene,
          scene_object::ScaleImportedScene(imported_scene,
                                           config.transform_scene().scale()));
    }
    const auto& transform = config.transform_scene();

    if (transform.has_rotation() && transform.has_translation()) {
      INTR_ASSIGN_OR_RETURN(
          imported_scene,
          scene_object::TransformImportedScene(
              imported_scene, transform.rotation(), transform.translation()));
    } else if (transform.has_rotation()) {
      INTR_ASSIGN_OR_RETURN(imported_scene,
                            scene_object::TransformImportedScene(
                                imported_scene, transform.rotation()));
    } else if (transform.has_translation()) {
      INTR_ASSIGN_OR_RETURN(imported_scene,
                            scene_object::TransformImportedScene(
                                imported_scene, transform.translation()));
    }
  }

  if (config.has_display_name() && IsSingleSceneObjectImport(config)) {
    INTR_RETURN_IF_ERROR(
        RenameSingleObjectScene(imported_scene, config.display_name()));
  }

  return absl::OkStatus();
}

}  // namespace intrinsic
