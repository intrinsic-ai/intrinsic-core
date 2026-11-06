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

#include "intrinsic/world/collision/coal_collision_checker.h"

#include <algorithm>
#include <optional>
#include <sstream>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/die_if_null.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "coal/BVH/BVH_model.h"
#include "coal/broadphase/broadphase_dynamic_AABB_tree.h"
#include "coal/broadphase/default_broadphase_callbacks.h"
#include "coal/collision.h"
#include "coal/octree.h"
#include "coal/shape/geometric_shapes.h"
#include "intrinsic/geometry/api/axis_aligned_bounding_box_3d.h"
#include "intrinsic/geometry/api/compute_axis_aligned_bounding_box_3d.h"
#include "intrinsic/geometry/api/shape_factory.h"
#include "intrinsic/geometry/internal/util/scale_shape.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/object_store/object_ref.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/collision_check_cache.h"
#include "intrinsic/world/collision/collision_checker_utils.h"
#include "intrinsic/world/collision/collision_checker_world.h"
#include "intrinsic/world/collision/util/rule_set_util.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/world.pb.h"
#include "octomap/OcTree.h"

namespace intrinsic {

struct CoalCollider {
  // When fully initialized, coal_obj may have its userData field pointing to
  // external metadata.
  coal::CollisionObject coal_obj;
  Pose3d entity_t_collider;
  PhysicalEntityId entity_id;
};

namespace {

constexpr double kScaleEqualityThreshold = 1e-8;
constexpr std::size_t kCoalGeoCacheCapacityBytes = 1 << 29;  // ~536 MB
constexpr double kCacheAlmostFullThreshold = 0.9;
constexpr double kOctomapTreeResolution = 0.01;

struct ScaledCoalGeo {
  std::shared_ptr<coal::CollisionGeometry> coal_geo;
  eigenmath::Vector3d scale;
  std::size_t mem_usage_bytes = 0;
};

// Explicit cache key identifying geometry data via ObjectStore reference
// fingerprints, avoiding raw pointers whose memory addresses may be recycled.
using GeoKey = std::variant<std::monostate, ObjectRef<Mesh>,
                            ObjectRef<shapes::PointCloud>>;

GeoKey GetExactGeoKey(const ExactGeometry& exact_geo) {
  if (exact_geo.HasMesh()) {
    return *exact_geo.GetMesh();
  }
  if (exact_geo.HasPointCloud()) {
    return *exact_geo.GetPointCloud();
  }
  return std::monostate{};
}

class CoalGeoCache {
 public:
  CoalGeoCache();
  ~CoalGeoCache();

  absl::StatusOr<std::shared_ptr<coal::CollisionGeometry>> GetOrComputeCoalGeo(
      const Geometry& geo, const eigenmath::Vector3d& requested_scale);

  std::string DebugString() const;
  // Empties cached geometry objects while preserving cumulative telemetry
  // counters (cache_hits_, cache_misses_) across clears, ensuring monotonic
  // metrics for production diagnostics and monitoring.
  void Clear() {
    absl::MutexLock lock(&mutex_);
    intr_to_coal_map_.clear();
  }

  CoalCollisionChecker::GeoCacheStats GetStats() const {
    absl::MutexLock lock(&mutex_);
    return CoalCollisionChecker::GeoCacheStats{
        .hits = cache_hits_,
        .misses = cache_misses_,
        .size_bytes = intr_to_coal_map_.size(),
    };
  }

