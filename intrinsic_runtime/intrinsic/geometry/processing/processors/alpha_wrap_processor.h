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

#ifndef INTRINSIC_GEOMETRY_PROCESSING_PROCESSORS_ALPHA_WRAP_PROCESSOR_H_
#define INTRINSIC_GEOMETRY_PROCESSING_PROCESSORS_ALPHA_WRAP_PROCESSOR_H_

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/processing/pipeline_config.pb.h"
#include "intrinsic/geometry/processing/processor.h"
#include "intrinsic/longrunning/cc/operation_context.h"

namespace intrinsic::geo {
// A processor that applies the alpha wrap algorithm on the input geometry to
// create new output geometry.
class AlphaWrapGeometryProcessor : public GeometryProcessor {
 public:
  // Creates an instance of AlphaWrapGeometryProcessor from the given config.
  static absl::StatusOr<std::unique_ptr<AlphaWrapGeometryProcessor>> Create(
      const intrinsic_proto::geometry::AlphaWrapProcessorConfig& config);

  absl::StatusOr<std::vector<Geometry>> Process(
      longrunning::OperationContext& context,
      const Geometry& input) const override;

 private:
  explicit AlphaWrapGeometryProcessor(double alpha);

  double alpha_;
};

}  // namespace intrinsic::geo
#endif  // INTRINSIC_GEOMETRY_PROCESSING_PROCESSORS_ALPHA_WRAP_PROCESSOR_H_
