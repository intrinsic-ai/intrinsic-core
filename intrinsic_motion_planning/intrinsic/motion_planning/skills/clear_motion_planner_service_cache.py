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

"""Clear the motion planner service cache."""

from absl import logging

from intrinsic.motion_planning import motion_planner_client
from intrinsic.motion_planning.skills import clear_motion_planner_service_cache_pb2
from intrinsic.skills.proto import footprint_pb2
from intrinsic.skills.python import skill_interface as skl
from intrinsic.skills.python import skill_interface_utils
from intrinsic.util.decorators import overrides

ClearMotionPlannerServiceCacheParams = (
    clear_motion_planner_service_cache_pb2.ClearMotionPlannerServiceCacheParams
)


class ClearMotionPlannerServiceCache(skl.Skill):
  """Clear the motion planner service cache."""

  @overrides(skl.Skill)
  def get_footprint(
      self,
      request: skl.GetFootprintRequest[ClearMotionPlannerServiceCacheParams],
      context: skl.GetFootprintContext,
  ) -> footprint_pb2.Footprint:
    """Should not run this skill in parallel with the move_robot skill."""
    footprint = footprint_pb2.Footprint(lock_the_universe=True)
    return footprint

  @overrides(skl.Skill)
  def execute(
      self,
      request: skl.ExecuteRequest[ClearMotionPlannerServiceCacheParams],
      context: skl.ExecuteContext,
  ) -> None:
    """Executes the clear_motion_planner_service_cache skill.

    Args:
      request: The execute request.
      context: Provides access to the motion planner client.
    """
    skill_params: ClearMotionPlannerServiceCacheParams = request.params
    world = context.object_world

    motion_planner = None
    if skill_params.HasField("motion_planner_service"):
      motion_planner = (
          motion_planner_client.get_motion_planner_service_asset_client(
              world.world_id, skill_params.motion_planner_service
          )
      )

    if motion_planner is not None:
      logging.info("Using Motion Planner Client from asset definition.")
    else:
      # TODO(b/524634328): Remove the Fallback Logic from the Skills.
      logging.info("Using Motion Planner Client from context.")
      motion_planner = context.motion_planner
    logging.info("Clearing all MPS caches.")
    motion_planner.clear_cache()

  @overrides(skl.Skill)
  def preview(
      self,
      request: skl.PreviewRequest[ClearMotionPlannerServiceCacheParams],
      context: skl.PreviewContext,
  ) -> None:
    return skill_interface_utils.preview_via_execute(self, request, context)