 private:
  using CoalGeoLru = LruCache<GeoKey, std::vector<ScaledCoalGeo>>;
  mutable absl::Mutex mutex_;
  CoalGeoLru intr_to_coal_map_ ABSL_GUARDED_BY(mutex_);
  int cache_hits_ ABSL_GUARDED_BY(mutex_) = 0;
  int cache_misses_ ABSL_GUARDED_BY(mutex_) = 0;
};

CoalGeoCache global_geo_cache;

coal::Transform3s ToCoalTransform(const Pose3d& pose) {
  coal::Transform3s t;
  t.setTranslation(pose.translation());
  t.setQuatRotation(pose.quaternion());
  return t;
}

Pose3d FromCoalTransform(const coal::Transform3s& t) {
  Pose3d pose;
  pose.translation() = t.getTranslation();
  pose.setQuaternion(t.getQuatRotation());
  return pose;
}

absl::StatusOr<geo::PrimitiveShapePtr> ScaleShapeApprox(
    const geo::PrimitiveShapePtr shape_ptr, const eigenmath::Vector3d& scale) {
  if (scale.isOnes(kScaleEqualityThreshold)) {
    return shape_ptr;
  }
  if (scale.isApproxToConstant(scale.x(), kScaleEqualityThreshold)) {
    return ScaleShape(shape_ptr, eigenmath::Vector3d::Constant(scale.x()));
  }
  return ScaleShape(shape_ptr, scale);
}

absl::StatusOr<ScaledCoalGeo> CreateScaledCoalGeoFromIntrPointCloud(
    const shapes::PointCloud& point_cloud, const eigenmath::Vector3d& scale) {
  if (!scale.isOnes(kScaleEqualityThreshold)) {
    std::stringstream ss;
    ss << scale.transpose();
    return absl::FailedPreconditionError(absl::StrCat(
        "Point cloud geometry must not be scaled (scale was ", ss.str(), ")"));
  }
  auto octree = std::make_shared<octomap::OcTree>(kOctomapTreeResolution);
  for (const eigenmath::Vector3d& p : point_cloud.getPoints()) {
    octree->updateNode(p(0), p(1), p(2), /*occupied=*/true);
  }
  octree->updateInnerOccupancy();

  std::shared_ptr<coal::OcTree> coal_octree =
      std::make_shared<coal::OcTree>(octree);
  const std::size_t mem_usage_bytes = coal_octree->getTree()->memoryUsage();
  return ScaledCoalGeo{.coal_geo = std::move(coal_octree),
                       .scale = scale,
                       .mem_usage_bytes = mem_usage_bytes};
}

ScaledCoalGeo CreateScaledCoalGeoFromIntrMesh(
    const Mesh& mesh, const eigenmath::Vector3d& scale) {
  auto coal_model = std::make_shared<coal::BVHModel<coal::OBB>>();
  coal_model->beginModel(mesh.face_count(), mesh.vertex_count());
  for (const auto& vertex : mesh.vertices()) {
    const coal::Vec3s scaled_vertex(vertex[0] * scale(0), vertex[1] * scale(1),
                                    vertex[2] * scale(2));
    coal_model->addVertex(scaled_vertex);
  }
  coal::Matrixx3i coal_triangles(mesh.face_count(), 3);
  for (size_t face_ii = 0; face_ii < mesh.face_count(); ++face_ii) {
    const Mesh::Face& face = mesh.face(face_ii);
    coal_triangles.row(face_ii) << face(0), face(1), face(2);
  }
  coal_model->addTriangles(coal_triangles);
  coal_model->endModel();

  const std::size_t mem_usage_bytes =
      static_cast<std::size_t>(coal_model->memUsage(/*msg=*/false));
  return ScaledCoalGeo{.coal_geo = std::move(coal_model),
                       .scale = scale,
                       .mem_usage_bytes = mem_usage_bytes};
}

absl::StatusOr<ScaledCoalGeo> ComputeScaledCoalGeoFromKey(
    const GeoKey& key, const eigenmath::Vector3d& scale) {
  if (std::holds_alternative<ObjectRef<Mesh>>(key)) {
    return CreateScaledCoalGeoFromIntrMesh(
        std::get<ObjectRef<Mesh>>(key).Value(), scale);
  }
  if (std::holds_alternative<ObjectRef<shapes::PointCloud>>(key)) {
    return CreateScaledCoalGeoFromIntrPointCloud(
        std::get<ObjectRef<shapes::PointCloud>>(key).Value(), scale);
  }
  return absl::FailedPreconditionError("Geometry was empty!");
}

absl::StatusOr<CoalCollider> MakeColliderFromIntrPrimitive(
    const eigenmath::Matrix4d& entity_t_geo,
    const geo::TransformedPrimitiveShapePtr& t_shape_ptr,
    const PhysicalEntityId entity_id) {
  const eigenmath::Matrix4d entity_t_shape =
      entity_t_geo * t_shape_ptr.ref_t_shape();
  INTR_ASSIGN_OR_RETURN(
      (const auto [entity_t_shape_pose, entity_t_shape_scale]),
      matrixToPoseAndScale(entity_t_shape));
  INTR_ASSIGN_OR_RETURN(
      const geo::PrimitiveShapePtr shape_ptr,
      ScaleShapeApprox(t_shape_ptr.shape(), entity_t_shape_scale));
  std::shared_ptr<coal::CollisionGeometry> coal_geo;
  switch (shape_ptr->getType()) {
    case shapes::ShapeType::BOX: {
      const shapes::Box& box = shape_ptr->get<shapes::Box>();
      const eigenmath::Vector3d& side_lengths = box.getSize();
      coal_geo = std::make_shared<coal::Box>(side_lengths(0), side_lengths(1),
                                             side_lengths(2));
      break;
    }
    case shapes::ShapeType::CYLINDER: {
      const shapes::Cylinder& cylinder = shape_ptr->get<shapes::Cylinder>();
      coal_geo = std::make_shared<coal::Cylinder>(cylinder.getRadius(),
                                                  cylinder.getLength());
      break;
    }
    case shapes::ShapeType::CAPSULE: {
      const shapes::Capsule& capsule = shape_ptr->get<shapes::Capsule>();
      coal_geo = std::make_shared<coal::Capsule>(capsule.getRadius(),
                                                 capsule.getLength());
      break;
    }
    case shapes::ShapeType::SPHERE: {
      const shapes::Sphere& sphere = shape_ptr->get<shapes::Sphere>();
      coal_geo = std::make_shared<coal::Sphere>(sphere.getRadius());
      break;
    }
    case shapes::ShapeType::ELLIPSOID: {
      const shapes::Ellipsoid& ellipsoid = shape_ptr->get<shapes::Ellipsoid>();
      coal_geo = std::make_shared<coal::Ellipsoid>(ellipsoid.getRadii());
      break;
    }
    default: {
      return absl::UnimplementedError(absl::StrCat(
          "Unhandled shape type: ", ToString(shape_ptr->getType())));
    }
  }
  return CoalCollider{.coal_obj = coal::CollisionObject(std::move(coal_geo)),
                      .entity_t_collider = entity_t_shape_pose,
                      .entity_id = entity_id};
}

absl::Status AddCollidersFromTransformedGeometry(
    const TransformedGeometry& t_geo, const PhysicalEntityId entity_id,
    std::vector<CoalCollider>& colliders) {
  const ExactGeometry& exact_geo = t_geo.shape().GetExactGeometry();
  if (!exact_geo.GetPrimitiveShapes().empty()) {
    std::vector<CoalCollider> primitive_colliders;
    primitive_colliders.reserve(exact_geo.GetPrimitiveShapes().size());
    absl::Status primitive_status = absl::OkStatus();
    for (const geo::TransformedPrimitiveShapePtr& t_shape_ptr :
         exact_geo.GetPrimitiveShapes()) {
      auto collider_or = MakeColliderFromIntrPrimitive(t_geo.ref_t_shape(),
                                                       t_shape_ptr, entity_id);
      if (!collider_or.ok()) {
        primitive_status = collider_or.status();
        break;
      }
      primitive_colliders.push_back(std::move(collider_or.value()));
    }
    if (primitive_status.ok()) {
      colliders.insert(colliders.end(), primitive_colliders.begin(),
                       primitive_colliders.end());
      return absl::OkStatus();
    }
    LOG(INFO) << "Failed to convert primitive; falling back to mesh. "
              << "Primitive conversion error: " << primitive_status;
  }

  INTR_ASSIGN_OR_RETURN(
      (const auto [entity_t_shape_pose, entity_t_shape_scale]),
      matrixToPoseAndScale(t_geo.ref_t_shape()));
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<coal::CollisionGeometry> coal_geo,
                        global_geo_cache.GetOrComputeCoalGeo(
                            t_geo.shape(), entity_t_shape_scale));
  colliders.push_back(
      CoalCollider{.coal_obj = coal::CollisionObject(std::move(coal_geo)),
                   .entity_t_collider = entity_t_shape_pose,
                   .entity_id = entity_id});
  return absl::OkStatus();
}

absl::StatusOr<std::vector<CoalCollider>> GenerateCoalCollidersFromEntityIds(
    const CollisionCheckerWorld& world,
    const std::vector<PhysicalEntityId>& entity_ids) {
  std::vector<CoalCollider> colliders;
  for (const PhysicalEntityId entity_id : entity_ids) {
    const NamedGeometrySet geo_set =
        world.GetGeometryForEntity(entity_id, kKindCollisionGeometry)
            .value_or(NamedGeometrySet());
    for (const auto& named_geo : geo_set) {
      const TransformedGeometry& t_geo = named_geo.second;
      INTR_RETURN_IF_ERROR(
          AddCollidersFromTransformedGeometry(t_geo, entity_id, colliders));
    }
  }
  return colliders;
}

absl::Status UpdateColliderTransforms(const CollisionCheckerWorld& world,
                                      std::vector<CoalCollider>& colliders) {
  Pose3d root_t_entity;
  for (int ii = 0; ii < colliders.size(); ++ii) {
    CoalCollider& collider = colliders[ii];
    // We need to compute root_t_collider. We already have
    // collider.entity_t_collider, so we just need to compute
    // root_t_entity. Since the colliders are grouped by entity_id, we only need
    // to compute root_t_entity when the entity_id changes.
    if (ii == 0 || collider.entity_id != colliders[ii - 1].entity_id) {
      root_t_entity = world.GetTransform(kRootEntityId, collider.entity_id);
    }
    const Pose3d root_t_collider = root_t_entity * collider.entity_t_collider;
    collider.coal_obj.setTransform(ToCoalTransform(root_t_collider));
  }
  return absl::OkStatus();
}

bool AreCollidersInCollision(const std::vector<CoalCollider>& colliders_1,
                             const std::vector<CoalCollider>& colliders_2,
                             double margin) {
  coal::CollisionRequest request;
  request.security_margin = margin;
  request.enable_contact = false;
  coal::CollisionResult result;
  for (const CoalCollider& collider_1 : colliders_1) {
    for (const CoalCollider& collider_2 : colliders_2) {
      result.clear();
      coal::collide(&collider_1.coal_obj, &collider_2.coal_obj, request,
                    result);
      if (result.isCollision()) {
        return true;
      }
    }
  }
  return false;
}

bool AreSetsInCollisionImpl(const NamedGeometrySet& left_tree,
                            const NamedGeometrySet& right_tree,
                            const Pose3d& left_t_right, double margin,
                            PhysicalEntityId left_id,
                            PhysicalEntityId right_id) {
  std::vector<CoalCollider> colliders_1;
  std::vector<CoalCollider> colliders_2;
  for (const auto& [name, t_geo] : left_tree) {
    CHECK_OK(AddCollidersFromTransformedGeometry(t_geo, left_id, colliders_1))
        << "Failed to add colliders for " << left_id;
  }
  for (const auto& [name, t_geo] : right_tree) {
    CHECK_OK(AddCollidersFromTransformedGeometry(t_geo, right_id, colliders_2))
        << "Failed to add colliders for " << right_id;
  }

  // We leave the first collider as left_t_collider and the right side will
  // be updated to be left_t_right_collider by using left_t_right as part of its
  // transform calculations.
  for (CoalCollider& collider_2 : colliders_2) {
    const Pose3d left_collider_t_right_collider =
        left_t_right * collider_2.entity_t_collider;
    collider_2.coal_obj.setTransform(
        ToCoalTransform(left_collider_t_right_collider));
  }

  return AreCollidersInCollision(colliders_1, colliders_2, margin);
}

// This function computes the paddings we will add to the objects' AABBs in the
// broadphase.
//
// Assume we have two objects A and B with a collision margin of `margin`. In
// order to correctly detect broadphase collisions between these objects (and
// include `margin`), we need to pad their AABBs such that:
//
// padding_A + padding_B >= margin
//
// In order to handle collision margins for dynamic-vs-dynamic object checks, we
// find the maximum collision margin `max_dynamic_margin` over all pairs of
// dynamic objects; then we pad the AABBs of all dynamic objects by
// `dynamic_padding = 0.5 * max_dynamic_margin`. This results in all dynamic
// object pairs supporting collision margins up to `max_dynamic_margin ==
// dynamic_padding + dynamic_padding`.
//
// In order to handle collision margins for the dynamic-vs-static object checks:
// for each static object o_i, we find the maximum collision margin
// `max_static_margin_i` between o_i and all the dynamic objects. Since all
// dynamic objects already have padding == `dynamic_padding`, we can set the
// AABB padding of `o_i` to be `padding_i = max_static_margin_i -
// dynamic_padding`. This results in all dynamic-static object pairs supporting
// collision margins up to `max_static_margin_i == padding_i + dynamic_padding`.
struct BroadphasePadding {
  double dynamic_object_padding = 0.0;
  WorldHashMap<PhysicalEntityId, double> static_object_paddings;
};
BroadphasePadding ComputeBroadphasePadding(
    const WorldHashSet<PhysicalEntityId>& static_object_ids,
    const WorldHashSet<PhysicalEntityId>& dynamic_object_ids,
    const WorldHashMap<PhysicalEntityIdPair,
                       intrinsic_proto::world::CollisionAction>&
        dynamic_dynamic_actions,
    const WorldHashMap<PhysicalEntityIdPair,
                       intrinsic_proto::world::CollisionAction>&
        dynamic_static_actions) {
  // In the below for-loop, we compute two values:
  //
  // (*) max_dynamic_margin: the maximum collision margin defined over all pairs
  // of dynamic objects.
  //
  // (*) max_static_margins: for each static object `o`, we store the maximum
  // collision margin between `o` and all the dynamic objects.
  double max_dynamic_margin = 0.0;
  WorldHashMap<PhysicalEntityId, double> max_static_margins;
  max_static_margins.reserve(static_object_ids.size());
  for (const PhysicalEntityId& entity_id : static_object_ids) {
    max_static_margins[entity_id] = 0.0;
  }
  for (auto dyn_obj_iter = dynamic_object_ids.begin();
       dyn_obj_iter != dynamic_object_ids.end(); ++dyn_obj_iter) {
    // Update max_dynamic_margin.
    for (auto other_dyn_obj_iter = std::next(dyn_obj_iter);
         other_dyn_obj_iter != dynamic_object_ids.end(); ++other_dyn_obj_iter) {
      auto action_iter = dynamic_dynamic_actions.find(
          PhysicalEntityIdPair(*dyn_obj_iter, *other_dyn_obj_iter));
      if (action_iter != dynamic_dynamic_actions.end() &&
          !action_iter->second.is_excluded()) {
        max_dynamic_margin =
            std::max(max_dynamic_margin,
                     (double)action_iter->second.margin().hard_margin());
      }
    }

    // Update max_static_margins
    for (const PhysicalEntityId& static_object_id : static_object_ids) {
      auto action_iter = dynamic_static_actions.find(
          PhysicalEntityIdPair(*dyn_obj_iter, static_object_id));
      if (action_iter != dynamic_static_actions.end() &&
          !action_iter->second.is_excluded()) {
        double& max_static_margin = max_static_margins[static_object_id];
        max_static_margin =
            std::max(max_static_margin,
                     (double)action_iter->second.margin().hard_margin());
      }
    }
  }

  const double dynamic_padding = 0.5 * max_dynamic_margin;

  WorldHashMap<PhysicalEntityId, double> static_object_paddings;
  for (auto& [static_obj_id, required_margin] : max_static_margins) {
    // padding + dynamic_padding >= required_margin
    const double padding = std::max(0.0, required_margin - dynamic_padding);
    static_object_paddings[static_obj_id] = padding;
  }

  return BroadphasePadding{
      .dynamic_object_padding = dynamic_padding,
      .static_object_paddings = std::move(static_object_paddings)};
}

std::unique_ptr<coal::BroadPhaseCollisionManager> GenerateStaticBroadphase(
    const CollisionCheckerWorld& world, std::vector<CoalCollider>& colliders,
    const WorldHashMap<PhysicalEntityId, double>& per_object_padding) {
  // Compute padded AABBs (and store pointers to colliders for Coal API call)
  std::vector<coal::CollisionObject*> coal_object_ptrs;
  coal_object_ptrs.reserve(colliders.size());
  for (CoalCollider& collider : colliders) {
    collider.coal_obj.computeAABB();
    const double padding = per_object_padding.at(collider.entity_id);
    if (padding > 0.0) {
      // TODO(b/485626274): if required_padding < 0, should we consider
      // shrinking the AABB?
      collider.coal_obj.getAABB().expand(padding);
    }
    coal_object_ptrs.push_back(&collider.coal_obj);
  }

  // TODO(b/500356117): We previously used
  // `coal::DynamicAABBTreeArrayCollisionManager` as the broadphase but we found
  // that it has memory leaks. Once those are fixed, we should consider
  // switching back to it because it was about 5% faster.
  auto broadphase = std::make_unique<coal::DynamicAABBTreeCollisionManager>();
  broadphase->registerObjects(coal_object_ptrs);
  return broadphase;
}

std::unique_ptr<coal::BroadPhaseCollisionManager> GenerateDynamicBroadphase(
    const CollisionCheckerWorld& world, std::vector<CoalCollider>& colliders,
    const double padding) {
  // Compute padded AABBs (and store pointers to colliders for Coal API call)
  std::vector<coal::CollisionObject*> coal_object_ptrs;
  coal_object_ptrs.reserve(colliders.size());
  for (CoalCollider& collider : colliders) {
    collider.coal_obj.computeAABB();
    if (padding > 0.0) {
      // TODO(b/485626274): if required_padding < 0, should we consider
      // shrinking the AABB?
      collider.coal_obj.getAABB().expand(padding);
    }
    coal_object_ptrs.push_back(&collider.coal_obj);
  }

  auto broadphase = std::make_unique<coal::DynamicAABBTreeCollisionManager>();
  broadphase->registerObjects(coal_object_ptrs);
  return broadphase;
}

// This callback is executed whenever the broadphase detects a possible
// collision between two objects. This callback is responsible for performing
// the narrowphase (exact) collision check between the two objects. This
// callback also considers the collision rules between the two objects - i.e.,
// it will return "no collision" if there is an exclusion rule between them, or
// will set margins for collision checking if appropriate.
struct CoalCollisionCallBack : public coal::CollisionCallBackBase {
  bool collide(coal::CollisionObject* obj1,
               coal::CollisionObject* obj2) override {
    if (done_) {
      return true;
    }
    const CoalCollisionChecker::ColliderUserData& obj1_data =
        *static_cast<CoalCollisionChecker::ColliderUserData*>(
            ABSL_DIE_IF_NULL(obj1->getUserData()));
    const CoalCollisionChecker::ColliderUserData& obj2_data =
        *static_cast<CoalCollisionChecker::ColliderUserData*>(
            ABSL_DIE_IF_NULL(obj2->getUserData()));

    const PhysicalEntityId obj1_id = obj1_data.entity_id;
    const PhysicalEntityId obj2_id = obj2_data.entity_id;
    // The broadphase always returns self-collisions. Skip these.
    if (obj1_id == obj2_id) {
      return false;
    }

    // If this object pair is excluded from collisions, early-return. Otherwise,
    // check if there is a collision margin for this object pair.
    double margin = 0.0;
    if (action_map_) {
      auto action_iter = action_map_->find(std::make_pair(obj1_id, obj2_id));
      if (action_iter != action_map_->end()) {
        if (action_iter->second.is_excluded()) {
          return false;
        }
        margin = action_iter->second.margin().hard_margin();
      }
    }

    coal::CollisionRequest request;
    request.security_margin = margin;
    request.enable_contact = false;

    absl::Time t_before;
    if (distance_stats_ != nullptr) {
      t_before = absl::Now();
    }

    result_.clear();
    coal::collide(obj1, obj2, request, result_);

    absl::Duration check_duration;
    if (distance_stats_ != nullptr) {
      check_duration = absl::Now() - t_before;
    }

    if (result_.isCollision()) {
      has_collision_ = true;
      if (collision_debug_ != nullptr) {
        collision_debug_->collisions.push_back({
            .left_object = obj1_id,
            .right_objects = {obj2_id},
            .desired_margin = margin,
        });
      } else {
        done_ = true;
      }
    }

    if (distance_stats_ != nullptr) {
      const PhysicalEntityIdPair collide_pair(obj1_id, obj2_id);
      ++distance_stats_->num_checks;
      CollisionStats& collision_stats =
          distance_stats_->time_by_pair[collide_pair];
      if (result_.isCollision()) {
        collision_stats.time_invalid += check_duration;
        ++collision_stats.checks_invalid;
      } else {
        collision_stats.time_clear += check_duration;
        ++collision_stats.checks_clear;
      }
      // TODO(b/485626086): Consider bringing back the collision checking cache.
      ++collision_stats.cache_misses;

      SlowCheckStats& slow_check = distance_stats_->slow_checks[collide_pair];
      SlowCheckStats::Data& slow_data = result_.isCollision()
                                            ? slow_check.in_collision
                                            : slow_check.not_in_collision;
      if (check_duration > slow_data.duration) {
        slow_data.duration = check_duration;
        coal::Transform3s obj1_t_obj2 =
            obj1->getTransform().inverseTimes(obj2->getTransform());
        slow_data.transform = FromCoalTransform(obj1_t_obj2);
        slow_data.margin = margin;
        slow_data.result = result_.isCollision();
      }
    }

    return done_;
  }
  coal::CollisionData data_;
  const WorldHashMap<PhysicalEntityIdPair,
                     intrinsic_proto::world::CollisionAction>* action_map_ =
      nullptr;
  DistanceCheckStatistics* distance_stats_ = nullptr;
  CollisionCheckingDebug* collision_debug_ = nullptr;
  coal::CollisionResult result_;
  bool has_collision_ = false;
  bool done_ = false;
};

CoalGeoCache::CoalGeoCache() : intr_to_coal_map_(kCoalGeoCacheCapacityBytes) {}
CoalGeoCache::~CoalGeoCache() { intr_to_coal_map_.clear(); }

absl::StatusOr<std::shared_ptr<coal::CollisionGeometry>>
CoalGeoCache::GetOrComputeCoalGeo(const Geometry& geo,
                                  const eigenmath::Vector3d& requested_scale) {
  const ExactGeometry& exact_geo = geo.GetExactGeometry();

  const GeoKey key = GetExactGeoKey(exact_geo);
  if (std::holds_alternative<std::monostate>(key)) {
    return absl::InvalidArgumentError("Unexpected ExactGeometry type!");
  }

  const auto find_matching_coal_geo =
      [&](const std::vector<ScaledCoalGeo>& scaled_geos)
      -> std::shared_ptr<coal::CollisionGeometry> {
    for (const ScaledCoalGeo& scaled_coal_geo : scaled_geos) {
      if (requested_scale.isApprox(scaled_coal_geo.scale,
                                   kScaleEqualityThreshold)) {
        return scaled_coal_geo.coal_geo;
      }
    }
    return nullptr;
  };

  // Fast path: check if the geometry at the requested scale is already cached.
  {
    absl::MutexLock lock(&mutex_);
    CoalGeoLru::ScopedLookup lookup(&intr_to_coal_map_, key);
    if (lookup.found()) {
      if (std::shared_ptr<coal::CollisionGeometry> coal_geo =
              find_matching_coal_geo(*lookup.value());
          coal_geo != nullptr) {
        ++cache_hits_;
        return coal_geo;
      }
    }
  }

  // Expensive conversion: construct the Coal representation (e.g. BVH tree or
  // Octree) without holding any lock, allowing concurrent construction across
  // multiple geometries on different threads.
  INTR_ASSIGN_OR_RETURN(const ScaledCoalGeo new_scaled_geo,
                        ComputeScaledCoalGeoFromKey(key, requested_scale));

  // Insert newly computed geometry into the cache under lock, handling
  // potential race conditions where another thread computed the same geometry
  // concurrently.
  absl::MutexLock lock(&mutex_);
  CoalGeoLru::ScopedLookup lookup(&intr_to_coal_map_, key);
  if (lookup.found()) {
    if (std::shared_ptr<coal::CollisionGeometry> coal_geo =
            find_matching_coal_geo(*lookup.value());
        coal_geo != nullptr) {
      // Another thread computed and inserted this geometry while this thread
      // was converting. Count this as a cache miss because this thread
      // experienced a cold miss in the fast path and performed redundant
      // conversion work. Return the winning thread's geometry.
      ++cache_misses_;
      return coal_geo;
    }
    // Key exists for another scale; append the newly scaled geometry.
    ++cache_misses_;
    lookup.value()->push_back(new_scaled_geo);

    std::size_t geo_bytes = 0;
    for (const ScaledCoalGeo& g : *lookup.value()) {
      geo_bytes += g.mem_usage_bytes;
    }
    intr_to_coal_map_.updateSize(key, lookup.value(), geo_bytes);
  } else {
    // Key does not exist; insert vector directly with its calculated size.
    ++cache_misses_;
    auto* scaled_coal_geos = new std::vector<ScaledCoalGeo>{new_scaled_geo};
    intr_to_coal_map_.insert(key, scaled_coal_geos,
                             new_scaled_geo.mem_usage_bytes);
  }

  const double cache_usage = static_cast<double>(intr_to_coal_map_.size()) /
                             static_cast<double>(intr_to_coal_map_.maxSize());
  if (cache_usage >= kCacheAlmostFullThreshold) {
    LOG_EVERY_N_SEC(INFO, 10.0)
        << "CoalGeoCache is almost full: " << intr_to_coal_map_.size() << " / "
        << intr_to_coal_map_.maxSize() << " B used.";
  }

  return new_scaled_geo.coal_geo;
}

std::string CoalGeoCache::DebugString() const {
  absl::MutexLock lock(&mutex_);
  return absl::StrCat("Cache hits / misses: ", cache_hits_, " / ",
                      cache_misses_, "\n",
                      "Current cache size: ", intr_to_coal_map_.size(), " B");
}

bool AreGeometrySetsInCollision(PhysicalEntityId object_id,
                                PhysicalEntityId other_object_id,
                                const NamedGeometrySet& object_spatial_tree,
                                const NamedGeometrySet& other_spatial_tree,
                                const Pose3d& object_t_other_object,
                                double margin,
                                CollisionCheckCache& cached_checks,
                                DistanceCheckStatistics* distance_stats)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(cached_checks.mutex_) {
  absl::Time start;
  if (distance_stats != nullptr) {
    start = absl::Now();
  }

  const auto cache_key =
      object_id < other_object_id
          ? std::make_tuple(object_id, other_object_id, object_t_other_object)
          : std::make_tuple(other_object_id, object_id,
                            Pose3d(object_t_other_object.inverse()));

  std::optional<bool> is_in_collision;
  bool cache_hit = true;

  const auto do_the_check =
      [&](CollisionCheckCacheValue&& starting_value)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(cached_checks.mutex_) {
            cache_hit = false;
            is_in_collision = AreSetsInCollisionImpl(
                object_spatial_tree, other_spatial_tree, object_t_other_object,
                margin, object_id, other_object_id);

            auto* cached_check =
                new CollisionCheckCacheValue(std::move(starting_value));
            cached_check->insert(std::make_pair(margin, *is_in_collision));

            cached_checks.Insert(cache_key, cached_check, cached_check->size());
          };

  CollisionCheckCacheValue* cached_check = cached_checks.Lookup(cache_key);
  if (cached_check == nullptr) {
    do_the_check({});
  } else {
    for (const auto& [saved_margin, cached_result] : *cached_check) {
      if (cached_result) {
        if (margin >= saved_margin) {
          is_in_collision = true;
          break;
        }
      } else {
        if (margin <= saved_margin) {
          is_in_collision = false;
          break;
        }
      }
    }

    if (is_in_collision.has_value()) {
      cached_checks.Release(cache_key, cached_check);
    } else {
      CollisionCheckCacheValue cached_check_copy = *cached_check;
      cached_checks.Release(cache_key, cached_check);
      do_the_check(std::move(cached_check_copy));
    }
  }

  DCHECK(is_in_collision.has_value()) << "We should always have a value here.";

  if (distance_stats != nullptr) {
    const MarginPairConflictStatus conflict_type =
        *is_in_collision ? MarginPairConflictStatus::kInvalid
                         : MarginPairConflictStatus::kClear;

    auto stop = absl::Now();
    auto duration = stop - start;
    auto key = collision_details::MakeOrderedPair(object_id, other_object_id);
    auto* stats = &distance_stats->time_by_pair[key];
    collision_details::UpdateStats(conflict_type, duration, cache_hit, stats);

    SlowCheckStats& slow_check = distance_stats->slow_checks[key];
    SlowCheckStats::Data& slow_check_data = *is_in_collision
                                                ? slow_check.in_collision
                                                : slow_check.not_in_collision;
    if (slow_check_data.duration < duration) {
      slow_check_data.duration = duration;
      slow_check_data.transform = object_t_other_object;
    }
  }

  return *is_in_collision;
}

}  // namespace

