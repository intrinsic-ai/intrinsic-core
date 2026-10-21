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

"""Lightweight Python wrapper around the resource registry."""

from __future__ import annotations

from typing import Optional

import grpc

from intrinsic.resources.proto import resource_handle_pb2
from intrinsic.resources.proto import resource_registry_pb2
from intrinsic.resources.proto import resource_registry_pb2_grpc
from intrinsic.util.grpc import error_handling

_RESOURCE_REGISTRY_MAX_PAGE_SIZE = 200


class ResourceRegistryClient:
  """Client library for the resource registry gRPC service."""

  _stub: resource_registry_pb2_grpc.ResourceRegistryStub

  def __init__(self, stub: resource_registry_pb2_grpc.ResourceRegistryStub):
    """Constructs a new ResourceRegistryClient object.

    Args:
      stub: The gRPC stub to be used for communication with the resource
        registry service.
    """
    self._stub = stub

  @classmethod
  def connect(cls, grpc_channel: grpc.Channel) -> ResourceRegistryClient:
    """Connects to a running resource registry.

    Args:
      grpc_channel: Channel to the resource registry gRPC service.

    Returns:
      A newly created instance of the ResourceRegistryClient class.
    """
    stub = resource_registry_pb2_grpc.ResourceRegistryStub(grpc_channel)
    return cls(stub)

  @error_handling.retry_on_grpc_unavailable
  def get_resource_instance(
      self, name: str
  ) -> resource_registry_pb2.ResourceInstance:
    """Returns the resource instance with the given name.

    Args:
      name: Name of the resource instance to return.

    Returns:
      The requested resource instance.
    """
    return self._stub.GetResourceInstance(
        resource_registry_pb2.GetResourceInstanceRequest(name=name)
    )

  def list_all_resource_instances(
      self, *, resource_family_id: Optional[str] = None
  ) -> list[resource_registry_pb2.ResourceInstance]:
    """Retrieves all available resource instances.

    Use with caution: This method retrieves all pages of relevant resource
    instances from the backend and blocks until all results have been retrieved.

    Args:
      resource_family_id: Query and return only resource instances with this
        family id.

    Returns:
      List of resource handles.
    """
    base_request = resource_registry_pb2.ListResourceInstanceRequest()
    if resource_family_id is not None:
      base_request.strict_filter.resource_family_id = resource_family_id

    return self._list_all_resource_instances(base_request)

  def list_all_resource_handles(
      self, *, capability_names: Optional[list[str]] = None
  ) -> list[resource_handle_pb2.ResourceHandle]:
    """Retrieves all handles of all resource instances which have handles.

    Use with caution: This method retrieves all pages of relevant resource
    instances from the backend and blocks until all results have been retrieved.

    Args:
      capability_names: Required capability names. Only handles having (at
        least) all of these capabilities will be returned.

    Returns:
      List of resource handles.
    """
    base_request = resource_registry_pb2.ListResourceInstanceRequest()
    if capability_names is not None:
      base_request.strict_filter.capability_names.extend(capability_names)

    all_instances = self._list_all_resource_instances(base_request)

    return [
        instance.resource_handle
        for instance in all_instances
        if instance.HasField('resource_handle')
    ]

  def batch_list_all_resource_handles(
      self, *, capability_names_batch: list[list[str]]
  ) -> list[list[resource_handle_pb2.ResourceHandle]]:
    """Retrieves all handles for each of the given capability requirements.

    Use with caution: This method retrieves all pages of relevant resource
    instances from the backend and blocks until all results have been retrieved.

    Args:
      capability_names_batch: Required capability names. For each of the given
        capability lists, the result will contain one list of handles which have
        (at least) all of these capabilities.

    Returns:
      One list of resource handles for every given list of capability names.
    """
    if not capability_names_batch:
      return []

    # The resource registry only supports filtering for one set of capabilities
    # at a time. To avoid sending many requests, we get all handles (unfiltered)
    # and then do post-filtering below.
    all_handles = self.list_all_resource_handles()

    result: list[list[resource_handle_pb2.ResourceHandle]] = []
    for required_capability_names in capability_names_batch:
      matching_handles = [
          handle
          for handle in all_handles
          if set(required_capability_names).issubset(handle.resource_data)
      ]
      result.append(matching_handles)

    return result

  def _list_all_resource_instances(
      self,
      base_request: resource_registry_pb2.ListResourceInstanceRequest = (
          resource_registry_pb2.ListResourceInstanceRequest()
      ),
  ) -> list[resource_registry_pb2.ResourceInstance]:
    """Retrieves all resource instances by querying all response pages."""
    request = resource_registry_pb2.ListResourceInstanceRequest()
    request.CopyFrom(base_request)
    request.page_size = _RESOURCE_REGISTRY_MAX_PAGE_SIZE

    result: list[resource_registry_pb2.ResourceInstance] = []
    while True:
      response = self._call_list_resource_instances(request)
      result.extend(response.instances)

      if response.next_page_token:
        request.page_token = response.next_page_token
      else:
        return result

  @error_handling.retry_on_grpc_unavailable
  def _call_list_resource_instances(
      self, request: resource_registry_pb2.ListResourceInstanceRequest
  ) -> list[resource_registry_pb2.ResourceInstance]:
    return self._stub.ListResourceInstances(request)
