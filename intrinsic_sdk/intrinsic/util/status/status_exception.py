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

"""Exception implementation with additional extended status."""

from __future__ import annotations

from collections.abc import Sequence
import datetime
import logging
import textwrap
import traceback
from typing import Optional
from typing import Union

from google.protobuf import message as proto_message
from google.rpc import code_pb2
from google.rpc import status_pb2
import grpc

from intrinsic.logging.proto import context_pb2
from intrinsic.util.grpc import limits as grpc_limits
from intrinsic.util.status import extended_status_pb2

# This key is taken from the grpc implementation and generates special behavior
# when sending it as trailing metadata.
_GRPC_DETAILS_METADATA_KEY = "grpc-status-details-bin"


class ExtendedStatusError(Exception, grpc.Status):
  """Class that represents an error with extended status information.

  The class uses the builder pattern, so you can parameterize and raise the
  exception like this:

  raise status_exception.ExtendedStatusError("ai.intrinsic.my_skill", 24543)
    .set_title("Failed to do fancy thing")

  Attributes:
    proto: proto representing the extended state
  """

  _extended_status: extended_status_pb2.ExtendedStatus
  _emit_traceback: bool
  _rpc_details: list[proto_message.Message]
  _grpc_code: grpc.StatusCode | None

  def __init__(
      self,
      component: str,
      code: int,
      *,
      title: str = "",
      user_message: str = "",
      debug_message: str = "",
      timestamp: datetime.datetime | None = None,
      grpc_code: grpc.StatusCode | None = None,
  ):
    """Initializes the instance.

    Args:
      component: Component for StatusCode where error originated.
      code: Numeric code specific to component for StatusCode.
      title: brief title of the error, make this meaningful and keep to a length
        of 75 characters if possible.
      user_message: if non-empty, set extended status user report message to
        this string.
      debug_message: if non-empty, set extended status debug report message to
        this string. Only set this in an environment where the data may be
        shared.
      timestamp: The time of the error. If None sets current time.
      grpc_code: Optional gRPC status code, you may set this to the desired
        general error code when using the instance as a gRPC status.
    """
    self._extended_status = extended_status_pb2.ExtendedStatus(
        status_code=extended_status_pb2.StatusCode(
            component=component, code=code
        )
    )
    self._emit_traceback = False
    self._rpc_details = []
    self._grpc_code = grpc_code
    if title:
      self.set_title(title)
    if user_message:
      self.set_user_message(user_message)
    if debug_message:
      self.set_debug_message(debug_message)
    self.set_timestamp(timestamp or datetime.datetime.now())
    super().__init__(user_message)

  @classmethod
  def create_from_proto(
      cls, proto: extended_status_pb2.ExtendedStatus
  ) -> ExtendedStatusError:
    if proto.HasField("status_code"):
      es_err = cls(proto.status_code.component, proto.status_code.code)
    else:
      # Bad, this shouldn't happen but we cannot do better here
      es_err = cls("", 0)
    es_err._extended_status = proto
    return es_err

  @property
  def proto(self) -> extended_status_pb2.ExtendedStatus:
    """Retrieves extended status encoded as ExtendedStatus proto."""
    if self._emit_traceback:
      self._extended_status.debug_report.stack_trace = "".join(
          traceback.format_exception(self)
      )

    return self._extended_status

  def set_extended_status(
      self, extended_status: extended_status_pb2.ExtendedStatus
  ) -> ExtendedStatusError:
    """Sets extended status directly from a proto."""
    # We do not allow overwriting the status code once set
    status_code = extended_status_pb2.StatusCode(
        component=self._extended_status.status_code.component,
        code=self._extended_status.status_code.code,
    )
    self._extended_status.CopyFrom(extended_status)
    self._extended_status.status_code.CopyFrom(status_code)
    return self

  def set_timestamp(
      self, timestamp: datetime.datetime | None = None
  ) -> ExtendedStatusError:
    """Sets time of error.

    Args:
      timestamp: The time of the error. If None sets current time.

    Returns:
      self
    """
    self._extended_status.timestamp.FromDatetime(
        timestamp or datetime.datetime.now()
    )
    return self

  @property
  def status_code(self) -> tuple[str, int]:
    if self._extended_status.HasField("status_code"):
      return (
          self._extended_status.status_code.component,
          self._extended_status.status_code.code,
      )
    return ("", 0)

  @property
  def timestamp(self) -> datetime.datetime:
    return self._extended_status.timestamp.ToDatetime()

  def set_title(self, title: str) -> ExtendedStatusError:
    """Sets title of error.

    Args:
      title: title string for the error

    Returns:
      self
    """
    self._extended_status.title = title
    return self

  @property
  def title(self) -> str:
    return self._extended_status.title

  def add_context(
      self, context_status: extended_status_pb2.ExtendedStatus
  ) -> ExtendedStatusError:
    """Adds context status.

    Args:
      context_status: another extended status that helps explain or further
        analyze this error.

    Returns:
      self
    """
    self._extended_status.context.append(context_status)
    return self

  def set_debug_message(self, message: str) -> ExtendedStatusError:
    """Sets debug report message.

    Call this function only if you intend for the receiver of the status to see
    it. An indicator could be running in the context of specific organizations.

    Args:
      message: human-readable debug message intended for developers

    Returns:
      self
    """
    self._extended_status.debug_report.message = message
    return self

  @property
  def debug_report(
      self,
  ) -> extended_status_pb2.ExtendedStatus.DebugReport | None:
    return self._extended_status.debug_report

  def set_user_message(self, message: str) -> ExtendedStatusError:
    """Sets user report message.

    This is the main error message and should almost always be set.

    Args:
      message: human-readable error message intended for users of the component.

    Returns:
      self
    """
    self._extended_status.user_report.message = message
    return self

  @property
  def user_report(self) -> extended_status_pb2.ExtendedStatus.UserReport | None:
    return self._extended_status.user_report

  def set_log_context(
      self, context: context_pb2.Context
  ) -> ExtendedStatusError:
    """Sets logging context.

    Note that this is specific to the structured data logger. This is NOT
    other extended status as context (use add_context() for those).

    Args:
      context: log context

    Returns:
      self
    """
    self._extended_status.related_to.log_context.CopyFrom(context)
    return self

  def emit_traceback_to_debug_report(self) -> ExtendedStatusError:
    """Enables emitting a traceback to debug report when proto is retrieved."""
    # We cannot directly add the traceback, as that is only created when the
    # error is raised (which also performs useful depth limitations on the
    # traceback).
    self._emit_traceback = True
    return self

  def __str__(self) -> str:
    """Converts the error to a string.

    Note that this does not include a traceback, even if enabled via emit. This
    is primarily because it would cause an infinite loop (traceback generation
    converts encountered exceptions to a string), and it may not always be set,
    e.g., if the raiser of the exception prints this for debugging.

    Returns:
      string representation of error.
    """
    strs: list[str] = []
    status_code = self._extended_status.status_code
    strs.append(f"StatusCode: {status_code.component}:{status_code.code}\n")
    if self._extended_status.HasField("timestamp"):
      strs.append(
          "Timestamp: "
          f" {self._extended_status.timestamp.ToDatetime().strftime('%c')}\n"
      )
    if self._extended_status.HasField("user_report"):
      strs.append(
          "User"
          f" Report:\n{textwrap.indent(self._extended_status.user_report.message, '  ')}\n"
      )
    if self._extended_status.HasField("debug_report"):
      strs.append(
          "Debug"
          f" Report:\n{textwrap.indent(self._extended_status.debug_report.message, '  ')}\n"
      )

    if self._extended_status.context:
      strs.append("\nContext\n")
      for i, context in enumerate(self._extended_status.context):
        strs.append(f"***** Context {i} *****\n")
        context_es = ExtendedStatusError.create_from_proto(context)
        strs.append(f"{context_es}\n")
      strs.append("\n")

    return "".join(strs)

  ### The following are for compatibility with grpc.Status

  def add_rpc_detail(self, msg: proto_message.Message) -> ExtendedStatusError:
    """Adds additional detail to be returned for grpc.Status interface.

    When used as a grpc.Status, the Extended status is added to the
    google.rpc.Status details. This function can be used to add additional other
    additional details to the rpc status.

    Args:
      msg: message to add to the google.rpc.Status details Any proto list.

    Returns:
      self
    """
    self._rpc_details.append(msg)
    return self

  @property
  def code(self) -> grpc.StatusCode:
    """Returns GRPC status code UNKNOWN.

    Only added to comply with grpc.Status interface.
    """
    if self._grpc_code is not None:
      return self._grpc_code

    code = self._extended_status.status_code.code
    for status_code in grpc.StatusCode:
      if status_code.value[0] == code:
        return status_code

    return grpc.StatusCode.UNKNOWN

  @property
  def details(self) -> str:
    """Returns the title as GRPC status message.

    Only added to comply with grpc.Status interface.
    """
    return self._extended_status.title

  @property
  def trailing_metadata(
      self,
  ) -> Optional[Sequence[tuple[str, Union[str, bytes]]]]:
    """Returns GRPC trailing metadata encoding this extended status.

    Only added to comply with grpc.Status interface.
    """
    rpc_status = status_pb2.Status(
        code=code_pb2.Code.Value(self.code.name),
        message=self._extended_status.title,
    )
    rpc_status.details.add().Pack(self._extended_status)
    for detail in self._rpc_details:
      rpc_status.details.add().Pack(detail)
    data = rpc_status.SerializeToString()
    if len(data) > grpc_limits.GRPC_RECOMMENDED_MAX_METADATA_HARD_LIMIT:
      logging.warning(
          "Status as trailing metadata is larger than recommended metadata"
          " hard limit (%d > %d). Will very likely result in error.",
          len(data),
          grpc_limits.GRPC_RECOMMENDED_MAX_METADATA_HARD_LIMIT,
      )
    elif len(data) > grpc_limits.GRPC_RECOMMENDED_MAX_METADATA_SOFT_LIMIT:
      logging.warning(
          "Status as trailing metadata is larger than recommended metadata"
          " soft limit (%d > %d). Will likely result in error.",
          len(data),
          grpc_limits.GRPC_RECOMMENDED_MAX_METADATA_SOFT_LIMIT,
      )
    elif len(data) > (grpc_limits.GRPC_RECOMMENDED_MAX_METADATA_SOFT_LIMIT / 2):
      logging.warning(
          "Status as trailing metadata is larger than half the recommended"
          " metadata soft limit (%d > %d). Will possibly result in error.",
          len(data),
          grpc_limits.GRPC_RECOMMENDED_MAX_METADATA_SOFT_LIMIT / 2,
      )

    return ((_GRPC_DETAILS_METADATA_KEY, data),)
