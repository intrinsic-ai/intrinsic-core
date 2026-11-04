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

"""Provides utility functions for Asset dependencies."""

import contextlib
from typing import Any
from typing import Sequence

from absl import logging
from google.protobuf import any_pb2
import grpc

from intrinsic.assets.data.proto.v1 import data_assets_pb2
from intrinsic.assets.data.proto.v1 import data_assets_pb2_grpc
from intrinsic.assets.dependencies import platform  
from intrinsic.assets.instances.connect import connect as connect_module
from intrinsic.assets.proto.v1 import grpc_connection_pb2
from intrinsic.assets.proto.v1 import resolved_dependency_pb2
from intrinsic.util.grpc import interceptor

_INGRESS_ADDRESS = "istio-ingressgateway.app-ingress.svc.cluster.local:80"


class MissingInterfaceError(ValueError):
  """Raised when an interface is not found in the resolved dependency."""


class NotDataError(ValueError):
  """Raised when an interface is not data."""


class NotGRPCError(ValueError):
  """Raised when an interface is not gRPC."""


# LINT.IfChange(utils)


def connect(
    dep: resolved_dependency_pb2.ResolvedDependency,
    iface: str,
    grpc_options: Sequence[tuple[str, Any]] | None = None,
) -> grpc.Channel:
  """Creates a gRPC channel to the provider of the specified interface.

  The returned channel will be intercepted to include any needed metadata for
  communicating with the provider.

  Args:
    dep: The resolved dependency.
    iface: The interface to connect to.
    grpc_options: Additional options for the gRPC channel.

  Returns:
    A gRPC channel to the provider of the specified interface.

  Raises:
    MissingInterfaceError: If the specified interface is not found in the
      resolved dependency.
    NotGRPCError: If the specified interface is not gRPC.
  """
  return connect_many(dep, [iface], grpc_options)[0]


def connect_many(
    dep: resolved_dependency_pb2.ResolvedDependency,
    ifaces: Sequence[str],
    grpc_options: Sequence[tuple[str, Any]] | None = None,
) -> list[grpc.Channel]:
  """Creates gRPC channels to the providers of the specified interfaces.

  The returned channels will be intercepted to include any needed metadata for
  communicating with the providers. Interfaces that share the exact same address
  and metadata headers will share the same underlying gRPC channel instance.

  Args:
    dep: The resolved dependency.
    ifaces: The interfaces to connect to.
    grpc_options: Additional options for the gRPC channels.

  Returns:
    A list of gRPC channels corresponding to `ifaces`.

  Raises:
    MissingInterfaceError: If any specified interface is not found in the
      resolved dependency.
    NotGRPCError: If any specified interface is not gRPC.
  """
  connections = [_get_grpc_connection(dep, iface) for iface in ifaces]
  channels_by_key: dict[
      tuple[str, tuple[tuple[str, str], ...]], grpc.Channel
  ] = {}
  channels: list[grpc.Channel] = []
  for iface, connection in zip(ifaces, connections):
    logging.info('Connecting to interface "%s"', iface)
    key = _connection_key(connection)
    if key not in channels_by_key:
      channels_by_key[key] = connect_module.connect(connection, grpc_options)
    channels.append(channels_by_key[key])
  return channels



def connect_with_runtime_asset_fallback_for_asset_migration_only(
    dep: resolved_dependency_pb2.ResolvedDependency,
    iface: str,
    grpc_options: Sequence[tuple[str, Any]] | None = None,
) -> grpc.Channel:
  """Creates a gRPC channel to the provider of the specified interface.

  If the interface or its connection details are missing from the resolved
  dependency, it falls back to using the runtime asset instance connection
  details.
  """
  return connect_many_with_runtime_asset_fallback_for_asset_migration_only(
      dep, [iface], grpc_options
  )[0]


