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

"""Helper functions to interact with ICON ResolvedDependency assets in Python."""

import dataclasses
from typing import Optional
from typing import TypeAlias

import grpc

from intrinsic.assets import interface_utils
from intrinsic.assets.dependencies import utils as asset_utils
from intrinsic.assets.proto.v1 import resolved_dependency_pb2
from intrinsic.icon.control.parts.hal.force_torque_sensor_part import hal_force_torque_sensor_part_config_pb2
from intrinsic.icon.equipment import force_control_settings_pb2
from intrinsic.icon.proto.v1 import service_pb2
from intrinsic.icon.proto.v1 import service_pb2_grpc
from intrinsic.icon.proto.v1 import types_pb2
from intrinsic.icon.python import errors
from intrinsic.icon.python import icon_api
from intrinsic.world.proto import object_world_refs_pb2
from intrinsic.world.python import object_world_client
from intrinsic.world.python import object_world_ids
from intrinsic.world.python import object_world_resources

HAL_ADIO_PART_TYPE_NAME = "HalADIOPart"
HAL_ARM_PART_TYPE_NAME = "HalArmPart"
HAL_FORCE_TORQUE_SENSOR_PART_TYPE_NAME = "HalForceTorqueSensorPart"
HAL_LINEAR_GRIPPER_PART_TYPE_NAME = "HalLinearGripperPart"
HAL_RANGEFINDER_PART_TYPE_NAME = "HalRangefinderPart"

PartType: TypeAlias = str
PartName: TypeAlias = str
HardwareResourceName: TypeAlias = str
HardwareResourceNameByPartName: TypeAlias = dict[PartName, HardwareResourceName]
PartConfigByPartName: TypeAlias = dict[PartName, types_pb2.PartConfig]

_DEFAULT_CONNECT_TIMEOUT_SECONDS = 5
_GRPC_OPTIONS = (
    ("grpc.max_receive_message_length", -1),
    ("grpc.max_send_message_length", -1),
)


def _is_empty_object_reference(
    ref: object_world_refs_pb2.ObjectReference,
) -> bool:
  if ref.HasField("by_name"):
    return not ref.by_name.object_name
  if ref.HasField("id"):
    return not ref.id
  return True


def _format_available_position_parts(
    position_parts: HardwareResourceNameByPartName,
) -> str:
  return ", ".join(
      f"({part_name}, {object_name})"
      for part_name, object_name in sorted(position_parts.items())
  )


def _format_available_part_names(
    parts_map: HardwareResourceNameByPartName,
) -> str:
  return ", ".join(sorted(parts_map.keys()))


def _sorted_force_torque_sensor_part_names(
    part_configs: PartConfigByPartName,
) -> list[PartName]:
  return sorted(
      part_name
      for part_name, part_config in part_configs.items()
      if part_config.config.Is(
          hal_force_torque_sensor_part_config_pb2.HalForceTorqueSensorPartConfig.DESCRIPTOR
      )
  )


def _get_single_part_name_by_type(
    parts: dict[PartType, HardwareResourceNameByPartName],
    part_type_name: PartType,
    part_description: str,
) -> PartName:
  """Returns the name of the single part of type `part_type_name` in `parts`.

  Checks `parts` to see if there is exactly one part of type `part_type_name`.

  Args:
    parts: Maps part type names to part names and hardware resource names.
    part_type_name: The part type name to look up (e.g. "HalADIOPart").
    part_description: A human-readable description of the part type used in
      error messages (e.g. "ADIO").

  Returns:
    The name of that part if there is exactly one.

  Raises:
    ValueError: If there are no such parts, or if there is more than one.
  """
  typed_parts = parts.get(part_type_name, {})
  if not typed_parts:
    raise ValueError(
        f"The given ICON instance does not have any {part_description} parts."
    )
  if len(typed_parts) > 1:
    raise ValueError(
        f"The given ICON instance has multiple {part_description} parts."
        f" Please specify which {part_description} part to use. Available"
        f" {part_description} parts for the selected ICON instance are:"
        f" {_format_available_part_names(typed_parts)}."
    )
  return next(iter(typed_parts.keys()))


def _icon_api_interface_uri() -> str:
  return (
      f"{interface_utils.GRPC_URI_PREFIX}"
      f"{service_pb2.DESCRIPTOR.services_by_name['IconApi'].full_name}"
  )


@dataclasses.dataclass
class PositionPartWithObject:
  """Pairs an ICON position part name with its corresponding kinematic object."""

  part_name: PartName
  kinematic_object: object_world_resources.KinematicObject


