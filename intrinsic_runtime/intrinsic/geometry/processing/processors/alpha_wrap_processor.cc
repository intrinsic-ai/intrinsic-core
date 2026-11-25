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

#include "intrinsic/geometry/processing/processors/alpha_wrap_processor.h"

#include <memory>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/construct_alpha_hull.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/processing/pipeline_config.pb.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
absl::StatusOr<std::unique_ptr<AlphaWrapGeometryProcessor>>
AlphaWrapGeometryProcessor::Create(
    const intrinsic_proto::geometry::AlphaWrapProcessorConfig& config) {
  if (config.alpha() <= 0.0) {
    return absl::InvalidArgumentError(
        "Alpha wrap requires a positive alpha value");
  }

  return absl::WrapUnique(new AlphaWrapGeometryProcessor(config.alpha()));
}

AlphaWrapGeometryProcessor::AlphaWrapGeometryProcessor(double alpha)
    : alpha_(alpha) {}

absl::StatusOr<std::vector<Geometry>> AlphaWrapGeometryProcessor::Process(
    longrunning::OperationContext& context, const Geometry& input) const {
  INTR_ASSIGN_OR_RETURN(auto result, ConstructAlphaHull(input, alpha_));
  context.AddProgress(1.0);
  return std::vector<Geometry>{std::move(result)};
}

}  // namespace intrinsic::geo