CoalCollisionChecker::CoalCollisionChecker(const CollisionCheckerWorld* world)
    : world_(*ABSL_DIE_IF_NULL(world)) {}

CoalCollisionChecker::~CoalCollisionChecker() = default;

void CoalCollisionChecker::ClearGlobalGeoCacheForTesting() {
  global_geo_cache.Clear();
}

std::string CoalCollisionChecker::GetGlobalGeoCacheDebugStringForTesting() {
  return global_geo_cache.DebugString();
}

CoalCollisionChecker::GeoCacheStats
CoalCollisionChecker::GetGlobalGeoCacheStatsForTesting() {
  return global_geo_cache.GetStats();
}

absl::StatusOr<std::unique_ptr<CoalCollisionChecker>>
CoalCollisionChecker::Create(
    const CollisionCheckerWorld* world,
    const WorldHashSet<PhysicalEntityId>& dynamic_object_ids,
    const intrinsic_proto::RuleSet& rule_set,
    const intrinsic_proto::world::CoalCollisionCheckerConfig& config) {
  // TODO(b/496542011): Remove this try/catch when we have a wrapper around Coal
  // that does not throw exceptions.
  try {
    return CreateImpl(world, dynamic_object_ids, rule_set, config);
  } catch (const std::exception& e) {
    LOG(ERROR) << "Coal CreateImpl() internal error: " << e.what();
    return absl::InternalError("Internal error in collision checker creation.");
  }
}

