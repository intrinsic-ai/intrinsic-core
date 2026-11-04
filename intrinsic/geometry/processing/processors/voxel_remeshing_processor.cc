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

#include "intrinsic/geometry/processing/processors/voxel_remeshing_processor.h"

#include <memory>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/remesh_mesh.h"
#include "intrinsic/geometry/api/remesh_mesh_memoize.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"
#include "intrinsic/geometry/processing/pipeline_config.pb.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/util/object_store/object_ref.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {

absl::StatusOr<std::unique_ptr<VoxelRemeshingGeometryProcessor>>
VoxelRemeshingGeometryProcessor::Create(
    const intrinsic_proto::geometry::VoxelRemeshingProcessorConfig& config) {
  if (config.resolution() < kMinVoxelRemeshResolution ||
      config.resolution() > kMaxVoxelRemeshResolution) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Voxel remesh resolution must be between %d and %d, but %d was "
        "provided.",
        kMinVoxelRemeshResolution, kMaxVoxelRemeshResolution,
        config.resolution()));
  }

  return absl::WrapUnique(
      new VoxelRemeshingGeometryProcessor(config.resolution()));
}

absl::StatusOr<std::vector<Geometry>> VoxelRemeshingGeometryProcessor::Process(
    longrunning::OperationContext& context, const Geometry& input) const {
  INTR_ASSIGN_OR_RETURN(
      Geometry result,
      RemeshMeshMemoize(input,
                        MeshoptRemeshOptions{.resolution = resolution_}));
  // Parts thinner than a voxel can disappear at low resolutions. An input that
  // was already empty stays empty without an error, since a higher resolution
  // would not help.
  INTR_ASSIGN_OR_RETURN(const ObjectRef<Mesh>& mesh,
                        result.GetExactGeometry().GetMesh());
  INTR_ASSIGN_OR_RETURN(const ObjectRef<Mesh>& input_mesh,
                        input.GetExactGeometry().GetMesh());
  if (mesh.Value().empty() && !input_mesh.Value().empty()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Voxel remeshing at resolution %d produced an empty mesh. Try a "
        "higher resolution.",
        resolution_));
  }
  context.AddProgress(1.0);
  return std::vector<Geometry>{std::move(result)};
}

}  // namespace intrinsic::geo
