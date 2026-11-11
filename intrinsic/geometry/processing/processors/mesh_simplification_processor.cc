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

#include "intrinsic/geometry/processing/processors/mesh_simplification_processor.h"

#include <cmath>
#include <memory>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/simplify_mesh_strict_memoize.h"
#include "intrinsic/geometry/processing/pipeline_config.pb.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/util/status/status_macros.h"

constexpr absl::Duration kDefaultSimplificationTimeLimit = absl::Minutes(2);

namespace intrinsic::geo {
absl::StatusOr<std::unique_ptr<MeshSimplificationGeometryProcessor>>
MeshSimplificationGeometryProcessor::Create(
    const intrinsic_proto::geometry::MeshSimplificationProcessorConfig&
        config) {
  if (!std::isfinite(config.max_hausdorff_distance()) ||
      config.max_hausdorff_distance() < 0.0) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Hausdorff distance must be finite and greater than or equal to 0.0 "
        "but %f was provided.",
        config.max_hausdorff_distance()));
  }
  INTR_ASSIGN_OR_RETURN(absl::Duration timeout,
                        ToAbslDuration(config.timeout()));
  if (timeout < absl::ZeroDuration()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Timeout length must be greater than or equal to 0.0 but got :",
        absl::FormatDuration(timeout), "secs"));
  }

  return absl::WrapUnique(new MeshSimplificationGeometryProcessor(
      config.max_hausdorff_distance(), timeout));
}

MeshSimplificationGeometryProcessor::MeshSimplificationGeometryProcessor(
    double max_hausdorff_distance, absl::Duration timeout)
    : max_hausdorff_distance_(max_hausdorff_distance), timeout_(timeout) {}

absl::StatusOr<std::vector<Geometry>>
MeshSimplificationGeometryProcessor::Process(
    longrunning::OperationContext& context, const Geometry& input) const {
  const StopToken stopToken = context.GetStopToken();
  const absl::Duration timeout = timeout_ == absl::ZeroDuration()
                                     ? kDefaultSimplificationTimeLimit
                                     : timeout_;
  INTR_ASSIGN_OR_RETURN(const TimeoutPredicate custom_early_stop_predicate,
                        TimeoutPredicate::Create(timeout));
  INTR_ASSIGN_OR_RETURN(
      Geometry result,
      SimplifyMeshStrictMemoize(input, max_hausdorff_distance_, stopToken,
                                custom_early_stop_predicate));
  context.AddProgress(1.0);
  return std::vector<Geometry>{std::move(result)};
}

}  // namespace intrinsic::geo
