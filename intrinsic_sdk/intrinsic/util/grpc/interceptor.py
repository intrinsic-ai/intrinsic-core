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

"""Helper Classes for creating gRPC interceptors."""

from collections.abc import Callable
from collections.abc import Container
from collections.abc import Sequence
import dataclasses
from typing import Optional

import grpc

from intrinsic.util.decorators import overrides


@dataclasses.dataclass(frozen=True)
class ClientCallDetails(grpc.ClientCallDetails):
  """Wrapper class for initializing a new ClientCallDetails instance.

  Experimental options are omitted.

  See:
  https://grpc.github.io/grpc/python/grpc.html#client-side-interceptor

  Attributes:
    method: The method name of the RPC.
    timeout: An optional duration of time in seconds to allow for the RPC.
    metadata: Optional :term:`metadata` to be transmitted to the service-side of
      the RPC.
    credentials: An optional CallCredentials for the RPC.
    wait_for_ready: This is an EXPERIMENTAL argument. An optional flag to enable
      :term:`wait_for_ready` mechanism.
  """

  method: str
  timeout: Optional[float]
  metadata: Optional[Sequence[tuple[str, str]]]
  credentials: Optional[grpc.CallCredentials]
  wait_for_ready: Optional[bool]


@dataclasses.dataclass
class ClientCallDetailsInterceptor(
    grpc.UnaryUnaryClientInterceptor,
    grpc.UnaryStreamClientInterceptor,
    grpc.StreamUnaryClientInterceptor,
    grpc.StreamStreamClientInterceptor,
):
  """Generic Client Interceptor that modifies the ClientCallDetails."""

  _fn: Callable[[ClientCallDetails], ClientCallDetails]

  def intercept_call(
      self,
      continuation: ...,
      client_call_details: ClientCallDetails,
      request: ...,
  ) -> ...:
    """Intercepts a RPC.

    Args:
      continuation: A function that proceeds with the invocation by executing
        the next interceptor in chain or invoking the actual RPC on the
        underlying Channel. It is the interceptor's responsibility to call it if
        it decides to move the RPC forward. The interceptor can use
        `response_future = continuation(client_call_details, request)` to
        continue with the RPC.
      client_call_details: A ClientCallDetails object describing the outgoing
        RPC.
      request: The request value for the RPC.

    Returns:
        If the response is unary:
          An object that is both a Call for the RPC and a Future.
          In the event of RPC completion, the return Call-Future's
          result value will be the response message of the RPC.
          Should the event terminate with non-OK status, the returned
          Call-Future's exception value will be an RpcError.

        If the response is streaming:
          An object that is both a Call for the RPC and an iterator of
          response values. Drawing response values from the returned
          Call-iterator may raise RpcError indicating termination of
          the RPC with non-OK status.
    """

    new_details = self._fn(client_call_details)
    return continuation(new_details, request)

  @overrides(grpc.UnaryUnaryClientInterceptor)
  def intercept_unary_unary(
      self,
      continuation: ...,
      client_call_details: ClientCallDetails,
      request: ...,
  ) -> ...:
    """Intercepts a unary-unary invocation asynchronously."""
    return self.intercept_call(continuation, client_call_details, request)

  @overrides(grpc.UnaryStreamClientInterceptor)
  def intercept_unary_stream(
      self,
      continuation: ...,
      client_call_details: ClientCallDetails,
      request: ...,
  ) -> ...:
    """Intercepts a unary-stream invocation."""
    return self.intercept_call(continuation, client_call_details, request)

  @overrides(grpc.StreamUnaryClientInterceptor)
  def intercept_stream_unary(
      self,
      continuation: ...,
      client_call_details: ClientCallDetails,
      request_iterator: ...,
  ) -> ...:
    """Intercepts a stream-unary invocation asynchronously."""
    return self.intercept_call(
        continuation, client_call_details, request_iterator
    )

  @overrides(grpc.StreamStreamClientInterceptor)
  def intercept_stream_stream(
      self,
      continuation: ...,
      client_call_details: ClientCallDetails,
      request_iterator: ...,
  ) -> ...:
    """Intercepts a stream-stream invocation."""
    return self.intercept_call(
        continuation, client_call_details, request_iterator
    )


def _AddHeaders(
    headers_func: Callable[[], Sequence[tuple[str, str]]],
) -> Callable[[ClientCallDetails], ClientCallDetails]:
  """Returns a function that adds headers to client call details."""

  def AddHeaders(client_call_details: ClientCallDetails) -> ClientCallDetails:
    headers = headers_func()
    if not headers:
      return client_call_details

    metadata = []
    if client_call_details.metadata is not None:
      metadata = list(client_call_details.metadata)

    for header, value in headers:
      metadata.append((header.lower(), value))

    return ClientCallDetails(
        client_call_details.method,
        client_call_details.timeout,
        metadata,
        client_call_details.credentials,
        client_call_details.wait_for_ready,
    )

  return AddHeaders


def HeaderAdderInterceptor(
    headers_func: Callable[[], Sequence[tuple[str, str]]],
) -> ClientCallDetailsInterceptor:
  """Returns an interceptor that adds headers generated lazily by header_func.

  Args:
    headers_func: a function that generates a list of headers.  This is
      evaluated for each call.  The call is not modified if this returns none or
      an empty list.
  """
  return ClientCallDetailsInterceptor(_AddHeaders(headers_func))


class RequiredMetadataInterceptor(grpc.ServerInterceptor):
  """Rejects requests from server without the required meta data."""

  def __init__(self, required_metadata: tuple[str, str]):
    self._required_metadata = required_metadata

  def _has_required_metadata(
      self, metadata: Container[tuple[str, str]]
  ) -> bool:
    return self._required_metadata in metadata

  @overrides(grpc.ServerInterceptor)
  def intercept_service(
      self,
      continuation: Callable[[grpc.HandlerCallDetails], grpc.RpcMethodHandler],
      handler_call_details: grpc.HandlerCallDetails,
  ) -> Optional[grpc.RpcMethodHandler]:
    if self._has_required_metadata(handler_call_details.invocation_metadata):  # pytype: disable=attribute-error
      return continuation(handler_call_details)
    return None
