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

"""Shared handlers for grpc errors."""

from collections.abc import Iterable
import functools
from typing import cast

from absl import logging
from google.protobuf import message as protobuf_message
from google.rpc import status_pb2
import grpc
import retrying

from intrinsic.util.status.get_extended_status import get_extended_status_from_rpc_error

# The Ingress will return UNIMPLEMENTED if the server it wants to forward to
# is unavailable, so we check for both UNAVAILABLE and UNIMPLEMENTED.
_UNAVAILABLE_CODES = [
    grpc.StatusCode.UNAVAILABLE,
    grpc.StatusCode.UNIMPLEMENTED,
]

# This key is taken from the grpc implementation and generates special behavior
# when sending it as trailing metadata.
_GRPC_DETAILS_METADATA_KEY = "grpc-status-details-bin"


def is_resource_exhausted_grpc_status(exception: Exception) -> bool:
  """Returns True if the given exception signals resource exhausted.

  Args:
    exception: The exception under evaluation.

  Returns:
    True if the given exception is a gRPC error that signals resource exhausted.
  """
  if isinstance(exception, grpc.Call):
    return (
        cast(grpc.Call, exception).code() == grpc.StatusCode.RESOURCE_EXHAUSTED
    )
  return False


def is_unavailable_grpc_status(exception: Exception) -> bool:
  """Returns True if the given exception signals temporary unavailability.

  Use to determine whether retrying a gRPC request might help.

  Args:
    exception: The exception under evaluation.

  Returns:
    True if the given exception is a gRPC error that signals temporary
    unavailability.
  """
  if isinstance(exception, grpc.Call):
    return cast(grpc.Call, exception).code() in _UNAVAILABLE_CODES
  return False


def make_grpc_status(
    code: grpc.StatusCode,
    message: str,
    details: Iterable[protobuf_message.Message],
) -> grpc.Status:
  """Generates a grpc status from the given data.

  Args:
    code: a grpc.StatusCode
    message: human readable error message
    details: iterable of additional information to include in the status.

  This function does some special packing of the information in a way that grpc
  recognizes, ensuring that all the data shows up on the other side of the call.

  Returns:
    a grpc.Status
  """
  my_status = status_pb2.Status(code=code.value[0], message=message)

  for msg in details:
    my_status.details.add().Pack(msg)

  grpc_status = grpc.Status()
  grpc_status.code = code
  grpc_status.details = message
  grpc_status.trailing_metadata = (
      (_GRPC_DETAILS_METADATA_KEY, my_status.SerializeToString()),
  )
  return grpc_status


def _is_resource_exhausted_grpc_status_with_logging(
    exception: Exception,
) -> bool:
  """Same as 'is_resource_exhausted_grpc_status' but also logs to the console."""
  is_resource_exhausted = is_resource_exhausted_grpc_status(exception)
  if is_resource_exhausted:
    print("Backend resource exhausted. Retrying ...")
  return is_resource_exhausted


def _is_unavailable_grpc_status_with_logging(exception: Exception) -> bool:
  """Same as 'is_unavailable_grpc_status' but also logs to the console."""
  is_unavailable = is_unavailable_grpc_status(exception)
  if is_unavailable:
    print(
        "Backend unavailable ("
        f"{cast(grpc.Call, exception).code()}). Retrying ..."
    )
  return is_unavailable


def _is_transient_error_grpc_status_with_logging(exception: Exception) -> bool:
  """Logs and returns true if the exception is a transient error."""
  return _is_unavailable_grpc_status_with_logging(
      exception
  ) or _is_resource_exhausted_grpc_status_with_logging(exception)


# Decorator that retries gRPC requests if the server is resource exhausted.
# The default policy here should happen with a larger delay than the default
# retry_on_grpc_unavailable policy to avoid spamming the backend.
retry_on_grpc_resource_exhausted = retrying.retry(
    retry_on_exception=_is_resource_exhausted_grpc_status_with_logging,
    stop_max_attempt_number=15,
    wait_exponential_multiplier=4,
    wait_exponential_max=20000,  # in milliseconds
    wait_incrementing_start=1000,  # in milliseconds
    wait_jitter_max=1000,  # in milliseconds
)


# Decorator that retries gRPC requests if the server is unavailable.
retry_on_grpc_unavailable = retrying.retry(
    retry_on_exception=_is_unavailable_grpc_status_with_logging,
    stop_max_attempt_number=15,
    wait_exponential_multiplier=3,
    wait_exponential_max=10000,  # in milliseconds
    wait_incrementing_start=500,  # in milliseconds
    wait_jitter_max=1000,  # in milliseconds
)

# Decorator that retries gRPC requests if the server is reporting a transient
# error.
# The default policy here should happen with a larger delay than the default
# retry_on_grpc_unavailable policy to avoid spamming the backend.
retry_on_grpc_transient_errors = retrying.retry(
    retry_on_exception=_is_transient_error_grpc_status_with_logging,
    stop_max_attempt_number=15,
    wait_exponential_multiplier=4,
    wait_exponential_max=20000,  # in milliseconds
    wait_incrementing_start=1000,  # in milliseconds
    wait_jitter_max=1000,  # in milliseconds
)


def log_extended_status(log_func=logging.info):
  """
  A decorator factory that intercepts gRPC errors that have an ExtendedStatus.

  Args:
      display_extended_status (callable): A function to handle the output
                                          of the extended status.
  """

  def decorator(func):
    @functools.wraps(func)
    def wrapper(*args, **kwargs):
      try:
        return func(*args, **kwargs)
      except grpc.RpcError as e:
        try:
          extended_status = get_extended_status_from_rpc_error(e)
          if extended_status:
            log_func(extended_status)
        except Exception as get_extended_status_error:
          logging.error(
              f"Failed to extract ExtendedStatus: {get_extended_status_error}"
          )

        # Always re-raise the original exception
        raise e

    return wrapper

  return decorator