@dataclasses.dataclass
class IconResolvedDependency:
  """Represents a connection to an ICON server and its configured parts.

  Attributes:
    client: Connected ICON API client.
    parts: Maps each `PartConfig.part_type_name` (e.g. "HalArmPart") to a dict
      mapping `PartConfig.name` to a non-empty
      `PartConfig.hardware_resource_name`.
    part_configs: Maps each `PartConfig.name` to the full `PartConfig` returned
      by the ICON server's `get_config()`.
  """

  client: icon_api.Client
  parts: dict[PartType, HardwareResourceNameByPartName] = dataclasses.field(
      default_factory=dict
  )
  part_configs: PartConfigByPartName = dataclasses.field(default_factory=dict)

  def get_position_part_and_kinematic_object(
      self,
      object_world: object_world_client.ObjectWorldClient,
      arm_part: Optional[object_world_refs_pb2.ObjectReference] = None,
  ) -> PositionPartWithObject:
    """Resolves a position part name and its KinematicObject from `parts`.

    If `arm_part` is None or empty, requires `parts` to have a single
    `"HalArmPart"` entry and looks up its kinematic object using its
    `hardware_resource_name`. If `arm_part` is provided and non-empty, looks up
    the kinematic object from `arm_part` in `object_world` and matches its name
    against the `hardware_resource_name`s of the `"HalArmPart"` entries in
    `parts`.

    Args:
      object_world: Object world client to query the kinematic object from.
      arm_part: Optional ObjectReference selecting a specific position part's
        world object.

    Returns:
      A PositionPartWithObject containing the resolved part name and its
      KinematicObject.

    Raises:
      ValueError: If `arm_part` is omitted when multiple position parts exist,
        if no position parts exist, or if `arm_part` does not correspond to any
        position part in `parts`.
    """
    position_parts = self.parts.get(HAL_ARM_PART_TYPE_NAME, {})
    if not position_parts:
      raise ValueError(
          "The given ICON instance does not have any position parts."
      )

    if arm_part is None or _is_empty_object_reference(arm_part):
      if len(position_parts) > 1:
        raise ValueError(
            "No arm part object reference was given, and the given ICON"
            " instance has multiple. Please specify an arm part object"
            " reference to select one part to use. Available (part_name,"
            " object_name) pairs for the selected ICON instance are:"
            f" {_format_available_position_parts(position_parts)}."
        )
      part_name, hardware_resource_name = next(iter(position_parts.items()))
      kinematic_object = object_world.get_kinematic_object(
          object_world_ids.WorldObjectName(hardware_resource_name)
      )
      return PositionPartWithObject(
          part_name=part_name,
          kinematic_object=kinematic_object,
      )

    kinematic_object = object_world.get_kinematic_object(arm_part)
    object_name = kinematic_object.name
    for current_part_name, current_object_name in position_parts.items():
      if current_object_name == object_name:
        return PositionPartWithObject(
            part_name=current_part_name,
            kinematic_object=kinematic_object,
        )

    raise ValueError(
        "Could not find a position part corresponding to object with name"
        f" '{object_name}'. Please check you have the right object and ICON"
        " instance selected. Available (part_name, object_name) pairs for the"
        " selected ICON instance are:"
        f" {_format_available_position_parts(position_parts)}."
    )

  def get_adio_part_names(self) -> list[PartName]:
    """Returns the sorted list of ADIO (`"HalADIOPart"`) part names.

    Returns:
      The `"HalADIOPart"` part names from `parts` sorted in ascending
      lexicographical order.

    Raises:
      ValueError: If no ADIO parts exist in `parts`.
    """
    adio_parts = self.parts.get(HAL_ADIO_PART_TYPE_NAME, {})
    if not adio_parts:
      raise ValueError("The given ICON instance does not have any ADIO parts.")
    return sorted(adio_parts.keys())

  # The following `get_*_part_name()` helpers check `parts` for the respective
  # HAL part type.
  # Return the name of the part if there is exactly one part of that type.
  # Raise a `ValueError` if there are no such parts, or if there is more than
  # one (in which case the caller has to ask the user to select one).
  #
  # Skills that let users optionally specify a part name should use a
  # user-provided part name as-is (so that any ICON part supporting the required
  # feature interfaces can be used) and only call these helpers if the part name
  # was omitted.

  def get_adio_part_name(self) -> PartName:
    """Returns the name of the single `"HalADIOPart"` part.

    Raises:
      ValueError: If there is no or more than one ADIO part in `parts`.
    """
    return _get_single_part_name_by_type(
        self.parts, HAL_ADIO_PART_TYPE_NAME, "ADIO"
    )

  def get_gripper_part_name(self) -> PartName:
    """Returns the name of the single `"HalLinearGripperPart"` part.

    Raises:
      ValueError: If there is no or more than one gripper part in `parts`.
    """
    return _get_single_part_name_by_type(
        self.parts, HAL_LINEAR_GRIPPER_PART_TYPE_NAME, "gripper"
    )

  def get_force_torque_sensor_part_name(self) -> PartName:
    """Returns the name of the single `"HalForceTorqueSensorPart"` part.

    Raises:
      ValueError: If there is no or more than one force-torque sensor part in
        `parts`.
    """
    return _get_single_part_name_by_type(
        self.parts,
        HAL_FORCE_TORQUE_SENSOR_PART_TYPE_NAME,
        "force-torque sensor",
    )

  def get_rangefinder_part_name(self) -> PartName:
    """Returns the name of the single `"HalRangefinderPart"` part.

    Raises:
      ValueError: If there is no or more than one rangefinder part in `parts`.
    """
    return _get_single_part_name_by_type(
        self.parts, HAL_RANGEFINDER_PART_TYPE_NAME, "rangefinder"
    )

  def get_force_control_settings(
      self,
      force_torque_sensor_part: PartName,
  ) -> force_control_settings_pb2.ForceControlSettings:
    """Returns the force control settings of a force-torque sensor part.

    Args:
      force_torque_sensor_part: Name of the force-torque sensor part.

    Returns:
      The `force_control_settings` from the `HalForceTorqueSensorPartConfig` of
      the part named `force_torque_sensor_part` in `part_configs`.

    Raises:
      ValueError: If `force_torque_sensor_part` does not exist, or if its config
        is not a `HalForceTorqueSensorPartConfig`.
    """
    part_config = self.part_configs.get(force_torque_sensor_part)
    if part_config is None:
      available_parts = _sorted_force_torque_sensor_part_names(
          self.part_configs
      )
      if not available_parts:
        raise ValueError(
            f"Could not find a part with name '{force_torque_sensor_part}'."
            " The selected ICON instance does not have any force-torque sensor"
            " parts."
        )
      raise ValueError(
          f"Could not find a part with name '{force_torque_sensor_part}'."
          " Please check you have the right part name and ICON instance"
          " selected. Available force-torque sensor parts for the selected ICON"
          f" instance are: {', '.join(available_parts)}."
      )
    ft_config = (
        hal_force_torque_sensor_part_config_pb2.HalForceTorqueSensorPartConfig()
    )
    if not part_config.config.Unpack(ft_config):
      raise ValueError(
          f"Part '{force_torque_sensor_part}' does not have a"
          " HalForceTorqueSensorPartConfig (config type URL:"
          f" '{part_config.config.type_url}'). Please check that you selected a"
          " force-torque sensor part."
      )
    return ft_config.force_control_settings


