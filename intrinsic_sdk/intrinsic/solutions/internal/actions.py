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

"""Lightweight Python wrappers around actions."""

import abc
import datetime
from typing import Optional

from intrinsic.executive.proto import behavior_call_pb2
from intrinsic.solutions import blackboard_value


class ActionBase(abc.ABC):
  """Abstract base class of an action.

  Derived classes need to override the getter for self.proto.
  """

  def __init__(self):
    self._project_timeout: Optional[datetime.timedelta] = None
    self._execute_timeout: Optional[datetime.timedelta] = None

  @property
  @abc.abstractmethod
  def proto(self) -> behavior_call_pb2.BehaviorCall:
    """Proto representation of action.

    Needs to be overridden by subclasses.

    Returns:
      Proto representation of action as behavior_call_pb2.BehaviorCall.

    Raises:
      NoImplementedError if the class fails to override method.
    """

  @property
  @abc.abstractmethod
  def result(self) -> blackboard_value.BlackboardValue | None:
    """Returns a BlackboardValue representing the result of the action."""

  @property
  def execute_timeout(self) -> Optional[datetime.timedelta]:
    """Timeout after which execution should be considered failed."""
    return self._execute_timeout

  @execute_timeout.setter
  def execute_timeout(self, timeout: datetime.timedelta) -> None:
    self._execute_timeout = timeout

  @property
  def project_timeout(self) -> Optional[datetime.timedelta]:
    """Timeout after which projection should be considered failed."""
    return self._project_timeout

  @project_timeout.setter
  def project_timeout(self, timeout: datetime.timedelta) -> None:
    self._project_timeout = timeout
