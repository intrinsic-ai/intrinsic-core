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

"""Blackboard access within the solution building library."""

import enum
import typing

from google.protobuf import any_pb2
from google.protobuf import empty_pb2
from google.protobuf import message
from google.protobuf import wrappers_pb2

from intrinsic.executive.proto import blackboard_service_pb2
from intrinsic.executive.proto import blackboard_service_pb2_grpc
from intrinsic.proto_tools.registry import proto_registry_client
from intrinsic.solutions import blackboard_value
from intrinsic.solutions import ipython
from intrinsic.solutions import utils
from intrinsic.solutions.internal import skill_utils
from intrinsic.util.grpc import error_handling
from intrinsic.util.status import extended_status_pb2


class ScopedBlackboardKey(typing.NamedTuple):
  """An entry on the blackboard.

  Attributes:
    key: The key of the entry.
    scope: The scope of the entry.
    type_url: The type URL of the entry.
  """

  key: str
  scope: str
  type_url: str


_WRAPPER_CLASSES = [
    wrappers_pb2.DoubleValue,
    wrappers_pb2.FloatValue,
    wrappers_pb2.Int64Value,
    wrappers_pb2.UInt64Value,
    wrappers_pb2.Int32Value,
    wrappers_pb2.UInt32Value,
    wrappers_pb2.BoolValue,
    wrappers_pb2.StringValue,
    wrappers_pb2.BytesValue,
]
_WRAPPER_TYPES = {cls.DESCRIPTOR.full_name: cls for cls in _WRAPPER_CLASSES}

_PYTHON_TYPE_TO_WRAPPERS = {
    int: {
        wrappers_pb2.Int64Value,
        wrappers_pb2.UInt64Value,
        wrappers_pb2.Int32Value,
        wrappers_pb2.UInt32Value,
    },
    float: {
        wrappers_pb2.DoubleValue,
        wrappers_pb2.FloatValue,
    },
    bool: {wrappers_pb2.BoolValue},
    str: {wrappers_pb2.StringValue},
    bytes: {wrappers_pb2.BytesValue},
}


@utils.protoenum(
    proto_enum_type=blackboard_service_pb2.BlackboardSnapshot.SnapshotSource,
    unspecified_proto_enum_map_to_none=blackboard_service_pb2.BlackboardSnapshot.SNAPSHOT_SOURCE_UNSPECIFIED,
    strip_prefix="SNAPSHOT_SOURCE_",
)
class SnapshotSource(enum.Enum):
  """Represents the reason why a snapshot was created."""


@utils.protoenum(
    proto_enum_type=blackboard_service_pb2.LoadBlackboardSnapshotRequest.IntegrationMode,
    unspecified_proto_enum_map_to_none=blackboard_service_pb2.LoadBlackboardSnapshotRequest.INTEGRATION_MODE_UNSPECIFIED,
    strip_prefix="INTEGRATION_MODE_",
)
class IntegrationMode(enum.Enum):
  """Represents how a snapshot is integrated into a blackboard."""


