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

#ifndef INTRINSIC_ICON_CONTROL_SERVICES_WORLD_FROM_SCENE_OBJECT_GZF_SERVICE_H_
#define INTRINSIC_ICON_CONTROL_SERVICES_WORLD_FROM_SCENE_OBJECT_GZF_SERVICE_H_

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/control/services/service.h"
#include "intrinsic/icon/control/services/world_service.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/service/test/world_service_fake.h"

namespace intrinsic::icon {

// Implementation of WorldService that reads information from a SceneObject
// .gzf file, including geometry data.
//
// (Don't forget to add your GZF as a `data` dependency to your machine rule so
// it's available at runtime!)
class WorldFromSceneObjectGzfService final
    : public ServiceImplBase<WorldFromSceneObjectGzfService, WorldService> {
 public:
  static absl::StatusOr<std::unique_ptr<WorldFromSceneObjectGzfService>> Create(
      absl::string_view scene_object_gzf_path);

  // WARNING: This does not fulfill the contract of WorldService (which promises
  // that the shared_ptr is always non-null).
  std::shared_ptr<const world::ObjectWorldClient> GetObjectWorldClient()
      const override {
    return object_world_client_;
  }

 private:
  absl::Status Initialize(absl::string_view scene_object_gzf_path);

  std::unique_ptr<FakeWorldService> world_service_;
  std::shared_ptr<const world::ObjectWorldClient> object_world_client_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_SERVICES_WORLD_FROM_SCENE_OBJECT_GZF_SERVICE_H_
