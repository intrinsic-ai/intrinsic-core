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

"""Provides access to resources."""

import re
from typing import Iterator
from typing import Union

from intrinsic.resources.client import resource_registry_client
from intrinsic.resources.proto import resource_handle_pb2
from intrinsic.solutions import provided
from intrinsic.solutions import providers

_REGEX_INVALID_PYTHON_VAR_CHARS = r"\W|^(?=\d)"


def resource_to_python_name(handle_name: str) -> str:
  # Replace characters which are not valid in Python names
  # This is to enable resources.ResourceName for any resource, in particular
  # ICON robot names contain "::".
  return re.sub(_REGEX_INVALID_PYTHON_VAR_CHARS, "_", handle_name)


class ResourceListImpl(provided.ResourceList):
  """List container for resource handles."""

  _handles: dict[str, provided.ResourceHandle]

  def __init__(self, handles: list[provided.ResourceHandle]):
    """Constructs a new instance.

    Args:
      handles: Initial resource handles to add.
    """
    self._handles = {}
    for h in handles:
      self.append(h)

  def append(self, handle: provided.ResourceHandle) -> None:
    self._handles[handle.name] = handle

    clean_name = resource_to_python_name(handle.name)
    if clean_name != handle.name:
      self._handles[clean_name] = handle

  def __getitem__(self, name: str) -> provided.ResourceHandle:
    if name not in self._handles:
      raise KeyError(f"Resource {name} not registered")
    return self._handles[name]

  def __setitem__(self, name: str, handle: provided.ResourceHandle) -> None:
    self._handles[name] = handle

  def __getattr__(self, name: str) -> provided.ResourceHandle:
    if name not in self._handles:
      raise AttributeError(f"Resource {name} not registered")
    return self._handles[name]

  def __dir__(self) -> list[str]:
    return sorted(
        filter(
            lambda x: not re.search(_REGEX_INVALID_PYTHON_VAR_CHARS, x),
            self._handles.keys(),
        )
    )

  def __len__(self) -> int:
    # self._handles may contain a resource twice if the name had to be
    # mangled to make it compatible with attr access. Therefore, call __dir__,
    # to get the unique elements and count them.
    return len(self.__dir__())

  def __iter__(self) -> Iterator[provided.ResourceHandle]:
    for name in dir(self):
      try:
        yield self.__getattr__(name)
      except AttributeError:
        continue

  def __str__(self) -> str:
    return f"ResourceList[{', '.join(self._handles.keys())}]"


class Resources(providers.ResourceProvider):
  """Wrapper to easily access resources from a solution."""

  _resource_registry: resource_registry_client.ResourceRegistryClient
  _resources: provided.ResourceList

  def __init__(
      self, resource_registry: resource_registry_client.ResourceRegistryClient
  ):
    """Constructs a new instance.

    Args:
      resource_registry: Resource registry client to fetch resources from.

    Raises:
      grpc.RpcError: If gRPC call to resource registry fails.
    """
    self._resource_registry = resource_registry
    self._resources = ResourceListImpl([])
    self.update()

  def update(self) -> None:
    self._resources = ResourceListImpl([])
    for handle in self._resource_registry.list_all_resource_handles():
      self._resources.append(provided.ResourceHandle(handle))

  def append(
      self,
      handle_or_proto: Union[
          provided.ResourceHandle, resource_handle_pb2.ResourceHandle
      ],
  ) -> None:
    handle: provided.ResourceHandle = None
    if isinstance(handle_or_proto, provided.ResourceHandle):
      handle = handle_or_proto
    elif isinstance(handle_or_proto, resource_handle_pb2.ResourceHandle):
      handle = provided.ResourceHandle(proto=handle_or_proto)
    else:
      raise TypeError("Invalid argument type")

    self._resources.append(handle)

  def __getitem__(self, name: str) -> provided.ResourceHandle:
    return self._resources[name]

  def __getattr__(self, name: str) -> provided.ResourceHandle:
    return getattr(self._resources, name)

  def __dir__(self) -> list[str]:
    return dir(self._resources)

  def __str__(self) -> str:
    return str(self._resources)
