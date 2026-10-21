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

"""ExecuteContext implementation provided by the skill service."""

from collections.abc import Mapping

# isort: off

from intrinsic.geometry.proto import geometry_service_pb2_grpc

# isort: on
from intrinsic.motion_planning import motion_planner_client
from intrinsic.resources.proto import resource_handle_pb2
from intrinsic.skills.python import execute_context
from intrinsic.skills.python import skill_canceller
from intrinsic.skills.python import skill_logging_context
from intrinsic.world.python import object_world_client


class ExecuteContextImpl(execute_context.ExecuteContext):
  """ExecuteContext implementation provided by the skill service.

  Attributes:
    canceller: Supports cooperative cancellation of the skill.
    context_id: A unique identifier shared across all interactions with the
      Skill for a single activation of a Skill node in a Process (including any
      preparation, planning, or execution calls).
    logging_context: The logging context of the execution.
    motion_planner: A client for the motion planning service.
    object_world: A client for interacting with the object world.
    resource_handles: A map of resource names to handles.
  """

  @property
  def canceller(self) -> skill_canceller.SkillCanceller:
    return self._canceller

  @property
  def context_id(self) -> str:
    return self._context_id


  @property
  def geometry_service(self) -> geometry_service_pb2_grpc.GeometryServiceStub:  # pylint: disable=g-missing-from-attributes
    return self._geometry_service



  @property
  def logging_context(self) -> skill_logging_context.SkillLoggingContext:
    return self._logging_context

  @property
  def motion_planner(self) -> motion_planner_client.MotionPlannerClient:
    return self._motion_planner

  @property
  def object_world(self) -> object_world_client.ObjectWorldClient:
    return self._object_world

  @property
  def resource_handles(
      self,
  ) -> Mapping[str, resource_handle_pb2.ResourceHandle]:
    return self._resource_handles

  def __init__(
      self,
      canceller: skill_canceller.SkillCanceller,

      geometry_service: geometry_service_pb2_grpc.GeometryServiceStub,

      logging_context: skill_logging_context.SkillLoggingContext,
      motion_planner: motion_planner_client.MotionPlannerClient,
      object_world: object_world_client.ObjectWorldClient,
      resource_handles: dict[str, resource_handle_pb2.ResourceHandle],
      context_id: str,
  ):
    self._canceller = canceller
    self._geometry_service = geometry_service  
    self._logging_context = logging_context
    self._motion_planner = motion_planner
    self._object_world = object_world
    self._resource_handles = resource_handles
    self._context_id = context_id
