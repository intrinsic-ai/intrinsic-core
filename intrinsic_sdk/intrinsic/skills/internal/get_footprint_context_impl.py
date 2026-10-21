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

"""GetFootprintContext implementation provided by the skill service."""

# isort: off

from intrinsic.geometry.proto import geometry_service_pb2_grpc

# isort: on
from intrinsic.motion_planning import motion_planner_client
from intrinsic.resources.proto import resource_handle_pb2
from intrinsic.skills.python import get_footprint_context
from intrinsic.world.python import object_world_client
from intrinsic.world.python import object_world_ids
from intrinsic.world.python import object_world_resources


class GetFootprintContextImpl(get_footprint_context.GetFootprintContext):
  """GetFootprintContext implementation provided by the skill service.

  It is provided by the skill service to a skill and allows access to the world
  and other services that a skill may use.

  Attributes:
    context_id: A unique identifier shared across all interactions with the
      Skill for a single activation of a Skill node in a Process (including any
      preparation, planning, or execution calls).
    motion_planner: A client for the motion planning service.
    object_world: A client for interacting with the object world.
  """


  @property
  def geometry_service(self) -> geometry_service_pb2_grpc.GeometryServiceStub:  # pylint: disable=g-missing-from-attributes
    return self._geometry_service



  @property
  def context_id(self) -> str:
    return self._context_id

  @property
  def motion_planner(self) -> motion_planner_client.MotionPlannerClient:
    return self._motion_planner

  @property
  def object_world(self) -> object_world_client.ObjectWorldClient:
    return self._object_world

  def __init__(
      self,

      geometry_service: geometry_service_pb2_grpc.GeometryServiceStub,

      motion_planner: motion_planner_client.MotionPlannerClient,
      object_world: object_world_client.ObjectWorldClient,
      resource_handles: dict[str, resource_handle_pb2.ResourceHandle],
      context_id: str,
  ):
    self._geometry_service = geometry_service  
    self._motion_planner = motion_planner
    self._object_world = object_world
    self._resource_handles = resource_handles
    self._context_id = context_id

  def get_frame_for_equipment(
      self, equipment_name: str, frame_name: object_world_ids.FrameName
  ) -> object_world_resources.Frame:
    return self.object_world.get_frame(
        frame_name, self._resource_handles[equipment_name]
    )

  def get_kinematic_object_for_equipment(
      self, equipment_name: str
  ) -> object_world_resources.KinematicObject:
    return self.object_world.get_kinematic_object(
        self._resource_handles[equipment_name]
    )

  def get_object_for_equipment(
      self, equipment_name: str
  ) -> object_world_resources.WorldObject:
    return self.object_world.get_object(self._resource_handles[equipment_name])
