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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_UTIL_CGAL_UTILS_H_
#define INTRINSIC_GEOMETRY_INTERNAL_UTIL_CGAL_UTILS_H_

#include <exception>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "intrinsic/geometry/internal/surface_mesh_3/surface_mesh_3d.h"

namespace intrinsic::geo {
/**
 * Wraps a callable in a try-catch block and converts any thrown
 * exceptions into an absl::StatusOr. Use this to wrap CGAL calls, which throw
 * an exception when certain pre-/post-conditions are not met. This causes
 * crashes unless the exceptions are handled.
 *
 * @param func The lambda or callable to execute, containing CGAL calls.
 * @return The result of the lambda wrapped in StatusOr, or an InternalError
 * if an exception was caught.
 *
 * Usage:
 * auto res = ProtectedCGALCall([&]() {
 *   return PMP::centroid(mesh);
 * });
 * if (res.ok()) {
 *   ...
 * }
 */
template <typename Func>
auto ProtectedCGALCall(Func func)
    -> std::conditional_t<std::is_void_v<std::invoke_result_t<Func>>,
                          absl::Status,
                          absl::StatusOr<std::invoke_result_t<Func>>> {
  using ReturnType = std::invoke_result_t<Func>;
  try {
    if constexpr (std::is_void_v<ReturnType>) {
      func();
      return absl::OkStatus();
    } else {
      return func();
    }
  } catch (const std::exception& e) {
    return absl::InternalError(
        absl::StrCat("Error during CGAL processing: ", e.what()));
  } catch (...) {
    return absl::UnknownError("Unknown exception caught during CGAL call.");
  }
}

// Returns the vertices indices for the face with the given index in the mesh.
std::vector<size_t> GetFaceVertices(const EpicSurfaceMesh3& mesh,
                                    size_t face_index);

}  // namespace intrinsic::geo
#endif  // INTRINSIC_GEOMETRY_INTERNAL_UTIL_CGAL_UTILS_H_
