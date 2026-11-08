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

#include "intrinsic/icon/control/services/world_from_scene_object_gzf_service.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/storage/gzf_storage.h"
#include "intrinsic/icon/control/services/service_registration.h"
#include "intrinsic/icon/control/services/world_service.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/util/scene_object_gzf.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/service/test/world_service_fake.h"
#include "intrinsic/world/world.h"

namespace intrinsic::icon {
namespace {

constexpr char kWorldId[] = "world";

}

absl::Status WorldFromSceneObjectGzfService::Initialize(
    absl::string_view scene_object_gzf_path) {
  std::string full_path =
      PathResolver::ResolveRunfilesPath(scene_object_gzf_path);
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<intrinsic::GZFile> gzf,
                        intrinsic::GZFile::Open(full_path));
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::scene_object::v1::SceneObject scene_object,
      scene_object::GetSceneObjectFromGzf(*gzf));

  auto gzf_lib = GetGzfGeometryLibrary(*gzf);
  WorldHashMap<std::string, World> worlds;
  {
    World world = World::CreateEmptyWorld();
    INTR_ASSIGN_OR_RETURN(auto object_world,
                          object_world::ObjectWorld::CreateView(world));

    INTR_ASSIGN_OR_RETURN(object_world::WorldObject * root,
                          object_world->GetObject(RootObjectId()));
    INTR_RETURN_IF_ERROR(
        root->CreateChildObject(WorldObjectName(scene_object.name()),
                                WorldObjectNameType::kNameIsGlobalAlias,
                                Pose3d::Identity(), gzf_lib.get(), scene_object)
            .status());
    worlds.emplace(kWorldId, std::move(world));
  }
  INTR_ASSIGN_OR_RETURN(
      world_service_,
      FakeWorldService::Create(
          /*worlds=*/std::move(worlds),
          FakeWorldService::CreateOptions{.enable_local_tcp = false}));

  object_world_client_ = std::make_shared<world::ObjectWorldClient>(
      kWorldId, world_service_->NewObjectStub());
  // Make a call to the ObjectWorldClient to ensure it's functional
  return object_world_client_->ListObjectNames().status();
}

absl::StatusOr<std::unique_ptr<WorldFromSceneObjectGzfService>>
WorldFromSceneObjectGzfService::Create(
    absl::string_view scene_object_gzf_path) {
  auto service = std::make_unique<WorldFromSceneObjectGzfService>();
  INTR_RETURN_IF_ERROR(service->Initialize(scene_object_gzf_path));
  return service;
}

INTRINSIC_REGISTER_SERVICE(WorldFromSceneObjectGzfService, WorldService);

}  // namespace intrinsic::icon
