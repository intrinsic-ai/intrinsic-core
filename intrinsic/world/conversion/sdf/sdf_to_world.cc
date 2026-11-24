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

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "intrinsic/geometry/api/axis_aligned_bounding_box_3d.h"
#include "intrinsic/geometry/api/compute_axis_aligned_bounding_box_3d.h"
#include "intrinsic/geometry/storage/dummy_storage.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/collections_component.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/component/ppr_component.h"
#include "intrinsic/world/conversion/sdf/world_from_sdf.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/world.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"

ABSL_FLAG(std::string, output_world_gz_file, "/tmp/world.gzf",
          "The output file to write the converted world GZFile to.");
ABSL_FLAG(std::string, world_sdf_file, "",
          "The input sdf file to read the world from.");
ABSL_FLAG(
    bool, generate_group_ids, false,
    "If set, the output world will have group ids generated for the models.");
ABSL_FLAG(double, large_mesh_checks_max_mesh_diagonal, 100,
          "The maximum allowable size for the diagonal of any single mesh. "
          "The conversion will stop with an error if any mesh has a bounding "
          "box that exceeds this value. Setting to a value < 0 disables these "
          "checks completely.");
ABSL_FLAG(std::string, models_as_resources, "",
          "A '|' separated list of model names matching desired resource names "
          "formatted as <model name>,<resource name>");

namespace intrinsic {

namespace {

// Checks for meshes with a bounding box diagonal of more than max_aabb_diag.
// Returns a descriptive error on failure.
absl::Status CheckForLargeMeshes(const World& world, double max_aabb_diag) {
  WorldHashMap<EntityId, double> id_to_large_diagonals;

  const double max_mesh_diagonal_sq = max_aabb_diag * max_aabb_diag;
  for (const GeometryEntityId& id :
       world.GetTypedEntityIds<GeometryComponentType>()) {
    INTR_ASSIGN_OR_RETURN(const GeometryComponent* component,
                          world.GetComponentByEntityId<GeometryComponent>(id));
    for (absl::string_view name : component->GetGeometryNames()) {
      INTR_ASSIGN_OR_RETURN(NamedGeometrySet set, component->GetGeometry(name));
      for (const auto& [_, geo] : set) {
        INTR_ASSIGN_OR_RETURN(AxisAlignedBoundingBox3d mesh_bounding_box,
                              ComputeAxisAlignedBoundingBox3d(geo.shape()));
        const eigenmath::Vector3d diag = mesh_bounding_box.GetDiagonal();
        if (diag.squaredNorm() > max_mesh_diagonal_sq) {
          id_to_large_diagonals.emplace(id, sqrt(diag.squaredNorm()));
        }
      }
    }
  }

  if (!id_to_large_diagonals.empty()) {
    std::string err_msg = absl::Substitute(
        "Meshes were found that have a diagonal larger than allowed size $0. "
        "Please double check that this mesh is provided in meter scale (as "
        "opposed to millimeters). If this is intentional, set "
        "--large_mesh_checks_max_mesh_diagonal to be larger, or disable by "
        "setting that flag to a negative value. The objects that have "
        "meshes that violate this diagonal are:",
        max_aabb_diag);
    for (const auto& [id, diag] : id_to_large_diagonals) {
      INTR_ASSIGN_OR_RETURN(const WorldEntity* entity, world.GetEntityById(id));
      absl::StatusOr<AttachmentEntityId> attachment_id_or =
          world.ValidateEntity<AttachmentEntityId>(id);
      if (attachment_id_or.ok()) {
        INTR_ASSIGN_OR_RETURN(
            std::string path_name,
            world.GetLocalNamePathString(*attachment_id_or, "|"));
        absl::StrAppend(
            &err_msg,
            absl::Substitute("\nid: $0 path_name: '$1' alias: '$2' diag: $3",
                             id.value(), path_name, entity->GetAlias(), diag));
      } else {
        absl::StrAppend(
            &err_msg,
            absl::Substitute("\nid: $0 local_name: '$1' alias: '$2' diag: $3",
                             id.value(), entity->GetLocalName(),
                             entity->GetAlias(), diag));
      }
    }
    return absl::InvalidArgumentError(err_msg);
  }
  return absl::OkStatus();
}

// Parses a "|" delimited list of "<model_name>,<resource_name>" strings and
// adds the resource name to the PPR components of the entities corresponding to
// the SDF model.
absl::Status TagModelsAsResources(World* world,
                                  absl::string_view models_as_resources) {
  for (absl::string_view model_as_resource :
       absl::StrSplit(models_as_resources, '|')) {
    const std::vector<absl::string_view> parsed_model_as_resource =
        absl::StrSplit(model_as_resource, ',');
    if (parsed_model_as_resource.size() != 2) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Each model and resource name passed to '--models_as_resources' is "
          "expected to be of the form <model_name>,<resource_name>. Got: $0",
          model_as_resource));
    }
    absl::string_view model = parsed_model_as_resource[0];
    absl::string_view resource = parsed_model_as_resource[1];

