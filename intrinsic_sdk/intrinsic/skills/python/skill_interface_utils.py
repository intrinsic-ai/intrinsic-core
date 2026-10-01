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

"""Utils for Skill implementations."""

from __future__ import annotations

from intrinsic.resources.proto import resource_handle_pb2
from intrinsic.skills.internal import execute_context_impl
from intrinsic.skills.internal import preview_context_impl
from intrinsic.skills.python import skill_interface


def preview_via_execute(
    skill: skill_interface.Skill[
        skill_interface.TParamsType, skill_interface.TResultType
    ],
    request: skill_interface.PreviewRequest[skill_interface.TParamsType],
    context: skill_interface.PreviewContext,
) -> skill_interface.TResultType:
  """Implements Skill.preview by calling Skill.execute.

  A skill can use this function to implement `preview` by calling
  `preview_via_execute` from within its implementation. E.g.:
  ```
  class MySkill(Skill):
    def preview(self, request: PreviewRequest, context: PreviewContext) -> ...:
      ...
      return preview_via_execute(self, request, context)
  ```

  A skill should only use this util to implement `preview` if its `execute`
  method does not require resources or modify the object world.

  Args:
    skill: The skill instance.
    request: The preview request.
    context: The preview context.

  Returns:
    The response from calling `skill.execute`.
  """
  return skill.execute(
      preview_to_execute_request(request),
      preview_to_execute_context(
          context=context,
          resource_handles={},
      ),
  )


def preview_to_execute_request(
    request: skill_interface.PreviewRequest[skill_interface.TParamsType],
) -> skill_interface.ExecuteRequest[skill_interface.TParamsType]:
  """Converts a PreviewRequest to an ExecuteRequest."""
  return skill_interface.ExecuteRequest(
      params=request.params,
  )


def preview_to_execute_context(
    context: skill_interface.PreviewContext,
    resource_handles: dict[str, resource_handle_pb2.ResourceHandle],
) -> skill_interface.ExecuteContext:
  """Converts a PreviewContext to an ExecuteContext."""
  return execute_context_impl.ExecuteContextImpl(
      canceller=context.canceller,

      geometry_service=context.geometry_service,

      logging_context=context.logging_context,
      motion_planner=context.motion_planner,
      object_world=context.object_world,
      resource_handles=resource_handles,
      context_id=context.context_id,
  )
