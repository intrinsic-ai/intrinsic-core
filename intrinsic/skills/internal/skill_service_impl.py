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

"""Implementations of Skill project, execute, and info servicers."""

from __future__ import annotations

from collections.abc import Callable
from concurrent import futures
import textwrap
import threading
import traceback
from typing import cast
from typing import NoReturn
from typing import Optional

from absl import logging
from google.longrunning import operations_pb2
from google.protobuf import any_pb2
from google.protobuf import descriptor as proto_descriptor
from google.protobuf import empty_pb2
from google.protobuf import message as proto_message
from google.protobuf import message_factory
from google.rpc import status_pb2
import grpc

from intrinsic.assets import id_utils
from intrinsic.geometry.proto import geometry_service_pb2_grpc
from intrinsic.logging.proto import context_pb2
from intrinsic.motion_planning import motion_planner_client
from intrinsic.motion_planning.proto.v1 import motion_planner_service_pb2_grpc
from intrinsic.skills.internal import default_parameters
from intrinsic.skills.internal import error_bindings
from intrinsic.skills.internal import error_utils
from intrinsic.skills.internal import execute_context_impl
from intrinsic.skills.internal import get_footprint_context_impl
from intrinsic.skills.internal import preview_context_impl
from intrinsic.skills.internal import runtime_data as rd
from intrinsic.skills.internal import skill_repository as skill_repo
from intrinsic.skills.proto import error_pb2
from intrinsic.skills.proto import footprint_pb2
from intrinsic.skills.proto import skill_service_pb2
from intrinsic.skills.proto import skill_service_pb2_grpc
from intrinsic.skills.proto import skills_pb2
from intrinsic.skills.python import proto_utils
from intrinsic.skills.python import skill_canceller
from intrinsic.skills.python import skill_interface as skl
from intrinsic.skills.python import skill_logging_context
from intrinsic.util.status import extended_status_pb2
from intrinsic.util.status import status_exception
from intrinsic.world.proto import object_world_service_pb2_grpc
from intrinsic.world.python import object_world_client

from pybind11_abseil import status  # isort: skip

# Maximum number of operations to keep in a SkillOperations instance.
# This value places a hard upper limit on the number of one type of skill that
# can execute simultaneously.
MAX_NUM_OPERATIONS = 100

# Length an extended status message is shortened to to make it the title.
EXTENDED_STATUS_TITLE_SHORTEN_TO_LENGTH = 75

# Default timeout, in seconds, to use when unspecified by caller.
# Should be kept in sync with skills::kClientDefaultTimeout.
DEFAULT_TIMEOUT = 180.0


class InvalidResultTypeError(TypeError):
  """A skill returned a result that does not match the expected type."""


class _CannotConstructRequestError(Exception):
  """The service cannot construct a request for a skill."""