    // In sdf_to_world, we store all model names as the object names, a.k.a. as
    // the alias of the corresponding collections entity.
    bool found_model = false;
    for (const CollectionsEntityId collections_id :
         world->GetTypedEntityIds<CollectionsComponentType>()) {
      INTR_ASSIGN_OR_RETURN(WorldEntity * entity,
                            world->GetEntityById(collections_id));
      if (entity->GetLocalName() == model) {
        LOG_IF(WARNING, found_model)
            << "More than one model named '" << model << "'!";
        found_model = true;

        // Set the PPR resource name of this entity.
        INTR_ASSIGN_OR_RETURN(PPRComponent * ppr,
                              entity->GetOrCreateComponent<PPRComponent>());
        ppr->SetResourceName(resource);

        // Set the PPR resource name on all entities in this collection.
        INTR_ASSIGN_OR_RETURN(const CollectionsComponent* collection,
                              entity->GetComponent<CollectionsComponent>());
        for (const CollectionsMemberEntityId member_id :
             collection->GetAllCollectionMembers()) {
          INTR_ASSIGN_OR_RETURN(entity, world->GetEntityById(member_id));
          INTR_ASSIGN_OR_RETURN(ppr,
                                entity->GetOrCreateComponent<PPRComponent>());
          ppr->SetResourceName(resource);
        }
      }
    }

    if (!found_model) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Could not find model named '$0', passed to '--models_as_resources'.",
          model));
    }
  }

  return absl::OkStatus();
}

}  // namespace

// This will load a world into memory from some format (supported by world_load)
// and it will output the world into a gzf file.
void MainImpl() {
  std::string output_world_gz_file = absl::GetFlag(FLAGS_output_world_gz_file);
  std::string input_sdf_file = absl::GetFlag(FLAGS_world_sdf_file);

  QCHECK(!output_world_gz_file.empty())
      << "--output_world_gz_file must be set.";
  QCHECK(!input_sdf_file.empty()) << "--world_sdf_file must be set.";

  std::string sdf_data;
  QCHECK_OK(file::GetContents(input_sdf_file, &sdf_data, file::Defaults()));
  ASSIGN_OR_DIE(auto gzfile, GZFile::Create(output_world_gz_file));

  sdf::WorldFromSdf world_from_sdf;
  world_from_sdf.SetGroupIdGeneration(absl::GetFlag(FLAGS_generate_group_ids));

  QCHECK_OK(world_from_sdf.Parse(sdf_data));
  ASSIGN_OR_DIE(auto world, world_from_sdf.GetWorld());

  if (auto models_as_resources = absl::GetFlag(FLAGS_models_as_resources);
      !models_as_resources.empty()) {
    QCHECK_OK(TagModelsAsResources(world.get(), models_as_resources));
  }

  // Perform checks for large meshes if enabled.
  const double max_mesh_diagonal =
      absl::GetFlag(FLAGS_large_mesh_checks_max_mesh_diagonal);
  if (max_mesh_diagonal > 0) {
    QCHECK_OK(CheckForLargeMeshes(*world, max_mesh_diagonal));
  }

  LOG(INFO) << "Writing world in gzfile format to: " << output_world_gz_file;
  QCHECK_OK(
      world->ToFile(gzfile.get(), GetDummyGeometryLibrary()->Deserializer()));
}

}  // namespace intrinsic

int main(int argc, char** argv) {
  InitIntrinsic(argv[0], argc, argv);
  intrinsic::MainImpl();
  return 0;
}