absl::StatusOr<std::unique_ptr<CoalCollisionChecker>>
CoalCollisionChecker::CreateImpl(
    const CollisionCheckerWorld* world,
    const WorldHashSet<PhysicalEntityId>& dynamic_object_ids,
    const intrinsic_proto::RuleSet& rule_set,
    const intrinsic_proto::world::CoalCollisionCheckerConfig& config) {
  std::unique_ptr<CoalCollisionChecker> checker =
      absl::WrapUnique(new CoalCollisionChecker(world));
  checker->config_ = config;

  checker->checker_data_ =
      GetCollisionCheckerData(dynamic_object_ids, checker->world_, rule_set);

  checker->dynamic_static_actions_ =
      GetActionMap(checker->checker_data_.rule_set,
                   checker->checker_data_.dynamic_object_ids,
                   checker->checker_data_.static_object_ids);
  checker->dynamic_dynamic_actions_ =
      GetActionMap(checker->checker_data_.rule_set,
                   checker->checker_data_.dynamic_object_ids,
                   checker->checker_data_.dynamic_object_ids);

  // First, we iterate over our dynamic and static objects and create Coal
  // colliders based on their geometry.
  {
    auto last_t = absl::Now();
    std::vector<PhysicalEntityId> dynamic_object_ids_sorted(
        checker->checker_data_.dynamic_object_ids.begin(),
        checker->checker_data_.dynamic_object_ids.end());

    std::vector<PhysicalEntityId> static_object_ids_sorted(
        checker->checker_data_.static_object_ids.begin(),
        checker->checker_data_.static_object_ids.end());

    // Sort the object IDs to ensure deterministic ordering.
    std::sort(dynamic_object_ids_sorted.begin(),
              dynamic_object_ids_sorted.end());
    std::sort(static_object_ids_sorted.begin(), static_object_ids_sorted.end());

    INTR_ASSIGN_OR_RETURN(checker->dynamic_coal_colliders_,
                          GenerateCoalCollidersFromEntityIds(
                              checker->world_, dynamic_object_ids_sorted));
    INTR_ASSIGN_OR_RETURN(checker->static_coal_colliders_,
                          GenerateCoalCollidersFromEntityIds(
                              checker->world_, static_object_ids_sorted));

    const absl::Duration geo_conversion_duration = absl::Now() - last_t;
    VLOG(1) << "Coal geo conversion time: "
            << absl::ToDoubleSeconds(geo_conversion_duration) << " s";
    VLOG(2) << global_geo_cache.DebugString();
  }

  // Create ColliderUserData structures and set the Coal objects to point to
  // those structures.
  {
    checker->static_collider_user_data_.resize(
        checker->static_coal_colliders_.size());
    for (int ii = 0; ii < checker->static_coal_colliders_.size(); ++ii) {
      checker->static_collider_user_data_[ii] = ColliderUserData{
          .entity_id = checker->static_coal_colliders_[ii].entity_id};
      checker->static_coal_colliders_[ii].coal_obj.setUserData(
          &checker->static_collider_user_data_[ii]);
    }

    checker->dynamic_collider_user_data_.resize(
        checker->dynamic_coal_colliders_.size());
    for (int ii = 0; ii < checker->dynamic_coal_colliders_.size(); ++ii) {
      checker->dynamic_collider_user_data_[ii] = ColliderUserData{
          .entity_id = checker->dynamic_coal_colliders_[ii].entity_id};
      checker->dynamic_coal_colliders_[ii].coal_obj.setUserData(
          &checker->dynamic_collider_user_data_[ii]);
    }
  }

  // Set the world transforms for static objects. We can do this at Init because
  // static object poses do not change.
  INTR_RETURN_IF_ERROR(UpdateColliderTransforms(
      checker->world_, checker->static_coal_colliders_));

  // Broadphase setup
  //
  // We build an AABB-based broadphase structure out of the colliders to help
  // Coal skip collision checks between objects whose AABBs do not overlap. When
  // Coal identifies a broadphase overlap (i.e., the AABBs overlap), then Coal
  // uses CoalCollisionCallback to perform the precise collision check between
  // those two objects.
  //
  // At collision checking time, we will have two broadphase structures: one
  // that holds all the dynamic objects (dynamic_broadphase) and one that holds
  // all the static objects (static_broadphase). We will do two broadphase
  // collision checks:
  //
  // (1) dynamic_broadphase vs. dynamic_broadphase (self-check)
  //
  // (2) dynamic_broadphase vs. static_broadphase
  //
  // We also need to add padding to the AABBs of our colliders so that the
  // broadphase correctly handles collision margins. See comments for
  // ComputeBroadphasePadding().
  //
  // TODO(b/485626973): Consider globally caching the broadphase structures when
  // they haven't changed like we do with CoalGeoCache.
  {
    absl::Time last_t = absl::Now();

    const BroadphasePadding broadphase_padding = ComputeBroadphasePadding(
        checker->checker_data_.static_object_ids,
        checker->checker_data_.dynamic_object_ids,
        checker->dynamic_dynamic_actions_, checker->dynamic_static_actions_);

    // Generate the static broadphase. We can do this in Init() because this
    // will never change.
    checker->static_broadphase_ = GenerateStaticBroadphase(
        checker->world_, checker->static_coal_colliders_,
        broadphase_padding.static_object_paddings);

    // We will compute the dynamic broadphase in IsInCollision() because it can
    // change every time.
    checker->dynamic_padding_ = broadphase_padding.dynamic_object_padding;

    const auto broadphase_setup_duration = absl::Now() - last_t;
    VLOG(1) << "Coal broadphase setup: "
            << absl::ToDoubleSeconds(broadphase_setup_duration) << " s";
  }

  return checker;
}

