# Copyright 2026 Intrinsic Innovation LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Defines the MotionPlannerClient class.

The MotionPlannerClient provides access to computations on top of the world.
This includes:
  * IK/FK
  * path planning
"""

import dataclasses
from typing import Any
from typing import Optional
from typing import Sequence

from google.protobuf import empty_pb2
# This import is required to use the *_grpc imports.
# pylint: disable=unused-import
import grpc

from intrinsic.assets.dependencies import utils as dep_utils
from intrinsic.assets.proto.v1 import resolved_dependency_pb2
from intrinsic.geometry.proto.v1 import transformed_geometry_pb2
from intrinsic.icon.proto import joint_space_pb2
from intrinsic.logging.proto import context_pb2
from intrinsic.math.python import data_types
from intrinsic.math.python import proto_conversion as math_proto_conversion
from intrinsic.motion_planning.proto import motion_target_pb2
from intrinsic.motion_planning.proto.v1 import compute_ik_pb2
from intrinsic.motion_planning.proto.v1 import geometric_constraints_pb2
from intrinsic.motion_planning.proto.v1 import motion_planner_config_pb2
from intrinsic.motion_planning.proto.v1 import motion_planner_service_pb2
from intrinsic.motion_planning.proto.v1 import motion_planner_service_pb2_grpc
from intrinsic.motion_planning.proto.v1 import motion_planning_pb2
from intrinsic.motion_planning.proto.v1 import motion_specification_pb2
from intrinsic.motion_planning.proto.v1 import robot_specification_pb2
from intrinsic.world.proto import collision_checker_config_pb2
from intrinsic.world.proto import collision_settings_pb2
from intrinsic.world.python import object_world_ids

MOTION_PLANNER_SERVICE_INTERFACE = (
    "grpc://intrinsic_proto.motion_planning.v1.MotionPlannerService"
)


def _repeated_vec_to_list_of_floats(
    vectors: list[joint_space_pb2.JointVec],
) -> list[list[float]]:
  return [list(vector.joints) for vector in vectors]


@dataclasses.dataclass
class CheckCollisionsOptions:
  """Options for Collision settings.

  Attributes:
    collision_settings: Settings for collision checking.
  """

  collision_settings: collision_settings_pb2.CollisionSettings | None = None


@dataclasses.dataclass
class IKOptions:
  """Options for IK.

  Attributes:
    max_num_solutions: The maximum number of solutions to be returned from IK
      computation. If not set (== 0), the underlying implementation has the
      freedom to choose. Negative values are invalid.
    starting_joints: The starting joint configuration to use. If not set, the
      current position of the robot in the world will be used.
    collision_settings: Collision Settings. If left empty, no collision checking
      is done.
    ensure_same_branch: Flag to choose IK solution is on the same kinematic
      branch as the starting joints of the robot.
    prefer_same_branch:  Flag that will prefer solutions on the same kinematic
      branch over those close to the starting_joints configuration.
    disable_error_on_collisions: If True, does not return an error when a
      starting state is in collision or when the robot cannot escape a
      collision.
    collision_checker_config: Configuration override for the collision checker.
    context: The logging context for the skill sending the request.
    disable_logging: If True, disables logging of the ComputeIk request data.
    caller_id: Optional caller ID to correlate the IKRequest logs with its caller.
  """

  max_num_solutions: int = 0
  starting_joints: list[float] | None = None
  collision_settings: collision_settings_pb2.CollisionSettings | None = None
  ensure_same_branch: bool = False
  prefer_same_branch: bool = False
  disable_error_on_collisions: bool = False
  collision_checker_config: (
      collision_checker_config_pb2.CollisionCheckerConfig | None
  ) = None
  context: context_pb2.Context | None = None
  disable_logging: bool = False
  caller_id: str | None = None


@dataclasses.dataclass
class MotionPlanningOptions:
  """Options for Motion Planning.

  Attributes:
    path_planning_time_out: Timeout for path planning algorithms in seconds.
    path_planning_step_size: Maximum step size deployed during path planning. If
      not specified, a default step size is used.
    lock_motion_configuration: Optional configuration for saving or loading a
      motion.
    skip_fuzzy_cache_check: If true, the cache will not check for fuzzy matches.
    compute_swept_volume: Optionally generate and return the swept volume for
      the computed path.
    shortcutting_combine_collinear_segments: If true, the joint_shortcutter will
      combine multiple collinear segments into a single segment. This speeds up
      planning time, but can result in longer paths and trajectories.
    collision_checker_config: Configuration override for the collision checker.
  """

  path_planning_time_out: int = 30
  path_planning_step_size: float | None = None
  lock_motion_configuration: (
      motion_planner_config_pb2.LockMotionConfiguration | None
  ) = None
  skip_fuzzy_cache_check: bool = False
  compute_swept_volume: bool = False
  shortcutting_combine_collinear_segments: bool = False
  collision_checker_config: (
      collision_checker_config_pb2.CollisionCheckerConfig | None
  ) = None
  collision_check_spacing_override: float | None = None
  enable_strict_trajectory_fallback: bool | None = None


@dataclasses.dataclass
class PlanTrajectoryResult:
  """Wrapped result from calling plan_trajectory.

  Attributes:
    trajectory: The computed joint trajectory containing position, velocity,
      and acceleration.
    swept_volume: An optional list of shapes that correspond to the swept
      volume of the trajectory.
    lock_motion_id: If the motion is locked based on the request, this is the ID
      of the motion for loading later. Note this field is not set if this
      response is from loading a locked motion.
  """

  trajectory: joint_space_pb2.JointTrajectoryPVA
  swept_volume: list[transformed_geometry_pb2.TransformedGeometry]
  lock_motion_id: Optional[str] = None

@dataclasses.dataclass
class PlanPathResult:
  """Wrapped result from calling plan_path."""

  path: motion_planning_pb2.Path
  swept_volume: list[transformed_geometry_pb2.TransformedGeometry]

@dataclasses.dataclass
class ComputeIkResult:
  """Wrapped result from calling compute_ik.

  Attributes:
    solutions: The computed IK solutions (joint configurations).
    ik_debug_information: Detailed internal diagnostics and metrics from the IK
      solver. NOTE: Unintuitively, the `ik_solutions` list within this debug
      structure contains all candidate configurations generated and evaluated
      during the search, including those that were ultimately REJECTED (e.g.,
      due to collisions, joint limit violations, or constraint failures).
  """

  solutions: list[list[float]]
  ik_debug_information: Optional[compute_ik_pb2.ComputeIkDebugInformation] = (
      None
  )

class MotionPlannerClientBase:
  """The base client for the MotionPlannerService.

  The class includes the externalized functionality of the MotionPlannerClient.
  """

  def __init__(
      self,
      world_id: str,
      stub: motion_planner_service_pb2_grpc.MotionPlannerServiceStub,
  ):
    self._world_id: str = world_id
    self._stub: motion_planner_service_pb2_grpc.MotionPlannerServiceStub = stub

  def clear_cache(self) -> empty_pb2.Empty:
    """Calls the ClearCache rpc."""
    return self._stub.ClearCache(empty_pb2.Empty())


class MotionPlannerClient(MotionPlannerClientBase):
  """Helper class for calling the rpcs in the MotionPlannerService.

  Provides additional computations on top of the world.
    * IK/FK
    * Path planning
  """

  def plan_trajectory(
      self,
      robot_specification: robot_specification_pb2.RobotSpecification,
      motion_specification: motion_specification_pb2.MotionSpecification,
      options: MotionPlanningOptions = MotionPlanningOptions(),
      caller_id: str = "Anonymous",
  ) -> PlanTrajectoryResult:
    """Plan trajectory for a given motion planning problem and robot.

    This method calls the Plan trajectory rpc.

    Args:
      robot_specification: Robot specification
      motion_specification: Motion specification, see MotionSpecification proto.
      options: Motion planning options that allows the path planning timeout to
        be set.
      caller_id: The id used for logging the request in the motion planner
        service.

    Returns:
      Discretized trajectory
    """
    request = motion_planner_service_pb2.MotionPlanningRequest(
        world_id=self._world_id,
        robot_specification=robot_specification,
        motion_specification=motion_specification,
        caller_id=caller_id,
    )
    request.motion_planner_config.timeout_sec.seconds = (
        options.path_planning_time_out
    )
    request.compute_swept_volume = options.compute_swept_volume

    if options.path_planning_step_size is not None:
      request.motion_planner_config.path_planning_step_size = (
          options.path_planning_step_size
      )
    if options.lock_motion_configuration:
      request.motion_planner_config.lock_motion_configuration.CopyFrom(
          options.lock_motion_configuration
      )
    if options.skip_fuzzy_cache_check:
      request.motion_planner_config.skip_fuzzy_cache_check = (
          options.skip_fuzzy_cache_check
      )
    if options.shortcutting_combine_collinear_segments:
      request.motion_planner_config.shortcutting_combine_collinear_segments = (
          options.shortcutting_combine_collinear_segments
      )
    if options.collision_checker_config is not None:
      request.motion_planner_config.collision_checker_config.CopyFrom(
          options.collision_checker_config
      )
    if options.collision_check_spacing_override is not None:
      request.motion_planner_config.collision_check_spacing_override = (
          options.collision_check_spacing_override
      )
    if options.enable_strict_trajectory_fallback is not None:
      request.motion_planner_config.enable_strict_trajectory_fallback = (
          options.enable_strict_trajectory_fallback
      )
    response = self._stub.PlanTrajectory(request)
    swept_volume = list(response.swept_volumes)
    lock_motion_id = (
        response.lock_motion_id if response.HasField("lock_motion_id") else None
    )
    return PlanTrajectoryResult(
        trajectory=response.discretized,
        swept_volume=swept_volume,
        lock_motion_id=lock_motion_id,
    )

  def plan_path(
      self,
      robot_specification: robot_specification_pb2.RobotSpecification,
      motion_specification: motion_specification_pb2.MotionSpecification,
      options: MotionPlanningOptions = MotionPlanningOptions(),
      caller_id: str = "Anonymous",
  ) -> PlanPathResult:
    """Plan path for a given motion planning problem and robot.

    This method calls the Plan Path rpc.

    Args:
      robot_specification: Robot specification
      motion_specification: Motion specification, see MotionSpecification proto.
      options: Motion planning options that allows the path planning timeout to
        be set.
      caller_id: The id used for logging the request in the motion planner
        service.

    Returns:
      Planned path consisting of waypoints.
    """
    request = motion_planner_service_pb2.MotionPlanningRequest(
        world_id=self._world_id,
        robot_specification=robot_specification,
        motion_specification=motion_specification,
        caller_id=caller_id,
    )
    request.motion_planner_config.timeout_sec.seconds = (
        options.path_planning_time_out
    )
    request.compute_swept_volume = options.compute_swept_volume

    if options.path_planning_step_size is not None:
      request.motion_planner_config.path_planning_step_size = (
          options.path_planning_step_size
      )
    if options.lock_motion_configuration:
      request.motion_planner_config.lock_motion_configuration.CopyFrom(
          options.lock_motion_configuration
      )
    if options.skip_fuzzy_cache_check:
      request.motion_planner_config.skip_fuzzy_cache_check = (
          options.skip_fuzzy_cache_check
      )
    if options.shortcutting_combine_collinear_segments:
      request.motion_planner_config.shortcutting_combine_collinear_segments = (
          options.shortcutting_combine_collinear_segments
      )
    if options.collision_checker_config is not None:
      request.motion_planner_config.collision_checker_config.CopyFrom(
          options.collision_checker_config
      )
    if options.collision_check_spacing_override is not None:
      request.motion_planner_config.collision_check_spacing_override = (
          options.collision_check_spacing_override
      )
    response = self._stub.PlanPath(request)
    swept_volume = list(response.swept_volumes)
    return PlanPathResult(
        path=response.path,
        swept_volume=swept_volume,
    )

  def compute_ik(
      self,
      robot_name: object_world_ids.WorldObjectName,
      target: (
          motion_target_pb2.CartesianMotionTarget
          | geometric_constraints_pb2.GeometricConstraint
      ),
      starting_joints: Optional[list[float]] = None,
      options: Optional[IKOptions] = None,
  ) -> ComputeIkResult:
    """Calls the ComputeIk rpc, doing argument conversion as necessary.

    Args:
      robot_name: Name of robot, must map to a kinematic object
      target: a target pose to compute ik for
      starting_joints: used as seed for ik, optional
      options: See IKOptions.

    Returns:
      A list of IK solutions
    """
    request = motion_planner_service_pb2.IkRequest(world_id=self._world_id)
    request.robot_reference.object_id.by_name.object_name = robot_name

    # Convert CartesianMotionTarget to GeometricConstraint::PoseEquality.
    if isinstance(target, motion_target_pb2.CartesianMotionTarget):
      request.target.cartesian_pose.moving_frame.CopyFrom(target.tool)
      request.target.cartesian_pose.target_frame.CopyFrom(target.frame)
      if target.HasField("offset"):
        request.target.cartesian_pose.target_frame_offset.CopyFrom(
            target.offset
        )
    else:
      request.target.CopyFrom(target)

    if starting_joints is not None:
      request.starting_joints.joints.extend(starting_joints)
    if options is not None:
      if options.collision_settings:
        request.collision_settings.CopyFrom(options.collision_settings)
      if options.ensure_same_branch:
        request.ensure_same_branch = options.ensure_same_branch
      if options.prefer_same_branch:
        request.prefer_same_branch = options.prefer_same_branch
      if options.max_num_solutions:
        request.max_num_solutions = options.max_num_solutions
      if options.disable_error_on_collisions:
        request.disable_error_on_collisions = (
            options.disable_error_on_collisions
        )
      if options.collision_checker_config:
        request.collision_checker_config.CopyFrom(
            options.collision_checker_config
        )
      if options.context:
        request.context.CopyFrom(options.context)
      if options.disable_logging:
        request.disable_logging = options.disable_logging
      if options.caller_id:
        request.caller_id = options.caller_id

    # Make the rpc.
    response = self._stub.ComputeIk(request)
    return ComputeIkResult(
        solutions=_repeated_vec_to_list_of_floats(response.solutions),
        ik_debug_information=response.ik_debug_information,
    )

  def compute_fk(
      self,
      robot_name: object_world_ids.WorldObjectName,
      joints: list[float],
      reference: object_world_ids.WorldObjectName,
      target: object_world_ids.WorldObjectName,
      reference_frame: Optional[object_world_ids.FrameName] = None,
      target_frame: Optional[object_world_ids.FrameName] = None,
  ) -> data_types.Pose3:
    """Calls the ComputeIk rpc, doing argument conversion as necessary.

    If reference_frame is not specified, the WorldObject 'reference' will be
    used directly. The same applies for target_frame/target.

    Args:
      robot_name: Name of robot, must map to a kinematic object
      joints: the joint configuration values to compute fk for
      reference: object name for the reference of the returned pose
      target: object name for the target of the returned pose
      reference_frame: name that specifies a frame under 'reference', optional
      target_frame: name that specifies a frame under 'target', optional

    Returns:
      The reference_t_target pose, i.e. the pose of 'target' in the frame of
      'reference'
    """
    request = motion_planner_service_pb2.FkRequest(world_id=self._world_id)
    request.robot_reference.object_id.by_name.object_name = robot_name
    request.joints.joints.extend(joints)
    if reference_frame is not None:
      # If frame is not None, then we'll assume the user is specifying a frame,
      # and otherwise the user is specifying an object.
      request.reference.by_name.frame.object_name = reference
      request.reference.by_name.frame.frame_name = reference_frame
    else:
      request.reference.by_name.object.object_name = reference
    if target_frame is not None:
      # If frame is not None, then we'll assume the user is specifying a frame,
      # and otherwise the user is specifying an object.
      request.target.by_name.frame.object_name = target
      request.target.by_name.frame.frame_name = target_frame
    else:
      request.target.by_name.object.object_name = target

    # Make the rpc.
    response = self._stub.ComputeFk(request)
    return math_proto_conversion.pose_from_proto(response.reference_t_target)

  def check_collisions(
      self,
      robot_name: object_world_ids.WorldObjectName,
      waypoints: list[joint_space_pb2.JointVec],
      options: CheckCollisionsOptions,
  ) -> motion_planner_service_pb2.CheckCollisionsResponse:
    """Calls the CheckCollisions rpc.

    Args:
      robot_name: Name of robot, must map to a kinematic object.
      waypoints: The waypoints define the path for which we check the collision.
        We also check the linear interpolation between the waypoints.
      options: Check collision options

    Returns:
      CheckCollisionResponse. See flag `has_collision` in response
      which indicates a collision.
    """

    request = motion_planner_service_pb2.CheckCollisionsRequest(
        world_id=self._world_id,
        robot_reference=robot_specification_pb2.RobotReference(),
        waypoint=waypoints,
        collision_settings=options.collision_settings,
    )
    request.robot_reference.object_id.by_name.object_name = robot_name
    response = self._stub.CheckCollisions(request)
    return response


def get_motion_planner_service_asset_client(
    world_id: str,
    mps_dependency: resolved_dependency_pb2.ResolvedDependency,
    grpc_options: Optional[Sequence[tuple[str, Any]]] = None,
) -> Optional[MotionPlannerClient]:
  """Returns a MotionPlannerClient connected to the MotionPlannerService Asset.

  Args:
    world_id: The world ID.
    mps_dependency: The resolved dependency.
    grpc_options: Optional gRPC options.

  Returns:
    A MotionPlannerClient or None if mps_dependency has no interfaces.
  """
  if not mps_dependency.interfaces:
    return None

  # Dynamically find the MotionPlannerService interface key.
  iface = None
  for key in mps_dependency.interfaces:
    if "MotionPlannerService" in key:
      iface = key
      break

  if iface is None:
    raise ValueError(
        "Could not find MotionPlannerService interface in resolved dependency. "
        f"Available interfaces: {list(mps_dependency.interfaces.keys())}"
    )

  channel = dep_utils.connect(
      mps_dependency,
      iface,
      grpc_options=grpc_options,
  )
  stub = motion_planner_service_pb2_grpc.MotionPlannerServiceStub(channel)
  return MotionPlannerClient(world_id, stub)