class SkillProjectorServicer(skill_service_pb2_grpc.ProjectorServicer):
  """Implementation of the skill Projector servicer."""

  def __init__(
      self,
      skill_repository: skill_repo.SkillRepository,
      object_world_service: object_world_service_pb2_grpc.ObjectWorldServiceStub,
      motion_planner_service: motion_planner_service_pb2_grpc.MotionPlannerServiceStub,
      geometry_service: geometry_service_pb2_grpc.GeometryServiceStub,
  ):
    self._skill_repository = skill_repository
    self._object_world_service = object_world_service
    self._motion_planner_service = motion_planner_service
    self._geometry_service = geometry_service

  def GetFootprint(
      self,
      footprint_request: skill_service_pb2.GetFootprintRequest,
      context: grpc.ServicerContext,
  ) -> skill_service_pb2.GetFootprintResult:
    """Runs Skill get_footprint operation with provided parameters.

    Args:
      footprint_request: GetFootprintRequest with skill instance to run
        get_footprint on.
      context: gRPC servicer context.

    Returns:
      GetFootprintResult containing results of the footprint calculation.

    Raises:
     grpc.RpcError:
      NOT_FOUND: If the skill is not found.
      INVALID_ARGUMENT: If unable to apply the default parameters.
      INTERNAL: If unable to get the skill's footprint.
      INVALID_ARGUMENT: When the required equipment does not match the
          requested.
    """
    skill_name = id_utils.name_from(footprint_request.instance.id_version)
    try:
      skill_project_instance = self._skill_repository.get_skill_project(
          skill_name
      )
    except skill_repo.InvalidSkillAliasError:
      _abort_with_status(
          context=context,
          code=status.StatusCode.NOT_FOUND,
          message=(
              f'Skill not found: {footprint_request.instance.id_version!r}.'
          ),
          skill_error_info=error_pb2.SkillErrorInfo(
              error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_GRPC
          ),
      )

    # Apply default parameters if available.
    skill_runtime_data = self._skill_repository.get_skill_runtime_data(
        skill_name
    )
    defaults = skill_runtime_data.parameter_data.default_value
    if defaults is not None and footprint_request.HasField('parameters'):
      try:
        default_parameters.apply_defaults_to_parameters(
            skill_runtime_data.parameter_data.descriptor,
            defaults,
            footprint_request.parameters,
        )
      except status.StatusNotOk as e:
        _abort_with_status(
            context=context,
            code=e.status.code(),
            message=str(e),
            skill_error_info=error_pb2.SkillErrorInfo(
                error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_SKILL
            ),
        )

    try:
      request = _proto_to_get_footprint_request(
          footprint_request, skill_runtime_data
      )
    except _CannotConstructRequestError as err:
      _abort_with_status(
          context=context,
          code=status.StatusCode.INTERNAL,
          message=(
              'Could not construct get footprint request for skill'
              f' {footprint_request.instance.id_version}: {err}.'
          ),
          skill_error_info=error_pb2.SkillErrorInfo(
              error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_SKILL
          ),
      )

    object_world = object_world_client.ObjectWorldClient(
        footprint_request.world_id,
        self._object_world_service,
        self._geometry_service,
    )
    motion_planner = motion_planner_client.MotionPlannerClient(
        footprint_request.world_id, self._motion_planner_service
    )

    context_id = str(footprint_request.context.executive_plan_action_id)

    footprint_context = get_footprint_context_impl.GetFootprintContextImpl(

        geometry_service=self._geometry_service,

        motion_planner=motion_planner,
        object_world=object_world,
        resource_handles=dict(footprint_request.instance.resource_handles),
        context_id=context_id,
    )

    try:
      skill_footprint = skill_project_instance.get_footprint(
          request, footprint_context
      )
    except Exception as err:  # pylint: disable=broad-except
      error_status = _handle_skill_error(
          err=err,
          skill_id=skill_runtime_data.skill_id,
          op_name='get_footprint',
          log_context=footprint_request.context,
          status_specs=skill_runtime_data.status_specs,
      )

      _abort_with_status(
          context=context,
          code=status.StatusCodeFromInt(error_status.code),
          message=error_status.message,
          skill_error_info=error_pb2.SkillErrorInfo(
              error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_SKILL
          ),
      )

    # Add required equipment to the footprint automatically
    required_equipment = skill_runtime_data.resource_data.required_resources
    for name, selector in required_equipment.items():
      if name in footprint_request.instance.resource_handles:
        handle = footprint_request.instance.resource_handles[name]
        skill_footprint.resource_reservation.append(
            footprint_pb2.ResourceReservation(
                type=selector.sharing_type, name=handle.name
            )
        )
      else:
        _abort_with_status(
            context=context,
            code=status.StatusCode.INVALID_ARGUMENT,
            message=(
                'Error when specifying equipment resources. Skill requires'
                f' {list(required_equipment)} but got'
                f' {list(footprint_request.instance.resource_handles)}.'
            ),
            skill_error_info=error_pb2.SkillErrorInfo(
                error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_SKILL
            ),
        )

    return skill_service_pb2.GetFootprintResult(footprint=skill_footprint)


