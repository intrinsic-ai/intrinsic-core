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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_APPEARANCE_GLB_APPEARANCE_SIMPLIFIER_H_
#define INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_APPEARANCE_GLB_APPEARANCE_SIMPLIFIER_H_

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/util/thread/stop_token.h"

namespace intrinsic::geo {

// Simplifies every triangle primitive of a renderable GLB with
// `meshopt_simplifyWithAttributes`, keeping UVs (texture seams are protected),
// normals, vertex colors, materials, and embedded textures intact.
//
// `target_triangle_ratio` is the fraction of each primitive's triangles to
// keep, in (0, 1], like gltfpack's -si. Each primitive is simplified until it
// has that many triangles, however much its shape changes; the triangles whose
// removal changes the shape and look least go first. Primitives can keep more:
// open edges stay fixed (so no gaps open between primitives), UV seams are
// protected, and every primitive keeps at least a few triangles.
//
// `glb_bytes` is not changed; the result is a new GLB. It is a copy of the
// input where only vertex and index data differ; materials, textures, nodes,
// extensions and extras are kept as they are. Packed (meshopt-compressed or
// quantized) GLBs are unpacked first; the result is an unpacked GLB. The result
// is validated before it is returned.
// Returns std::nullopt, meaning "keep the original", if the GLB can't be
// unpacked or parsed, or uses glTF data the buffer code doesn't support (e.g.
// accessors without a bufferView), there is nothing to simplify, the result
// would not have fewer triangles, or nothing would be left. Primitives whose
// data can't be read (e.g. sparse accessors) are kept as they are. Returns
// InvalidArgumentError if `target_triangle_ratio` is not in (0, 1],
// CancelledError if `stop_token` is triggered, and InternalError if writing
// the simplified GLB fails, which would be a bug.
absl::StatusOr<std::optional<std::string>> SimplifyGlbPreservingAppearance(
    absl::string_view glb_bytes, double target_triangle_ratio,
    const StopToken& stop_token = StopToken());

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_APPEARANCE_GLB_APPEARANCE_SIMPLIFIER_H_
