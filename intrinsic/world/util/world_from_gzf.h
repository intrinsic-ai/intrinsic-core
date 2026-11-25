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

#ifndef INTRINSIC_WORLD_UTIL_WORLD_FROM_GZF_H_
#define INTRINSIC_WORLD_UTIL_WORLD_FROM_GZF_H_

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/world/world.h"

namespace intrinsic {
namespace testing {

enum class ExpectedWorldType {
  World = 0,
  SceneObject = 2,
};

// Loads a SceneObject from `gzf_file` (resolved into runfiles) and adds it to
// `world` under the root object as a resource with `object_name` (or
// `scene_object.name()` if `object_name` is std::nullopt).
absl::Status AddSceneObjectFromGzfToWorld(
    World& world, absl::string_view gzf_file,
    std::optional<absl::string_view> object_name = std::nullopt);

// Loads a SceneObject from an already-resolved `gzf_file` path and adds it to
// `world` under the root object as a resource with `object_name` (or
// `scene_object.name()` if `object_name` is std::nullopt).
absl::Status AddSceneObjectFromGzfToWorldWithResolvedPath(
    World& world, absl::string_view gzf_file,
    std::optional<absl::string_view> object_name = std::nullopt);

// Returns a World from the given gzf file, this file will be resolved into
// runfiles before being used. The world_type specifies how to load the gzf file
// into a world object by specifying the type of gzf file. When `world_type` is
// `ExpectedWorldType::SceneObject`, `object_name` optionally overrides the
// scene object's default name.
absl::StatusOr<World> GetWorldForTest(
    const std::string& gzf_file,
    ExpectedWorldType world_type = ExpectedWorldType::World,
    std::optional<absl::string_view> object_name = std::nullopt);

// Returns a World from the given gzf file. The world_type specifies how to load
// the gzf file into a world object by specifying the type of gzf file. When
// `world_type` is `ExpectedWorldType::SceneObject`, `object_name` optionally
// overrides the scene object's default name.
absl::StatusOr<World> GetWorldForTestWithResolvedPath(
    const std::string& gzf_file,
    ExpectedWorldType world_type = ExpectedWorldType::World,
    std::optional<absl::string_view> object_name = std::nullopt);

}  // namespace testing
}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_UTIL_WORLD_FROM_GZF_H_