MarginPairConflictStatus CoalCollisionChecker::IsInCollision(
    CollisionCheckingDebug* collision_debug) const {
  // Update the poses of the dynamic colliders in place.
  CHECK_OK(UpdateColliderTransforms(world_, dynamic_coal_colliders_));

  // TODO(b/496542011): Remove try/catch blocks when we have a wrapper around
  // Coal that does not throw exceptions.
  std::unique_ptr<coal::BroadPhaseCollisionManager> dynamic_broadphase;
  try {
    // Generate the dynamic broadphase.
    dynamic_broadphase = GenerateDynamicBroadphase(
        world_, dynamic_coal_colliders_, dynamic_padding_);
  } catch (const std::exception& e) {
    LOG(ERROR) << "Coal GenerateDynamicBroadphase() internal error: "
               << e.what();
    return MarginPairConflictStatus::kInvalid;
  }

  CoalCollisionCallBack callback;
  callback.distance_stats_ = distance_stats_;
  callback.collision_debug_ = collision_debug;

  bool has_collision = false;

  {  // Check for collisions among the dynamic objects.
    callback.action_map_ = &dynamic_dynamic_actions_;

    // Detect collisions!
    try {
      dynamic_broadphase->collide(&callback);
    } catch (const std::exception& e) {
      LOG(ERROR) << "Coal collide() internal error: " << e.what();
      return MarginPairConflictStatus::kInvalid;
    }

    if (callback.has_collision_) {
      has_collision = true;
      if (collision_debug == nullptr) {
        return MarginPairConflictStatus::kInvalid;
      }
    }
  }

  {  // Check for collisions between the dynamic objects and the static objects.
    callback.action_map_ = &dynamic_static_actions_;

    // Detect collisions!
    try {
      dynamic_broadphase->collide(static_broadphase_.get(), &callback);
    } catch (const std::exception& e) {
      LOG(ERROR) << "Coal collide() internal error: " << e.what();
      return MarginPairConflictStatus::kInvalid;
    }

    if (callback.has_collision_) {
      has_collision = true;
      if (collision_debug == nullptr) {
        return MarginPairConflictStatus::kInvalid;
      }
    }
  }

  return has_collision ? MarginPairConflictStatus::kInvalid
                       : MarginPairConflictStatus::kClear;
}

