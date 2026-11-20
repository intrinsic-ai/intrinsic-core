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

#include "intrinsic/geometry/internal/legacy/point_cloud/load_point_cloud_from_buffer.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/internal/legacy/point_cloud/ai_scene_to_point_cloud.h"
#include "intrinsic/geometry/internal/mesh/io/load_ai_scene_from_buffer.h"
#include "intrinsic/geometry/internal/point_cloud/point_cloud_riegeli_coder.h"  // IWYU pragma: keep
#include "intrinsic/geometry/shapes/point_cloud.h"
#include "intrinsic/util/object_store/object_ref.h"
#include "intrinsic/util/object_store/object_store.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo::legacy {

absl::StatusOr<ObjectRef<PointCloud>> LoadPointCloudFromBuffer(
    const std::string& file_content, const std::string& extension,
    const eigenmath::Vector3d& scale) {
  INTR_ASSIGN_OR_RETURN(auto scene,
                        LoadAiSceneFromBuffer(file_content, extension));
  INTR_ASSIGN_OR_RETURN(
      PointCloud point_cloud,
      AiSceneToPointCloud(*scene, scale, /*compute_missing_normals=*/false));
  return DeDuplicate(std::move(point_cloud));
}

}  // namespace intrinsic::geo::legacy
