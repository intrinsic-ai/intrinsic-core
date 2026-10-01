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

#ifndef INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_CLIENT_H_
#define INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_CLIENT_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/empty.pb.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/proto/v1/transformed_geometry.pb.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/motion_planning/proto/motion_target.pb.h"
#include "intrinsic/motion_planning/proto/v1/compute_ik.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_config.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.grpc.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/transform_node.h"
#include "intrinsic/world/proto/collision_checker_config.pb.h"
#include "intrinsic/world/proto/collision_settings.pb.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"

namespace intrinsic {
namespace motion_planning {

// Provides access to the motion planning service for a specific world in the
// world service.
class MotionPlannerClient {
 public:
  // Creates a client for the world with the given id using a
  // `ChannelInterface`.
  MotionPlannerClient(absl::string_view world_id,
                      std::shared_ptr<ChannelInterface> channel);

  // Creates a client wrapping a motion_planner_service stub (e.g. for testing).
  MotionPlannerClient(absl::string_view world_id,
                      std::shared_ptr<intrinsic_proto::motion_planning::v1::
                                          MotionPlannerService::StubInterface>
                          motion_planner_service,
                      ClientContextFactory client_context_factory =
                          DefaultClientContextFactory);

  // Options for motion planning.
  struct MotionPlanningOptions {
    // Timeout for path planning algorithms.
    double path_planning_time_out = 30;

    std::optional<double> path_planning_step_size = std::nullopt;

    // Optionally generate and return the swept volume for the computed path.
    bool compute_swept_volume = false;

    // Optional configuration for saving or loading a motion.
    std::optional<intrinsic_proto::motion_planning::v1::LockMotionConfiguration>
        lock_motion_configuration = std::nullopt;

    // If true, the cache will not check for fuzzy matches. Default to false.
    bool skip_fuzzy_cache_check = false;

    // If true, the joint_shortcutter will combine multiple collinear segments
    // into a single segment. This speeds up planning time, but can result in
    // longer paths and trajectories.
    bool shortcutting_combine_collinear_segments = false;

    // Optionally overrides the default collision checker used by the motion
    // planner.
    intrinsic_proto::world::CollisionCheckerConfig collision_checker_config;

    // Optionally overrides the default collision check spacing used by the
    // motion planner.
    std::optional<double> collision_check_spacing_override = std::nullopt;

    // Optionally override the default strict trajectory fallback setting.
    std::optional<bool> enable_strict_trajectory_fallback = std::nullopt;

    // Returns the default set of options to use with the plan path requests.
    static const MotionPlanningOptions& Defaults();
  };

  // Wrapped result from calling PlanTrajectory. Contains both the trajectory
  // and an optional set of shapes that correspond to the swept volume of the
  // trajectory.
  struct PlanTrajectoryResult {
    intrinsic_proto::icon::JointTrajectoryPVA trajectory;
    std::vector<intrinsic_proto::geometry::v1::TransformedGeometry>
        swept_volume;
    // If the motion is locked based on the request, this is the id of the
    // motion for loading later. Note this field is not set if this response is
    // from loading a locked motion.
    std::optional<std::string> lock_motion_id;
  };

  struct PlanPathResult {
    // The planned path. Contains at least two waypoints if planning was
    // successful. Otherwise, it is empty.
    intrinsic_proto::motion_planning::v1::Path path;
    // The swept volume of the path. Only populated if compute_swept_volume is
    // true in the MotionPlanningOptions.
    std::vector<intrinsic_proto::geometry::v1::TransformedGeometry>
        swept_volume;
  };

  // Plans a trajectory for a given motion planning problem and robot.
  // caller_id: The id used for logging the request in the motion planner
  // service.
  absl::StatusOr<PlanTrajectoryResult> PlanTrajectory(
      const intrinsic_proto::motion_planning::v1::RobotSpecification&
          robot_specification,
      const intrinsic_proto::motion_planning::v1::MotionSpecification&
          motion_specification,
      const MotionPlanningOptions& options = MotionPlanningOptions::Defaults(),
      const std::string& caller_id = "Anonymous",
      const intrinsic_proto::data_logger::Context& context =
          intrinsic_proto::data_logger::Context());

  // Plans a path for a given motion planning problem and robot.
  // caller_id: The id used for logging the request in the motion planner
  // service.
  absl::StatusOr<PlanPathResult> PlanPath(
      const intrinsic_proto::motion_planning::v1::RobotSpecification&
          robot_specification,
      const intrinsic_proto::motion_planning::v1::MotionSpecification&
          motion_specification,
      const MotionPlanningOptions& options = MotionPlanningOptions::Defaults(),
      const std::string& caller_id = "Anonymous",
      const intrinsic_proto::data_logger::Context& context =
          intrinsic_proto::data_logger::Context());

