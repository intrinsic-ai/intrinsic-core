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

"""Utility for getting extended status from a gRPC status."""

import grpc
from grpc_status import rpc_status

from intrinsic.util.status import extended_status_pb2


def get_extended_status_from_rpc_error(
    rpc_error: grpc.RpcError,
) -> extended_status_pb2.ExtendedStatus | None:
  """Extracts optional extended status from a gRPC status.

  Typical usage example:

    try:
      my_service_stub.MyCall(request)
    except grpc.RpcError as rpc_error:
      extended_status = get_extended_status_from_rpc_error(rpc_error)
      if extended_status is not None:
        # Do something with the extended status.
      else:
        # Do something with the regular grpc.RpcError.

  Args:
    rpc_error: The gRPC error that might contain an extended status.

  Returns:
    The extended status if rpc_error has one attached, otherwise None.

  Raises:
    RuntimeError: If a rpc status detail contains the ExstendedStatus message
      type but cannot be unpacked.
  """
  status = rpc_status.from_call(rpc_error)
  if status is None:
    return None
  for detail in status.details:
    if detail.Is(extended_status_pb2.ExtendedStatus.DESCRIPTOR):
      es = extended_status_pb2.ExtendedStatus()
      if not detail.Unpack(es):
        raise RuntimeError(
            f"Failed to unpack extended status from rpc status detail: {detail}"
        ) from rpc_error
      return es

  return None
