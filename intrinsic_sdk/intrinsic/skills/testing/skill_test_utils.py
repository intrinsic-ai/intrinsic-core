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

"""Provides utils for skill testing."""

from __future__ import annotations

import concurrent.futures
from typing import cast
from typing import Optional
from unittest import mock
import uuid

from google.protobuf import empty_pb2
import grpc

from intrinsic.assets import id_utils

# isort: off

from intrinsic.geometry.proto import geometry_service_pb2_grpc

# isort: on
from intrinsic.icon.release import file_helpers
from intrinsic.logging.proto import context_pb2
from intrinsic.motion_planning import motion_planner_client
from intrinsic.motion_planning.proto.v1 import motion_planner_service_pb2_grpc
from intrinsic.resources.proto import resource_handle_pb2
from intrinsic.skills.internal import execute_context_impl
from intrinsic.skills.internal import get_footprint_context_impl
from intrinsic.skills.internal import preview_context_impl
from intrinsic.skills.proto import skill_manifest_pb2
from intrinsic.skills.python import execute_request
from intrinsic.skills.python import get_footprint_request
from intrinsic.skills.python import preview_request
from intrinsic.skills.python import skill_canceller
from intrinsic.skills.python import skill_interface
from intrinsic.skills.python import skill_logging_context
from intrinsic.world.proto import object_world_service_pb2_grpc
from intrinsic.world.python import object_world_client

_TEST_WORLD_ID = "world"


def _make_local_server() -> tuple[grpc.Server, str]:
  server = grpc.server(concurrent.futures.ThreadPoolExecutor())
  port = server.add_insecure_port("localhost:0")
  address = f"localhost:{port}"
  return server, address


def make_grpc_server_with_channel() -> tuple[grpc.Server, grpc.Channel]:
  """Makes a gRPC server and channel suitable for use by unit tests.

  Use this to create fakes of services built into a context object, like the
  world service.

  To use the server, the caller must:

    1. Add a servicer to the returned service
    2. Call server.start()

  Returns:
    A gRPC server
    A channel that can be used to communicate with the server
  """
  server, address = _make_local_server()
  channel = grpc.insecure_channel(address)
  return server, channel


def make_grpc_server_with_resource_handle(
    resource_name: str,
) -> tuple[grpc.Server, resource_handle_pb2.ResourceHandle]:
  """Makes a gRPC server and resource handle suitable for use by unit tests.

  Use this function to create connections with services passed in via the
  EquipmentPack on a context object.

  To use the server, the caller must:

    1. Add a servicer to the returned service
    2. Call server.start()

  Args:
    resource_name: the name of a resource as used in the dependencies section of
      a skill manifest.

  Returns:
    A gRPC server
    A resource handle that can be put into a context object to be used by a
      skill in a unit test.
  """
  server, address = _make_local_server()
  handle = resource_handle_pb2.ResourceHandle(
      connection_info=resource_handle_pb2.ResourceConnectionInfo(
          grpc=resource_handle_pb2.ResourceGrpcConnectionInfo(address=address),
      ),
      name=resource_name,
  )
  return server, handle


def make_test_execute_request(
    params: Optional[execute_request.TParamsType] = None,
) -> skill_interface.ExecuteRequest[execute_request.TParamsType]:
  """Makes an ExecuteRequest for testing.

  All arguments are optional; testing defaults are used for any omitted
  argument.

  Returns:
    The testing ExecuteRequest.
  """
  if params is None:
    params = empty_pb2.Empty()

  return skill_interface.ExecuteRequest(
      params=params,
  )


