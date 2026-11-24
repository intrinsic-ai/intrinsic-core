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

#ifndef INTRINSIC_GEOMETRY_API_SIMPLIFY_PRESERVING_APPEARANCE_H_
#define INTRINSIC_GEOMETRY_API_SIMPLIFY_PRESERVING_APPEARANCE_H_

#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/util/thread/stop_token.h"

namespace intrinsic::geo {

// Simplifies `geo` while preserving its visual appearance.
//
// `target_triangle_ratio` is the fraction of each mesh primitive's triangles
// to keep, in (0, 1] (e.g. 0.25 = a quarter of the triangles).
//
// The geometry is simplified once, on its GLB: the user-supplied renderable
// (`KeepRenderableForSerialization()`), which carries textures, UVs, vertex
// colors and materials, or else a renderable generated from the exact mesh.
// The exact mesh is then rebuilt from the simplified GLB, so the renderable
// and the exact mesh always match. A generated renderable is not stored.
// `material_properties` are preserved. Returns `geo` unchanged if its mesh is
// empty or nothing can be simplified, including when the GLB uses glTF data
// the simplifier doesn't support. A file without triangles, such as an empty
// scene, loads as an empty point cloud and is also returned unchanged.
//
// Returns InvalidArgumentError if `target_triangle_ratio` is not in (0, 1] or
// `geo` contains primitive shapes or a non-empty point cloud, and
// CancelledError if `stop_token` is triggered. Other errors from simplifying
// or reading the GLB are returned.
absl::StatusOr<Geometry> SimplifyPreservingAppearance(
    const Geometry& geo, double target_triangle_ratio,
    StopToken stop_token = StopToken());

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_API_SIMPLIFY_PRESERVING_APPEARANCE_H_
