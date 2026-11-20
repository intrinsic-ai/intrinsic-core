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

#ifndef INTRINSIC_GEOMETRY_GLTF_GLTF_VALIDATION_H_
#define INTRINSIC_GEOMETRY_GLTF_GLTF_VALIDATION_H_

#include "absl/status/status.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {

// Checks the glTF rules gltf_buffer_reader and gltf_buffer_writer rely on; run
// it on a parsed model before editing buffers and again before serializing.
// Checks that:
// - every referenced accessor, bufferView, buffer, mesh and skin exists, and
//   accessor data (including sparse indices and values) lies inside its
//   buffer;
// - buffers, bufferViews and accessors are non-empty, and sparse indices are
//   unsigned integers below the accessor's count;
// - every primitive has a POSITION, its attributes and morph targets have the
//   same count, and its indices are unsigned integers below it and below the
//   largest value of their type (which glTF forbids);
// - POSITION is a float or 8/16-bit integer VEC3 with finite values and
//   bounds;
// - nodes with a skin or morph weights have a mesh.
// Returns InvalidArgumentError otherwise. Non-sparse accessors without a
// bufferView, and matrices with 8/16-bit components, are rejected although
// glTF allows them.
absl::Status ValidateGltfBuffers(const tinygltf::Model& model);

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_GLTF_GLTF_VALIDATION_H_
