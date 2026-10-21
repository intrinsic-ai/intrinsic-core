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

"""PreviewContext implementation provided by the skill service."""

import datetime

from google.protobuf import duration_pb2
from google.protobuf import timestamp_pb2

# isort: off

from intrinsic.geometry.proto import geometry_service_pb2_grpc

# isort: on
from intrinsic.motion_planning import motion_planner_client
from intrinsic.resources.proto import resource_handle_pb2
from intrinsic.skills.proto import prediction_pb2
from intrinsic.skills.python import preview_context
from intrinsic.skills.python import skill_canceller
from intrinsic.skills.python import skill_logging_context
from intrinsic.world.proto import object_world_updates_pb2
from intrinsic.world.python import object_world_client
from intrinsic.world.python import object_world_ids
from intrinsic.world.python import object_world_resources


class PreviewContextImpl(preview_context.PreviewContext):
  """PreviewContext implementation provided by the skill service.

  Attributes:
    canceller: Supports cooperative cancellation of the skill.
    context_id: A unique identifier shared across all interactions with the
      Skill for a single activation of a Skill node in a Process (including any
      preparation, planning, or execution calls).
    logging_context: The logging context of the execution.
    motion_planner: A client for the motion planning service.
    object_world: A client for interacting with the object world.
      NOTE: Any updates to this world will be ignored by the skill service. Use
        `record_world_update` to record any effects that executing the skill is
        expected to have on the world.
    world_updates: A list of updates that have been recorded by
      `record_world_update`.
      NOTE: NOT part of the `PreviewContext` interface.
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
  def world_updates(self) -> list[prediction_pb2.TimedWorldUpdate]:
    return self._world_updates

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

    self._world_updates: list[prediction_pb2.TimedWorldUpdate] = []

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

  def record_world_update(
      self,
      update: object_world_updates_pb2.ObjectWorldUpdate,
      elapsed: float,
      duration: float,
  ) -> None:
    if elapsed < 0:
      raise ValueError("`elapsed` must be non-negative.")
    if duration < 0:
      raise ValueError("`duration` must be non-negative.")

    base_time = (
        self._world_updates[-1].start_time
        if self._world_updates
        else timestamp_pb2.Timestamp()
    )

    start_time = timestamp_pb2.Timestamp()
    start_time.FromDatetime(
        base_time.ToDatetime() + datetime.timedelta(seconds=elapsed)
    )
    time_until_update = duration_pb2.Duration()
    time_until_update.FromTimedelta(datetime.timedelta(seconds=duration))

    timed_update = prediction_pb2.TimedWorldUpdate(
        start_time=start_time,
        time_until_update=time_until_update,
        world_updates=object_world_updates_pb2.ObjectWorldUpdates(
            updates=[update]
        ),
    )
    self._world_updates.append(timed_update)
