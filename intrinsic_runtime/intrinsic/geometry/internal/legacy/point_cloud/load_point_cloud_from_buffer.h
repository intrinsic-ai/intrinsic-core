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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_LEGACY_POINT_CLOUD_LOAD_POINT_CLOUD_FROM_BUFFER_H_
#define INTRINSIC_GEOMETRY_INTERNAL_LEGACY_POINT_CLOUD_LOAD_POINT_CLOUD_FROM_BUFFER_H_

#include <string>

#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/shapes/point_cloud.h"
#include "intrinsic/util/object_store/object_ref.h"

namespace intrinsic::geo::legacy {

// Attempts to load the input file content as a point cloud. The currently
// supported file formats are pts and ply, other formats may work in the future.
absl::StatusOr<ObjectRef<PointCloud>> LoadPointCloudFromBuffer(
    const std::string& file_content, const std::string& extension,
    const eigenmath::Vector3d& scale);

}  // namespace intrinsic::geo::legacy
#endif  // INTRINSIC_GEOMETRY_INTERNAL_LEGACY_POINT_CLOUD_LOAD_POINT_CLOUD_FROM_BUFFER_H_
