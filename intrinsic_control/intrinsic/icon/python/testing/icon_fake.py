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

"""Client-side Python for testing the ICON Application Layer using fakes.

Provides a Python wrapper around the fake ICON service for application
developers and skill authors who wish to write more in-depth tests.

See: go/mocks#prefer-fakes
"""

from collections.abc import Callable
from collections.abc import Iterable
from collections.abc import Mapping
from collections.abc import Sequence
from typing import Optional
from typing import Union

from google.protobuf import any_pb2
import portpicker
from pybind11_abseil import status

from intrinsic.icon.proto.v1 import service_pb2
from intrinsic.icon.python import reactions
from intrinsic.icon.python.testing import channel_fake


class UninitializedError(Exception):
  """UninitializedError signals that you are using a part of the fake that is not initialized yet."""


class ActionBehavior:
  """Stores default behaviors for a single use of an action.

  Attributes:
    conditions: List of Conditions specified by `trigger_conditions=...`.
    state_variables: Mapping of StateVariableValues specified by
      `state_variables=...`.
    robot_status: The fake robot status specified by `set_robot_status=...`.
    parameter_checker: A callback that evaluates the parameters of the fake
      action. Gets an Any proto, and should raise an exception if the parameters
      are not as expected.
    on_start: A callback that is invoked when the action becomes active.
  """

  def __init__(
      self,
      trigger_conditions: Optional[Iterable[reactions.Condition]] = None,
      report_state_variables: Optional[
          Sequence[Mapping[str, Union[bool, float]]]
      ] = None,
      set_robot_status: Optional[service_pb2.GetStatusResponse] = None,
      parameter_checker: Optional[Callable[[any_pb2.Any], None]] = None,
      on_start: Optional[Callable[[], None]] = None,
  ):
    """Initializes the fake behavior to perform for an action.

    Args:
      trigger_conditions: Sets conditions to trigger, in the order specified.
      report_state_variables: State variables to report, in the order specified.
      set_robot_status: Sets the fake robot status (obtainable with
        `icon_client.status()`).
      parameter_checker: A callback that evaluates the parameters of the fake
        action. Gets an Any proto, and should raise an exception if the
        parameters are not as expected.
      on_start: A callback that is invoked when the action becomes active. Note
        that `on_start` is called while the fake session mutex is held; it is
        safe to use for notifications (e.g. `threading.Event`) and setting
        flags, but must not call back into session methods or session behaviors.
    """
    self.conditions = trigger_conditions
    self.state_variables = report_state_variables
    self.robot_status = set_robot_status
    self.on_start = on_start

    if parameter_checker:

      def cb_wrapped(parameter_proto: any_pb2.Any) -> status.Status:
        try:
          parameter_checker(parameter_proto)
        # convert ALL exceptions to InvalidArgumentError.
        except Exception as exc:  # pylint: disable=broad-exception-caught
          return status.invalid_argument_error(str(exc))
        else:
          return status.Status.OkStatus()

      self.parameter_checker = cb_wrapped
    else:
      self.parameter_checker = None


class SessionBehavior:
  """Stores default behaviors for a single session.

  Attributes:
    action_behaviors: List of action ids and their corresponding fake behaviors.
  """

  def __init__(self):
    self.action_behaviors = []

  def expect_action(self, action_id: int, behavior: ActionBehavior) -> None:
    """Specifies the behavior to take the next time `action_id` becomes active.

    Subsequent calls append behaviors for subsequent activations of the actions
    during this session. Note that it is valid for subsequent calls to use the
    same `action_id`; this is useful when an action may become active multiple
    times during a session.

    Args:
      action_id: The ID of the action that will perform the behavior.
      behavior: The fake action behavior to perform.
    """
    self.action_behaviors.append((action_id, behavior))