  // Options for ik.
  struct IkOptions {
    // The starting joint configuration to use. If empty (=default), the current
    // position of a robot in the world will be used.
    eigenmath::VectorXd starting_joints;

    // The maximum number of solutions to be returned. If not set (== 0), the
    // underlying implementation has the freedom to choose. Negative values are
    // invalid.
    //
    // Choosing a smaller value may make some implementations faster, but this
    // depends on the underlying implementation and is not guaranteed.
    std::optional<int> max_num_solutions;

    // Optional collision settings. Leaving it empty means NO collision
    // checking.
    std::optional<intrinsic_proto::world::CollisionSettings> collision_settings;

    // Optional same branch IK flag. Defaults to false.
    bool ensure_same_branch = false;

    // Optional same branch Ik flag that will prefer solutions on the same
    // kinematic branch over those close to the starting_joint configuration.
    // Defaults to false.
    bool prefer_same_branch = false;

    // If true, will not return an error when a starting state is in collision
    // or when the robot cannot escape a collision.
    bool disable_error_on_collisions = false;

    // Optional override for default collision checker configuration.
    std::optional<intrinsic_proto::world::CollisionCheckerConfig>
        collision_checker_config = std::nullopt;

    // The logging context for the skill sending the request.
    std::optional<intrinsic_proto::data_logger::Context> context = std::nullopt;

    // Optional boolean flag to disable logging of the ComputeIk request data.
    bool disable_logging = false;

    static const IkOptions& Defaults();
  };

  // Result of the IK computation.
  //
  // This struct wraps the computed IK solutions (each solution is a joint
  // configuration) and holds metadata and diagnostic information returned
  // by the service.
  struct ComputeIkResult {
    // The computed IK solutions (joint configurations).
    std::vector<eigenmath::VectorXd> solutions;

    // Detailed internal diagnostics and metrics from the IK solver.
    // NOTE: The `ik_solutions` list within this debug structure
    // contains all candidate configurations generated and evaluated during the
    // search, including those that were ultimately REJECTED (e.g., due to
    // collisions, joint limit violations, or constraint failures).
    intrinsic_proto::motion_planning::v1::ComputeIkDebugInformation
        ik_debug_information;
  };

  // Computes inverse kinematics. max_num_solutions = 0 allows the server to
  // pick the number of solutions.
  absl::StatusOr<ComputeIkResult> ComputeIk(
      const world::KinematicObject& robot,
      const intrinsic_proto::motion_planning::CartesianMotionTarget&
          cartesian_target,
      const IkOptions& options = IkOptions::Defaults());

  // Computes inverse kinematics. max_num_solutions = 0 allows the server to
  // pick the number of solutions.
  absl::StatusOr<ComputeIkResult> ComputeIk(
      const world::KinematicObject& robot,
      const intrinsic_proto::motion_planning::v1::GeometricConstraint&
          geometric_target,
      const IkOptions& options = IkOptions::Defaults());

  // Computes forward kinematics.
  //
  // The returned transform is reference_t_target, i.e. the frame of "target" in
  // the frame of "reference".
  //
  // Typically, some of the joints of the robot should lie in between these
  // two frames, otherwise you wouldn't see any changes to the returned
  // transform.
  //
  // As an example, "reference" could be the base link of a robot, and target
  // might be the robot's end-effector.
  absl::StatusOr<Pose3d> ComputeFk(
      const world::KinematicObject& robot,
      const eigenmath::VectorXd& joint_values,
      const intrinsic_proto::world::TransformNodeReferenceByName& reference,
      const intrinsic_proto::world::TransformNodeReferenceByName& target);
  // Overload for the case where the reference and target are provided in the
  // form of objects retrieved from ObjectWorldClient.
  absl::StatusOr<Pose3d> ComputeFk(const world::KinematicObject& robot,
                                   const eigenmath::VectorXd& joint_values,
                                   const world::TransformNode& reference,
                                   const world::TransformNode& target);
  // Options for check collisions.
  struct CheckCollisionsOptions {
    // Optional collision settings.
    std::optional<intrinsic_proto::world::CollisionSettings> collision_settings;
  };

  // Checks collisions for a given path.
  absl::StatusOr<intrinsic_proto::motion_planning::v1::CheckCollisionsResponse>
  CheckCollisions(const world::KinematicObject& robot,
                  const std::vector<eigenmath::VectorXd>& waypoints,
                  const CheckCollisionsOptions& options = {});

  // Clear the PlanTrajectory caches.
  absl::StatusOr<google::protobuf::Empty> ClearCache();

 private:
  std::string world_id_;
  std::shared_ptr<
      intrinsic_proto::motion_planning::v1::MotionPlannerService::StubInterface>
      motion_planner_service_;
  ClientContextFactory client_context_factory_;
};

}  // namespace motion_planning
}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_CLIENT_H_
