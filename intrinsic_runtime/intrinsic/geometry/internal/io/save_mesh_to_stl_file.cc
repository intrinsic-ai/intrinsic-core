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

#include "intrinsic/geometry/internal/io/save_mesh_to_stl_file.h"

#include <cstdint>
#include <fstream>
#include <vector>

#include "Eigen/Core"
#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"
#include "intrinsic/util/status/status_macros.h"
#include "ortools/base/file.h"
#include "ortools/base/options.h"

namespace intrinsic::geo {
namespace {

const absl::string_view kStlHeader =
    "STL File                                                                  "
    "      ";

struct Face3f {
  explicit Face3f(const eigenmath::Vector3d& p1, const eigenmath::Vector3d& p2,
                  const eigenmath::Vector3d& p3)
      : v1_x(p1.x()),
        v1_y(p1.y()),
        v1_z(p1.z()),
        v2_x(p2.x()),
        v2_y(p2.y()),
        v2_z(p2.z()),
        v3_x(p3.x()),
        v3_y(p3.y()),
        v3_z(p3.z()) {
    const auto n = (p2 - p1).cross(p3 - p1);
    normal_x = n.x();
    normal_y = n.y();
    normal_z = n.z();
  }

  float normal_x = 0.0;
  float normal_y = 0.0;
  float normal_z = 0.0;

  float v1_x;
  float v1_y;
  float v1_z;

  float v2_x;
  float v2_y;
  float v2_z;

  float v3_x;
  float v3_y;
  float v3_z;

  int16_t attributes = 0;
} ABSL_ATTRIBUTE_PACKED;

static_assert(sizeof(Face3f) == 50, "Que?");

}  // namespace

absl::Status SaveMeshToBinaryStlFile(absl::string_view filename,
                                     const Mesh& mesh) {
  const uint32_t num_faces = mesh.face_count();
  const auto& vertices = mesh.vertices();

  std::ofstream fout;
  fout.open(filename, std::ios_base::binary);
  fout.write((char*)kStlHeader.data(), kStlHeader.size());
  fout.write((char*)&num_faces, 4);
  for (const auto& f : mesh.faces()) {
    const Face3f face_data(vertices[f[0]], vertices[f[1]], vertices[f[2]]);
    fout.write((char*)&face_data, sizeof(Face3f));
  }
  fout.close();
  return absl::OkStatus();
}

}  // namespace intrinsic::geo
