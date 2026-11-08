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

#include "intrinsic/icon/control/services/assembly_from_world_service.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "internal/testing.h"
#include "intrinsic/icon/control/services/world_from_scene_object_gzf_service.h"

namespace intrinsic::icon {
namespace {

using ::testing::SizeIs;

TEST(AssemblyFromWorldServiceTest, Works) {
  ASSERT_OK_AND_ASSIGN(
      auto world_service,
      WorldFromSceneObjectGzfService::Create(
          "intrinsic_control/intrinsic/models/robot_definitions/ur/"
          "ur3e_scene_object.gzf"));
  ASSERT_OK_AND_ASSIGN(
      auto assembly_from_world_service,
      AssemblyFromWorldService::Create({}, world_service.get()));
  auto skeleton = assembly_from_world_service->GetSkeletonOrNull();
  ASSERT_NE(skeleton, nullptr);
  EXPECT_THAT(skeleton->GetAllJointIds(), SizeIs(6));
}

}  // namespace
}  // namespace intrinsic::icon