std::vector<std::pair<PhysicalEntityId, PhysicalEntityId>>
GetCollisionsBetweenSets(const CollisionCheckerWorld& world,
                         const WorldHashSet<PhysicalEntityId>& set1,
                         const WorldHashSet<PhysicalEntityId>& set2,
                         bool check_upper_triangle_only) {
  WorldHashSet<PhysicalEntityId> full_set;
  if (set1.empty() || set2.empty()) {
    for (const auto& entity_id : world.GetEntityIds()) {
      if (entity_id == kRootEntityId) {
        continue;
      }
      auto entity = world.GetEntityById(entity_id);
      if (entity.ok() && (*entity)->IsEntityValid<PhysicalEntityId>()) {
        full_set.emplace(entity_id);
      }
    }
  }
  const WorldHashSet<PhysicalEntityId>& effective_set1 =
      set1.empty() ? full_set : set1;
  const WorldHashSet<PhysicalEntityId>& effective_set2 =
      set2.empty() ? full_set : set2;

  WorldHashSet<PhysicalEntityId> all_ids = effective_set1;
  all_ids.insert(effective_set2.begin(), effective_set2.end());
  absl::erase_if(all_ids, [](const auto& id) { return id == kRootEntityId; });

  auto rules = FillRuleSet(world, world.GetDefaultRuleSet());
  auto action_map = GetActionMap(rules, effective_set1, effective_set2);

  WorldHashMap<PhysicalEntityId, Pose3d> object_ts_world;
  object_ts_world.reserve(all_ids.size());

  WorldHashMap<PhysicalEntityId, NamedGeometrySet> object_geos;
  object_geos.reserve(all_ids.size());
  for (const auto& id : all_ids) {
    object_geos[id] = world.GetSpatialTreeInModelSpace(id);
    if (!object_geos[id].empty()) {
      object_ts_world[id] = world.GetTransform(id, kRootEntityId);
    }
  }

  std::vector<std::pair<PhysicalEntityId, PhysicalEntityId>> out;
  WorldHashSet<std::pair<PhysicalEntityId, PhysicalEntityId>> already_checked;

  auto cache = world.GetCachedChecks();
  absl::MutexLock lock(&cache->mutex_);
  for (const PhysicalEntityId& id1 : effective_set1) {
    if (id1 == kRootEntityId) {
      continue;
    }
    const NamedGeometrySet& geo1 = object_geos[id1];
    if (geo1.empty()) {
      continue;
    }

    const Pose3d world_t_id1 = object_ts_world[id1].inverse();
    for (const PhysicalEntityId& id2 : effective_set2) {
      if (id2 == kRootEntityId) {
        continue;
      }
      if (check_upper_triangle_only) {
        if (id1 >= id2) {
          continue;
        }
      } else {
        if (id1 == id2 || already_checked.contains(std::make_pair(id1, id2))) {
          continue;
        }
        already_checked.emplace(id1, id2);
        already_checked.emplace(id2, id1);
      }

      const NamedGeometrySet& geo2 = object_geos[id2];
      if (geo2.empty()) {
        continue;
      }

      auto action_it = action_map.find(std::make_pair(id1, id2));
      if (action_it != action_map.end() && action_it->second.is_excluded()) {
        continue;
      }

      const intrinsic_proto::world::CollisionAction* action;
      if (action_it != action_map.end()) {
        action = &action_it->second;
      } else {
        action = &GetDefaultAction();
      }

      const Pose3d id2_t_id1 = object_ts_world[id2] * world_t_id1;
      if (AreGeometrySetsInCollision(id2, id1, geo2, geo1, id2_t_id1,
                                     action->margin().hard_margin(), *cache,
                                     /*distance_stats=*/nullptr)) {
        out.emplace_back(id1, id2);
      }
    }
  }
  return out;
}