def make_test_execute_context(
    canceller: Optional[skill_canceller.SkillCanceller] = None,

    geometry_service: Optional[
        geometry_service_pb2_grpc.GeometryServiceStub
    ] = None,

    logging_context: Optional[skill_logging_context.SkillLoggingContext] = None,
    motion_planner: Optional[motion_planner_client.MotionPlannerClient] = None,
    object_world: Optional[object_world_client.ObjectWorldClient] = None,
    resource_handles: Optional[
        dict[str, resource_handle_pb2.ResourceHandle]
    ] = None,
    context_id: Optional[str] = None,
) -> skill_interface.ExecuteContext:
  """Makes an ExecuteContext for testing.

  All arguments are optional; testing defaults are used for any omitted
  argument.



  Args:
    canceller: The SkillCanceller to use, or None for a default canceller.
    geometry_service: Stub to the geometry service, or None for a mock stub.
    logging_context: The skill logging context of the execution, or None for a
      mock context.
    motion_planner: The motion planner client to provide, or None for a mock
      client.
    object_world: The object world client to provide, or None for a mock client.
    resource_handles: A map of resource names to handles, or None for an empty
      map.
    context_id: Context ID string, or None for a random UUID.



  Returns:
    The testing ExecuteContext.
  """
  if context_id is None:
    context_id = str(uuid.uuid4())
  if canceller is None:
    canceller = skill_canceller.SkillCancellationManager(ready_timeout=30.0)

  if geometry_service is None:
    geometry_service = cast(
        geometry_service_pb2_grpc.GeometryServiceStub, mock.MagicMock()
    )

  if logging_context is None:
    logging_context = skill_logging_context.SkillLoggingContext(
        data_logger_context=context_pb2.Context(),
        skill_id="mock.package.mock_execute_name",
    )

  if motion_planner is None:
    motion_planner = motion_planner_client.MotionPlannerClient(
        world_id=_TEST_WORLD_ID,
        stub=cast(
            motion_planner_service_pb2_grpc.MotionPlannerServiceStub,
            mock.MagicMock(),
        ),
    )
  if object_world is None:
    object_world = object_world_client.ObjectWorldClient(
        world_id=_TEST_WORLD_ID,
        stub=cast(
            object_world_service_pb2_grpc.ObjectWorldServiceStub,
            mock.MagicMock(),
        ),

        geometry_service_stub=geometry_service,

    )
  if resource_handles is None:
    resource_handles = {}

  return execute_context_impl.ExecuteContextImpl(
      canceller=canceller,
      geometry_service=geometry_service,  
      logging_context=logging_context,
      motion_planner=motion_planner,
      object_world=object_world,
      resource_handles=resource_handles,
      context_id=context_id,
  )


def make_test_preview_request(
    params: Optional[preview_request.TParamsType] = None,
) -> skill_interface.PreviewRequest[preview_request.TParamsType]:
  """Makes a PreviewRequest for testing.

  All arguments are optional; testing defaults are used for any omitted
  argument.



  Args:
    internal_data: Internal data bytes, or None for empty bytes.
    params: The skill parameters proto, or None for Empty().



  Returns:
    The testing PredictRequest.
  """
  if params is None:
    params = empty_pb2.Empty()

  return skill_interface.PreviewRequest(
      params=params,
  )


def make_test_preview_context(
    canceller: Optional[skill_canceller.SkillCanceller] = None,

    geometry_service: Optional[
        geometry_service_pb2_grpc.GeometryServiceStub
    ] = None,

    logging_context: Optional[skill_logging_context.SkillLoggingContext] = None,
    motion_planner: Optional[motion_planner_client.MotionPlannerClient] = None,
    object_world: Optional[object_world_client.ObjectWorldClient] = None,
    resource_handles: Optional[
        dict[str, resource_handle_pb2.ResourceHandle]
    ] = None,
    context_id: Optional[str] = None,
) -> skill_interface.PreviewContext:
  """Makes a PreviewContext for testing.

  All arguments are optional; testing defaults are used for any omitted
  argument.



  Args:
    canceller: The SkillCanceller to use, or None for a default canceller.
    geometry_service: Stub to the geometry service, or None for a mock stub.
    logging_context: The logging context of the execution, or None for an empty
      context.
    motion_planner: The motion planner client to provide, or None for a mock
      client.
    object_world: The object world client to provide, or None for a mock client.
    resource_handles: Handles for the required resources for this skill, or None
      for an empty map.
    context_id: Context ID string, or None for a random UUID.



  Returns:
    The testing PreviewContext.
  """
  if context_id is None:
    context_id = str(uuid.uuid4())
  if canceller is None:
    canceller = skill_canceller.SkillCancellationManager(ready_timeout=30.0)

  if geometry_service is None:
    geometry_service = cast(
        geometry_service_pb2_grpc.GeometryServiceStub, mock.MagicMock()
    )

  if logging_context is None:
    logging_context = skill_logging_context.SkillLoggingContext(
        data_logger_context=context_pb2.Context(),
        skill_id="mock.package.mock_preview_name",
    )
  if motion_planner is None:
    motion_planner = motion_planner_client.MotionPlannerClient(
        world_id=_TEST_WORLD_ID,
        stub=cast(
            motion_planner_service_pb2_grpc.MotionPlannerServiceStub,
            mock.MagicMock(),
        ),
    )
  if object_world is None:
    object_world = object_world_client.ObjectWorldClient(
        world_id=_TEST_WORLD_ID,
        stub=cast(
            object_world_service_pb2_grpc.ObjectWorldServiceStub,
            mock.MagicMock(),
        ),

        geometry_service_stub=geometry_service,

    )
  if resource_handles is None:
    resource_handles = {}

  return preview_context_impl.PreviewContextImpl(
      canceller=canceller,
      geometry_service=geometry_service,  
      logging_context=logging_context,
      motion_planner=motion_planner,
      object_world=object_world,
      resource_handles=resource_handles,
      context_id=context_id,
  )