class SkillExecutorServicer(skill_service_pb2_grpc.ExecutorServicer):
  """Servicer implementation for the skill Executor service."""

  def __init__(
      self,
      skill_repository: skill_repo.SkillRepository,
      object_world_service: (
          object_world_service_pb2_grpc.ObjectWorldServiceStub
      ),
      motion_planner_service: (
          motion_planner_service_pb2_grpc.MotionPlannerServiceStub
      ),
      geometry_service: geometry_service_pb2_grpc.GeometryServiceStub,
  ):
    self._skill_repository = skill_repository
    self._object_world_service = object_world_service
    self._motion_planner_service = motion_planner_service
    self._geometry_service = geometry_service

    self._operations = _SkillOperations()

  def StartExecute(
      self,
      request: skill_service_pb2.ExecuteRequest,
      context: grpc.ServicerContext,
  ) -> operations_pb2.Operation:
    """Starts executing the skill as a long-running operation.

    Args:
      request: Execute request with skill instance to execute.
      context: gRPC servicer context.

    Returns:
      Operation representing the skill execution operation.

    Raises:
      grpc.RpcError:
        NOT_FOUND: If the skill cannot be found.
        INTERNAL: If the default parameters cannot be applied.
        ALREADY_EXISTS: If a skill execution operation with the specified name
            (i.e., the skill instance name) already exists.
        FAILED_PRECONDITION: If the operation cache is already full of
            unfinished operations.
    """
    skill_name = id_utils.name_from(request.instance.id_version)
    operation = self._make_operation(
        name=request.instance.instance_name,
        skill_name=skill_name,
        context=context,
    )

    skill = self._skill_repository.get_skill_execute(skill_name)

    try:
      skill_request = skl.ExecuteRequest(
          params=_resolve_params(request.parameters, operation.runtime_data),
      )
    except _CannotConstructRequestError as err:
      _abort_with_status(
          context=context,
          code=status.StatusCode.INTERNAL,
          message=(
              'Could not construct execute request for skill'
              f' {operation.runtime_data.skill_id}: {err}'
          ),
          skill_error_info=error_pb2.SkillErrorInfo(
              error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_SKILL
          ),
      )

    logging_context = skill_logging_context.SkillLoggingContext(
        data_logger_context=request.context,
        skill_id=operation.runtime_data.skill_id,
    )

    context_id = str(request.context.executive_plan_action_id)

    skill_context = execute_context_impl.ExecuteContextImpl(
        canceller=operation.canceller,

        geometry_service=self._geometry_service,

        logging_context=logging_context,
        motion_planner=motion_planner_client.MotionPlannerClient(
            request.world_id, self._motion_planner_service
        ),
        object_world=object_world_client.ObjectWorldClient(
            request.world_id, self._object_world_service, self._geometry_service
        ),
        resource_handles=dict(request.instance.resource_handles),
        context_id=context_id,
    )

    def execute() -> skill_service_pb2.ExecuteResult:
      result = skill.execute(skill_request, skill_context)

      # Verify that the skill returned the expected type.
      got_name = None if result is None else result.DESCRIPTOR.full_name
      want_name = operation.runtime_data.return_type_data.message_full_name
      if got_name != want_name:
        got = 'None' if got_name is None else got_name
        want = 'None' if want_name is None else want_name
        raise InvalidResultTypeError(
            f'Unexpected return type (expected: {want}, got: {got}).'
        )

      if result is None:
        result_any = None
      else:
        result_any = any_pb2.Any()
        result_any.Pack(result)

      return skill_service_pb2.ExecuteResult(result=result_any)

    operation.start(op=execute, op_name='execute', log_context=request.context)

    return operation.operation

  def StartPreview(
      self,
      request: skill_service_pb2.PreviewRequest,
      context: grpc.ServicerContext,
  ) -> operations_pb2.Operation:
    """Starts previewing the skill as a long-running operation.

    Args:
      request: Preview request with skill instance to preview.
      context: gRPC servicer context.

    Returns:
      Operation representing the skill execution operation.

    Raises:
      grpc.RpcError:
        NOT_FOUND: If the skill cannot be found.
        INTERNAL: If the default parameters cannot be applied.
        ALREADY_EXISTS: If a skill execution operation with the specified name
            (i.e., the skill instance name) already exists.
        FAILED_PRECONDITION: If the operation cache is already full of
            unfinished operations.
    """
    skill_name = id_utils.name_from(request.instance.id_version)
    operation = self._make_operation(
        name=request.instance.instance_name,
        skill_name=skill_name,
        context=context,
    )

    skill = self._skill_repository.get_skill_execute(skill_name)

    try:
      skill_request = skl.PreviewRequest(
          params=_resolve_params(request.parameters, operation.runtime_data),
      )
    except _CannotConstructRequestError as err:
      _abort_with_status(
          context=context,
          code=status.StatusCode.INTERNAL,
          message=(
              'Could not construct preview request for skill'
              f' {operation.runtime_data.skill_id}: {err}'
          ),
          skill_error_info=error_pb2.SkillErrorInfo(
              error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_SKILL
          ),
      )

    logging_context = skill_logging_context.SkillLoggingContext(
        data_logger_context=request.context,
        skill_id=operation.runtime_data.skill_id,
    )

    context_id = str(request.context.executive_plan_action_id)

    skill_context = preview_context_impl.PreviewContextImpl(
        canceller=operation.canceller,

        geometry_service=self._geometry_service,

        logging_context=logging_context,
        motion_planner=motion_planner_client.MotionPlannerClient(
            request.world_id, self._motion_planner_service
        ),
        object_world=object_world_client.ObjectWorldClient(
            request.world_id, self._object_world_service, self._geometry_service
        ),
        resource_handles=dict(request.instance.resource_handles),
        context_id=context_id,
    )

    def preview() -> skill_service_pb2.PreviewResult:
      result = skill.preview(skill_request, skill_context)

      # Verify that the skill returned the expected type.
      got_name = None if result is None else result.DESCRIPTOR.full_name
      want_name = operation.runtime_data.return_type_data.message_full_name
      if got_name != want_name:
        got = 'None' if got_name is None else got_name
        want = 'None' if want_name is None else want_name
        raise InvalidResultTypeError(
            f'Unexpected return type (expected: {want}, got: {got}).'
        )

      if result is None:
        result_any = None
      else:
        result_any = any_pb2.Any()
        result_any.Pack(result)

      return skill_service_pb2.PreviewResult(
          result=result_any, expected_states=skill_context.world_updates
      )

    operation.start(op=preview, op_name='preview', log_context=request.context)

    return operation.operation

  def GetOperation(
      self,
      get_request: operations_pb2.GetOperationRequest,
      context: grpc.ServicerContext,
  ) -> operations_pb2.Operation:
    """Gets the current state of a skill execution operation.

    Args:
      get_request: Get request with skill execution operation name.
      context: gRPC servicer context.

    Returns:
      Operation representing the skill execution operation.

    Raises:
      grpc.RpcError:
        NOT_FOUND: If the operation cannot be found.
    """
    try:
      operation = self._operations.get(get_request.name)
    except self._operations.OperationNotFoundError as err:
      _abort_with_status(
          context=context,
          code=status.StatusCode.NOT_FOUND,
          message=str(err),
          skill_error_info=error_pb2.SkillErrorInfo(
              error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_GRPC
          ),
      )

    return operation.operation

  def CancelOperation(
      self,
      cancel_request: operations_pb2.CancelOperationRequest,
      context: grpc.ServicerContext,
  ) -> empty_pb2.Empty:
    """Requests cancellation of a skill execution operation.

    Args:
      cancel_request: Cancel request with skill operation name.
      context: gRPC servicer context.

    Returns:
      Empty.

    Raises:
      grpc.RpcError:
        NOT_FOUND: If the operation cannot be found.
        FAILED_PRECONDITION: If the operation was already cancelled.
        UNIMPLEMENTED: If the skill does not support cancellation.
        INTERNAL: If a skill cancellation callback raises an error.
    """
    try:
      operation = self._operations.get(cancel_request.name)
    except self._operations.OperationNotFoundError as err:
      _abort_with_status(
          context=context,
          code=status.StatusCode.NOT_FOUND,
          message=str(err),
          skill_error_info=error_pb2.SkillErrorInfo(
              error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_GRPC
          ),
      )

    try:
      operation.request_cancellation()
    except skill_canceller.SkillAlreadyCancelledError:
      _abort_with_status(
          context=context,
          code=status.StatusCode.FAILED_PRECONDITION,
          message=(
              'The operation has already been cancelled:'
              f' {cancel_request.name!r}.'
          ),
          skill_error_info=error_pb2.SkillErrorInfo(
              error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_GRPC
          ),
      )
    except NotImplementedError as err:
      _abort_with_status(
          context=context,
          code=status.StatusCode.UNIMPLEMENTED,
          message=str(err),
          skill_error_info=error_pb2.SkillErrorInfo(
              error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_SKILL
          ),
      )
    # Catch any additional errors here, since cancelling the skill may entail
    # calling a user-provided cancellation callback.
    except Exception:  # pylint: disable=broad-except
      logging.exception('Skill cancellation raised an error.')
      _abort_with_status(
          context=context,
          code=status.StatusCode.INTERNAL,
          message=(
              'Failure during skill cancellation. Error:'
              f' {traceback.format_exc()}'
          ),
          skill_error_info=error_pb2.SkillErrorInfo(
              error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_SKILL
          ),
      )

    return empty_pb2.Empty()

  def WaitOperation(
      self,
      wait_request: operations_pb2.WaitOperationRequest,
      context: grpc.ServicerContext,
  ) -> operations_pb2.Operation:
    """Waits for a skill execution operation to finish.

    Args:
      wait_request: Wait request with the skill operation name.
      context: gRPC servicer context.

    Returns:
      Operation representing the skill execution operation.

    Raises:
      grpc.RpcError:
        DEADLINE_EXCEEDED: If the operation does not finish within the specified
            timeout.
        NOT_FOUND: If the operation is not found.
    """
    try:
      operation = self._operations.get(wait_request.name)
    except self._operations.OperationNotFoundError as err:
      _abort_with_status(
          context=context,
          code=status.StatusCode.NOT_FOUND,
          message=str(err),
          skill_error_info=error_pb2.SkillErrorInfo(
              error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_GRPC
          ),
      )

    try:
      return operation.wait(
          wait_request.timeout.ToNanoseconds() / 1e9
          if wait_request.HasField('timeout')
          else None
      )
    except TimeoutError as err:
      _abort_with_extended_status(
          context,
          status_exception.ExtendedStatusError(
              error_bindings.SKILL_SERVICE_COMPONENT,
              error_bindings.SKILL_SERVICE_WAIT_TIMEOUT_CODE,
              user_message=str(err),
              title='Waiting for skill to finish timed out',
              grpc_code=grpc.StatusCode.DEADLINE_EXCEEDED,
          ).add_rpc_detail(
              error_pb2.SkillErrorInfo(
                  error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_SKILL
              )
          ),
      )

  def ClearOperations(
      self, clear_request: empty_pb2.Empty, context: grpc.ServicerContext
  ) -> empty_pb2.Empty:
    """Clears the internal store of skill execution operations.

    Args:
      clear_request: Empty.
      context: gRPC servicer context.

    Returns:
      Empty.

    Raises:
      grpc.RpcError:
        FAILED_PRECONDITION: If any stored operation is not yet finished.
    """
    try:
      self._operations.clear()
    except self._operations.OperationNotFinishedError as err:
      _abort_with_status(
          context=context,
          code=status.StatusCode.FAILED_PRECONDITION,
          message=str(err),
          skill_error_info=error_pb2.SkillErrorInfo(
              error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_GRPC
          ),
      )

    return empty_pb2.Empty()

  def _make_operation(
      self, name: str, skill_name: str, context: grpc.ServicerContext
  ) -> _SkillOperation:
    """Makes a new skill operation and adds it to the collection."""
    try:
      runtime_data = self._skill_repository.get_skill_runtime_data(skill_name)
    except skill_repo.InvalidSkillAliasError:
      _abort_with_status(
          context=context,
          code=status.StatusCode.NOT_FOUND,
          message=f'Skill not found: {skill_name!r}.',
          skill_error_info=error_pb2.SkillErrorInfo(
              error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_GRPC
          ),
      )

    operation = _SkillOperation(name=name, runtime_data=runtime_data)

    try:
      self._operations.add(operation)
    except self._operations.OperationError as err:
      if isinstance(err, self._operations.OperationAlreadyExistsError):
        code = status.StatusCode.ALREADY_EXISTS
      elif isinstance(err, self._operations.OperationCacheFullError):
        code = status.StatusCode.FAILED_PRECONDITION
      else:
        code = status.StatusCode.INTERNAL

      _abort_with_status(
          context,
          code=code,
          message=str(err),
          skill_error_info=error_pb2.SkillErrorInfo(
              error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_GRPC
          ),
      )

    return operation


