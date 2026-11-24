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

"""A skill that does a primitive sequence to accomplish insertion."""

import datetime
import enum
import logging

import numpy as np

from intrinsic.icon.actions import force_primitive_utils
from intrinsic.icon.actions import stop_utils
from intrinsic.icon.control.primitives.force_control.proto import controller_params_pb2
from intrinsic.icon.control.primitives.force_control.proto import force_primitives_pb2
from intrinsic.icon.equipment import equipment_utils
from intrinsic.icon.python import cancelation
from intrinsic.icon.python import create_action_utils
from intrinsic.icon.python import icon_api
from intrinsic.icon.python import icon_logging
from intrinsic.logging.proto import log_item_pb2
from intrinsic.manipulation.skills.force import contact_stiffness_pb2
from intrinsic.manipulation.skills.force import move_to_contact_pb2
from intrinsic.manipulation.skills.force import utils
from intrinsic.math.python import data_types
from intrinsic.math.python import proto_conversion
from intrinsic.platform.pubsub.python import pubsub
from intrinsic.skills.proto import footprint_pb2
from intrinsic.skills.python import skill_interface
from intrinsic.util import decorators
from intrinsic.world.python import object_world_client
from intrinsic.world.python import object_world_resources

_EQUIPMENT_SLOT: str = "robot"


STOP_FORCE_RATIO = 0.5
MIN_APPROACH_TIME_S = 0.05
SETTLING_TIMEOUT_S = 2.0


# Error codes, sync with manifest
PARAMETER_ERROR_CODE = 10201
STABILIZE_ERROR_CODE = 10301
APPROACH_TIMEOUT_ERROR_CODE = 10302
TIMEOUT_ERROR_CODE = 10303

# Alias for nicer code below
SkillError = skill_interface.SkillError


class ActionId(enum.IntEnum):
  TARE = 0
  APPROACH = 1
  STABILIZE = 2
  JOINT_STOP = 4


MIN_STIFFNESS = 1.0e-3
MAX_STIFFNESS = 1.0e7


def _get_motion_direction(
    skill_params: move_to_contact_pb2.MoveToContactParams,
    world: object_world_client.ObjectWorldClient,
    robot_node: object_world_resources.TransformNode,
) -> np.ndarray:
  """Resolves motion direction from skill params.

  Args:
    skill_params: Move to contact skill params.
    world: Object world to query for frames.
    robot_node: Reference for base of robot being controlled.

  Returns:
    Direction to move, specified in robot base frame.

  Raises:
    SkillError: If one of fixed_vector or target are not defined. If direction
      of fixed_vector has norm close to 0.
  """

  if skill_params.HasField("fixed_vector"):
    if skill_params.fixed_vector.HasField("reference"):
      vector_ref = skill_params.fixed_vector.reference
    else:
      vector_ref = skill_params.tool

    if not skill_params.fixed_vector.HasField("direction"):
      vector_dir = np.array([0, 0, 1.0])
    else:
      vector_dir = np.array([
          skill_params.fixed_vector.direction.x,
          skill_params.fixed_vector.direction.y,
          skill_params.fixed_vector.direction.z,
      ])

    if np.isclose(np.linalg.norm(vector_dir), 0.0):
      raise SkillError(
          PARAMETER_ERROR_CODE,
          "Norm of direction vector cannot be close to 0 (is"
          f" {np.linalg.norm(vector_dir)}).",
      )
  else:
    vector_ref = skill_params.tool
    tool_t_target = world.get_transform(
        node_a=world.get_transform_node(skill_params.tool),
        node_b=world.get_transform_node(skill_params.target),
    )
    vector_dir = tool_t_target.translation
    if np.isclose(np.linalg.norm(vector_dir), 0.0):
      raise SkillError(
          PARAMETER_ERROR_CODE,
          "Target frame's translation must not match tool's translation.",
      )

  robot_t_motionref = world.get_transform(
      node_a=robot_node, node_b=world.get_transform_node(vector_ref)
  )
  vector_dir = vector_dir / np.linalg.norm(vector_dir)
  translational_motion_direction = robot_t_motionref.rotation.rotate_point(
      vector_dir
  )

  return translational_motion_direction


