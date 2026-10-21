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

#ifndef INTRINSIC_MOTION_PLANNING_PATH_PLANNING_KINEMATICS_SYSTEM_PROXY_UTIL_H_
#define INTRINSIC_MOTION_PLANNING_PATH_PLANNING_KINEMATICS_SYSTEM_PROXY_UTIL_H_

#include <cstddef>
#include <memory>
#include <optional>

#include "absl/status/statusor.h"
#include "google/protobuf/empty.pb.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/path_planning/path_planner_definitions.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/proto/collision_checker_config.pb.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

// Creates a KinematicsSystemProxy for a robot. Takes a CollisionCheckerConfig
// to override default collision checker settings.
absl::StatusOr<std::unique_ptr<KinematicsSystemProxy>>
CreateKinematicsProxyWithConfig(
    const World& world, RobotCollectionsEntityId robot_id,
    const intrinsic_proto::world::CollisionCheckerConfig&
        collision_checker_config,
    std::vector<std::unique_ptr<ConstraintInterface>> constraints = {},
    const intrinsic_proto::RuleSet& rule_set = intrinsic_proto::RuleSet(),
    bool disable_collision_checking = false);

// Creates a KinematicsSystemProxy for a robot that is parent to any entity
// tagged with robot_part_label. The RobotComponent::GetBaseLink and
// World::GetFinalEntityOfRobotKinematicChain are used to create the kinematic
// chain.
absl::StatusOr<std::unique_ptr<KinematicsSystemProxy>> CreateKinematicsProxy(
    const World& world, RobotCollectionsEntityId robot_id,
    std::vector<std::unique_ptr<ConstraintInterface>> constraints = {},
    const intrinsic_proto::RuleSet& rule_set = intrinsic_proto::RuleSet(),
    bool disable_collision_checking = false);

// Options for configuring KinematicsSystemProxy creation.
struct KinematicsProxyOptions {
  // Configuration settings for the collision checker. If unset
  // (CONFIG_NOT_SET), uses the default collision checker configuration (Coal).
  intrinsic_proto::world::CollisionCheckerConfig collision_checker_config;

  // Optional uniform geometric path constraints that are enforced by the
  // proxy's IsValid check in addition to the collision checker.
  std::optional<
      intrinsic_proto::motion_planning::v1::UniformGeometricConstraint>
      constraints_proto = std::nullopt;

  // Collision rules applied on top of the world's default rule set (rules here
  // take precedence over the world's defaults). Defaults to a
  // default-constructed (empty) RuleSet, meaning no overrides.
  intrinsic_proto::RuleSet rule_set;

  // If true, no collision checker is created and
  // `KinematicsSystemProxy::GetCollisionChecker()` returns nullptr.
  bool disable_collision_checking = false;

  // When set, a ConcurrentProxy backed by this many worker proxies is created.
  // Must be > 0.
  std::optional<int> maybe_concurrent_thread_count = std::nullopt;
};

// Creates a KinematicsSystemProxy for the kinematic chain defined by the
// `robot` kinematic object. The proxy holds a collision checker initialized
// with the provided ruleset. If no rule set is provided the default rule set
// defined in the world is used unless disable_collision checking is set to
// true. Additionally, the user can also define uniform geometric path
// constraints that will be used for the IsValid check together with the
// collision checker if defined. User needs to define either geometric path
// constraints or collision checker. Otherwise an InvalidArgumentError will
// occur.
// Depending on the robot robot kinematic structure, either a DofViewProxy (in
// case of a branching strucutre) or a WorldRobotProxy is initialized.
absl::StatusOr<std::unique_ptr<KinematicsSystemProxy>> CreateKinematicsProxy(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    const KinematicsProxyOptions& options = {});

// Short-hand for the above call when you don't need the actual object world
// object.
absl::StatusOr<std::unique_ptr<KinematicsSystemProxy>> CreateKinematicsProxy(
    const World& world, const WorldObjectName& robot_name,
    std::optional<
        intrinsic_proto::motion_planning::v1::UniformGeometricConstraint>
        constraints_proto = std::nullopt,
    const intrinsic_proto::RuleSet& rule_set = intrinsic_proto::RuleSet(),
    bool disable_collision_checking = false);

// Create a KinematicsSystemProxy for the robots with the given path segment and
// optional geometric constraints.
// The path segment holds the collision rule set.
absl::StatusOr<std::unique_ptr<KinematicsSystemProxy>> CreateKinematicsProxy(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot, const PathSegment& path_segment,
    std::optional<
        intrinsic_proto::motion_planning::v1::UniformGeometricConstraint>
        constraints_proto = std::nullopt);

// Create a KinematicsSystemProxy for the robots with the given path segment and
// object_world. Also takes a CollisionCheckerConfig to override default
// collision checker.
absl::StatusOr<std::unique_ptr<KinematicsSystemProxy>>
CreateKinematicsProxyWithConfig(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot, const PathSegment& path_segment,
    const intrinsic_proto::world::CollisionCheckerConfig&
        collision_checker_config,
    std::optional<
        intrinsic_proto::motion_planning::v1::UniformGeometricConstraint>
        constraints_proto = std::nullopt);

// Creates a projector which (iteratively) projects the configuration onto the
// constraint manifolds, in order to obtain the closest configuration that
// satisfies all constraints. The parameter `max_constraint_error_norm` governs
// how small each projection step is performed, to take into account the
// locality assumption of a Jacobian matrix, i.e. the aggregated constraint
// Jacobian. The projector will throw a `kInternalError` if the projection does
// not converge within `max_projection_steps` iterations.
ConstraintManifoldProjector CreateConstraintManifoldProjector(
    const KinematicsSystemProxy& proxy,
    size_t max_projection_steps =
        KinematicsSystemProxy::kDefaultMaxProjectionSteps,
    double max_constraint_error_norm =
        KinematicsSystemProxy::kDefaultMaxConstraintErrorNorm);

// This creates a proxy based on `options`, but the resulting proxy will
// have collision margins that are relaxed according to `relative_factor` and
// `absolute_factor`. Collision margins must be updated in two places: in the
// World itself and the rule set in `options`. So we create a copy of the
// World and transform the World's collision margins, AND we transform the
// collision margins in `options.rule_set`.
//
// For each margin, we compute two candidate relaxations:
//
// (1) margin * path_refinement_validation_margin_relative_factor
//
// (2) max(0, margin - path_refinement_validation_margin_absolute_factor)
//
// We use the _minimum_ of the above 2 candidates.
absl::StatusOr<std::unique_ptr<KinematicsSystemProxy>>
CreateProxyWithRelaxedMargins(const object_world::ObjectWorld& object_world,
                              const object_world::KinematicObject& robot,
                              const KinematicsProxyOptions& options,
                              double relative_factor, double absolute_factor);

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_PATH_PLANNING_KINEMATICS_SYSTEM_PROXY_UTIL_H_
