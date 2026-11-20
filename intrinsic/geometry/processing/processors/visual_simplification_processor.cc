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

#include "intrinsic/geometry/processing/processors/visual_simplification_processor.h"

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/simplify_preserving_appearance.h"
#include "intrinsic/geometry/processing/pipeline_config.pb.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {

absl::StatusOr<std::unique_ptr<VisualSimplificationGeometryProcessor>>
VisualSimplificationGeometryProcessor::Create(
    const intrinsic_proto::geometry::VisualSimplificationProcessorConfig&
        config) {
  const double ratio = config.target_triangle_ratio();
  if (!std::isfinite(ratio) || ratio <= 0.0 || ratio > 1.0) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Target triangle ratio must be in (0, 1], but %f was provided.",
        ratio));
  }
  return absl::WrapUnique(new VisualSimplificationGeometryProcessor(ratio));
}

absl::StatusOr<std::vector<Geometry>>
VisualSimplificationGeometryProcessor::Process(
    longrunning::OperationContext& context, const Geometry& input) const {
  INTR_ASSIGN_OR_RETURN(Geometry result, SimplifyPreservingAppearance(
                                             input, target_triangle_ratio_,
                                             context.GetStopToken()));
  context.AddProgress(1.0);
  return std::vector<Geometry>{std::move(result)};
}

}  // namespace intrinsic::geo
