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

#include "intrinsic/geometry/internal/util/gltf_merge.h"

#include <cstddef>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {
namespace {

void MergeMaterials(const tinygltf::Model& sub_model, int texture_offset,
                    tinygltf::Model& main_model) {
  for (auto mat : sub_model.materials) {
    if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0) {
      mat.pbrMetallicRoughness.baseColorTexture.index += texture_offset;
    }
    if (mat.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
      mat.pbrMetallicRoughness.metallicRoughnessTexture.index += texture_offset;
    }
    if (mat.normalTexture.index >= 0) {
      mat.normalTexture.index += texture_offset;
    }
    if (mat.occlusionTexture.index >= 0) {
      mat.occlusionTexture.index += texture_offset;
    }
    if (mat.emissiveTexture.index >= 0) {
      mat.emissiveTexture.index += texture_offset;
    }
    main_model.materials.push_back(std::move(mat));
  }
}

void MergeMeshes(const tinygltf::Model& sub_model, int accessor_offset,
                 int material_offset, tinygltf::Model& main_model) {
  for (auto mesh : sub_model.meshes) {
    for (auto& prim : mesh.primitives) {
      if (prim.indices >= 0) {
        prim.indices += accessor_offset;
      }
      for (auto& [attr, acc_idx] : prim.attributes) {
        acc_idx += accessor_offset;
      }
      if (prim.material >= 0) {
        prim.material += material_offset;
      }
    }
    main_model.meshes.push_back(std::move(mesh));
  }
}

bool IsSingleTrivialMeshNode(const tinygltf::Model& sub_model) {
  return sub_model.nodes.size() == 1 && sub_model.nodes[0].mesh >= 0 &&
         sub_model.nodes[0].children.empty() &&
         sub_model.nodes[0].translation.empty() &&
         sub_model.nodes[0].rotation.empty() &&
         sub_model.nodes[0].scale.empty() && sub_model.nodes[0].matrix.empty();
}

void AttachSubModelNodeHierarchy(const tinygltf::Model& sub_model,
                                 int mesh_offset, tinygltf::Node& geom_node,
                                 tinygltf::Model& main_model) {
  const int node_base = main_model.nodes.size();
  for (auto sub_node : sub_model.nodes) {
    if (sub_node.mesh >= 0) {
      sub_node.mesh += mesh_offset;
    }
    for (int& child_idx : sub_node.children) {
      child_idx += node_base;
    }
    main_model.nodes.push_back(std::move(sub_node));
  }
  std::vector<int> sub_roots;
  const int scene_idx = (sub_model.defaultScene >= 0 &&
                         sub_model.defaultScene < sub_model.scenes.size())
                            ? sub_model.defaultScene
                            : 0;
  if (!sub_model.scenes.empty() && scene_idx < sub_model.scenes.size()) {
    sub_roots = sub_model.scenes[scene_idx].nodes;
  } else {
    for (size_t i = 0; i < sub_model.nodes.size(); ++i) {
      sub_roots.push_back(i);
    }
  }
  for (const int root_idx : sub_roots) {
    geom_node.children.push_back(node_base + root_idx);
  }
}

void AttachMultiMeshPartNodes(size_t num_meshes, int mesh_offset,
                              tinygltf::Node& geom_node,
                              tinygltf::Model& main_model) {
  for (size_t m = 0; m < num_meshes; ++m) {
    tinygltf::Node part_node;
    part_node.name = absl::StrCat(geom_node.name, "_part_", m);
    part_node.mesh = mesh_offset + m;
    const int part_idx = main_model.nodes.size();
    main_model.nodes.push_back(std::move(part_node));
    geom_node.children.push_back(part_idx);
  }
}

}  // namespace

MergeGltfResourcesResult MergeGltfResources(const tinygltf::Model& sub_model,
                                            tinygltf::Model& main_model) {
  const int buffer_offset = main_model.buffers.size();
  const int buffer_view_offset = main_model.bufferViews.size();
  const int accessor_offset = main_model.accessors.size();
  const int material_offset = main_model.materials.size();
  const int mesh_offset = main_model.meshes.size();
  const int texture_offset = main_model.textures.size();
  const int image_offset = main_model.images.size();
  const int sampler_offset = main_model.samplers.size();

  for (auto buffer : sub_model.buffers) {
    main_model.buffers.push_back(std::move(buffer));
  }

  for (auto view : sub_model.bufferViews) {
    if (view.buffer >= 0) {
      view.buffer += buffer_offset;
    }
    main_model.bufferViews.push_back(std::move(view));
  }

  for (auto acc : sub_model.accessors) {
    if (acc.bufferView >= 0) {
      acc.bufferView += buffer_view_offset;
    }
    main_model.accessors.push_back(std::move(acc));
  }

  for (auto sampler : sub_model.samplers) {
    main_model.samplers.push_back(std::move(sampler));
  }

  for (auto img : sub_model.images) {
    if (img.bufferView >= 0) {
      img.bufferView += buffer_view_offset;
    }
    main_model.images.push_back(std::move(img));
  }

  for (auto tex : sub_model.textures) {
    if (tex.sampler >= 0) {
      tex.sampler += sampler_offset;
    }
    if (tex.source >= 0) {
      tex.source += image_offset;
    }
    main_model.textures.push_back(std::move(tex));
  }

  MergeMaterials(sub_model, texture_offset, main_model);
  MergeMeshes(sub_model, accessor_offset, material_offset, main_model);

  return MergeGltfResourcesResult{.mesh_offset = mesh_offset};
}

void AttachSubModelToModel(tinygltf::Model& main_model,
                           const tinygltf::Model& sub_model,
                           AttachOptions options) {
  tinygltf::Node geom_node;
  geom_node.name = std::move(options.node_name);
  geom_node.translation = std::move(options.translation);
  geom_node.rotation = std::move(options.rotation);
  geom_node.scale = std::move(options.scale);

  if ((sub_model.meshes.size() == 1 && sub_model.nodes.empty()) ||
      IsSingleTrivialMeshNode(sub_model)) {
    const int mesh_index_in_sub =
        sub_model.nodes.empty() ? 0 : sub_model.nodes[0].mesh;
    geom_node.mesh = options.mesh_offset + mesh_index_in_sub;
  } else if (!sub_model.nodes.empty()) {
    AttachSubModelNodeHierarchy(sub_model, options.mesh_offset, geom_node,
                                main_model);
  } else {
    AttachMultiMeshPartNodes(sub_model.meshes.size(), options.mesh_offset,
                             geom_node, main_model);
  }

  const int geom_idx = main_model.nodes.size();
  main_model.nodes.push_back(std::move(geom_node));
  main_model.nodes[options.parent_node_idx].children.push_back(geom_idx);
}

}  // namespace intrinsic::geo
