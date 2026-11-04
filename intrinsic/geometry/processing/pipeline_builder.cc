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

#include "intrinsic/geometry/processing/pipeline_builder.h"

#include <memory>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/geometry/processing/pipeline.h"
#include "intrinsic/geometry/processing/pipeline_config.pb.h"
#include "intrinsic/geometry/processing/processor.h"
#include "intrinsic/geometry/processing/processors/alpha_wrap_processor.h"
#include "intrinsic/geometry/processing/processors/coacd_processor.h"
#include "intrinsic/geometry/processing/processors/convex_hull_processor.h"
#include "intrinsic/geometry/processing/processors/mesh_simplification_processor.h"
#include "intrinsic/geometry/processing/processors/noop_processor.h"
#include "intrinsic/geometry/processing/processors/scale_processor.h"
#include "intrinsic/geometry/processing/processors/voxel_remeshing_processor.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
absl::StatusOr<std::unique_ptr<GeometryProcessingPipeline>>
BuildGeometryPipeline(
    const intrinsic_proto::geometry::PipelineConfiguration& config) {
  if (config.steps_size() == 0) {
    return absl::InvalidArgumentError("Cannot create a pipeline with no steps");
  }

  std::vector<std::unique_ptr<GeometryProcessor>> processors;
  processors.reserve(config.steps_size());

  for (const auto& step_config : config.steps()) {
    switch (step_config.config_case()) {
      case intrinsic_proto::geometry::PipelineStep::kNoOp: {
        INTR_ASSIGN_OR_RETURN(
            auto processor, NoOpGeometryProcessor::Create(step_config.no_op()));
        processors.emplace_back(std::move(processor));
        break;
      }
      case intrinsic_proto::geometry::PipelineStep::kAlphaWrap: {
        INTR_ASSIGN_OR_RETURN(
            auto processor,
            AlphaWrapGeometryProcessor::Create(step_config.alpha_wrap()));
        processors.emplace_back(std::move(processor));
        break;
      }
      case intrinsic_proto::geometry::PipelineStep::kConvexHull: {
        INTR_ASSIGN_OR_RETURN(
            auto processor,
            ConvexHullGeometryProcessor::Create(step_config.convex_hull()));
        processors.emplace_back(std::move(processor));
        break;
      }
      case intrinsic_proto::geometry::PipelineStep::kMeshSimplification: {
        INTR_ASSIGN_OR_RETURN(auto processor,
                              MeshSimplificationGeometryProcessor::Create(
                                  step_config.mesh_simplification()));
        processors.emplace_back(std::move(processor));
        break;
      }
      case intrinsic_proto::geometry::PipelineStep::kScale: {
        INTR_ASSIGN_OR_RETURN(auto processor,
                              ScaleProcessor::Create(step_config.scale()));
        processors.emplace_back(std::move(processor));
        break;
      }
      case intrinsic_proto::geometry::PipelineStep::kCoacd: {
        INTR_ASSIGN_OR_RETURN(auto processor, CoacdGeometryProcessor::Create(
                                                  step_config.coacd()));
        processors.emplace_back(std::move(processor));
        break;
      }
      case intrinsic_proto::geometry::PipelineStep::kVoxelRemeshing: {
        INTR_ASSIGN_OR_RETURN(auto processor,
                              VoxelRemeshingGeometryProcessor::Create(
                                  step_config.voxel_remeshing()));
        processors.emplace_back(std::move(processor));
        break;
      }
      case intrinsic_proto::geometry::PipelineStep::CONFIG_NOT_SET: {
        return absl::InvalidArgumentError("Step has invalid config");
      }
    }
  }

  return std::make_unique<GeometryProcessingPipeline>(std::move(processors));
}

}  // namespace intrinsic::geo
