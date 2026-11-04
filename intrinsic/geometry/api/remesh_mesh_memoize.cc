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

#include "intrinsic/geometry/api/remesh_mesh_memoize.h"

#include <memory>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/remesh_mesh.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"
#include "intrinsic/util/demangle.h"
#include "intrinsic/util/object_store/memoize.h"
#include "intrinsic/util/object_store/object_ref.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
namespace {

class RemeshMeshMemoizeFunctor {
 public:
  explicit RemeshMeshMemoizeFunctor(const MeshoptRemeshOptions& options)
      : options_(options) {}

  template <typename Geo>
  absl::StatusOr<ObjectRef<Mesh>> operator()(const Geo&) const {
    LOG(WARNING) << "intrinsic::RemeshMeshMemoizeFunctor does not support\n"
                 << "operator()(const Geo&) const, with \n"
                 << "Geo = " << Demangle<Geo>();
    return absl::InvalidArgumentError(
        "RemeshMeshMemoizeFunctor does not support argument type.");
  }

  absl::StatusOr<ObjectRef<Mesh>> operator()(const ObjectRef<Mesh>& geo) const {
    return Memoize(
        MemoizeOptions("RemeshMeshMemoizeFunctor-v1"),
        [](ObjectRef<Mesh> mesh_ref, int resolution, bool solve,
           bool shell) -> absl::StatusOr<Mesh> {
          INTR_ASSIGN_OR_RETURN(
              RemeshMeshResult remeshed,
              RemeshMesh(mesh_ref.Value(), MeshoptRemeshOptions{
                                               .resolution = resolution,
                                               .solve = solve,
                                               .shell = shell,
                                           }));
          return std::move(remeshed.mesh);
        },
        geo, options_.resolution, options_.solve, options_.shell);
  }

 private:
  MeshoptRemeshOptions options_;
};

}  // namespace

absl::StatusOr<Geometry> RemeshMeshMemoize(
    const Geometry& geo, const MeshoptRemeshOptions& options) {
  if (options.resolution < kMinVoxelRemeshResolution ||
      options.resolution > kMaxVoxelRemeshResolution) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Voxel remesh resolution must be in [%d, %d], but got %d.",
        kMinVoxelRemeshResolution, kMaxVoxelRemeshResolution,
        options.resolution));
  }
  if (!geo.GetExactGeometry().GetPrimitiveShapes().empty()) {
    return absl::InvalidArgumentError(
        "Voxel remeshing does not support primitive shapes.");
  }

  INTR_ASSIGN_OR_RETURN(
      ObjectRef<Mesh> mesh,
      geo.GetExactGeometry().visit(RemeshMeshMemoizeFunctor(options)));

  Geometry::Provenance provenance = {
      .human_readable_update_reason = "Voxel remesh",
      .previous_geometry = std::make_shared<const Geometry>(geo),
  };

  // Clear any stale precomputed renderable while preserving material
  // properties (matching `ComputeConvexHull`).
  return Geometry(
      ExactGeometry(std::move(mesh), geo.GetExactGeometry().options()),
      /*renderable=*/nullptr, /*keep_renderable=*/false,
      geo.material_properties(), std::move(provenance));
}

}  // namespace intrinsic::geo
