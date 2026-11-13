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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_UTIL_EXPORT_AS_GLTF_H_
#define INTRINSIC_GEOMETRY_INTERNAL_UTIL_EXPORT_AS_GLTF_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "Eigen/Core"
#include "absl/status/statusor.h"
#include "assimp/scene.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/material.h"

namespace intrinsic::geo {

// Returns a string representing the serialized glb data present in the given
// aiScene.
absl::StatusOr<std::string> ExportAiSceneAsGltf(const aiScene& scene,
                                                const Eigen::Matrix4d& trans);

// Returns a string representing the serialized glb given data with the applied
// transform.
absl::StatusOr<std::string> ExportAsGltf(std::string glb_bytes,
                                         const Eigen::Matrix4d& trans);

// Returns a string representing the serialized glb data from the given
// ExactGeometry.
//
// `colors` is an optional contiguous span of uint8 RGB values formatted in
// [R0, G0, B0, R1, G1, B1, ...] order. The expected size depends on the
// geometry type:
// - PointCloud (N points): 3*N (per-point RGB) or 3 (uniform RGB). Point cloud
//   colors are interpreted as sRGB-encoded and decoded to linear space, as
//   required by glTF.
// - Single primitive shape: 3 (single RGB tuple).
// - Compound primitive shapes (M shapes): 3*M (per-primitive RGB) or 3 (uniform
// RGB).
// - Mesh (V vertices): 3*V (per-vertex RGB) or 3 (uniform RGB).
//
// Pre-conditions:
// - If `colors` has a value, its size must match one of the valid sizes for
//   the underlying geometry type.
//
// Returns InvalidArgumentError if pre-conditions fail.
// TODO(stoyang): Move to geometry/io as we setup the reader/writer concept.
absl::StatusOr<std::string> ExportAsGltf(
    const ExactGeometry& geometry, const Material& material = Material(),
    std::optional<std::span<const uint8_t>> colors = std::nullopt);

}  // namespace intrinsic::geo
#endif  // INTRINSIC_GEOMETRY_INTERNAL_UTIL_EXPORT_AS_GLTF_H_
