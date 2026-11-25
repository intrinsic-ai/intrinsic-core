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

#ifndef INTRINSIC_GEOMETRY_PROCESSING_PIPELINE_BUILDER_H_
#define INTRINSIC_GEOMETRY_PROCESSING_PIPELINE_BUILDER_H_

#include <memory>

#include "absl/status/statusor.h"
#include "intrinsic/geometry/processing/pipeline.h"
#include "intrinsic/geometry/processing/pipeline_config.pb.h"

namespace intrinsic::geo {
// Returns a new pipeline created from the given configuration.
absl::StatusOr<std::unique_ptr<GeometryProcessingPipeline>>
BuildGeometryPipeline(
    const intrinsic_proto::geometry::PipelineConfiguration& config);

}  // namespace intrinsic::geo
#endif  // INTRINSIC_GEOMETRY_PROCESSING_PIPELINE_BUILDER_H_
