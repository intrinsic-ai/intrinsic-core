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

"""skill_init provides initialization functions for setting up skill service."""

from concurrent import futures
import time

from absl import logging
import grpc

from intrinsic.geometry.proto import geometry_service_pb2_grpc
from intrinsic.motion_planning.proto.v1 import motion_planner_service_pb2_grpc
from intrinsic.skills.internal import skill_repository as skill_repo
from intrinsic.skills.internal import skill_service_impl
from intrinsic.skills.proto import skill_service_config_pb2
from intrinsic.skills.proto import skill_service_pb2_grpc
from intrinsic.world.proto import object_world_service_pb2_grpc


def _create_object_world_service_stub(
    address: str, connection_timeout: int
) -> object_world_service_pb2_grpc.ObjectWorldServiceStub:
  channel = grpc.insecure_channel(address)
  # Blocks for the duration of the timeout until the channel is ready.
  grpc.channel_ready_future(channel).result(connection_timeout)
  return object_world_service_pb2_grpc.ObjectWorldServiceStub(channel)


def _create_motion_planner_service_stub(
    address: str, connection_timeout: int
) -> motion_planner_service_pb2_grpc.MotionPlannerServiceStub:
  channel = grpc.insecure_channel(address)
  # Blocks for the duration of the timeout until the channel is ready.
  grpc.channel_ready_future(channel).result(timeout=connection_timeout)
  return motion_planner_service_pb2_grpc.MotionPlannerServiceStub(channel)


def _create_geometry_service_stub(
    address: str, connection_timeout: int
) -> geometry_service_pb2_grpc.GeometryServiceStub:
  options = [
      ("grpc.max_receive_message_length", -1),
      ("grpc.max_send_message_length", -1),
  ]
  channel = grpc.insecure_channel(address, options=options)
  # Blocks for the duration of the timeout until the channel is ready.
  grpc.channel_ready_future(channel).result(timeout=connection_timeout)
  return geometry_service_pb2_grpc.GeometryServiceStub(channel)


def skill_init(
    skill_repository: skill_repo.SkillRepository,
    skill_service_config: skill_service_config_pb2.SkillServiceConfig,
    num_threads: int,
    skill_service_port: int,
    world_service_address: str,
    motion_planner_service_address: str,
    geometry_service_address: str,
    connection_timeout: int,
):
  """Starts the skill services on a gRPC server at port `skill_service_port`.

  This server hosts services required for serving a skill including:
    * SkillProjectorServicer
    * SkillExecutorServicer
    * SkillInformationServicer

  Establishes connections to common clients of skills.
  The `connection_timeout` applies to the establishment of each connection, not
  the cumulative connection time.

  The skills services are configured using the proto data contained in the
  service_config.

  If setup passes, this method does not return until the gRPC skill server is
  shutdown. This normally occurs when the process is killed.

  Args:
    skill_repository: The skill repository used to create the skill instance
    skill_service_config: Configuration file for this skill service
    num_threads: The number of thread that the gRPC skill service should use
    skill_service_port: The port that the gRPC service should use
    world_service_address: The address of the world service
    motion_planner_service_address: The address of the motion planner service
    geometry_service_address: The address of the geometry service
    connection_timeout: The connection timeout

  Raises:
    RuntimeError: if skill service fails to use skill_service_port
  """
  server = grpc.server(
      futures.ThreadPoolExecutor(max_workers=num_threads),
      options=(("grpc.so_reuseport", 0),),
  )  # pytype: disable=wrong-keyword-args

  object_world_service = _create_object_world_service_stub(
      world_service_address, connection_timeout
  )
  motion_planner_service = _create_motion_planner_service_stub(
      motion_planner_service_address, connection_timeout
  )
  geometry_service = _create_geometry_service_stub(
      geometry_service_address, connection_timeout
  )

  # Initialize the projector service.
  projector_servicer = skill_service_impl.SkillProjectorServicer(
      skill_repository=skill_repository,
      object_world_service=object_world_service,
      motion_planner_service=motion_planner_service,
      geometry_service=geometry_service,
  )
  skill_service_pb2_grpc.add_ProjectorServicer_to_server(
      projector_servicer, server
  )

  # Initialize the executor service.
  executor_servicer = skill_service_impl.SkillExecutorServicer(
      skill_repository=skill_repository,
      object_world_service=object_world_service,
      motion_planner_service=motion_planner_service,
      geometry_service=geometry_service,
  )
  skill_service_pb2_grpc.add_ExecutorServicer_to_server(
      executor_servicer, server
  )

  # Initialize the skill information service if --skill_service_config_filename
  # given (which means we're running a modular skill server).
  if skill_service_config.HasField("skill_description"):
    if (
        skill_service_config.skill_description.skill_name
        not in skill_repository.get_skill_aliases()
    ):
      raise ValueError(
          "Could not find skill {} in skill modules.".format(
              skill_service_config.skill_description.skill_name
          )
      )
    logging.info(
        "Adding skill information server with modular skill %s",
        skill_service_config.skill_description.skill_name,
    )

    skill_info_servicer = skill_service_impl.SkillInformationServicer(
        skill_service_config.skill_description
    )
    skill_service_pb2_grpc.add_SkillInformationServicer_to_server(
        skill_info_servicer, server
    )

  # Initialize server with insecure port.
  endpoint = "[::]:{}".format(skill_service_port)
  added_port = server.add_insecure_port(endpoint)
  if added_port != skill_service_port:
    raise RuntimeError(f"Failed to use port {skill_service_port}")
  server.start()

  logging.info("""==========================================================
      """)
  logging.info("--------------------------------")
  logging.info("-- Skill service listening on %s", endpoint)
  logging.info("--------------------------------")

  # Keep server running until an interrupt signal is received.
  try:
    while True:
      # Sleep for a day.
      time.sleep(60 * 60 * 24)
  except KeyboardInterrupt:
    pass
  finally:
    server.stop(None)
