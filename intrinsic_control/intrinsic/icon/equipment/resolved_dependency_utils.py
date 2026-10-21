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
from intrinsic.icon.proto.v1 import service_pb2
from intrinsic.icon.proto.v1 import service_pb2_grpc
from intrinsic.icon.python import errors
from intrinsic.icon.python import icon_api
from intrinsic.world.proto import object_world_refs_pb2
from intrinsic.world.python import object_world_client
from intrinsic.world.python import object_world_ids
from intrinsic.world.python import object_world_resources

HAL_ARM_PART_TYPE_NAME = "HalArmPart"

PartType: TypeAlias = str
PartName: TypeAlias = str
HardwareResourceName: TypeAlias = str
HardwareResourceNameByPartName: TypeAlias = dict[PartName, HardwareResourceName]

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
  """

  client: icon_api.Client
  parts: dict[PartType, HardwareResourceNameByPartName] = dataclasses.field(
      default_factory=dict
  )

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


def connect_to_icon_resolved_dependency(
    resolved_dependency: resolved_dependency_pb2.ResolvedDependency,
    connect_timeout: float = _DEFAULT_CONNECT_TIMEOUT_SECONDS,
) -> IconResolvedDependency:
  """Connects to an ICON server and extracts part names and hardware resources.

  Queries `get_config()` on the ICON server to populate `parts` keyed by
  `part_type_name` and `name`.

  Args:
    resolved_dependency: The resolved dependency for the robot we want to
      control.
    connect_timeout: Time in seconds to wait for the ICON gRPC server to be
      ready.

  Returns:
    An IconResolvedDependency instance with the connected client and a `parts`
    mapping of `part_type_name` -> `part_name` -> `hardware_resource_name`.

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
  return out
