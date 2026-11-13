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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_LEGACY_POINT_CLOUD_PTS_TO_AI_SCENE_H_
#define INTRINSIC_GEOMETRY_INTERNAL_LEGACY_POINT_CLOUD_PTS_TO_AI_SCENE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "absl/status/statusor.h"
#include "assimp/scene.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/material.h"
#include "intrinsic/geometry/shapes/point_cloud.h"

namespace intrinsic::geo {

// Returns assimp scene given the pts file.
absl::StatusOr<std::unique_ptr<aiScene>> PtsFileToAiScene(
    const std::string& file_content,
    eigenmath::Vector3d scale = eigenmath::Vector3d::Ones());

// Returns assimp scene for the given point cloud and optional colors and
// material.
//
// `colors` is an optional contiguous span of sRGB-encoded uint8 RGB values
// formatted in [R0, G0, B0, R1, G1, B1, ...] order (e.g. pixels of a camera
// image). They are decoded to linear space, as required by glTF.
// - If size is 3*N (where N is point_cloud.getPoints().size()), per-point
//   colors are assigned to vertex colors.
// - If size is 3, a uniform color is applied to the material's diffuse color.
//
// Pre-conditions:
// - If `colors` has a value, its size must be either 3 (for a uniform RGB
// color) or 3 * point_cloud.getPoints().size() (for per-point RGB colors).
//
// Returns InvalidArgumentError if pre-conditions fail.
absl::StatusOr<std::unique_ptr<aiScene>> PointCloudToAiScene(
    const PointCloud& point_cloud,
    eigenmath::Vector3d scale = eigenmath::Vector3d::Ones(),
    std::optional<Material> material = std::nullopt,
    std::optional<std::span<const uint8_t>> colors = std::nullopt);

}  // namespace intrinsic::geo
#endif  // INTRINSIC_GEOMETRY_INTERNAL_LEGACY_POINT_CLOUD_PTS_TO_AI_SCENE_H_
