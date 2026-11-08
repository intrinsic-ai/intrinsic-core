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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/icon/control/services/world_service.h"
#include "intrinsic/icon/utils/world_utils.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/world_object.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::NotNull;
using ::testing::Pair;

TEST(WorldFromSceneObjectGzfServiceTest, SceneObjectNotFound) {
  EXPECT_THAT(
      WorldFromSceneObjectGzfService::Create("invented_path/scene_object.gzf"),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("file")));
}

TEST(WorldFromSceneObjectGzfServiceTest, CreateAndUse) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<WorldService> world_service,
                       WorldFromSceneObjectGzfService::Create(
                           "intrinsic_control/intrinsic/models/"
                           "robot_definitions/ur/ur3e_scene_object.gzf"));

  std::shared_ptr<const world::ObjectWorldClient> world_client =
      world_service->GetObjectWorldClient();
  ASSERT_THAT(world_client, NotNull());

  ASSERT_OK_AND_ASSIGN(std::vector<WorldObjectName> object_names,
                       world_client->ListObjectNames());
  EXPECT_THAT(object_names, Contains(WorldObjectName{"ur3e_scene_object"}));
  ASSERT_OK_AND_ASSIGN(
      world::WorldObject kinematic_object,
      world_client->GetObject(WorldObjectName{"ur3e_scene_object"}));
  ASSERT_OK_AND_ASSIGN(
      auto skeleton,
      GetSkeletonForObject(*(world_client),
                           WorldObjectName{"ur3e_scene_object"}));
  EXPECT_THAT(skeleton.skeleton->GetSolverKeys(),
              Contains(Pair(_, Contains(Pair(_, Contains("ur"))))));
}

}  // namespace
}  // namespace intrinsic::icon
