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

"""Skill interface."""

import abc
from typing import Generic
from typing import TypeVar
from typing import Union

from google.protobuf import message as proto_message

from intrinsic.skills.proto import footprint_pb2
from intrinsic.skills.proto import skill_service_pb2
from intrinsic.skills.python import execute_context
from intrinsic.skills.python import execute_request
from intrinsic.skills.python import get_footprint_context
from intrinsic.skills.python import get_footprint_request
from intrinsic.skills.python import preview_context
from intrinsic.skills.python import preview_request
from intrinsic.util.status import status_exception

# Imported for convenience of skill implementations.
ExecuteContext = execute_context.ExecuteContext
ExecuteRequest = execute_request.ExecuteRequest
GetFootprintContext = get_footprint_context.GetFootprintContext
GetFootprintRequest = get_footprint_request.GetFootprintRequest
PreviewContext = preview_context.PreviewContext
PreviewRequest = preview_request.PreviewRequest

TParamsType = TypeVar('TParamsType', bound=proto_message.Message)
TResultType = TypeVar('TResultType', bound=Union[proto_message.Message, None])

# Error code shared with absl::StatusCode and google.rpc codes.
_CANCELLED_STATUS_CODE = 1


class SkillError(status_exception.ExtendedStatusError):
  """An error occurred in a skill.

  Raise this exception when encountering an error in a skill. Skill errors
  require a status code that identifies the error (or group of errors). These
  may be used in behavior trees that use the skill to create recoveries, and for
  aggregation and contacting support to identify the specific error (or group of
  errors).

  Specify the error codes you raise in the skill's manifest in the status_info
  field. It is preferred to add a title to the manifest, then you can leave this
  out when raising the exception.

  The message becomes the user report message, it will be directly visible
  to the user. Pass an internal report message if you want to give more in-depth
  information (and potentially more targeted to a skill developer when
  investigating an error). It is expected that this is not immediately visible
  to the user.

  For the message, be specific and ideally create a formatted string if there is
  additional data that could help to identify the cause of an issue.

  For example, a call to raise an error may look like this:

  raise SkillError(2342, f"Object out of range, distance {distance}")

  The Skill SDK will automatically set the appropriate component (skill ID),
  log context, and timestamp.
  """

  def __init__(
      self,
      code: int,
      message: str,
      *,
      title: str | None = None,
      debug_message: str | None = None,
  ):
    """Creates a skill error.

    Args:
      code: Numeric error code.
      message: Error message, should be a string with concise and specific
        information about the error, e.g., a formatted string including useful
        data. Pass longer or internal data as debug_message. The message is set
        on the user report.
      title: Optional title, if not given, will use title provided for the given
        code in the skill's manifest (preferred).
      debug_message: Set a message with additional information. This will be not
        be immediately displayed when showing the error. Provide in internal
        environments.
    """
    super().__init__(
        component='',  # will be automatically set by skill framework
        code=code,
        user_message=message,
    )

    if title is not None:
      self.set_title(title)

    if debug_message is not None:
      self.set_debug_message(debug_message)


class SkillCancelledError(SkillError):
  """A skill was aborted due to a cancellation request.

  Raise this error when stopping execution on a user's request. An additional
  message may be provided but is generally not accessible to users.
  """

  def __init__(self, message: str = ''):
    """Creates a new instance.

    Args:
      message: A message which is NOT shown to the user and only exists for
        general compatibility with Python's error interface.
    """
    super().__init__(_CANCELLED_STATUS_CODE, message)



# TODO: b/338577358 - Inherit from ExtendedStatusError and migrate skills

class InvalidSkillParametersError(ValueError):
  """Invalid arguments were passed to the skill parameters."""


