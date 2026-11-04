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

"""A skill that controls a pinch gripper."""

from absl import logging
from google.protobuf.internal import containers
import grpc

from intrinsic.assets import interface_utils
from intrinsic.assets.dependencies import utils as asset_utils
from intrinsic.hardware.gripper.eoat import eoat_service_pb2
from intrinsic.hardware.gripper.eoat import eoat_service_pb2_grpc
from intrinsic.manipulation.skills import control_pinch_gripper_error_codes as error_codes
from intrinsic.manipulation.skills import control_pinch_gripper_pb2
from intrinsic.skills.proto import footprint_pb2
from intrinsic.skills.python import skill_interface as skl
from intrinsic.world.proto import object_world_refs_pb2
from intrinsic.world.proto import object_world_updates_pb2
from intrinsic.world.python import object_world_resources

_PREDICTED_MOVEMENT_DURATION_SECS: float = 1.0

_EQUIPMENT_SLOT: str = "pinch_gripper"

PinchGripperCommands = control_pinch_gripper_pb2.PinchGripperCommand


def _pinch_gripper_interface_uri() -> str:
  return (
      f"{interface_utils.GRPC_URI_PREFIX}"
      f"{eoat_service_pb2.DESCRIPTOR.services_by_name['PinchGripper'].full_name}"
  )


# Message fragments of the EOAT gripper service (`HealthStateMachine::IsEnabled`)
# and of the GPIO client (`GPIOClient::WriteInternal`) behind it; used to tell
# apart failures that share a gRPC status code. Mirrors
# gripper_skill_errors.cc, which the C++ gripper skills share.
_NOT_ENABLED_MESSAGE_FRAGMENT: str = "is not enabled"
# `GPIOClient::WriteInternal` wraps stream transport failures and an ABORTED
# session status (e.g. ICON not enabled) into INTERNAL statuses whose messages
# name "GPIO". Every other status of the GPIO server is forwarded verbatim
# (PERMISSION_DENIED for signals claimed by another session, INVALID_ARGUMENT
# for a value of the wrong type, the OPC UA server's INTERNAL write failures).
# Those are deliberately left on GRIPPER_SERVICE_ERROR_CODE: none of the
# specific codes fits a configuration or contention problem, and matching the
# GPIO backends' message texts would not be robust. The user message carries
# the server's text.
# TODO(b/559844222): Classify by the ExtendedStatus codes the gripper services
# attach (PRs #58961, #58963) instead of message fragments.
_GPIO_MESSAGE_FRAGMENT: str = "GPIO"
_SIGNALS_NOT_FOUND_WRITABLE_MESSAGE_FRAGMENT: str = "not found writable"


def _gripper_service_error_code(
    status_code: grpc.StatusCode | None, details: str
) -> int:
  """Maps a failed gripper service RPC to the most specific skill error code.

  Classifies by gRPC status code first and by message second. Statuses that
  match no specific code fall back to GRIPPER_SERVICE_ERROR_CODE.

  Args:
    status_code: The gRPC status code of the failed RPC, if known.
    details: The status message of the failed RPC.

  Returns:
    One of the error codes declared in control_pinch_gripper_error_codes.py.
  """
  if status_code == grpc.StatusCode.FAILED_PRECONDITION:
    if _NOT_ENABLED_MESSAGE_FRAGMENT in details:
      return error_codes.GRIPPER_NOT_ENABLED_ERROR_CODE
    return error_codes.GRIPPER_SERVICE_ERROR_CODE
  if status_code == grpc.StatusCode.INTERNAL:
    if _GPIO_MESSAGE_FRAGMENT in details:
      # In simulation (`gazebo/plugins/gpio/gpio_service.cc`), an unknown signal
      # aborts the write stream with `"Write signals failed with error
      # NOT_FOUND: Signals [...] not found writable among N plugin(s)..."`,
      # which `GPIOClient::WriteInternal` wraps into an INTERNAL status.
      if _SIGNALS_NOT_FOUND_WRITABLE_MESSAGE_FRAGMENT in details:
        return error_codes.GRIPPER_IO_SIGNALS_NOT_FOUND_ERROR_CODE
      return error_codes.GRIPPER_IO_WRITE_FAILED_ERROR_CODE
    return error_codes.GRIPPER_SERVICE_ERROR_CODE
  if status_code == grpc.StatusCode.NOT_FOUND:
    # The hardware GPIO service passes NOT_FOUND through for signal names it
    # does not know, e.g. "Signal `x` was not found." (ICON).
    return error_codes.GRIPPER_IO_SIGNALS_NOT_FOUND_ERROR_CODE
  if status_code in (
      grpc.StatusCode.UNAVAILABLE,
      grpc.StatusCode.DEADLINE_EXCEEDED,
  ):
    return error_codes.GRIPPER_SERVICE_UNREACHABLE_ERROR_CODE
  return error_codes.GRIPPER_SERVICE_ERROR_CODE


