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

#include "intrinsic/geometry/internal/point_cloud/pts_to_point_cloud.h"

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/internal/point_cloud/validate_pts.h"
#include "intrinsic/geometry/shapes/point_cloud.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {

absl::StatusOr<PointCloud> PtsFileToPointCloud(
    const std::string& file_content) {
  return PtsFileToPointCloud(file_content, eigenmath::Vector3d::Ones());
}

absl::StatusOr<PointCloud> PtsFileToPointCloud(
    const std::string& file_content, const eigenmath::Vector3d& scale) {
  std::stringstream stream(file_content);
  INTR_ASSIGN_OR_RETURN(
      const int num_vertices,
      ParseAndValidatePtsPointCount(stream, file_content.size()));
  std::vector<eigenmath::Vector3d> points;
  points.reserve(num_vertices);

  for (int i = 0; i < num_vertices; ++i) {
    float x, y, z;
    int r, g, b;
    int intensity;
    stream >> x >> y >> z;
    stream >> intensity;
    stream >> r >> g >> b;
    if (stream.bad() || stream.fail()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Bad point cloud data on line ", i + 1));
    }
    points.emplace_back(x * scale.x(), y * scale.y(), z * scale.z());
  }

  return PointCloud(std::move(points), {});
}

}  // namespace intrinsic::geo
