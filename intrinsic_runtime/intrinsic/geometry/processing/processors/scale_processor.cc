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

#include "intrinsic/geometry/processing/processors/scale_processor.h"

#include <memory>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/substitute.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/apply_transform.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
absl::StatusOr<std::unique_ptr<ScaleProcessor>> ScaleProcessor::Create(
    const intrinsic_proto::geometry::ScaleProcessorConfig& config) {
  if (config.scale_factor() <= 0.0) {
    return absl::InvalidArgumentError(
        absl::Substitute("Scale factor must be positive but $0 was provided.",
                         config.scale_factor()));
  }

  return absl::WrapUnique(new ScaleProcessor(config.scale_factor()));
}

absl::StatusOr<std::vector<Geometry>> ScaleProcessor::Process(
    longrunning::OperationContext& context, const Geometry& input) const {
  const eigenmath::Matrix4d scale_matrix =
      eigenmath::Vector4d(scale_, scale_, scale_, 1.0).asDiagonal();

  INTR_ASSIGN_OR_RETURN(Geometry scaled_geometry,
                        ApplyTransform(input, scale_matrix));
  context.AddProgress(1.0);
  return std::vector<Geometry>{std::move(scaled_geometry)};
}

ScaleProcessor::ScaleProcessor(double scale) : scale_(scale) {}

}  // namespace intrinsic::geo
