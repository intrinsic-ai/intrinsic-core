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

"""Provides utilities for creating gRPC connections."""

from __future__ import annotations

import dataclasses
from typing import Optional


@dataclasses.dataclass
class ConnectionParams:
  """Specifies how Client should connect to a gRPC server.

  Servers running behind an ingress in a kubernetes cluster require the
  appropriate metadata information to be set.
  """

  address: str
  instance_name: Optional[str]
  header: Optional[str] = "x-resource-instance-name"

  @classmethod
  def no_ingress(cls, address: str) -> ConnectionParams:
    """Helper for connecting to an instance of a server not behind an ingress.

    Args:
      address: The full address, including port number, on which to connect.

    Returns:
      A ConnectionParams that can be used with Client.connect_with_params
    """
    return cls(address=address, instance_name=None, header=None)

  @classmethod
  def local_port(cls, port: int) -> ConnectionParams:
    """Helper for connecting to a local instance of a server on a specific port.

    This primarily should be used for testing purposes.  It will not specify
    information for ingress into a kubernetes cluster.

    Args:
      port: The port number on which to connect localhost.

    Returns:
      A ConnectionParams that can be used with Client.connect_with_params
    """
    return cls(address=f"localhost:{port}", instance_name=None, header=None)

  def headers(self) -> Optional[list[tuple[str, str]]]:
    """Generates the http headers needed to route to the appropriate ingress."""
    if not self.header or not self.instance_name:
      return None
    return [(self.header, self.instance_name)]
