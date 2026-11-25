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

#include "intrinsic/geometry/processing/pipeline.h"

#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/processing/processor.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/stop_token.h"

namespace intrinsic::geo {
GeometryProcessingPipeline::GeometryProcessingPipeline(
    std::vector<std::unique_ptr<GeometryProcessor>> processors)
    : processors_(std::move(processors)) {}

absl::StatusOr<std::vector<Geometry>> GeometryProcessingPipeline::Process(
    longrunning::OperationContext& context, const Geometry& input) const {
  std::vector<Geometry> geometries = {input};

  context.PushProgressMultiplier(1.0 / processors_.size());
  absl::Cleanup pipeline_cleanup = [&context] {
    context.PopProgressMultiplier();
  };
  for (size_t i = 0; i < processors_.size(); ++i) {
    if (context.GetStopToken().stop_requested()) {
      return absl::CancelledError("Stop requested while processing");
    }

    std::vector<Geometry> next_geometries;
    {
      context.PushProgressMultiplier(1.0 / geometries.size());
      absl::Cleanup step_cleanup = [&context] {
        context.PopProgressMultiplier();
      };
      for (const auto& geo : geometries) {
        INTR_ASSIGN_OR_RETURN(auto processed,
                              processors_[i]->Process(context, geo));
        next_geometries.insert(next_geometries.end(),
                               std::make_move_iterator(processed.begin()),
                               std::make_move_iterator(processed.end()));
      }
    }
    geometries = std::move(next_geometries);
  }

  return geometries;
}

}  // namespace intrinsic::geo
