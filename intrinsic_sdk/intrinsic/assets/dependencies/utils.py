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

  Returns:
    A gRPC channel to the provider of the specified interface.

  Raises:
    MissingInterfaceError: If the specified interface is not found in the
      resolved dependency.
    NotGRPCError: If the specified interface is not gRPC.
  """
  iface_proto = _find_interface(dep, iface)
  if not iface_proto.HasField("grpc") or not iface_proto.grpc.HasField(
      "connection"
  ):
    raise NotGRPCError(
        "Interface is not gRPC or no connection information is available:"
        f" {iface}"
    )

  return connect_module.connect(iface_proto.grpc.connection, grpc_options)



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
  try:
    iface_proto = _find_interface(dep, iface)
    if iface_proto.HasField("grpc") and iface_proto.grpc.HasField("connection"):
      return connect(dep, iface, grpc_options)
  except MissingInterfaceError:
    pass

  cloned = resolved_dependency_pb2.ResolvedDependency()
  cloned.CopyFrom(dep)
  cloned.name = platform.RUNTIME_INSTANCE_NAME
  connection = cloned.interfaces[iface].grpc.connection
  connection.address = _INGRESS_ADDRESS
  del connection.metadata[:]
  meta = connection.metadata.add()
  meta.key = "x-resource-instance-name"
  meta.value = platform.RUNTIME_INSTANCE_NAME
  return connect(cloned, iface, grpc_options)





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