class SkillInformationServicer(skill_service_pb2_grpc.SkillInformationServicer):
  """Implementation of the skill Information service."""

  def __init__(self, skill: skills_pb2.Skill):
    self._skill = skill

  def GetSkillInfo(
      self, request: empty_pb2.Empty, context: grpc.ServicerContext
  ) -> skill_service_pb2.SkillInformationResult:
    """Runs Skill information retrieval operation.

    Args:
      request: Request (currently empty).
      context: grpc server context.

    Returns:
      SkillInformationResult containing skill information.
    """
    result = skill_service_pb2.SkillInformationResult()
    result.skill.CopyFrom(self._skill)
    return result


class _SkillOperations:
  """A collection of skill operations."""

  class OperationError(Exception):
    """Base _SkillOperations error."""

  class OperationAlreadyExistsError(OperationError, ValueError):
    """An operation already exists in the collection."""

  class OperationNotFinishedError(OperationError, RuntimeError):
    """A disallowed action was performed on an unfinished operation."""

  class OperationNotFoundError(OperationError, ValueError):
    """A requested operation was not found."""

  class OperationCacheFullError(OperationError, RuntimeError):
    """The skill operation cache is full of unfinished operations."""

  def __init__(self):
    self._lock = threading.Lock()
    self._operations: dict[str, _SkillOperation] = {}

  def add(self, operation: _SkillOperation) -> None:
    """Adds an operation to the collection.

    Args:
      operation: The operation to add.

    Raises:
      OperationAlreadyExistsError: If an operation with the same name already
        exists in the collection.
      OperationCacheFullError: If the cache is already full of unfinished
        operations.
    """
    with self._lock:
      # First remove the oldest finished operation if we've reached our limit of
      # tracked operations.
      while len(self._operations) >= MAX_NUM_OPERATIONS:
        old_operation_name = None
        for name, old_operation in self._operations.items():
          if old_operation.finished:
            assert name is not None, 'Operation name was unexpectedly None.'
            old_operation_name = name
            break

        if old_operation_name is None:
          raise self.OperationCacheFullError(
              f'Cannot add operation {operation.name!r}, since there are'
              f' already {len(self._operations)} unfinished operations.'
          )

        del self._operations[old_operation_name]

      if operation.name in self._operations:
        raise self.OperationAlreadyExistsError(
            f'An operation already exists with name {operation.name!r}.'
        )

      self._operations[operation.name] = operation

  def get(self, name: str) -> _SkillOperation:
    """Gets an operation by name.

    Args:
      name: The operation name.

    Returns:
      The operation.

    Raises:
      OperationNotFoundError: If no operation with the specified name exists.
    """
    with self._lock:
      try:
        return self._operations[name]
      except KeyError as err:
        raise self.OperationNotFoundError(
            f'No operation found with name {name!r}.'
        ) from err

  def clear(self) -> None:
    """Clears all operations in the collection.

    NOTE: The operations must all be finished before clearing them. If any
    operation is not yet finished, no operations will be cleared, and an error
    will be raised.

    Raises:
      OperationNotFinishedError: If any operation is not yet finished.
    """
    with self._lock:
      unfinished_operation_names = []
      for operation in self._operations.values():
        if not operation.finished:
          unfinished_operation_names.append(operation.name)

      if unfinished_operation_names:
        names_list = ', '.join(unfinished_operation_names)
        raise self.OperationNotFinishedError(
            f'The following operations are not yet finished: {names_list}.'
        )

      self._operations = {}