class SkillExecuteInterface(abc.ABC, Generic[TParamsType, TResultType]):
  """Interface for skill execution.

  Implementations of the SkillExecuteInterface define how a skill behaves when
  it is executed.

  Errors are reported by raising a SkillError in the respective functions.
  """

  @abc.abstractmethod
  def execute(
      self, request: ExecuteRequest[TParamsType], context: ExecuteContext
  ) -> TResultType:
    """Executes the skill.

    If the skill implementation supports cancellation, it should:
    1) Set `supports_cancellation` to true in its manifest.
    2) Stop as soon as possible and leave resources in a safe and recoverable
       state when a cancellation request is received (via its ExecuteContext).
       Cancelled skill executions should end by raising SkillCancelledError.

    Any error raised by the skill will be handled by the executive that runs the
    process to which the skill belongs. The effect of the error will depend on
    how the skill is integrated into that process' behavior tree. For instance,
    if the skill is part of a fallback node, a skill error will trigger the
    fallback behavior. If the skill is not part of any node that handles errors,
    then a skill error will cause the process to fail.

    Currently, there is no way to distinguish between potentially recoverable
    failures that should lead to fallback handling (e.g., failure to achieve the
    skill's objective) and other failures that should cause the entire process
    to abort (e.g., failure to connect to a gRPC service).

    Args:
      request: The execute request, including parameters for the execution.
      context: Provides access to the world and other services that the skill
        may use.

    Returns:
      The skill's result message, or None if it does not return a result.

    Raises:
      InvalidSkillParametersError: If the arguments provided to skill parameters
        are invalid.
      SkillCancelledError: If the skill is aborted due to a cancellation
        request.
      SkillError: Errors encountered while executing methods of this interface.
    """

  def preview(
      self, request: PreviewRequest[TParamsType], context: PreviewContext
  ) -> TResultType:
    """Previews the expected outcome of executing the skill.

    `preview` enables an application developer to perform a "dry run" of skill
    execution (or execution of a process that includes that skill) in order to
    preview the effect of executing the skill/process, but without any
    real-world side-effects that normal execution would entail.

    Skill developers should override this method with their implementation. The
    implementation will not have access to hardware resources that are provided
    to `execute`, but will have access to a hypothetical world in which to
    preview the execution. The implementation should return the expected output
    of executing the skill in that world.

    If a skill does not override the default implementation, any process that
    includes that skill will not be executable in "preview" mode.

    NOTE: In preview mode, the object world provided by the PreviewContext
    is treated as the -actual- state of the physical world, rather than as the
    belief state that it represents during normal skill execution. Because of
    this difference, a skill in preview mode cannot introduce or correct
    discrepancies between the physical and belief world (since they are
    identical). For example, if a perception skill only updates the belief state
    when it is executed, then its implementation of `preview` would necessarily
    be a no-op.

    If executing the skill is expected to affect the physical world, then the
    implementation should record the timing of its expected effects using
    `context.record_world_update`. It should NOT make changes to the object
    world via interaction with `context.object_world`.

    The .skill_interface_utils module provides convenience utils that can be
    used to implement `preview` in common scenarios. E.g.:
    - `preview_via_execute`: If executing the skill does not require resources
      or modify the world.

    Args:
      request: The preview request.
      context: Provides access to services that the skill may use.

    Returns:
      The skill's expected result message, or None if it does not return a
      result.

    Raises:
      InvalidSkillParametersError: If the arguments provided to skill parameters
        are invalid.
      SkillCancelledError: If the skill preview is aborted due to a cancellation
        request.
    """
    del request  # Unused in this default implementation.
    del context  # Unused in this default implementation.
    raise NotImplementedError(
        f'Skill "{type(self).__name__!r} has not implemented `preview`.'
    )


class SkillProjectInterface(abc.ABC, Generic[TParamsType, TResultType]):
  """Interface for skill projecting.

  Implementations of SkillProjectInterface predict how a skill might behave
  during execution. The methods of this interface should be invokable prior to
  execution to allow a skill to:
  * provide an understanding of what the footprint of the skill on the workcell
    will be when it is executed.

  Errors are reported by raising a SkillError in the respective functions.

  Raises:
    SkillError: Errors encountered while executing methods of this interface.
  """

  def get_footprint(
      self,
      request: GetFootprintRequest[TParamsType],
      context: GetFootprintContext,
  ) -> footprint_pb2.Footprint:
    """Returns the resources required for running this skill.

    Skill developers should override this method with their implementation.

    If a skill does not implement `get_footprint`, the default implementation
    specifies that the skill needs exclusive access to everything. The skill
    will therefore not be able to execute in parallel with any other skill.

    Args:
      request: The get footprint request.
      context: Provides access to the world and other services that a skill may
        use.

    Returns:
      The skill's footprint.
    """
    del request  # Unused in this default implementation.
    del context  # Unused in this default implementation.
    return footprint_pb2.Footprint(lock_the_universe=True)


class Skill(
    SkillProjectInterface[TParamsType, TResultType],
    SkillExecuteInterface[TParamsType, TResultType],
    Generic[TParamsType, TResultType],
):
  """Interface for skills.

  This interface combines all skill constituents:
  - SkillProjectInterface: Skill prediction.
  - SkillExecuteInterface: Skill execution.

  In case of errors raise a SkillError.
  """
