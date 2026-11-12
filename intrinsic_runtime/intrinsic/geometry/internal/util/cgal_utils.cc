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

#include "intrinsic/geometry/internal/util/cgal_utils.h"

namespace intrinsic::geo {
std::vector<size_t> GetFaceVertices(const EpicSurfaceMesh3& mesh,
                                    size_t face_index) {
  EpicSurfaceMesh3::Face_index fi(face_index);
  std::vector<size_t> vertices;
  for (auto v : mesh.vertices_around_face(mesh.halfedge(fi))) {
    vertices.push_back(static_cast<size_t>(v));
  }
  return vertices;
}

}  // namespace intrinsic::geo