class _SkillOperation:
  """Encapsulates a single skill operation.

  Attributes:
    canceller: Supports cooperative cancellation of the operation.
    finished: True if the operation has finished.
    name: A unique name for the operation.
    operation: The current operation proto for the operation.
    runtime_data: The skill's runtime data.
  """

  class OperationAlreadyStartedError(RuntimeError):
    """A disallowed action was taken on an already-started operation."""

  @property
  def canceller(self) -> skill_canceller.SkillCancellationManager:
    return self._canceller

  @property
  def finished(self) -> bool:
    return self._finished_event.is_set()

  @property
  def name(self) -> str:
    return self.operation.name

  @property
  def operation(self) -> operations_pb2.Operation:
    with self._lock:
      return self._operation

  @property
  def runtime_data(self) -> rd.SkillRuntimeData:
    return self._runtime_data

  def __init__(self, name: str, runtime_data: rd.SkillRuntimeData) -> None:
    """Initializes the instance.

    Args:
      name: A unique name for the operation.
      runtime_data: The skill's runtime data.
    """
    self._canceller = skill_canceller.SkillCancellationManager(
        ready_timeout=(
            runtime_data.execution_options.cancellation_ready_timeout.total_seconds()
        )
    )
    self._operation = operations_pb2.Operation(name=name, done=False)
    self._runtime_data = runtime_data

    self._started = False
    self._cancelled = False
    self._finished_event = threading.Event()
    self._lock = threading.RLock()

    self._pool = futures.ThreadPoolExecutor()

  def start(
      self,
      op: Callable[[], proto_message.Message],
      op_name: str,
      log_context: Optional[context_pb2.Context] = None,
  ) -> None:
    """Starts executing the skill operation.

    Args:
      op: The operation callable. It should return a proto result.
      op_name: A name to describe the operation.
      log_context: Data logger context to add to ExtendedStatus.

    Raises:
      OperationAlreadyStartedError: If an operation has already started.
    """
    with self._lock:
      if self._started:
        raise self.OperationAlreadyStartedError(
            f'Execution has already started: {self.name!r}.'
        )
      self._started = True

    self._pool.submit(self._execute, op, op_name, log_context=log_context)

  def request_cancellation(self) -> None:
    """Requests cancellation of the operation.

    Valid requests are ignored if the skill has already finished.

    Raises:
      Exception: Any exception raised when calling the cancellation callback.
      NotImplementedError: If the skill does not support cancellation.
      SkillAlreadyCancelledError: If the skill was already cancelled.
    """
    if not self.runtime_data.execution_options.supports_cancellation:
      raise NotImplementedError(
          f'Skill does not support cancellation: {self.name!r}.'
      )
    if self.canceller.cancelled:
      raise skill_canceller.SkillAlreadyCancelledError(
          f'The skill was already cancelled: {self.name!r}.'
      )
    if self.finished:
      logging.debug(
          (
              'Ignoring cancellation request, since operation %r has already'
              ' finished.'
          ),
          self.name,
      )
      return

    self.canceller.cancel()

  def wait(self, timeout: float | None) -> operations_pb2.Operation:
    """Waits for the operation to finish.

    Args:
      timeout: The maximum number of seconds to wait for the operation to finish
        before timing out, or None to use a default timeout

    Returns:
      The state of the Operation when it finished.

    Raises:
      TimeoutError: If the operation did not finish within the specified
        timeout.
    """
    if timeout is None:
      timeout = (
          self._runtime_data.execution_options.execution_timeout.total_seconds()
      )

    if not self._finished_event.wait(timeout=timeout):
      raise TimeoutError(
          f'Skill operation {self.name!r} did not finish within {timeout}s.'
      )

    return self.operation

  def _execute(
      self,
      op: Callable[[], proto_message.Message],
      op_name: str,
      log_context: Optional[context_pb2.Context],
  ) -> None:
    """Executes the skill operation.

    Args:
      op: The operation callable. It should return a proto result.
      op_name: A name to describe the operation.
      log_context: Data logger context to add to ExtendedStatus.
    """
    result = None
    error_status = None
    try:
      result = op()
    # Since we are calling user-provided code here, we want to be as broad as
    # possible and catch anything that could occur.
    except Exception as err:  # pylint: disable=broad-except
      error_status = _handle_skill_error(
          err=err,
          skill_id=self._runtime_data.skill_id,
          op_name=op_name,
          log_context=log_context,
          status_specs=self._runtime_data.status_specs,
      )

    if error_status is not None:
      error_status.details.add().Pack(
          error_pb2.SkillErrorInfo(
              error_type=error_pb2.SkillErrorInfo.ERROR_TYPE_SKILL
          )
      )
      self.operation.error.CopyFrom(error_status)
    if result is not None:
      self.operation.response.Pack(result)

    self.operation.done = True

    self._finished_event.set()