def make_test_get_footprint_request(
    params: Optional[get_footprint_request.TParamsType] = None,
) -> skill_interface.GetFootprintRequest[get_footprint_request.TParamsType]:
  """Makes a GetFootprintRequest for testing.

  All arguments are optional; testing defaults are used for any omitted
  argument.

  Returns:
    The testing GetFootprintRequest.
  """
  if params is None:
    params = empty_pb2.Empty()

  return skill_interface.GetFootprintRequest(
      params=params,
  )


def make_test_get_footprint_context(

    geometry_service: Optional[
        geometry_service_pb2_grpc.GeometryServiceStub
    ] = None,

    motion_planner: Optional[motion_planner_client.MotionPlannerClient] = None,
    object_world: Optional[object_world_client.ObjectWorldClient] = None,
    resource_handles: Optional[
        dict[str, resource_handle_pb2.ResourceHandle]
    ] = None,
    context_id: Optional[str] = None,
) -> skill_interface.GetFootprintContext:
  """Makes a GetFootprintContext for testing.

  All arguments are optional; testing defaults are used for any omitted
  argument.



  Args:
    geometry_service: Stub to the geometry service, or None for a mock stub.
    motion_planner: The motion planner client to provide, or None for a mock
      client.
    object_world: The object world client to provide, or None for a mock client.
    resource_handles: Handles for the required resources for this skill, or None
      for an empty map.
    context_id: Context ID string, or None for a random UUID.



  Returns:
    The testing GetFootprintContext.
  """
  if context_id is None:
    context_id = str(uuid.uuid4())

  if geometry_service is None:
    geometry_service = cast(
        geometry_service_pb2_grpc.GeometryServiceStub, mock.MagicMock()
    )

  if motion_planner is None:
    motion_planner = motion_planner_client.MotionPlannerClient(
        world_id=_TEST_WORLD_ID,
        stub=cast(
            motion_planner_service_pb2_grpc.MotionPlannerServiceStub,
            mock.MagicMock(),
        ),
    )
  if object_world is None:
    object_world = object_world_client.ObjectWorldClient(
        world_id=_TEST_WORLD_ID,
        stub=cast(
            object_world_service_pb2_grpc.ObjectWorldServiceStub,
            mock.MagicMock(),
        ),

        geometry_service_stub=geometry_service,

    )
  if resource_handles is None:
    resource_handles = {}

  return get_footprint_context_impl.GetFootprintContextImpl(
      geometry_service=geometry_service,  
      motion_planner=motion_planner,
      object_world=object_world,
      resource_handles=resource_handles,
      context_id=context_id,
  )


def get_skill_manifest(path: str) -> skill_manifest_pb2.SkillManifest:
  """Loads the skill manifest.

  The skill manifest proto is loaded from the resources (build dependencies and
  data files).

  Args:
    path: Real path on disk to the manifest generated by a skill_manifest rule.

  Returns:
    The skill's manifest proto.
  """
  manifest = skill_manifest_pb2.SkillManifest()
  file_helpers.load_binary_proto(path, manifest)
  return manifest


def make_test_skill_logging_context_from_manifest(
    manifest: skill_manifest_pb2.SkillManifest,
    data_logger_context: Optional[context_pb2.Context] = None,
) -> skill_logging_context.SkillLoggingContext:
  """Makes a SkillLoggingContext from a skill manifest.

  Args:
    manifest: The skill manifest.
    data_logger_context: The data logger logging context, or None for an empty
      context.

  Returns:
    The testing SkillLoggingContext.
  """
  if data_logger_context is None:
    data_logger_context = context_pb2.Context()

  return skill_logging_context.SkillLoggingContext(
      data_logger_context=data_logger_context,
      skill_id=id_utils.id_from(manifest.id.package, manifest.id.name),
  )