class Blackboard:
  """Convenience wrapper for blackboard access."""

  _stub: blackboard_service_pb2_grpc.ExecutiveBlackboardStub
  _operation_name: str
  _proto_registry: proto_registry_client.ProtoRegistryClient

  def __init__(
      self,
      stub: blackboard_service_pb2_grpc.ExecutiveBlackboardStub,
      operation_name: str,
      proto_registry: proto_registry_client.ProtoRegistryClient,
  ):
    """Initializes the blackboard.

    Args:
      stub: The gRPC stub to be used for blackboard related calls.
      operation_name: The name of the operation this blackboard belongs to.
      proto_registry: Optional ProtoRegistry service wrapper.
    """
    self._stub = stub
    self._operation_name = operation_name
    self._proto_registry = proto_registry

  def _resolve_key_and_scope(
      self, key: str | blackboard_value.BlackboardValue, scope: str | None
  ) -> tuple[str, str | None]:
    """Resolves the key and scope from the input.

    If key is a BlackboardValue, the key and scope are extracted from it.
    """
    if isinstance(key, blackboard_value.BlackboardValue):
      if scope is not None:
        raise ValueError(
            f"Cannot provide explicit scope '{scope}' when using a"
            " BlackboardValue."
        )
      if not key.is_toplevel_value:
        raise ValueError(
            f"BlackboardValue with path {key.value_access_path()} is not a"
            " toplevel value."
        )
      return key.value_access_path(), key.scope()
    if isinstance(key, str):
      return key, scope
    raise TypeError(
        f"Expected str or BlackboardValue for 'key', got {type(key)}"
    )

  @error_handling.retry_on_grpc_unavailable
  @error_handling.log_extended_status(
      ipython.display_extended_status_proto_if_ipython
  )
  def delete_value(
      self,
      key: str | blackboard_value.BlackboardValue,
      scope: str | None = None,
  ) -> None:
    """Deletes a specific value from the blackboard.

    Args:
      key: The key or BlackboardValue to delete.
      scope: Optional scope. If not specified, the value is deleted from the
        main process scope.
    """
    key, scope = self._resolve_key_and_scope(key, scope)
    request = blackboard_service_pb2.DeleteBlackboardValueRequest(
        key=key,
        scope=scope or "",
        operation_name=self._operation_name,
    )
    self._stub.DeleteBlackboardValue(request)

  @error_handling.retry_on_grpc_unavailable
  @error_handling.log_extended_status(
      ipython.display_extended_status_proto_if_ipython
  )
  def list_keys(self, scope: str | None = None) -> list[ScopedBlackboardKey]:
    """Lists keys on the blackboard.

    Args:
      scope: Optional scope to filter by.

    Returns:
      A list of ScopedBlackboardKey objects containing key and scope.
    """
    request = blackboard_service_pb2.ListBlackboardValuesRequest(
        operation_name=self._operation_name,
        scope=scope,
        view=blackboard_service_pb2.ListBlackboardValuesRequest.ANY_TYPEURL_ONLY,
    )
    response = self._stub.ListBlackboardValues(request)
    return [
        ScopedBlackboardKey(key=v.key, scope=v.scope, type_url=v.value.type_url)
        for v in response.values
    ]

  @error_handling.retry_on_grpc_unavailable
  @error_handling.log_extended_status(
      ipython.display_extended_status_proto_if_ipython
  )
  def get_value_any(
      self,
      key: str | blackboard_value.BlackboardValue,
      scope: str | None = None,
  ) -> any_pb2.Any:
    """Gets a value from the blackboard as an Any proto.

    Args:
      key: The key or BlackboardValue to retrieve.
      scope: Optional scope.

    Returns:
      The value from the blackboard as an Any proto.
    """
    key, scope = self._resolve_key_and_scope(key, scope)
    request = blackboard_service_pb2.GetBlackboardValueRequest(
        key=key,
        scope=scope,
        operation_name=self._operation_name,
    )
    response = self._stub.GetBlackboardValue(request)
    return response.value

  def get_value(
      self,
      key: str | blackboard_value.BlackboardValue,
      scope: str | None = None,
  ) -> int | float | bool | str | bytes | any_pb2.Any:
    """Gets a value from the blackboard.

    Args:
      key: The key or BlackboardValue to retrieve.
      scope: Optional scope.

    Returns:
      The value from the blackboard. If it is a known wrapper type, the native
      Python value is returned. Otherwise, the Any proto is returned.
    """
    any_value = self.get_value_any(key, scope)

    type_url = any_value.type_url
    proto_name = type_url.rpartition("/")[-1]
    if proto_name in _WRAPPER_TYPES:
      wrapper = _WRAPPER_TYPES[proto_name]()
      any_value.Unpack(wrapper)
      return wrapper.value

    if self._proto_registry is not None:
      try:
        fds = self._proto_registry.get_descriptor_set_by_typeurl(type_url)
        msg = skill_utils.create_message_from_file_descriptor_set(
            fds, proto_name
        )
        any_value.Unpack(msg)
        return msg
      except Exception:  # pylint: disable=broad-except
        pass

    return any_value

  @error_handling.retry_on_grpc_unavailable
  @error_handling.log_extended_status(
      ipython.display_extended_status_proto_if_ipython
  )
  def update_value(
      self,
      key: str | blackboard_value.BlackboardValue,
      value: (
          int
          | float
          | bool
          | str
          | bytes
          | any_pb2.Any
          | message.Message
          | skill_utils.MessageWrapper
      ),
      scope: str | None = None,
  ) -> None:
    """Updates a value on the blackboard.

    Args:
      key: The key or BlackboardValue to update.
      value: The value to set. Can be an Any proto, a generic protobuf message,
        a MessageWrapper or a native Python type (int, float, bool, str, bytes).
      scope: Optional scope.

    Raises:
      TypeError: If the value type does not match the existing blackboard value.
    """
    key, scope = self._resolve_key_and_scope(key, scope)
    existing_any = self.get_value_any(key, scope)
    existing_type = existing_any.type_url.rpartition("/")[-1]

    if isinstance(value, any_pb2.Any):
      any_val = value
    elif isinstance(value, skill_utils.MessageWrapper):
      any_val = value.to_any()
    elif isinstance(value, message.Message):
      any_val = any_pb2.Any()
      any_val.Pack(value)
    elif (py_type := type(value)) in _PYTHON_TYPE_TO_WRAPPERS:
      wrapper_cls = _WRAPPER_TYPES.get(existing_type)
      if wrapper_cls in _PYTHON_TYPE_TO_WRAPPERS[py_type]:
        any_val = any_pb2.Any()
        any_val.Pack(wrapper_cls(value=value))
      else:
        article = "an" if py_type is int else "a"
        raise TypeError(
            f"Type mismatch for key '{key}': existing type {existing_type} is"
            f" not {article} {py_type.__name__} wrapper"
        )
    else:
      raise TypeError(
          "Expected Any, Message, MessageWrapper or native type for 'value',"
          f" got {type(value)}"
      )

    # Check for type mismatch
    if any_val.type_url != existing_any.type_url:
      raise TypeError(
          f"Type mismatch for key '{key}': existing type"
          f" {existing_any.type_url}, new type {any_val.type_url}"
      )

    request = blackboard_service_pb2.UpdateBlackboardValueRequest(
        value=blackboard_service_pb2.BlackboardValue(
            key=key,
            scope=scope or "",
            operation_name=self._operation_name,
            value=any_val,
        )
    )
    self._stub.UpdateBlackboardValue(request)

  @error_handling.retry_on_grpc_unavailable
  @error_handling.log_extended_status(
      ipython.display_extended_status_proto_if_ipython
  )
  def create_snapshot(
      self,
      display_name: str = "",
      snapshot_source: SnapshotSource = SnapshotSource.USER,
  ) -> blackboard_service_pb2.BlackboardSnapshot:
    """Saves the current operation's blackboard as a snapshot.

    Args:
      display_name: A user-friendly name for the snapshot.
      snapshot_source: The source/reason for creating this snapshot.

    Returns:
      The created BlackboardSnapshot proto.
    """
    request = blackboard_service_pb2.CreateBlackboardSnapshotRequest(
        operation_name=self._operation_name,
        display_name=display_name,
        snapshot_source=snapshot_source.value,
    )
    response = self._stub.CreateBlackboardSnapshot(request)
    return response.snapshot

  @error_handling.retry_on_grpc_unavailable
  @error_handling.log_extended_status(
      ipython.display_extended_status_proto_if_ipython
  )
  def load_snapshot(
      self,
      handle: str | blackboard_service_pb2.BlackboardSnapshot,
      integration_mode: IntegrationMode = IntegrationMode.MERGE,
  ) -> blackboard_service_pb2.LoadBlackboardSnapshotResponse:
    """Loads a previously saved snapshot into the current operation's blackboard.

    Args:
      handle: The handle of the snapshot to load, or the BlackboardSnapshot
        proto itself.
      integration_mode: How to integrate the snapshot values into the existing
        blackboard.

    Returns:
      The LoadBlackboardSnapshotResponse.
    """
    if isinstance(handle, blackboard_service_pb2.BlackboardSnapshot):
      handle = handle.handle

    request = blackboard_service_pb2.LoadBlackboardSnapshotRequest(
        operation_name=self._operation_name,
        handle=handle,
        integration_mode=integration_mode.value,
    )
    response = self._stub.LoadBlackboardSnapshot(request)
    # 80114: Successfully restored snapshot
    if (
        response.diagnostics.status_code.code != 80114
        or response.diagnostics.severity
        != extended_status_pb2.ExtendedStatus.Severity.INFO
    ):
      ipython.display_extended_status_proto_if_ipython(response.diagnostics)
    return response