def _get_contact_stiffness(
    skill_params: move_to_contact_pb2.MoveToContactParams,
) -> contact_stiffness_pb2.ContactStiffnessParams:
  """Resolves contact stiffness from skill params.

  Args:
    skill_params: Move to contact skill params.

  Returns:
    Contact stiffness.
  """
  contact_stiffness = contact_stiffness_pb2.ContactStiffnessParams()
  if (
      skill_params.WhichOneof("contact_stiffness")
      == "default_contact_stiffness"
  ):
    contact_stiffness.default_contact_stiffness = (
        skill_params.default_contact_stiffness
    )
  else:
    contact_stiffness.mechanical_stiffness_at_contact = (
        skill_params.mechanical_stiffness_at_contact
    )
  return contact_stiffness


def _get_controller_params(
    params: move_to_contact_pb2.MoveToContactParams,
) -> controller_params_pb2.ControllerParams:
  """Resolves controller params from skill params."""
  controller_params = controller_params_pb2.ControllerParams()
  translational_stiffness_oneof = params.WhichOneof("translational_stiffness")
  if (
      translational_stiffness_oneof
      == "tool_translational_stiffness_in_nullspace"
  ):
    controller_params.translational_stiffness_orthogonal_to_direction = (
        params.tool_translational_stiffness_in_nullspace
    )
  elif translational_stiffness_oneof == "no_translational_compliance":
    controller_params.no_translational_compliance.CopyFrom(
        controller_params_pb2.ControllerParams.NotCompliant()
    )

  rotational_stiffness_oneof = params.WhichOneof("rotational_stiffness")
  if rotational_stiffness_oneof == "tool_rotational_stiffness_in_nullspace":
    controller_params.rotational_stiffness_orthogonal_to_direction = (
        params.tool_rotational_stiffness_in_nullspace
    )
  elif rotational_stiffness_oneof == "no_rotational_compliance":
    controller_params.no_rotational_compliance.CopyFrom(
        controller_params_pb2.ControllerParams.NotCompliant()
    )
  return controller_params