bool AreObjectsInCollision(const CollisionCheckerWorld& world,
                           PhysicalEntityId left_id,
                           PhysicalEntityId right_id) {
  if (left_id == right_id) {
    return false;
  }
  return !GetCollisionsBetweenSets(world, {left_id}, {right_id},
                                   /*check_upper_triangle_only=*/false)
              .empty();
}

namespace {

// Computes a conservative axis-aligned bounding box enclosing `t_geo` in its
// reference frame, i.e. it applies `t_geo.ref_t_shape()` to `t_geo.shape()` and
// computes the AABB. If two geometries have non-overlapping bounding boxes,
// `AreGeometriesInCollision()` is guaranteed to return false. Returns an empty
// bounding box when `t_geo` contains no geometry.
absl::StatusOr<geo::AxisAlignedBoundingBox3d> ComputeConservativeAABB(
    const TransformedGeometry& t_geo) {
  const ExactGeometry& exact_geo = t_geo.shape().GetExactGeometry();
  // When analytical primitive shapes are available, compute their Coal AABBs
  // directly in O(1) without touching `global_geo_cache`. Calling
  // `ComputeAxisAlignedBoundingBox3d()` on curved primitives would instead scan
  // their discretized fallback mesh, whose vertices are inscribed inside the
  // true curved surface and could under-approximate grazing contacts.
  if (!exact_geo.GetPrimitiveShapes().empty()) {
    geo::AxisAlignedBoundingBox3d primitive_aabb;
    bool all_primitives_converted = true;
    for (const geo::TransformedPrimitiveShapePtr& t_shape_ptr :
         exact_geo.GetPrimitiveShapes()) {
      absl::StatusOr<CoalCollider> collider_or = MakeColliderFromIntrPrimitive(
          t_geo.ref_t_shape(), t_shape_ptr, PhysicalEntityId(kInvalidEntityId));
      if (!collider_or.ok()) {
        // Shapes unsupported by `MakeColliderFromIntrPrimitive()` (such as
        // `Frustum`) fall back to mesh collision in
        // `AddCollidersFromTransformedGeometry()`, so fall through to the mesh
        // AABB path below to match narrowphase behavior.
        all_primitives_converted = false;
        break;
      }
      collider_or->coal_obj.setTransform(
          ToCoalTransform(collider_or->entity_t_collider));
      collider_or->coal_obj.computeAABB();
      const coal::AABB& coal_aabb = collider_or->coal_obj.getAABB();
      primitive_aabb.ExtendBy(
          geo::AxisAlignedBoundingBox3d(coal_aabb.min_, coal_aabb.max_));
    }
    if (all_primitives_converted) {
      return primitive_aabb;
    }
  }

  INTR_ASSIGN_OR_RETURN(const geo::AxisAlignedBoundingBox3d unpadded_local_aabb,
                        ComputeAxisAlignedBoundingBox3d(t_geo.shape()),
                        _ << "Failed to compute local AABB for geometry.");
  // Return empty boxes immediately before padding or calling `GetCorners()`:
  // an empty `AxisAlignedBoundingBox3d` stores `(min = +MAX, max = -MAX)`, so
  // padding would overflow and `ExtendBy()` on its corners would invert it into
  // `[-MAX, +MAX]`.
  if (unpadded_local_aabb.IsEmpty()) {
    return unpadded_local_aabb;
  }

  // `CreateScaledCoalGeoFromIntrPointCloud()` converts point clouds into an
  // OctoMap `OcTree` with `kOctomapTreeResolution` voxels in the shape's local
  // frame. An occupied voxel can reach up to one voxel beyond a point near the
  // opposite face of its cell, so pad the local AABB by one voxel on every side
  // before applying `ref_t_shape()`.
  const geo::AxisAlignedBoundingBox3d local_aabb = [&]() {
    if (!exact_geo.HasPointCloud()) {
      return unpadded_local_aabb;
    }
    const eigenmath::Vector3d voxel_padding =
        eigenmath::Vector3d::Constant(kOctomapTreeResolution);
    return geo::AxisAlignedBoundingBox3d(
        unpadded_local_aabb.GetMin() - voxel_padding,
        unpadded_local_aabb.GetMax() + voxel_padding);
  }();

  // Use exact equality rather than fuzzy `isIdentity()`: skipping the corner
  // transform on a nearly-identity `ref_t_shape()` could under-approximate the
  // reference-frame AABB relative to narrowphase, while non-exact matrices
  // safely fall through to the 8-corner loop below.
  if (t_geo.ref_t_shape() == eigenmath::Matrix4d::Identity()) {
    return local_aabb;
  }

  geo::AxisAlignedBoundingBox3d ref_aabb;
  for (const eigenmath::Vector3d& corner : local_aabb.GetCorners()) {
    const eigenmath::Vector3d ref_corner =
        (t_geo.ref_t_shape() * corner.homogeneous()).head<3>();
    ref_aabb.ExtendBy(ref_corner);
  }
  return ref_aabb;
}

}  // namespace