def _skill_error_to_code_and_action(
    err: Exception,
) -> tuple[status.StatusCode, str]:
  """Returns a status code and action description for a skill error."""
  # LINT.IfChange(skill_error_actions)
  if isinstance(err, skl.SkillCancelledError):
    return status.StatusCode.CANCELLED, 'was cancelled during'
  elif isinstance(err, skl.InvalidSkillParametersError):
    return (
        status.StatusCode.INVALID_ARGUMENT,
        'was passed invalid parameters during',
    )
  elif isinstance(err, NotImplementedError):
    return status.StatusCode.UNIMPLEMENTED, 'has not implemented'
  elif isinstance(err, TimeoutError):
    return status.StatusCode.DEADLINE_EXCEEDED, 'timed out during'

  return status.StatusCode.INTERNAL, 'raised an error during'
  # LINT.ThenChange(
  #     //intrinsic/skills/internal/error_utils.cc:skill_error_actions
  # )


def _handle_skill_error(
    err: Exception,
    skill_id: str,
    op_name: str,
    log_context: Optional[context_pb2.Context],
    status_specs: rd.StatusSpecs,
) -> status_pb2.Status:
  """Handles an error raised by a skill."""
  code, action = _skill_error_to_code_and_action(err)
  message = f'Skill {skill_id} {action} {op_name}:'
  logging.exception(message)

  details = []

  if isinstance(err, status_exception.ExtendedStatusError):
    es_proto = cast(status_exception.ExtendedStatusError, err).proto
  else:
    # Convert the error to an extended status
    es_proto = extended_status_pb2.ExtendedStatus(
        status_code=extended_status_pb2.StatusCode(
            component=skill_id, code=status.StatusCodeAsInt(code)
        ),
        title=f'Skill {action} {op_name}',
        user_report=extended_status_pb2.ExtendedStatus.UserReport(
            message=str(err)
        ),
    )

  # Always overwrite the component. We do not give a skill author freedom to
  # choose this field.
  if not es_proto.HasField('status_code') or not es_proto.status_code.component:
    es_proto.status_code.component = skill_id
  elif es_proto.status_code.component != skill_id:
    # this may be a case of a skill author simply returning an error
    # previously received from, e.g., a service. This should not be the
    # skill's status, but we also cannot just overwrite it. Wrap this into a
    # new extended status.
    new_es_proto = extended_status_pb2.ExtendedStatus(
        status_code=extended_status_pb2.StatusCode(component=skill_id)
    )
    new_es_proto.context.add().CopyFrom(es_proto)
    es_proto = new_es_proto

  if log_context is not None:
    # If the extended status has no log context set, yet, add it
    if not es_proto.HasField('related_to') or not es_proto.related_to.HasField(
        'log_context'
    ):
      es_proto.related_to.log_context.CopyFrom(log_context)

    # Set timestamp to now if none set
    if not es_proto.HasField('timestamp'):
      es_proto.timestamp.GetCurrentTime()

    if not es_proto.title:
      if (
          es_proto.HasField('status_code')
          and es_proto.status_code.code in status_specs.specs
      ):
        es_proto.title = status_specs.specs[es_proto.status_code.code].title
      elif es_proto.HasField('user_report') and es_proto.user_report.message:
        es_proto.title = textwrap.shorten(
            es_proto.user_report.message,
            EXTENDED_STATUS_TITLE_SHORTEN_TO_LENGTH,
            placeholder='...',
        )
      elif es_proto.HasField('status_code'):
        es_proto.title = (
            f'{es_proto.status_code.component}:{es_proto.status_code.code}'
        )

    if log_context is not None:
      # If the extended status has no log context set, yet, add it
      if not es_proto.HasField(
          'related_to'
      ) or not es_proto.related_to.HasField('log_context'):
        es_proto.related_to.log_context.CopyFrom(log_context)

    status_any = any_pb2.Any()
    status_any.Pack(es_proto)
    details.append(status_any)

  return status_pb2.Status(
      code=status.StatusCodeAsInt(code),
      message=f'{message}\n{err}',
      details=details,
  )


