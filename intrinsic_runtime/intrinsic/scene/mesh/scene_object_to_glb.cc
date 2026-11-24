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

#include "intrinsic/scene/mesh/scene_object_to_glb.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/geometric_transform.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/geometry/api/renderable.h"
#include "intrinsic/geometry/api/renderable_generation.h"
#include "intrinsic/geometry/gltf/gltf_io.h"
#include "intrinsic/geometry/gltf/gltf_merge.h"
#include "intrinsic/geometry/proto/v1/geometry.pb.h"
#include "intrinsic/geometry/proto/v1/transformed_geometry.pb.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/scene_object/scene_object_view.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/proto/geometry_component.pb.h"
#include "tiny_gltf.h"

namespace intrinsic::scene_object {
namespace {

using ::intrinsic::Geometry;
using ::intrinsic::GeometryLibrary;
using ::intrinsic::kKindVisualGeometry;
using ::intrinsic::Pose3d;
using ::intrinsic::eigenmath::Quaterniond;
using ::intrinsic::eigenmath::Vector3d;
using ::intrinsic::geo::AttachOptions;
using ::intrinsic::geo::AttachSubModelToModel;
using ::intrinsic::geo::GenerateRenderableWithMaterialOverrides;
using ::intrinsic::geo::GetPoseAndScale;
using ::intrinsic::geo::MergeGltfResources;
using ::intrinsic::geo::MergeGltfResourcesResult;
using ::intrinsic::geo::ParseGlb;
using ::intrinsic::geo::Renderable;
using ::intrinsic::geo::SerializeGltfToGlb;
using ::intrinsic::geo::ToGeometry;
using ::intrinsic_proto::geometry::v1::TransformedGeometry;
using ::intrinsic_proto::scene_object::v1::Entity;
using ::intrinsic_proto::scene_object::v1::SceneObject;

// Converts a 3D vector to a glTF 3-element double array [x, y, z].
std::vector<double> ToGltfVector(const Vector3d& v) {
  return {v.x(), v.y(), v.z()};
}

// Converts a quaternion to a glTF 4-element double array [x, y, z, w].
std::vector<double> ToGltfQuaternion(const Quaterniond& q) {
  return {q.x(), q.y(), q.z(), q.w()};
}

// Creates a glTF node for every entity in `view`, resolves its local pose,
// wires parent-child entity indices into `model`, and returns a map from entity
// name to its node index in `model.nodes`. Nodes are created in the tree order
// of `view`, which makes the node indices independent of the entity order in
// the SceneObject and guarantees that parents are created before children.
absl::StatusOr<absl::flat_hash_map<std::string, int>> BuildEntityNodeHierarchy(
    const SceneObjectView& view, tinygltf::Model& model) {
  absl::flat_hash_map<std::string, int> entity_name_to_node_idx;
  entity_name_to_node_idx.reserve(view.GetNumEntities());
  for (const Entity* entity : view.GetEntitiesInTreeOrder()) {
    const int node_idx = model.nodes.size();
    entity_name_to_node_idx[entity->name()] = node_idx;

    tinygltf::Node node;
    node.name = entity->name();

    const Entity* parent = view.GetParent(entity->name());
    INTR_ASSIGN_OR_RETURN(const Pose3d local_pose,
                          view.GetPoseInParent(entity->name()));
    if (parent != nullptr && parent->has_joint() && entity->has_link() &&
        !local_pose.isApprox(Pose3d::Identity(), 1e-5)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Link entity '", entity->name(), "' parented to joint '",
                       entity->parent_name(),
                       "' must have an identity parent_t_this transform."));
    }
    if (parent != nullptr) {
      node.translation = ToGltfVector(local_pose.translation());
      node.rotation = ToGltfQuaternion(local_pose.quaternion());
    }

    model.nodes.push_back(std::move(node));

    if (parent == nullptr) {
      model.scenes[0].nodes.push_back(node_idx);
    } else {
      model.nodes[entity_name_to_node_idx.at(parent->name())]
          .children.push_back(node_idx);
    }
  }

  return entity_name_to_node_idx;
}