bool AreGeometriesInCollision(const TransformedGeometry& left,
                              const TransformedGeometry& right) {
  const absl::StatusOr<geo::AxisAlignedBoundingBox3d> left_aabb =
      ComputeConservativeAABB(left);
  const absl::StatusOr<geo::AxisAlignedBoundingBox3d> right_aabb =
      ComputeConservativeAABB(right);
  if (left_aabb.ok() && right_aabb.ok() && !left_aabb->DoOverlap(*right_aabb)) {
    return false;
  }

  std::vector<CoalCollider> colliders_1;
  std::vector<CoalCollider> colliders_2;
  CHECK_OK(AddCollidersFromTransformedGeometry(
      left, PhysicalEntityId(kInvalidEntityId), colliders_1));
  CHECK_OK(AddCollidersFromTransformedGeometry(
      right, PhysicalEntityId(kInvalidEntityId), colliders_2));
  for (CoalCollider& collider : colliders_1) {
    collider.coal_obj.setTransform(ToCoalTransform(collider.entity_t_collider));
  }
  for (CoalCollider& collider : colliders_2) {
    collider.coal_obj.setTransform(ToCoalTransform(collider.entity_t_collider));
  }
  return AreCollidersInCollision(colliders_1, colliders_2, /*margin=*/0.0);
}

}  // namespace intrinsic