def _abort_with_extended_status(
    context: grpc.ServicerContext, es: status_exception.ExtendedStatusError
) -> NoReturn:
  """Raises the error to grpc.

  This is required to get the NoReturn annotation which is not present on the
  grpc API directly.

  Args:
    context: Servicer context to call abort on. See context.abort_with_status.
    es: Extended status to report with error via gRPC.
  """
  context.abort_with_status(es)

  # This will never be raised, but we need it to satisfy static type checking,
  # since context.abort_with_status does not properly annotate its return value
  # as NoReturn.
  raise AssertionError('This error should not have been raised.')


def _abort_with_status(
    context: grpc.ServicerContext,
    code: status.StatusCode,
    message: str,
    skill_error_info: error_pb2.SkillErrorInfo,
) -> NoReturn:
  """Calls context.abort_with_status.

  This function annotates its (lack of) return type properly so the static type
  checker doesn't think execution might continue after its call (and, e.g.,
  complain about variables possibly being uninitialized when they are used).

  Args:
    context: See context.abort_with_status.
    code: See context.abort_with_status.
    message: See context.abort_with_status.
    skill_error_info: See context.abort_with_status.
  """
  context.abort_with_status(
      error_utils.make_grpc_status_with_error_info(
          code=code, message=message, skill_error_info=skill_error_info
      )
  )

  # This will never be raised, but we need it to satisfy static type checking,
  # since context.abort_with_status does not properly annotate its return value
  # as NoReturn.
  raise AssertionError('This error should not have been raised.')