def _gripper_service_error(e: grpc.RpcError) -> skl.SkillError:
  """Wraps a failed gripper service RPC into the most specific SkillError.

  The user message names the gRPC status code and, if the service sent one,
  the status message; the full gRPC error stays in the debug message.

  Args:
    e: The error raised by the gripper service stub.

  Returns:
    The SkillError to raise in place of `e`.
  """
  if isinstance(e, grpc.Call):
    status_code = e.code()
    details = e.details() or ""
  else:
    status_code = None
    details = str(e)
  code_name = status_code.name if status_code is not None else "UNKNOWN"
  message = f"Pinch gripper service RPC failed ({code_name})"
  if details:
    message += f": {details}"
  return skl.SkillError(
      _gripper_service_error_code(status_code, details),
      message,
      debug_message=str(e),
  )


class ControlPinchGripper(skl.Skill):
  """A skill that controls a pinch gripper."""

  def execute(
      self,
      request: skl.ExecuteRequest[PinchGripperCommands],
      context: skl.ExecuteContext,
  ) -> None:
    """Executes a given pinch gripper command and updates the world."""
    world = context.object_world
    try:
      gripper_object = world.get_kinematic_object(
          request.params.pinch_gripper.object.name
      )
    except (ValueError, grpc.RpcError) as e:
      raise skl.SkillError(
          error_codes.PARAMETERIZATION_ERROR_CODE,
          f"Parameter `pinch_gripper` must reference a kinematic object: {e}",
      ) from e

    try:
      channel = asset_utils.connect(
          request.params.pinch_gripper,
          _pinch_gripper_interface_uri(),
      )
    except (asset_utils.MissingInterfaceError, asset_utils.NotGRPCError) as e:
      raise skl.SkillError(
          error_codes.GRIPPER_SERVICE_ERROR_CODE,
          f"Failed to connect to pinch gripper: {e}",
      ) from e
    stub = eoat_service_pb2_grpc.PinchGripperStub(channel)

    command, expected_position = self._parse_params(
        request.params, gripper_object
    )
    try:
      if command == "grasp":
        logging.info("pinch_gripper.grasp:\n%s", request.params.grasp)
        stub.Grasp(request.params.grasp)
      elif command == "release":
        logging.info("pinch_gripper.release:\n%s", request.params.release)
        stub.Release(request.params.release)
      else:
        raise skl.SkillError(
            error_codes.PARAMETERIZATION_ERROR_CODE,
            f"Command is not supported: {request.params}!",
        )
    except grpc.RpcError as e:
      raise _gripper_service_error(e) from e

    logging.info("Move gripper joints to %s", expected_position)

    world.update_joint_positions(
        gripper_object, joint_positions=expected_position
    )

  def get_footprint(
      self,
      request: skl.GetFootprintRequest[PinchGripperCommands],
      context: skl.GetFootprintContext,
  ) -> footprint_pb2.Footprint:
    """Returns the footprint of this skill."""
    return footprint_pb2.Footprint(
        object_reservation=[
            footprint_pb2.ObjectWorldReservation(
                type=footprint_pb2.ObjectWorldReservation.WRITE,
                object=object_world_refs_pb2.ObjectReferenceByName(
                    object_name=request.params.pinch_gripper.object.name
                ),
            )
        ],
        lock_the_universe=False,
    )

  def preview(
      self,
      request: skl.PreviewRequest[PinchGripperCommands],
      context: skl.PreviewContext,
  ) -> None:
    gripper_object = context.object_world.get_kinematic_object(
        request.params.pinch_gripper.object.name
    )

    positions = gripper_object.joint_positions
    logging.info("Found gripper with joint positions: %s", positions)

    _, expected_position = self._parse_params(request.params, gripper_object)

    world_update = object_world_updates_pb2.ObjectWorldUpdate(
        update_object_joints=(
            object_world_updates_pb2.UpdateObjectJointsRequest(
                object=gripper_object.reference,
                joint_positions=expected_position,
                joint_names=gripper_object.joint_entity_names,
            )
        )
    )
    context.record_world_update(
        update=world_update,
        elapsed=0.0,
        duration=_PREDICTED_MOVEMENT_DURATION_SECS,
    )

  def _parse_params(
      self,
      params: PinchGripperCommands,
      gripper_object: object_world_resources.KinematicObject,
  ) -> tuple[str, containers.RepeatedScalarFieldContainer]:
    """Determine the expected joint position given a command."""
    if params.WhichOneof("command") == "grasp":
      return (
          "grasp",
          gripper_object.joint_application_limits.min_position.values,
      )
    elif params.WhichOneof("command") == "release":
      return (
          "release",
          gripper_object.joint_application_limits.max_position.values,
      )
    else:
      raise skl.SkillError(
          error_codes.PARAMETERIZATION_ERROR_CODE,
          f"Command is not supported: {params}!",
      )
