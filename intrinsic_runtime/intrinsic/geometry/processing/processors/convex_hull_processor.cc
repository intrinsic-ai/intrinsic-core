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

#include "intrinsic/geometry/processing/processors/convex_hull_processor.h"

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/compute_convex_hull.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/processing/pipeline_config.pb.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
absl::StatusOr<std::unique_ptr<ConvexHullGeometryProcessor>>
ConvexHullGeometryProcessor::Create(
    const intrinsic_proto::geometry::ConvexHullProcessorConfig& config) {
  return std::make_unique<ConvexHullGeometryProcessor>();
}

absl::StatusOr<std::vector<Geometry>> ConvexHullGeometryProcessor::Process(
    longrunning::OperationContext& context, const Geometry& input) const {
  INTR_ASSIGN_OR_RETURN(auto result, ComputeConvexHull(input));
  context.AddProgress(1.0);
  return std::vector<Geometry>{std::move(result)};
}

}  // namespace intrinsic::geo