def _proto_to_get_footprint_request(
    proto: skill_service_pb2.GetFootprintRequest,
    skill_runtime_data: rd.SkillRuntimeData,
) -> skl.GetFootprintRequest:
  """Converts a GetFootprintRequest proto to the request to send to the skill.

  Args:
    proto: The proto to convert.
    skill_runtime_data: The runtime data for the skill.

  Returns:
    The request to send to the skill.

  Raises:
    _CannotConstructRequestError: If the request cannot be converted.
  """
  try:
    params = _unpack_any_from_descriptor(
        proto.parameters, skill_runtime_data.parameter_data.descriptor
    )
  except proto_utils.ProtoMismatchTypeError as err:
    raise _CannotConstructRequestError(str(err)) from err

  return skl.GetFootprintRequest(
      params=params,
  )


def _resolve_params(
    params_any: any_pb2.Any, skill_runtime_data: rd.SkillRuntimeData
) -> proto_message.Message:
  """Applies defaults and resolves a params Any into its target message type."""
  defaults = skill_runtime_data.parameter_data.default_value
  if defaults is not None:
    try:
      default_parameters.apply_defaults_to_parameters(
          msg_descriptor=skill_runtime_data.parameter_data.descriptor,
          default_value_any=defaults,
          parameters_any=params_any,
      )
    except status.StatusNotOk as err:
      raise _CannotConstructRequestError(str(err)) from err

  try:
    return _unpack_any_from_descriptor(
        params_any, skill_runtime_data.parameter_data.descriptor
    )
  except proto_utils.ProtoMismatchTypeError as err:
    raise _CannotConstructRequestError(str(err)) from err


def _unpack_any_from_descriptor(
    any_message: any_pb2.Any, descriptor: proto_descriptor.Descriptor
) -> proto_message.Message:
  """Unpacks a proto Any into a message, given the message's Descriptor.

  Args:
    any_message: a proto Any message.
    descriptor: The descriptor of the target message type.

  Returns:
    The unpacked proto message.

  Raises:
    proto_utils.ProtoMismatchTypeError: If the type of `proto_message` does not
      match that of the specified Any proto.
  """
  # Cache the generated class to save time and provide a consistent type to the
  # skill.
  try:
    proto_message_type = _message_type_cache[descriptor.full_name]
  except KeyError:
    proto_message_type = _message_type_cache[descriptor.full_name] = (
        message_factory.GetMessageClass(descriptor)
    )

  return proto_utils.unpack_any(any_message, proto_message_type())


# Cache used by _unpack_any_from_descriptor.
_message_type_cache = {}
