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

#ifndef INTRINSIC_GEOMETRY_API_COMPUTE_COACD_H_
#define INTRINSIC_GEOMETRY_API_COMPUTE_COACD_H_

#include <vector>

#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/geometry.h"

namespace intrinsic::geo {
struct CoacdOptions {
  double threshold = 0.05;
  int max_convex_hull = -1;
};

// Computes an approximate convex decomposition of the input geometry using the
// CoACD algorithm.
absl::StatusOr<std::vector<Geometry>> ComputeCoacd(
    const Geometry& geo, const CoacdOptions& options = {});

}  // namespace intrinsic::geo
#endif  // INTRINSIC_GEOMETRY_API_COMPUTE_COACD_H_