class IconFake:
  """Wrapper for the fake ICON gRPC service.

  Constructs a backend via C++ bindings and generates a port to connect to it,
  as inprocess backends are not supported in Python.

  One reason for providing this wrapper class is to manage the lifecycle of the
  fake channel. The client needs to retain a reference to the channel in order
  to connect.
  """

  def __init__(
      self,
      validation_mode: channel_fake.ValidationMode = channel_fake.ValidationMode.NICE,
  ):
    """Creates the fake ICON service.

    Args:
      validation_mode: Option to choose the validation mode, e.g. make it
        strict.
    """
    self._port = portpicker.pick_unused_port()
    self._builder = channel_fake.Builder(
        channel_fake.Options(False, self._port), validation_mode
    )
    self._channel: Optional[channel_fake.RtclControllerChannelFake] = None

  def init_robot_status(
      self, robot_status: service_pb2.GetStatusResponse
  ) -> None:
    """Initializes the robot status with the given fake status.

    Args:
      robot_status: The fake GetStatusResponse to initialize with.
    """
    self._builder.WithInitialRobotStatus(robot_status)

  def expect_session(self, behavior: SessionBehavior):
    """Specifies the default behaviors for the next session.

    Subsequent calls create session expectations for subsequent sessions.

    Args:
      behavior: The fake session behavior.
    """
    session_will = channel_fake.SessionWill()
    for action_id, action_behavior in behavior.action_behaviors:
      # Convert the behavior to be compatible with the C++ bindings of Action
      # behavior via ActionWill. For example, use the underlying protos for
      # Conditions and strongly typed ActionInstanceIds.
      action_will = channel_fake.ActionWill()
      if action_behavior.conditions:
        action_will = action_will.TriggerConditionsInOrder(
            [condition.proto for condition in action_behavior.conditions]
        )
      if action_behavior.state_variables:
        action_will = action_will.ReportStateVariablesInOrder(
            action_behavior.state_variables
        )
      if action_behavior.robot_status:
        action_will = action_will.SetRobotStatus(action_behavior.robot_status)
      if action_behavior.parameter_checker:
        action_will = action_will.ExpectParameter(
            action_behavior.parameter_checker
        )
      if action_behavior.on_start:
        action_will = action_will.OnStart(action_behavior.on_start)
      session_will = session_will.OnAction(
          channel_fake.ActionInstanceId(action_id), action_will
      )
    self._builder.OnNextSession(session_will)

  def start(self) -> int:
    """Starts the fake ICON service.

    Returns:
      The port for connecting to the service.
    """
    # Retain a reference to the channel so the caller can connect.
    self._channel = self._builder.Build()
    return self._port

  def set_status(self, new_robot_status: service_pb2.GetStatusResponse) -> None:
    """Overwrites the current robot status with a new one.

    The common usage pattern is to first retrieve the current status, update
    some parts and then send the new one. In many cases it might be more
    appropriate to adjust the robot status as a result of an action. In such
    cases, use ActionBehavior's set_status functionality instead.

    Args:
      new_robot_status: New robot status for all parts to apply immediately.
    """
    self._channel.SetRobotStatus(new_robot_status)

  def get_part_properties(
      self,
  ) -> Mapping[str, Mapping[str, Union[bool, float]]]:
    """Returns the current map from part name to property values for each part.

    Raises:
      UninitializedError: If called before start()
    """
    if self._channel is None:
      raise UninitializedError(
          'You must call start() on your fake before getting part properties.'
      )
    return self._channel.GetPartProperties()

  def set_part_properties(
      self, new_part_properties: Mapping[str, Mapping[str, Union[bool, float]]]
  ):
    """Overwrites the current part property map with a new one.

    You should ideally use this once at the start of your test, to initialize
    the types and values of all properties that your test cares about. If you do
    not initialize part values, any attempt to set a part value via the ICON
    client will fail.

    Args:
      new_part_properties: A map from part name to a map from property name to
        property value. Overwrites any previous part property values.

    Raises:
      UninitializedError: If called before start()
    """
    if self._channel is None:
      raise UninitializedError(
          'You must call start() on your fake before setting part properties.'
      )
    self._channel.SetPartProperties(new_part_properties)
