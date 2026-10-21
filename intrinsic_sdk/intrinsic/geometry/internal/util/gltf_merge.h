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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_UTIL_GLTF_MERGE_H_
#define INTRINSIC_GEOMETRY_INTERNAL_UTIL_GLTF_MERGE_H_

#include <string>
#include <vector>

#include "tiny_gltf.h"

namespace intrinsic::geo {

// Holds the starting index offsets in `main_model` where resources from a
// `sub_model` were appended by `MergeGltfResources`.
struct MergeGltfResourcesResult {
  int mesh_offset = 0;
};

// Appends the buffers, bufferViews, accessors, samplers, images, textures,
// materials, and meshes from `sub_model` into `main_model`, remapping all
// internal index references.
MergeGltfResourcesResult MergeGltfResources(const tinygltf::Model& sub_model,
                                            tinygltf::Model& main_model);

// Options for creating a geometry node and attaching a `sub_model`'s mesh and
// node hierarchy under `main_model.nodes[parent_node_idx]`.
struct AttachOptions {
  std::string node_name;
  std::vector<double> translation;
  std::vector<double> rotation;
  std::vector<double> scale;
  int parent_node_idx = 0;
  int mesh_offset = 0;
};

// Creates a new node in `main_model` configured by `options`, attaches the
// mesh and node hierarchy of `sub_model` (whose meshes start at
// `options.mesh_offset` in `main_model.meshes`) to that node, and adds it as a
// child of `main_model.nodes[options.parent_node_idx]`:
// 1. Single mesh with no extra node hierarchy/transform: assigns the mesh
//    directly to the new node without creating extra child nodes.
// 2. Non-trivial node hierarchy (`!sub_model.nodes.empty()`): copies
//    `sub_model.nodes` into `main_model.nodes` with remapped mesh and child
//    indices, and parents `sub_model`'s root nodes under the new node.
// 3. Multiple meshes with no nodes (`sub_model.nodes.empty()`): creates a
//    `<node_name>_part_<m>` child node under the new node for each mesh (since
//    a glTF node can reference only one mesh).
void AttachSubModelToModel(tinygltf::Model& main_model,
                           const tinygltf::Model& sub_model,
                           AttachOptions options);

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_INTERNAL_UTIL_GLTF_MERGE_H_