def connect_many_with_runtime_asset_fallback_for_asset_migration_only(
    dep: resolved_dependency_pb2.ResolvedDependency,
    ifaces: Sequence[str],
    grpc_options: Sequence[tuple[str, Any]] | None = None,
) -> list[grpc.Channel]:
  """Creates gRPC channels to the providers of the specified interfaces.

  Interfaces that share the exact same address and metadata headers will share
  the same underlying gRPC channel instance.

  For any interface where the interface or its connection details are missing
  from the resolved dependency, it falls back to using the runtime asset
  instance connection details.
  """
  channels_by_key: dict[
      tuple[str, tuple[tuple[str, str], ...]], grpc.Channel
  ] = {}
  channels: list[grpc.Channel] = []
  for iface in ifaces:
    try:
      connection = _get_grpc_connection(dep, iface)
    except (MissingInterfaceError, NotGRPCError):
      connection = grpc_connection_pb2.GrpcConnection(
          address=_INGRESS_ADDRESS,
          metadata=[
              grpc_connection_pb2.GrpcConnection.Metadata(
                  key="x-resource-instance-name",
                  value=platform.RUNTIME_INSTANCE_NAME,
              )
          ],
      )
    logging.info('Connecting to interface "%s"', iface)
    key = _connection_key(connection)
    if key not in channels_by_key:
      channels_by_key[key] = connect_module.connect(connection, grpc_options)
    channels.append(channels_by_key[key])
  return channels





def get_data_payload(
    dep: resolved_dependency_pb2.ResolvedDependency,
    iface: str,
    data_assets_client: data_assets_pb2_grpc.DataAssetsStub | None = None,
) -> any_pb2.Any:
  """Returns the DataAsset payload for the specified interface.

  If no DataAssets client is provided, an insecure connection to the DataAssets
  service via the ingress gateway will be created. This connection is valid for
  services running in the same cluster as the DataAssets service.

  Args:
    dep: The resolved dependency.
    iface: The interface to get the data payload for.
    data_assets_client: The DataAssets client to use.

  Returns:
    The DataAsset payload for the specified interface.

  Raises:
    MissingInterfaceError: If the specified interface is not found in the
      resolved dependency.
    NotDataError: If the specified interface is not data or no data dependency
      information is available.
  """
  iface_proto = _find_interface(dep, iface)
  if not iface_proto.HasField("data"):
    raise NotDataError(
        "Interface is not data or no data dependency information is available:"
        f" {iface}"
    )

  channel = contextlib.nullcontext()
  if data_assets_client is None:
    data_assets_client, channel = _make_default_data_assets_client()

  with channel:
    da = data_assets_client.GetDataAsset(
        data_assets_pb2.GetDataAssetRequest(id=iface_proto.data.id)
    )

  return da.data


def _get_grpc_connection(
    dep: resolved_dependency_pb2.ResolvedDependency, iface: str
) -> grpc_connection_pb2.GrpcConnection:
  """Returns the GrpcConnection for the specified interface."""
  iface_proto = _find_interface(dep, iface)
  if not iface_proto.HasField("grpc") or not iface_proto.grpc.HasField(
      "connection"
  ):
    raise NotGRPCError(
        "Interface is not gRPC or no connection information is available:"
        f" {iface}"
    )
  return iface_proto.grpc.connection


def _connection_key(
    connection: grpc_connection_pb2.GrpcConnection,
) -> tuple[str, tuple[tuple[str, str], ...]]:
  """Returns a hashable key for deduplicating GrpcConnections."""
  # Python's `sorted` is guaranteed to be stable, so sorting by key alone makes
  # different keys order-independent while preserving the relative order of
  # duplicate keys (which is significant in gRPC/HTTP2).
  headers = sorted(
      ((m.key, m.value) for m in connection.metadata),
      key=lambda kv: kv[0],
  )
  return (connection.address, tuple(headers))


def _find_interface(
    dep: resolved_dependency_pb2.ResolvedDependency, iface: str
) -> resolved_dependency_pb2.ResolvedDependency.Interface:
  """Returns the interface for the specified interface."""
  if iface not in dep.interfaces:
    if not dep.interfaces:
      explanation = "no interfaces provided"
    else:
      keys = ", ".join(dep.interfaces.keys())
      explanation = f"got interfaces: {keys}"
    raise MissingInterfaceError(
        f"Interface not found in resolved dependency (want {iface},"
        f" {explanation})"
    )
  return dep.interfaces[iface]


def _make_default_data_assets_client() -> (
    tuple[data_assets_pb2_grpc.DataAssetsStub, grpc.Channel]
):
  """Creates an insecure channel to the DataAssets service via the ingress gateway."""
  channel = grpc.insecure_channel(_INGRESS_ADDRESS)
  return data_assets_pb2_grpc.DataAssetsStub(channel), channel


# LINT.ThenChange(
#  //intrinsic_sdk/intrinsic/assets/dependencies/utils.go:utils,
#  //intrinsic_sdk/intrinsic/assets/dependencies/utils.h:utils
# )
