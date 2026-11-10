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

#include "intrinsic/scene/service/generate_import_metadata.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/apply_transform.h"
#include "intrinsic/geometry/api/axis_aligned_bounding_box_3d.h"
#include "intrinsic/geometry/api/compute_mesh_stats.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/geometry/api/mesh_stats.h"
#include "intrinsic/geometry/internal/conversion_utils/to_epic.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/scene_object/scene_object_view.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/geometry_component.h"

namespace intrinsic {
namespace {

using ::intrinsic::scene_object::SceneObjectView;
using ::intrinsic_proto::scene_object::v1::GeometryStats;
using ::intrinsic_proto::scene_object::v1::ImportedScene;
using ::intrinsic_proto::scene_object::v1::ImportResultMetadata;
using ::intrinsic_proto::scene_object::v1::SceneObject;

constexpr double kVolumeEpsilon = 1e-12;

AxisAlignedBoundingBox3d SummaryStatsToAabb(
    const GeometryStats& summary_stats) {
  const auto center = intrinsic_proto::FromProto(summary_stats.aabb_center());
  const auto extent = intrinsic_proto::FromProto(summary_stats.aabb_extent());
  const eigenmath::Vector3d minCorner = center - extent / 2.0;
  const eigenmath::Vector3d maxCorner = center + extent / 2.0;
  return AxisAlignedBoundingBox3d(
      {minCorner.x(), minCorner.y(), minCorner.z()},
      {maxCorner.x(), maxCorner.y(), maxCorner.z()});
}

GeometryStats InitializeGeometryStats(const MeshStats& mesh_stats) {
  GeometryStats stats;
  stats.set_num_triangles(0);
  stats.set_num_vertices(0);
  stats.set_closed(true);
  stats.set_contains_primitives(false);
  stats.set_min_circumradius(std::numeric_limits<double>::max());

  const auto center = mesh_stats.aabb.GetCenter();
  const auto extent = mesh_stats.aabb.GetDiagonal();
  *stats.mutable_aabb_center() = ToProto(center);
  *stats.mutable_aabb_extent() = ToVectorProto(extent);
  return stats;
}

eigenmath::Vector3d ComputeWeightedAverage(const eigenmath::Vector3d& p1,
                                           const eigenmath::Vector3d& p2,
                                           double w1, double w2) {
  if (w1 <= kVolumeEpsilon && w2 <= kVolumeEpsilon) {
    return eigenmath::Vector3d::Zero();
  }
  if (w1 <= kVolumeEpsilon) {
    return p2;
  }
  if (w2 <= kVolumeEpsilon) {
    return p1;
  }
  return (p1 * w1 + p2 * w2) / (w1 + w2);
}

void UpdateVolumeProperties(GeometryStats& stats, const MeshStats& mesh_stats) {
  // Aggregated volumetric centroids and volume are only valid if the mesh is
  // closed.
  if (!stats.closed()) {
    stats.clear_volumetric_centroid();
    stats.clear_volume();
  } else {
    *stats.mutable_volumetric_centroid() = ToProto(
        ComputeWeightedAverage(FromProto(stats.volumetric_centroid()),
                               FromEpic3(mesh_stats.volumetric_centroid),
                               stats.volume(), mesh_stats.volume));
    stats.set_volume(stats.volume() + mesh_stats.volume);
  }
}

void UpdateVertexProperties(GeometryStats& stats, const MeshStats& mesh_stats) {
  *stats.mutable_vertex_centroid() = ToProto(ComputeWeightedAverage(
      FromProto(stats.vertex_centroid()), FromEpic3(mesh_stats.vertex_centroid),
      stats.num_vertices(), mesh_stats.num_vertices));
  stats.set_num_vertices(stats.num_vertices() + mesh_stats.num_vertices);
}

void IncrementStats(GeometryStats& stats, const MeshStats& mesh_stats) {
  AxisAlignedBoundingBox3d combined_aabb = SummaryStatsToAabb(stats);
  combined_aabb.ExtendBy(mesh_stats.aabb);
  const auto combined_center = combined_aabb.GetCenter();
  const auto combined_extent = combined_aabb.GetDiagonal();
  *stats.mutable_aabb_center() = ToProto(combined_center);
  *stats.mutable_aabb_extent() = ToVectorProto(combined_extent);
  stats.set_num_triangles(stats.num_triangles() + mesh_stats.num_triangles);
  stats.set_closed(stats.closed() && mesh_stats.closed);
  stats.set_contains_primitives(stats.contains_primitives() ||
                                mesh_stats.contains_primitives);
  stats.set_min_circumradius(
      std::min(stats.min_circumradius(), mesh_stats.min_circumradius));
  UpdateVertexProperties(stats, mesh_stats);
  UpdateVolumeProperties(stats, mesh_stats);
}

void AggregateStats(
    absl::string_view scene_key, const MeshStats& visual_stats,
    absl::flat_hash_map<std::string, GeometryStats>& scene_to_stats) {
  if (!scene_to_stats.contains(scene_key)) {
    scene_to_stats[scene_key] = InitializeGeometryStats(visual_stats);
  }
  IncrementStats(scene_to_stats.at(scene_key), visual_stats);
}

}  // namespace

absl::StatusOr<ImportResultMetadata> GenerateImportMetadata(
    const ImportedScene& scene, const GeometryDeserializer& geo_deserializer) {
  ImportResultMetadata result;
  absl::flat_hash_map<std::string, GeometryStats> scene_to_visual_stats;
  absl::flat_hash_map<std::string, GeometryStats> scene_to_collision_stats;

  for (const auto& scene_object :
       std::views::values(scene.scene_objects().objects())) {
    // Pre-compute the absolute pose of every entity in the scene object.
    INTR_ASSIGN_OR_RETURN(const SceneObjectView view,
                          SceneObjectView::Create(scene_object));
    INTR_ASSIGN_OR_RETURN(const auto absolute_poses, view.GetRootPoses());

    for (const auto& entity : scene_object.entities()) {
      if (!entity.has_link()) continue;

      // The absolute position of this specific entity relative to the Scene
      // Object's root.
      const Pose3d& root_t_entity = absolute_poses.at(entity.name());

      INTR_ASSIGN_OR_RETURN(
          auto geom_component,
          GeometryComponent::FromProto(entity.link().geometry_component()));

      for (const auto& [geo_type, stats_map] : {
               std::make_pair("Intrinsic_Visual", &scene_to_visual_stats),
               std::make_pair("Intrinsic_Collision", &scene_to_collision_stats),
           }) {
        auto named_geos =
            geom_component->GetGeometry(geo_type, geo_deserializer);
        if (!named_geos.ok()) {
          // If the mesh doesn't exist that skip it safely.
          if (absl::IsNotFound(named_geos.status())) {
            continue;
          }
          // If it failed for another reason return the error.
          return named_geos.status();
        }
        for (const auto& [_, transformed_geo] : *named_geos) {
          const auto& raw_shape = transformed_geo.shape();
          const auto& entity_t_shape = transformed_geo.ref_t_shape();

          // Move the shape into the coordinate frame of the Entity
          // it belongs to.
          INTR_ASSIGN_OR_RETURN(auto geometry_in_entity_frame,
                                ApplyTransform(raw_shape, entity_t_shape));

          // Move that Entity (and its shape) into the absolute Root
          // frame. Equation: root_T_shape = root_T_entity * entity_T_shape *
          // shape
          INTR_ASSIGN_OR_RETURN(
              auto geometry_in_root_frame,
              ApplyTransform(geometry_in_entity_frame, root_t_entity));

          // Calculate true bounding box.
          INTR_ASSIGN_OR_RETURN(const auto mesh_stats,
                                ComputeMeshStats(geometry_in_root_frame));

          AggregateStats(scene_object.name(), mesh_stats, *stats_map);
        }
      }
    }
  }

  result.mutable_scene_object_to_visual_stats()->insert(
      scene_to_visual_stats.begin(), scene_to_visual_stats.end());
  result.mutable_scene_object_to_collision_stats()->insert(
      scene_to_collision_stats.begin(), scene_to_collision_stats.end());

  return result;
}
}  // namespace intrinsic