class BlackboardSnapshots:
  """Convenience wrapper for blackboard snapshots."""

  _stub: blackboard_service_pb2_grpc.ExecutiveBlackboardStub

  def __init__(self, stub: blackboard_service_pb2_grpc.ExecutiveBlackboardStub):
    """Initializes the blackboard snapshots.

    Args:
      stub: The gRPC stub to be used for blackboard related calls.
    """
    self._stub = stub

  @error_handling.retry_on_grpc_unavailable
  @error_handling.log_extended_status(
      ipython.display_extended_status_proto_if_ipython
  )
  def list(self) -> list[blackboard_service_pb2.BlackboardSnapshot]:
    """Lists the currently saved snapshots.

    Returns:
      A list of BlackboardSnapshot objects.
    """
    snapshots = []
    page_token = ""
    while True:
      request = blackboard_service_pb2.ListBlackboardSnapshotsRequest(
          page_size=100,
          page_token=page_token,
      )
      response = self._stub.ListBlackboardSnapshots(request)
      snapshots.extend(response.snapshots)
      page_token = response.next_page_token
      if not page_token:
        break
    return snapshots

  @error_handling.retry_on_grpc_unavailable
  @error_handling.log_extended_status(
      ipython.display_extended_status_proto_if_ipython
  )
  def delete(
      self, handle: str | blackboard_service_pb2.BlackboardSnapshot
  ) -> None:
    """Deletes a given snapshot.

    Args:
      handle: The handle of the snapshot to delete, or the BlackboardSnapshot
        proto itself.
    """
    if isinstance(handle, blackboard_service_pb2.BlackboardSnapshot):
      handle = handle.handle

    request = blackboard_service_pb2.DeleteBlackboardSnapshotRequest(
        handle=handle
    )
    self._stub.DeleteBlackboardSnapshot(request)
