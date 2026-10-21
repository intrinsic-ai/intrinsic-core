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

#ifndef INTRINSIC_MOTION_PLANNING_SERVICE_CACHE_CACHE_KEY_NORMALIZATION_H_
#define INTRINSIC_MOTION_PLANNING_SERVICE_CACHE_CACHE_KEY_NORMALIZATION_H_

#include <cstddef>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/proto/v1/robot_specification.pb.h"
#include "intrinsic/util/proto/pb_hash.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/transform_node_internal.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/proto/collision_settings.pb.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

// Function determining if `object` is a kinematic object, returning `true` if
// it is and `false` otherwise.
bool IsKinematicObject(const object_world::WorldObject& object);

// Holds the `geometry_fingerprint` (hash of the geometry) and `ref_t_shape`
// affine transformation matrix of a single collision geometry obtained from a
// world entity, representing the collision geometry of the workd entity
// in the motion planning cache in a entity independent way.
struct EntityCollisionGeometryFeature {
  std::string geometry_fingerprint;
  eigenmath::Matrix4d ref_t_shape = eigenmath::Matrix4d::Identity();
};

// Computes a deterministic, lock-free fingerprint for the collision shape of
// `geometry`, ignoring visual `Renderable` properties.
// - For `Mesh` and `PointCloud`, this is an O(1) operation that reads the
//   precomputed `ObjectStore` content hash without serializing vertex buffers.
// - For primitive shapes, hashes the exact primitive parameters (radius, size).
absl::StatusOr<std::string> ComputeCollisionGeometryFingerprint(
    const Geometry& geometry);

// Extracts the geometry fingerprint and `ref_t_shape` matrix for each collision
// geometry attached to `entity_id` in `entity_world`. Returns an empty vector
// (`absl::OkStatus()`) if `entity_id` exists in `entity_world` but has no
// `GeometryComponent` or no collision geometry (`kKindCollisionGeometry`).
// Returns `absl::NotFoundError` if `entity_id` does not exist in
// `entity_world`, or propagates any error encountered while fingerprinting
// collision shapes.
absl::StatusOr<std::vector<EntityCollisionGeometryFeature>>
ExtractEntityCollisionGeometryFeatures(const World& entity_world,
                                       EntityId entity_id);

// Group signature representing the normalized discrete problem topology used as
// input to the Group ID hash. Continuous parameters (such as
// `start_configuration`), per-segment `collision_settings`, and ephemeral
// runtime or perception IDs are stripped or normalized to canonical paths so
// that equivalent planning problems map to the same cache group.
struct MotionPlanningCacheGroupSignature {
  // Constants for hash combination to guarantee deterministic,
  // process-independent group IDs.
  static constexpr size_t kHashCombineMagicConstant = 0x9e3779b9;
  static constexpr int kHashCombineLeftShift = 6;
  static constexpr int kHashCombineRightShift = 2;

  // The normalized motion specification, in which the the collision settings
  // are cleared and all frame references use normalized `by_name` paths where
  // object names are replaced with geometric fingerprints for all non-kinematic
  // objects.
  intrinsic_proto::motion_planning::v1::MotionSpecification
      normalized_motion_specification;

  // The normalized robot specification, where the start configuration is
  // cleared and the robot name is normalized.
  intrinsic_proto::motion_planning::v1::RobotSpecification
      normalized_robot_specification;

  // Compute the group ID hash for this signature. This is done based on boost's
  // `hash_combine` algorithm, which is intentionally used instead of
  // `absl::Hash` as it uses per-process random seeds and a hash function that
  // is deterministic is used here.
  static size_t ComputeGroupId(
      const intrinsic_proto::motion_planning::v1::MotionSpecification&
          motion_specification,
      const intrinsic_proto::motion_planning::v1::RobotSpecification&
          robot_specification) {
    const pb_hash pb_hasher{};
    const size_t motion_hash = pb_hasher(motion_specification);
    const size_t robot_hash = pb_hasher(robot_specification);
    return motion_hash ^ (robot_hash + kHashCombineMagicConstant +
                          (motion_hash << kHashCombineLeftShift) +
                          (motion_hash >> kHashCombineRightShift));
  }

  // Compute the group ID hash for this signature, see the static
  // `ComputeGroupId()` function for details.
  inline size_t ComputeGroupId() const {
    return ComputeGroupId(normalized_motion_specification,
                          normalized_robot_specification);
  }
};

// Resolves a normalized identifier token for `object`: returns the configured
// cell instance name for `KinematicObject`s, a `geo_...` collision geometry
// fingerprint token when collision geometry is present, or `object`'s resource
// name as a fallback when `object` has no collision geometry. Propagates any
// unexpected error encountered while extracting collision geometry
// fingerprints.
absl::StatusOr<std::string> GetNormalizedObjectToken(
    const object_world::WorldObject& object);

// Builds the full normalized hierarchical path string for `object` by
// traversing its ancestor chain from the world root down to `object` and
// joining each ancestor's normalized token (from `GetNormalizedObjectToken()`,
// including its fallback to the object's resource name when an ancestor
// has no collision geometry) with `/`. Returns `"root"` if `object` is the
// world root, or propagates any unexpected error from
// `GetNormalizedObjectToken()`.
absl::StatusOr<std::string> GetNormalizedFullPath(
    const object_world::WorldObject& object);

// Resolves a normalized hierarchical path or identifier string for `node` in
// `object_world` (returning `root` for the world root,
// `<normalized_parent_path>/<frame_name>` for frames, or
// `GetNormalizedFullPath()` for world objects).
absl::StatusOr<std::string> GetNormalizedTransformNodeName(
    const object_world::ObjectWorld& object_world,
    const object_world::TransformNode& node);

// Normalizes `collision_settings` in-place using `object_world` so that
// equivalent collision rules produce identical protos. Replaces object and
// entity IDs with normalized `by_name` paths, orders `left` and `right` sides
// consistently, and sorts and deduplicates all rules. Returns an error if any
// referenced object or entity is not found in `object_world`.
absl::Status NormalizeCollisionSettings(
    const object_world::ObjectWorld& object_world,
    intrinsic_proto::world::CollisionSettings& collision_settings);

// Creates a normalized `MotionPlanningCacheGroupSignature` from `request` using
// `object_world` by replacing ephemeral object/frame IDs with canonical
// hierarchical `by_name` paths and clearing non-topological fields such as
// `start_configuration` and segment `collision_settings`.
absl::StatusOr<MotionPlanningCacheGroupSignature>
CreateMotionPlanningCacheGroupSignature(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::MotionPlanningRequest& request);

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_SERVICE_CACHE_CACHE_KEY_NORMALIZATION_H_