def connect_to_icon_resolved_dependency(
    resolved_dependency: resolved_dependency_pb2.ResolvedDependency,
    connect_timeout: float = _DEFAULT_CONNECT_TIMEOUT_SECONDS,
) -> IconResolvedDependency:
  """Connects to an ICON server and extracts part names and hardware resources.

  Queries `get_config()` on the ICON server to populate `parts` keyed by
  `part_type_name` and `name` as well as `part_configs` keyed by `name`.

  Args:
    resolved_dependency: The resolved dependency for the robot we want to
      control.
    connect_timeout: Time in seconds to wait for the ICON gRPC server to be
      ready.

  Returns:
    An IconResolvedDependency instance with the connected client, a `parts`
    mapping of `part_type_name` -> `part_name` -> `hardware_resource_name`, and
    a `part_configs` mapping of `part_name` -> `PartConfig`.

  Raises:
    KeyError: If resolved_dependency does not include any connection config.
    errors.Client.ServerError: If the ICON server fails to become ready within
      connect_timeout, or if any part has an empty part_type_name or
      hardware_resource_name.
  """
  try:
    channel = asset_utils.connect(
        resolved_dependency,
        _icon_api_interface_uri(),
        grpc_options=_GRPC_OPTIONS,
    )
  except (asset_utils.MissingInterfaceError, asset_utils.NotGRPCError) as e:
    raise KeyError("No ICON connection config was provided.") from e

  try:
    grpc.channel_ready_future(channel).result(timeout=connect_timeout)
  except grpc.FutureTimeoutError as e:
    raise errors.Client.ServerError("Failed to connect to ICON server") from e

  client = icon_api.Client(service_pb2_grpc.IconApiStub(channel))
  out = IconResolvedDependency(client=client)

  config = client.get_config()
  for part_config in config.part_configs:
    if not part_config.part_type_name:
      raise errors.Client.ServerError(
          "ICON server returned empty part_type_name for part"
          f" '{part_config.name}'."
      )
    if not part_config.hardware_resource_name:
      raise errors.Client.ServerError(
          "ICON server returned empty hardware_resource_name for part"
          f" '{part_config.name}'."
      )
    out.parts.setdefault(part_config.part_type_name, {})[
        part_config.name
    ] = part_config.hardware_resource_name
    out.part_configs[part_config.name] = part_config
  return out
