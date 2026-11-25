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

#include "intrinsic/world/util/world_from_gzf.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/storage/gzf_storage.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/resources/proto/geometric_resource_data.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/util/scene_object_gzf.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/proto/world_fragment.pb.h"
#include "intrinsic/world/world.h"

namespace intrinsic {
namespace testing {

absl::Status AddSceneObjectFromGzfToWorld(
    World& world, absl::string_view gzf_file,
    std::optional<absl::string_view> object_name) {
  return AddSceneObjectFromGzfToWorldWithResolvedPath(
      world, PathResolver::ResolveRunfilesPathForTest(gzf_file), object_name);
}

absl::Status AddSceneObjectFromGzfToWorldWithResolvedPath(
    World& world, absl::string_view gzf_file,
    std::optional<absl::string_view> object_name) {
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<GZFile> gzfile, GZFile::Open(gzf_file));
  auto gzf_lib = GetGzfGeometryLibrary(*gzfile);
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::scene_object::v1::SceneObject scene_object,
      scene_object::GetSceneObjectFromGzf(*gzfile));

  INTR_ASSIGN_OR_RETURN(auto object_world,
                        object_world::ObjectWorld::CreateView(world));
  INTR_ASSIGN_OR_RETURN(object_world::WorldObject * root,
                        object_world->GetObject(RootObjectId()));
  intrinsic_proto::resources::GeometricResourceInstanceData
      resource_instance_data;
  resource_instance_data.set_name(object_name.value_or(scene_object.name()));
  *resource_instance_data.mutable_scene_object() = std::move(scene_object);
  INTR_RETURN_IF_ERROR(
      root->CreateChildResource(resource_instance_data,
                                WorldObjectNameType::kNameIsGlobalAlias,
                                Pose3d::Identity(), gzf_lib.get())
          .status());

  // Deserialize geometries.
  for (const auto entity_id : world.GetTypedEntityIds<GeometryEntityId>()) {
    INTR_ASSIGN_OR_RETURN(
        GeometryComponent * geo_component,
        world.GetComponentByEntityId<GeometryComponent>(entity_id));

    for (const std::string& geo_set_name : geo_component->GetGeometryNames()) {
      INTR_RETURN_IF_ERROR(
          geo_component->GetGeometry(geo_set_name, gzf_lib->Deserializer())
              .status());
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<World> GetWorldForTest(
    const std::string& gzf_file, ExpectedWorldType world_type,
    std::optional<absl::string_view> object_name) {
  return GetWorldForTestWithResolvedPath(
      PathResolver::ResolveRunfilesPathForTest(gzf_file), world_type,
      object_name);
}

absl::StatusOr<World> GetWorldForTestWithResolvedPath(
    const std::string& gzf_file, ExpectedWorldType world_type,
    std::optional<absl::string_view> object_name) {
  if (world_type == ExpectedWorldType::SceneObject) {
    World world = World::CreateEmptyWorld();
    INTR_RETURN_IF_ERROR(AddSceneObjectFromGzfToWorldWithResolvedPath(
        world, gzf_file, object_name));
    return world;
  }
  if (object_name.has_value()) {
    return absl::InvalidArgumentError(
        "object_name is only supported when world_type is "
        "ExpectedWorldType::SceneObject.");
  }

  INTR_ASSIGN_OR_RETURN(std::unique_ptr<GZFile> gzfile, GZFile::Open(gzf_file));
  INTR_ASSIGN_OR_RETURN(World world, World::FromFile(*gzfile));
  auto gzf_lib = GetGzfGeometryLibrary(*gzfile);

  // Deserialize geometries.
  for (const auto entity_id : world.GetTypedEntityIds<GeometryEntityId>()) {
    INTR_ASSIGN_OR_RETURN(
        GeometryComponent * geo_component,
        world.GetComponentByEntityId<GeometryComponent>(entity_id));

    for (const std::string& geo_set_name : geo_component->GetGeometryNames()) {
      INTR_RETURN_IF_ERROR(
          geo_component->GetGeometry(geo_set_name, gzf_lib->Deserializer())
              .status());
    }
  }

  return world;
}

}  // namespace testing
}  // namespace intrinsic
