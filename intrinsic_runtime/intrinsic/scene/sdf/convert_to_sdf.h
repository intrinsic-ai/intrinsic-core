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

#ifndef INTRINSIC_SCENE_SDF_CONVERT_TO_SDF_H_
#define INTRINSIC_SCENE_SDF_CONVERT_TO_SDF_H_

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/world/proto/geometry_component.pb.h"
#include "intrinsic/world/proto/kinematics_component.pb.h"
#include "intrinsic/world/proto/physics_component.pb.h"
#include "intrinsic/world/proto/sensor_component.pb.h"

namespace intrinsic {
namespace sdf {

// Fixes invalid SDF names by replacing reserved characters with
// kSdfNamePartsSeparator.
std::string FixSDFNameForReservedCharacters(absl::string_view name);

// Converts a Vector3d to an SDF-compatible string.
std::string Vec3ToString(eigenmath::Vector3d vec3);

// Converts a Pose3d to an SDF <pose> tag string.
std::string Pose3ToPoseString(const Pose3d& pose3,
                              absl::string_view relative_to = "");

// Converts a KinematicsComponent::MotionType to an SDF-compatible string.
absl::StatusOr<std::string> MotionTypeToString(
    intrinsic_proto::world::KinematicsComponent::MotionType type);

absl::StatusOr<std::string> CommonCameraPropertiesToString(
    const intrinsic_proto::world::SensorComponent::CommonCameraProperties&
        props,
    absl::string_view sensor_name,
    std::optional<absl::string_view> trigger_topic);

absl::StatusOr<std::string> ForceTorqueSpecToString(
    const intrinsic_proto::world::SensorComponent::ForceTorque& spec,
    absl::string_view sensor_name);

absl::StatusOr<std::string> LidarSpecToString(
    const intrinsic_proto::world::SensorComponent::Lidar& spec);

struct InertialOptions {
  // Enables automatic computation of inertial matrix if all of these conditions
  // are true:
  // 1. Link is not static.
  // 2. Link has collision geometry.
  // 3. Link has unit inertia.
  //
  // If enabled, small mass and inertia are added to the link's internal
  // properties.
  bool enable_auto_inertial = false;

  // Density to use in kg/m^3 if `enable_auto_inertial` is set.
  // Defaults to density of water.
  float default_density = 1000;
};

struct InertialProperties {
  // Whether the corresponding link has collision geometry.
  bool has_collision_geo = false;

  // Whether the corresponding link is considered static in physics simulation.
  bool is_static = false;
};

// Returns SDFormat xml string for the inertial properties corresponding to the
// PhysicsComponent.
absl::StatusOr<std::string> InertialSpecToString(
    const intrinsic_proto::world::PhysicsComponent& physics,
    const InertialOptions& options, const InertialProperties& properties);

struct JointProperties {
  // Name of the joint.
  std::string joint_name;

  // Name of the parent link that the joint is connected to.
  std::string parent_name;

  // Name of the child link that the joint is connected  to.
  // SDFformat requires that a joint is connected to only one link.
  std::string child_name;

  // Pose of the joint in the reference of the child link.
  Pose3d child_t_joint;

  // Name of the entity that the joint's pose is relative to.
  // If empty, the pose is relative to the child link (default SDF behavior).
  std::string pose_relative_to;

  // SDFormat xml string for nested sensors attached to the joint.
  std::string sensors_sdf;
};

// Returns SDFormat xml string for a joint given the KinematicsComponent.
// `user_data_map` is only needed for legacy `kGazeboJointPhysics`.
absl::StatusOr<std::string> JointSpecToString(
    const intrinsic_proto::world::KinematicsComponent& kinematics,
    const JointProperties& properties,
    const google::protobuf::Map<std::string, std::string>& user_data_map = {});

struct LinkProperties {
  // Name of the link.
  std::string name;

  // Pose of the link relative to `parent_name`. Should be empty for root entity
  // within a model.
  Pose3d parent_t_link = Pose3d::Identity();

  // Name of the parent entity within the model.
  std::string parent_name;

  // Whether the link is considered static in physics simulation.
  // Used in auto inertia computation, if enabled.
  bool is_static = false;

  // SDFormat xml string for nested sensors attached to the link.
  std::string sensors_sdf;
};

struct ConvexDecompositionOptions {
  // Enables convex decomposition for collision meshes.
  bool enabled = false;

  // Maximum number of convex hulls for collision mesh convex decomposition.
  // Only valid if `enabled` is true.
  int max_convex_hulls = 32;

  // Optional default parameter to specify the voxel resolution for convex
  // decomposition. Only used if the mesh geometry does not provide one.
  // Only valid if `enabled` is true.
  std::optional<int> resolution = std::nullopt;
};

struct LinkOptions {
  // Options for inertial property generation.
  InertialOptions inertial_options;

  // Options for convex decomposition of collision meshes.
  ConvexDecompositionOptions convex_decomposition_options;

  // Directory where geometries referenced in the Link will be saved.
  // Should exist prior to calling the function.
  //
  // If empty, no geometries will be saved. If non-empty,
  // `geometry_deserializer` must be set.
  //
  // Hint for speed-up: re-use the same directory from previous conversion to
  // SDF to avoid writing out meshes again. Note that meshes not referenced from
  // the SDF are not deleted, only the new ones are written out.
  std::string save_geopath;

  // Deserializer to use for resolving geometry references.
  // Must be set if `save_geopath` is non-empty.
  std::optional<const GeometryDeserializer*> geometry_deserializer =
      std::nullopt;
};

// Returns SDFormat xml string for a link given GeometryComponent and
// PhysicsComponent.
absl::StatusOr<std::string> LinkSpecToString(
    const intrinsic_proto::world::GeometryComponent& geometry,
    const intrinsic_proto::world::PhysicsComponent& physics,
    const LinkProperties& properties, const LinkOptions& options);

}  // namespace sdf
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SDF_CONVERT_TO_SDF_H_