// Generates a glTF sub-model for `tg`, merges its resources into `main_model`,
// and attaches a child node for `geom_name` under `parent_entity_node_idx`
// with any local `ref_t_shape` transform from `tg`.
absl::Status AttachGeometryNode(absl::string_view geom_name,
                                const TransformedGeometry& tg,
                                const GeometryLibrary& geo_lib,
                                int parent_entity_node_idx,
                                tinygltf::Model& main_model) {
  INTR_ASSIGN_OR_RETURN(const Geometry geometry,
                        ToGeometry(tg.geometry(), &geo_lib.Deserializer()));
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<const Renderable> renderable,
                        GenerateRenderableWithMaterialOverrides(geometry));
  if (renderable == nullptr) {
    return absl::InternalError(absl::StrCat(
        "Failed to generate renderable for geometry: ", geom_name));
  }
  const std::string glb_bytes = renderable->GetGLBString();
  if (glb_bytes.empty()) {
    return absl::InternalError(absl::StrCat(
        "Renderable has empty GLB string for geometry: ", geom_name));
  }
  INTR_ASSIGN_OR_RETURN(tinygltf::Model sub_model, ParseGlb(glb_bytes));
  const MergeGltfResourcesResult merge_result =
      MergeGltfResources(sub_model, main_model);

  AttachOptions attach_options{
      .node_name = absl::StrCat(main_model.nodes[parent_entity_node_idx].name,
                                "/", geom_name),
      .parent_node_idx = parent_entity_node_idx,
      .mesh_offset = merge_result.mesh_offset,
  };
  if (tg.has_ref_t_shape()) {
    INTR_ASSIGN_OR_RETURN((const auto [pose, scale]),
                          GetPoseAndScale(tg.ref_t_shape()));
    attach_options.translation = ToGltfVector(pose.translation());
    attach_options.rotation = ToGltfQuaternion(pose.quaternion());
    if (!scale.isApprox(Vector3d::Ones())) {
      attach_options.scale = ToGltfVector(scale);
    }
  }

  AttachSubModelToModel(main_model, sub_model, std::move(attach_options));
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<tinygltf::Model> SceneObjectToGltf(
    const SceneObject& so, const GeometryLibrary& geo_lib) {
  tinygltf::Model model;
  model.asset.version = "2.0";
  model.asset.generator = "Intrinsic SceneObjectToGlb";
  model.defaultScene = 0;
  model.scenes.resize(1);
  model.scenes[0].name = so.name().empty() ? "Scene" : so.name();

  if (so.entities().empty()) {
    return model;
  }

  INTR_ASSIGN_OR_RETURN(const SceneObjectView view,
                        SceneObjectView::Create(so));
  INTR_ASSIGN_OR_RETURN(
      (const absl::flat_hash_map<std::string, int> entity_name_to_node_idx),
      BuildEntityNodeHierarchy(view, model));

  for (const Entity* entity : view.GetEntitiesInTreeOrder()) {
    if (!entity->has_link()) {
      continue;
    }
    const int entity_node_idx = entity_name_to_node_idx.at(entity->name());
    const auto& named_geos =
        entity->link().geometry_component().named_geometries();
    const auto visual_it = named_geos.find(kKindVisualGeometry);
    if (visual_it == named_geos.end()) {
      continue;
    }

    // Proto map iteration order is unspecified, so geometries are attached in
    // order of their name to keep the node and mesh indices deterministic.
    std::vector<std::pair<absl::string_view, const TransformedGeometry*>>
        sorted_geometries;
    sorted_geometries.reserve(visual_it->second.named_geometries().size());
    for (const auto& [geom_name, tg] : visual_it->second.named_geometries()) {
      sorted_geometries.emplace_back(geom_name, &tg);
    }
    // Geometry names are unique, so the pairs are ordered by name.
    std::ranges::sort(sorted_geometries);
    for (const auto& [geom_name, tg] : sorted_geometries) {
      INTR_RETURN_IF_ERROR(
          AttachGeometryNode(geom_name, *tg, geo_lib, entity_node_idx, model));
    }
  }

  return model;
}

absl::StatusOr<std::string> SceneObjectToGlb(const SceneObject& so,
                                             const GeometryLibrary& geo_lib) {
  INTR_ASSIGN_OR_RETURN(const tinygltf::Model model,
                        SceneObjectToGltf(so, geo_lib));
  return SerializeGltfToGlb(model);
}

}  // namespace intrinsic::scene_object
