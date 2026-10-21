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

"""Implements a SkillRepository to only serve a single skill."""

from collections.abc import Callable
import threading

from intrinsic.assets import id_utils
from intrinsic.skills.internal import runtime_data as rd
from intrinsic.skills.internal import skill_repository as repo
from intrinsic.skills.python import skill_interface as skl
from intrinsic.util.decorators import overrides


class SingleSkillFactory(repo.SkillRepository):
  """Implements a SkillRepository to only serve a single skill."""

  def __init__(
      self,
      skill_runtime_data: rd.SkillRuntimeData,
      create_skill: Callable[[], skl.Skill],
  ):
    """Creates a SingleSkillFactory.

    Uses the data from `skill_runtime_data` and the `create_skill` function to
    create new skill instances when requested.

    Args:
      skill_runtime_data: The skill's runtime data.
      create_skill: The function to create a new skill instance.
    """
    self._skill_runtime_data = skill_runtime_data
    # SkillRuntimeData should always have a valid ID after construction, so
    # idutils.name_from should never die.
    # TODO(b/286896397)
    self._skill_alias = id_utils.name_from(skill_runtime_data.skill_id)
    self._create_skill = create_skill
    self._lock = threading.Lock()

  @overrides(repo.SkillRepository)
  def get_skill(self, skill_alias: str) -> skl.Skill:
    """Returns a new skill instance.

    Args:
      skill_alias: The skill alias.

    Raises:
      InvalidSkillAliasError: If the skill is not found in the factory.
    """
    self._validate_skill_alias(skill_alias)
    with self._lock:
      return self._create_skill()

  @overrides(repo.SkillRepository)
  def get_skill_execute(self, skill_alias: str) -> skl.SkillExecuteInterface:
    """Returns a new skill execute interface.

    Args:
      skill_alias: The skill's alias.

    Raises:
      InvalidSkillAliasError: If the skill is not found in the factory.
    """
    self._validate_skill_alias(skill_alias)
    with self._lock:
      return self._create_skill()

  @overrides(repo.SkillRepository)
  def get_skill_project(self, skill_alias: str) -> skl.SkillProjectInterface:
    """Returns a new skill project interface.

    Args:
      skill_alias: The skill's alias.

    Raises:
      InvalidSkillAliasError: If the skill is not found in the factory.
    """
    self._validate_skill_alias(skill_alias)
    with self._lock:
      return self._create_skill()

  @overrides(repo.SkillRepository)
  def get_skill_runtime_data(self, skill_alias: str) -> rd.SkillRuntimeData:
    """Returns runtime data for the skill.

    Args:
      skill_alias: The skill's alias.

    Raises:
      InvalidSkillAliasError: If the skill is not found in the factory.
    """
    self._validate_skill_alias(skill_alias)
    return self._skill_runtime_data

  @overrides(repo.SkillRepository)
  def get_skill_aliases(self) -> list[str]:
    """Returns the list of aliases of the registered skill."""
    return [self._skill_alias]

  def _validate_skill_alias(self, skill_alias: str) -> None:
    """Checks if the skill alias matches the registered skill.

    Args:
      skill_alias: The skill's alias.

    Raises:
      InvalidSkillAliasError: If the skill is not found in the factory.
    """
    if skill_alias != self._skill_alias:
      raise repo.InvalidSkillAliasError(
          f'The skill alias [{skill_alias}] does not match the registered '
          f'skill, which is: {self._skill_alias}'
      )
