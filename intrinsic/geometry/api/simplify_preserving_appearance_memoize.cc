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

#include "intrinsic/geometry/api/simplify_preserving_appearance_memoize.h"

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "assimp/scene.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/renderable.h"
#include "intrinsic/geometry/api/renderable_generation.h"
#include "intrinsic/geometry/internal/mesh/io/ai_scene_to_mesh.h"
#include "intrinsic/geometry/internal/mesh/io/load_ai_scene_from_buffer.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"
#include "intrinsic/geometry/internal/mesh_simplification/appearance/glb_appearance_simplifier.h"
#include "intrinsic/geometry/shapes/point_cloud.h"
#include "intrinsic/util/object_store/memoize.h"
#include "intrinsic/util/object_store/object_ref.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/stop_token.h"

namespace intrinsic::geo {
namespace {

// Converts a GLB into a plain triangle mesh. Node transforms are baked into the
// vertices, so the mesh is in the same frame as the exact geometry the GLB
// renders (e.g. a unit-conversion scale on the root node is applied). This is
// the same Assimp path used to load GLB mesh files.
absl::StatusOr<Mesh> GlbToMesh(const std::string& glb) {
  INTR_ASSIGN_OR_RETURN(const std::unique_ptr<aiScene> scene,
                        LoadAiSceneFromBuffer(glb, "glb"));
  // AiSceneToMesh merges vertices at the same position, so the mesh stays
  // connected across UV and normal seams.
  return AiSceneToMesh(*scene);
}

// Returns InvalidArgumentError unless `value` is in (0, 1].
absl::Status CheckInUnitInterval(absl::string_view name, double value) {
  if (!std::isfinite(value) || value <= 0.0 || value > 1.0) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "%s must be in (0, 1], but %f was provided.", name, value));
  }
  return absl::OkStatus();
}

// Returns true if `exact` is an empty point cloud, false if it is not a point
// cloud, and InvalidArgumentError if it is a point cloud with points.
absl::StatusOr<bool> IsEmptyPointCloud(const ExactGeometry& exact) {
  if (!exact.HasPointCloud()) {
    return false;
  }
  INTR_ASSIGN_OR_RETURN(const ObjectRef<PointCloud>& point_cloud,
                        exact.GetPointCloud());
  if (!point_cloud.Value().getPoints().empty()) {
    return absl::InvalidArgumentError(
        "SimplifyPreservingAppearanceMemoize supports triangle meshes, but the "
        "geometry is a point cloud.");
  }
  return true;
}

// Simplifies `glb`, caching the result. Returns an empty string if nothing
// could be simplified (e.g. only points or lines). Cancelled runs are not
// cached. Other errors are cached, since they are deterministic for a given
// input. Bump the version in the name whenever the output for the same input
// changes.
absl::StatusOr<ObjectRef<std::string>> SimplifyGlbMemoize(
    const std::string& glb, double target_triangle_ratio,
    const StopToken& stop_token) {
  return Memoize(
      MemoizeOptions("SimplifyGlbPreservingAppearance-v1"),
      [](const std::string& glb, double target_triangle_ratio,
         const StopToken& stop_token) -> absl::StatusOr<std::string> {
        INTR_ASSIGN_OR_RETURN(std::optional<std::string> simplified_glb,
                              SimplifyGlbPreservingAppearance(
                                  glb, target_triangle_ratio, stop_token));
        return std::move(simplified_glb).value_or("");
      },
      glb, target_triangle_ratio, stop_token);
}

// Rebuilds the exact mesh from a simplified GLB, caching the result.
absl::StatusOr<ObjectRef<Mesh>> GlbToMeshMemoize(const std::string& glb) {
  return Memoize(MemoizeOptions("SimplifiedGlbToMesh-v1"), &GlbToMesh, glb);
}

}  // namespace

absl::StatusOr<Geometry> SimplifyPreservingAppearanceMemoize(
    const Geometry& geo, double target_triangle_ratio, StopToken stop_token) {
  INTR_RETURN_IF_ERROR(
      CheckInUnitInterval("target_triangle_ratio", target_triangle_ratio));
  if (stop_token.stop_requested()) {
    return absl::CancelledError(
        "SimplifyPreservingAppearanceMemoize cancelled.");
  }
  if (!geo.GetExactGeometry().GetPrimitiveShapes().empty()) {
    return absl::InvalidArgumentError(
        "SimplifyPreservingAppearanceMemoize does not support primitive "
        "shapes.");
  }
  // A file without triangles (e.g. an empty scene, or one whose triangles are
  // all degenerate) loads as a point cloud. An empty one has nothing to
  // simplify.
  INTR_ASSIGN_OR_RETURN(const bool empty_point_cloud,
                        IsEmptyPointCloud(geo.GetExactGeometry()));
  if (empty_point_cloud) {
    return geo;
  }
  INTR_ASSIGN_OR_RETURN(const ObjectRef<Mesh>& mesh,
                        geo.GetExactGeometry().GetMesh());
  if (mesh.Value().empty()) {
    return geo;
  }

  // The GLB to simplify: the user-supplied renderable, which carries the UVs,
  // vertex colors and materials, or else one generated from the exact mesh.
  const bool user_renderable =
      geo.KeepRenderableForSerialization() && geo.GetRenderable() != nullptr;
  std::shared_ptr<const Renderable> input_renderable = geo.GetRenderable();
  if (!user_renderable) {
    INTR_ASSIGN_OR_RETURN(input_renderable,
                          GenerateRenderableForExactGeometry(
                              geo.GetExactGeometry(), std::nullopt));
  }

  // Simplify once, on the GLB. Both the renderable and the exact mesh are
  // taken from this result, so they always match.
  INTR_ASSIGN_OR_RETURN(const ObjectRef<std::string> simplified_glb,
                        SimplifyGlbMemoize(input_renderable->GetGLBString(),
                                           target_triangle_ratio, stop_token));
  // Nothing could be simplified (e.g. only points or lines).
  if (simplified_glb.Value().empty()) {
    return geo;
  }
  INTR_ASSIGN_OR_RETURN(ObjectRef<Mesh> simplified_mesh,
                        GlbToMeshMemoize(simplified_glb.Value()),
                        _ << "Reading the simplified GLB");

  // A generated renderable is not stored; it is regenerated from the new
  // exact mesh when needed.
  std::shared_ptr<const Renderable> renderable;
  if (user_renderable) {
    renderable = std::make_shared<const Renderable>(simplified_glb.Value());
  }
  return Geometry(
      ExactGeometry(std::move(simplified_mesh),
                    geo.GetExactGeometry().options()),
      std::move(renderable), /*keep_renderable=*/user_renderable,
      geo.material_properties(),
      Geometry::Provenance{
          .human_readable_update_reason = "Simplify preserving appearance",
          .previous_geometry = std::make_shared<const Geometry>(geo),
      });
}

}  // namespace intrinsic::geo
