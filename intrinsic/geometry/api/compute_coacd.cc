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

#include "intrinsic/geometry/api/compute_coacd.h"

#include <memory>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "coacd.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/internal/conversion_utils/mesh_conversion.h"
#include "intrinsic/geometry/internal/indexed_triangle_set_3/indexed_triangle_set_3.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"
#include "intrinsic/util/demangle.h"
#include "intrinsic/util/object_store/memoize.h"
#include "intrinsic/util/object_store/object_ref.h"
#include "intrinsic/util/object_store/object_store.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
namespace {

absl::StatusOr<std::vector<coacd::Mesh>> RunCoACDSafely(
    const coacd::Mesh& input_mesh, const CoacdOptions& options) {
  // CoACD parameter constants
  // Number of points to sample on the mesh surface for checking decomposition
  // quality.
  constexpr int kSampleResolution = 2000;
  // Maximum depth of the MCTS search tree.
  constexpr int kMctsMaxDepth = 3;
  // Disable PCA alignment to preserve original coordinates.
  constexpr bool kPca = false;
  // Disable decimation to preserve original mesh density.
  constexpr bool kDecimate = false;
  // Maximum number of vertices per decomposed convex hull.
  constexpr int kMaxChVertex = 256;
  // Disable extrusion.
  constexpr bool kExtrude = false;
  // Extrusion margin (unused when extrusion is disabled).
  constexpr double kExtrudeMargin = 0.01;
  // Use convex hull approximation mode.
  constexpr char kApxMode[] = "ch";
  // Preprocess resolution.
  constexpr int kPreprocessResolution = 50;
  // Preprocessing mode.
  constexpr char kPreprocessMode[] = "auto";
  // Number of MCTS nodes.
  constexpr int kMctsNodes = 20;
  // Number of MCTS iterations.
  constexpr int kMctsIterations = 150;
  // Solver seed (strictly deterministic).
  constexpr int kSeed = 0;
  // Enable mesh merging to guarantee max_convex_hull limits are respected.
  constexpr bool kMerge = true;

  // Call CoACD with "auto" preprocessing mode.
  try {
    return coacd::CoACD(input_mesh, options.threshold, options.max_convex_hull,
                        kPreprocessMode, kPreprocessResolution,
                        kSampleResolution, kMctsNodes, kMctsIterations,
                        kMctsMaxDepth, kPca, kMerge, kDecimate, kMaxChVertex,
                        kExtrude, kExtrudeMargin, kApxMode, kSeed);
  } catch (const std::exception& e) {
    return absl::InternalError(absl::StrCat("CoACD failed: ", e.what()));
  } catch (...) {
    return absl::InternalError("CoACD failed with unknown fatal error");
  }
}

class ComputeCoacdFunctor {
 public:
  explicit ComputeCoacdFunctor(const CoacdOptions& options)
      : options_(options) {}

  template <typename Geo>
  absl::StatusOr<ObjectRef<std::vector<ObjectRef<Mesh>>>> operator()(
      const Geo& geo) const {
    LOG(WARNING) << "WARNING: intrinsic::ComputeCoacdFunctor does not support\n"
                 << "operator()(const Geo&)) const, with \n"
                 << "Geo = " << Demangle<Geo>();
    return absl::InvalidArgumentError(
        "ComputeCoacdFunctor does not support argument type.");
  }

  template <>
  absl::StatusOr<ObjectRef<std::vector<ObjectRef<Mesh>>>> operator()(
      const ObjectRef<PointCloud>&) const {
    return absl::InvalidArgumentError(
        "ComputeCoacd does not support Point Cloud geometry.");
  }

  template <>
  absl::StatusOr<ObjectRef<std::vector<ObjectRef<Mesh>>>> operator()(
      const ObjectRef<Mesh>& geo) const {
    // We use the default MemoizeOptions which has
    // save_memoize_on_cancellation = false. This ensures that if the
    // operation is cancelled, the cancelled state is not cached.
    // Note that other errors (like deterministic CoACD failures)
    // WILL be cached, which is desired as they are deterministic.
    return Memoize(
        MemoizeOptions("ComputeCoacdFunctor-v1"),
        [](ObjectRef<Mesh> geo, double threshold, int max_convex_hull)
            -> absl::StatusOr<std::vector<ObjectRef<Mesh>>> {
          if (geo.Value().empty()) {
            return std::vector<ObjectRef<Mesh>>();
          }
          // Convert the mesh to a clean coacd::Mesh using ToCoacdMesh(), which
          // deduplicates coincident vertices, removes isolated points, and
          // cleans polygon soup before feeding to CoACD.

          EpicIndexedTriangleSet3 indexed_triangle_set =
              ToEpicIndexedTriangleSet3(geo.Value());

          INTR_ASSIGN_OR_RETURN(coacd::Mesh coacd_input_mesh,
                                ToCoacdMesh(std::move(indexed_triangle_set)));

          CoacdOptions options{
              .threshold = threshold,
              .max_convex_hull = max_convex_hull,
          };
          INTR_ASSIGN_OR_RETURN(std::vector<coacd::Mesh> decomposed_meshes,
                                RunCoACDSafely(coacd_input_mesh, options));

          std::vector<EpicIndexedTriangleSet3> output_hulls =
              FromCoacdMeshes(decomposed_meshes);

          std::vector<ObjectRef<Mesh>> hull_refs;
          hull_refs.reserve(output_hulls.size());
          for (const auto& hull : output_hulls) {
            // DeDuplicate puts each generated convex hull Mesh into the central
            // Global Object Store (the shared deduplication cache for
            // geometry). Since convex decomposition often yields
            // duplicate/overlapping hulls across separate bodies in path
            // planning, this maps them in-memory to the same immutable pointer
            // references (ObjectRef), vastly reducing the overall RAM footprint
            // and serialization runtime overhead.
            hull_refs.push_back(DeDuplicate(ToMesh(hull)));
          }
          return hull_refs;
        },
        geo, options_.threshold, options_.max_convex_hull);
  }

 private:
  CoacdOptions options_;
};

absl::Status ValidateCoacdOptions(const CoacdOptions& options) {
  if (options.threshold < 0.0 || options.threshold > 1.0) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "threshold must be between 0.0 and 1.0 but %f was provided.",
        options.threshold));
  }
  if (options.max_convex_hull < 1 && options.max_convex_hull != -1) {
    return absl::InvalidArgumentError(
        absl::StrFormat("max_convex_hull must be at least 1 or -1 (unlimited) "
                        "but %d was provided.",
                        options.max_convex_hull));
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::vector<Geometry>> ComputeCoacd(
    const Geometry& geo, const CoacdOptions& options) {
  INTR_RETURN_IF_ERROR(ValidateCoacdOptions(options));
  INTR_ASSIGN_OR_RETURN(auto refs_ref, geo.GetExactGeometry().visit(
                                           ComputeCoacdFunctor(options)));

  std::vector<Geometry> geometries;
  geometries.reserve(refs_ref.Value().size());
  for (const auto& mesh_ref : refs_ref.Value()) {
    Geometry::Provenance provenance = {
        .human_readable_update_reason =
            "Compute approximate convex decomposition hull",
        .previous_geometry = std::make_shared<const Geometry>(geo),
    };
    geometries.push_back(
        Geometry(ExactGeometry(mesh_ref, geo.GetExactGeometry().options()),
                 /*renderable=*/nullptr, /*keep_renderable=*/false,
                 geo.material_properties(), std::move(provenance)));
  }
  return geometries;
}

}  // namespace intrinsic::geo