class MoveToContact(skill_interface.Skill):
  """A skill that moves the TCP until it makes contact with the environment."""

  _pubsub = pubsub.PubSub()

  @decorators.overrides(skill_interface.Skill)
  def get_footprint(
      self,
      request: skill_interface.GetFootprintRequest[
          move_to_contact_pb2.MoveToContactParams
      ],
      context: skill_interface.GetFootprintContext,
  ) -> footprint_pb2.Footprint:
    robot_node = context.get_kinematic_object_for_equipment(_EQUIPMENT_SLOT)

    return footprint_pb2.Footprint(
        object_reservation=[
            footprint_pb2.ObjectWorldReservation(
                type=footprint_pb2.ObjectWorldReservation.WRITE,
                object=robot_node.reference.by_name,
            ),
        ],
    )

  def validate_parameters(
      self,
      params: move_to_contact_pb2.MoveToContactParams,
      max_force: float,
  ):
    """Validates parameters for this skill."""
    if params.contact_force > max_force:
      raise SkillError(
          PARAMETER_ERROR_CODE,
          "Parameter contact_force must be less than or equal to the "
          f"force_control_settings.excessive_force_threshold ({max_force}) "
          "defined in the force-torque-sensor part config.",
      )
    elif params.contact_force <= 0.0:
      raise SkillError(
          PARAMETER_ERROR_CODE,
          "Parameter contact_force must be greater than 0 (is"
          f" {params.contact_force}).",
      )
    elif params.timeout_sec < 0.0:
      raise SkillError(
          PARAMETER_ERROR_CODE,
          "Parameter timeout_sec must be non-negative (is"
          f" {params.timeout_sec}).",
      )
    elif not params.HasField("tool"):
      raise SkillError(PARAMETER_ERROR_CODE, "Parameter tool must be set.")
    elif params.WhichOneof("motion_direction") is None:
      raise SkillError(
          PARAMETER_ERROR_CODE,
          "Parameter 'fixed_vector' or 'target' must be set.",
      )
    elif params.HasField("fixed_vector") and params.fixed_vector.HasField(
        "direction"
    ):
      direction = params.fixed_vector.direction
      direction_norm = np.linalg.norm(
          np.array([direction.x, direction.y, direction.z])
      )
      if direction_norm <= 0.0:
        raise SkillError(
            PARAMETER_ERROR_CODE,
            "Norm of direction vector must be greater than 0 (is"
            f" {direction_norm}).",
        )

    # Only call to validate contact stiffness parameters.
    _get_contact_stiffness(params)

  @decorators.overrides(skill_interface.Skill)
  def execute(
      self,
      request: skill_interface.ExecuteRequest[
          move_to_contact_pb2.MoveToContactParams
      ],
      context: skill_interface.ExecuteContext,
  ) -> None:
    logging.info("Executing MoveToContact skill version 2.")
    params = request.params

    resource_handle = context.resource_handles[_EQUIPMENT_SLOT]
    force_control_params = utils.ForceControlParams.from_resource_handle(
        resource_handle
    )

    self.validate_parameters(
        params, max_force=force_control_params.excessive_force_threshold
    )

    world = context.object_world
    icon_client: icon_api.Client = equipment_utils.init_icon_client(
        resource_handle
    )
    position_part_name: str = equipment_utils.get_position_part_name(
        resource_handle
    )
    ft_part_name: str = equipment_utils.get_force_torque_sensor_part_name(
        resource_handle
    )

    robot_object = world.get_kinematic_object(resource_handle)
    tool_node = world.get_transform_node(params.tool)
    motion_direction_in_base = _get_motion_direction(
        params, world, robot_object
    )
    tip_t_tool = world.get_transform(
        node_a=robot_object.get_single_iso_flange_frame(),
        node_b=tool_node,
    )

    projected_wrench_deadband_norm = (
        utils.compute_projected_wrench_deadband_norm(
            force_control_params.sensed_wrench_deadband,
            motion_direction_in_base,
        )
    )
    min_contact_force = 2.0 * projected_wrench_deadband_norm

    if params.contact_force < min_contact_force:
      raise SkillError(
          PARAMETER_ERROR_CODE,
          "Parameter contact_force must be greater than 2 * norm of "
          f"force_control_settings.sensed_wrench_deadband({min_contact_force}) "
          "defined in the force-torque-sensor part config.",
      )

    stop_switching_force = max(
        params.contact_force * STOP_FORCE_RATIO,
        min_contact_force,
    )

    tare_action = create_action_utils.create_tare_force_torque_sensor_action(
        action_id=ActionId.TARE, force_torque_sensor_part_name=ft_part_name
    )

    task_params = create_action_utils.build_task_params(
        robot_tip_t_robot_tool=tip_t_tool,
        robot_base_t_task=data_types.Pose3.identity(),
        contact_stiffness=_get_contact_stiffness(params),
    )

    approach_primitive = force_primitives_pb2.MakeContact(
        max_contact_force=params.contact_force,
        motion_direction=force_primitives_pb2.Direction(
            vector=proto_conversion.ndarray_to_vector3_proto(
                motion_direction_in_base
            ),
        ),
        controller_params=_get_controller_params(params),
    )

    approach_action = create_action_utils.create_make_contact_action(
        action_id=ActionId.APPROACH,
        joint_position_part_name=position_part_name,
        ft_sensor_part_name=ft_part_name,
        primitive=approach_primitive,
        force_control_settings=force_control_params,
        task_params=task_params,
    )

    stabilize_action = create_action_utils.create_make_contact_action(
        action_id=ActionId.STABILIZE,
        joint_position_part_name=position_part_name,
        ft_sensor_part_name=ft_part_name,
        primitive=approach_primitive,
        force_control_settings=force_control_params,
        task_params=task_params,
        disable_reference_lowpass_filter=True,
    )

    stop_action = create_action_utils.create_stop_action(
        action_id=ActionId.JOINT_STOP,
        joint_position_part_name=position_part_name,
    )

    approach_output_item: log_item_pb2.LogItem | None = None
    stabilize_output_item: log_item_pb2.LogItem | None = None

    def approach_output_callback(
        item: log_item_pb2.LogItem,
    ) -> None:
      nonlocal approach_output_item
      approach_output_item = item

    def stabilize_output_callback(
        item: log_item_pb2.LogItem,
    ) -> None:
      nonlocal stabilize_output_item
      stabilize_output_item = item

    robot_topic_name = icon_client.get_config().server_config.name

    # This creates a lot of overhead in the subscribtion but there is no better
    # way to get the last item on the topic after the action is finished.
    approach_topic = icon_logging.action_topic_name(
        robot_topic_name, approach_action.id
    )
    approach_subscription = self._pubsub.CreateSubscription(
        approach_topic,
        log_item_pb2.LogItem(),
        approach_output_callback,
    )

    stabilize_topic = icon_logging.action_topic_name(
        robot_topic_name, stabilize_action.id
    )
    stabilize_subscription = self._pubsub.CreateSubscription(
        stabilize_topic,
        log_item_pb2.LogItem(),
        stabilize_output_callback,
    )

    timeout = False

    try:
      with icon_client.start_session(
          [position_part_name, ft_part_name],
          context=context.logging_context.data_logger_context,
      ) as session:
        session.add_action_sequence([
            tare_action,
            (
                approach_action,
                icon_api.Condition.all_of([
                    # The minimal approach time ensures that short force spikes
                    # when starting the motion do not trigger the reaction. This
                    # happens sometimes in simulation and makes the skill flaky.
                    icon_api.Condition.is_greater_than(
                        force_primitive_utils.StateVariables.ELAPSED_TIME_SECONDS,
                        MIN_APPROACH_TIME_S,
                    ),
                    icon_api.Condition.is_greater_than(
                        force_primitive_utils.StateVariables.SENSED_FORCE,
                        stop_switching_force,
                    ),
                ]),
            ),
            (
                stabilize_action,
                icon_api.Condition.all_of([
                    icon_api.Condition.is_greater_than(
                        force_primitive_utils.StateVariables.SENSED_FORCE,
                        stop_switching_force,
                    ),
                    icon_api.Condition.is_true(
                        force_primitive_utils.StateVariables.IS_SETTLED
                    ),
                ]),
            ),
            stop_action,
        ])

        def _log_state_transition(
            timestamp: datetime.datetime, from_action_id: int, to_action_id: int
        ):
          del timestamp, from_action_id, to_action_id
          nonlocal timeout
          logging.warning(
              "Settling in contact timed out, stopping move_to_contact motion."
          )
          timeout = True

        session.add_transition(
            stabilize_action,
            stop_action,
            icon_api.Condition.is_greater_than(
                force_primitive_utils.StateVariables.ELAPSED_TIME_SECONDS,
                SETTLING_TIMEOUT_S,
            ),
            _log_state_transition,
        )

        icon_canceller = cancelation.IconSkillCanceller(
            session,
            context.canceller,
            tare_action,
            stop_action,
            success_condition=icon_api.Condition.is_true(
                stop_utils.StateVariables.IS_SETTLED
            ),
        )
        icon_canceller.start_and_wait(
            timeout_s=params.timeout_sec,
        )

        if icon_canceller.was_canceled():
          raise skill_interface.SkillCancelledError()
    finally:
      approach_subscription.Unsubscribe()
      stabilize_subscription.Unsubscribe()

    approach_output = None
    if approach_output_item is not None:
      approach_output, _ = icon_logging.unpack_streaming_output_logitem(
          approach_output_item,
          force_primitive_utils.StreamingOutputType,
      )

    stabilize_output = None
    if stabilize_output_item is not None:
      stabilize_output, _ = icon_logging.unpack_streaming_output_logitem(
          stabilize_output_item,
          force_primitive_utils.StreamingOutputType,
      )

    if timeout or icon_canceller.timed_out():
      if stabilize_output is not None:
        if stabilize_output.sensed_force_magnitude > min_contact_force:
          logging.warning(
              "Stabilize action did not settle but reached a valid contact"
              " force (%s N).",
              stabilize_output.sensed_force_magnitude,
          )
        else:
          raise SkillError(
              STABILIZE_ERROR_CODE,
              "Stabilize action timed out without making contact. Force is "
              f"{stabilize_output.sensed_force_magnitude} N.",
          )
      elif approach_output is not None:
        if approach_output.sensed_force_magnitude > min_contact_force:
          logging.warning(
              "Approach action did not switch to settling action but reached a "
              "valid contact force (%s N).",
              approach_output.sensed_force_magnitude,
          )

        else:
          raise SkillError(
              APPROACH_TIMEOUT_ERROR_CODE,
              "Approach action timed out without making contact. Force is "
              f"{approach_output.sensed_force_magnitude} N.",
          )
      else:
        raise SkillError(TIMEOUT_ERROR_CODE, "Move to contact timed out.")

    icon_status = icon_client.get_status()
    wrench_at_tip = proto_conversion.wrench_from_proto(
        icon_status.part_status[ft_part_name].wrench_at_tip
    )

    contact_force_norm = np.linalg.norm(wrench_at_tip.force)
    if contact_force_norm < projected_wrench_deadband_norm:
      logging.warning(
          "Contact was lost in the stop phase, contact force of %s N is lower"
          " than the projected wrench deadband of %s N.",
          contact_force_norm,
          projected_wrench_deadband_norm,
      )

  @decorators.overrides(skill_interface.Skill)
  def preview(
      self,
      request: skill_interface.PreviewRequest[
          move_to_contact_pb2.MoveToContactParams
      ],
      context: skill_interface.PreviewContext,
  ) -> None:
    # Not correct, but replicates the default implementation that the executive
    # used to use.
    return None
