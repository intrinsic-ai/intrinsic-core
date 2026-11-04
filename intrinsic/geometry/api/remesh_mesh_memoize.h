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

#ifndef INTRINSIC_GEOMETRY_API_REMESH_MESH_MEMOIZE_H_
#define INTRINSIC_GEOMETRY_API_REMESH_MESH_MEMOIZE_H_

#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/remesh_mesh.h"

namespace intrinsic::geo {

// Constructs a new `Geometry` by voxel-remeshing the input triangle mesh using
// `RemeshMesh` (`meshopt_remesh`) with `options`. Rejects primitive-shape and
// point-cloud geometries.
//
// Unlike `RemeshMesh`, which operates directly on an in-memory `Mesh` without
// object-store dependencies, `RemeshMeshMemoize` operates on `Geometry`,
// memoizes the remeshed `ObjectRef<Mesh>` in the object store for identical
// inputs, and records `Geometry` provenance.
absl::StatusOr<Geometry> RemeshMeshMemoize(
    const Geometry& geo, const MeshoptRemeshOptions& options = {});

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_API_REMESH_MESH_MEMOIZE_H_
