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

#include "intrinsic/geometry/api/simplify_mesh_strict_memoize.h"

#include <cmath>
#include <memory>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/internal/conversion_utils/mesh_conversion.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"
#include "intrinsic/geometry/internal/mesh_simplification/meshopt_locality_simplifier.h"
#include "intrinsic/geometry/internal/util/timeout_predicate.h"
#include "intrinsic/util/object_store/memoize.h"
#include "intrinsic/util/object_store/object_ref.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/stop_token.h"

namespace intrinsic::geo {
namespace {

class SimplifyMeshStrictMemoizeFunctor {
 public:
  explicit SimplifyMeshStrictMemoizeFunctor(
      double max_hausdorff_distance, StopToken stop_token,
      const TimeoutPredicate& custom_early_stop_predicate)
      : max_hausdorff_distance_(max_hausdorff_distance),
        stop_token_(std::move(stop_token)),
        custom_early_stop_predicate_(custom_early_stop_predicate) {}

  template <typename Geo>
  absl::StatusOr<ObjectRef<Mesh>> operator()(const Geo& geo) const {
    LOG(WARNING) << "WARNING: intrinsic::SimplifyMeshStrictMemoizeFunctor does "
                    "not support\n"
                 << "operator()(const Geo&)) const, with \n"
                 << "Geo = " << Demangle<Geo>();
    return absl::InvalidArgumentError(
        "SimplifyMeshStrictMemoizeFunctor does not support argument "
        "type.");
  }

  // Simplifies `geo`, caching the result.
  template <>
  absl::StatusOr<ObjectRef<Mesh>> operator()(const ObjectRef<Mesh>& geo) const {
    return Memoize(
        MemoizeOptions("SimplifyMeshStrictMemoizeFunctor-v1"),
        [](ObjectRef<Mesh> geo, double max_hausdorff_distance,
           const StopToken& stop_token,
           const TimeoutPredicate& custom_early_stop_predicate)
            -> absl::StatusOr<Mesh> {
          if (stop_token.stop_requested() ||
              custom_early_stop_predicate.HasTimedOut()) {
            return absl::CancelledError(
                "SimplifyMeshStrictMemoizeFunctor stopped early.");
          }

          INTR_ASSIGN_OR_RETURN(const MeshoptBuffers buffers,
                                ToMeshoptBuffers(geo.Value()));
          absl::StatusOr<MeshoptLocalityResult> result =
              SimplifyWithLocalityBound(buffers, max_hausdorff_distance,
                                        stop_token, custom_early_stop_predicate,
                                        /*regularize=*/true);
          if (absl::IsDeadlineExceeded(result.status())) {
            return absl::CancelledError(result.status().message());
          }
          INTR_RETURN_IF_ERROR(result.status());
          // Nothing to simplify, e.g. only degenerate triangles.
          if (result->buffers.indices.empty()) {
            return geo.Value().Clone();
          }
          // On a timeout part way through, this is the partly simplified mesh.
          return FromMeshoptBuffers(result->buffers);
        },
        geo, max_hausdorff_distance_, stop_token_,
        custom_early_stop_predicate_);
  }

 private:
  double max_hausdorff_distance_;
  StopToken stop_token_;
  const TimeoutPredicate& custom_early_stop_predicate_;
};

}  // namespace

absl::StatusOr<Geometry> SimplifyMeshStrictMemoize(
    const Geometry& geo, double max_hausdorff_distance, StopToken stop_token,
    const TimeoutPredicate& custom_early_stop_predicate) {
  if (!std::isfinite(max_hausdorff_distance) || max_hausdorff_distance < 0.0) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Max Hausdorff distance must be greater than or equal "
                        "to 0.0, but %f was provided.",
                        max_hausdorff_distance));
  }
  // Checked before the cache, so a stop request is honoured even if the
  // result is already cached.
  if (stop_token.stop_requested()) {
    return absl::CancelledError("SimplifyMeshStrictMemoize cancelled.");
  }
  if (max_hausdorff_distance == 0.0) {
    return geo;
  }
  if (!geo.GetExactGeometry().GetPrimitiveShapes().empty()) {
    return absl::InvalidArgumentError(
        "Simplified mesh does not support primitive shapes.");
  }
  INTR_ASSIGN_OR_RETURN(const ObjectRef<Mesh>& mesh,
                        geo.GetExactGeometry().GetMesh());
  if (mesh.Value().empty()) {
    LOG(WARNING) << "Empty mesh provided to SimplifyMeshStrictMemoize.";
    return geo;
  }
  if (custom_early_stop_predicate.HasTimedOut()) {
    return geo;
  }
  absl::StatusOr<ObjectRef<Mesh>> simplified_mesh =
      geo.GetExactGeometry().visit(SimplifyMeshStrictMemoizeFunctor(
          max_hausdorff_distance, stop_token, custom_early_stop_predicate));
  if (absl::IsCancelled(simplified_mesh.status()) &&
      !stop_token.stop_requested() &&
      custom_early_stop_predicate.HasTimedOut()) {
    // Timed out before any simplification was done.
    return geo;
  }
  INTR_RETURN_IF_ERROR(simplified_mesh.status());
  Geometry::Provenance provenance = {
      .human_readable_update_reason = "Construct simplified mesh",
      .previous_geometry = std::make_shared<const Geometry>(geo),
  };

  // Clear any stale precomputed renderable while preserving material
  // properties (matching `ComputeConvexHull` and `RemeshMeshMemoize`).
  return Geometry(ExactGeometry(*std::move(simplified_mesh),
                                geo.GetExactGeometry().options()),
                  /*renderable=*/nullptr, /*keep_renderable=*/false,
                  geo.material_properties(), std::move(provenance));
}

}  // namespace intrinsic::geo
