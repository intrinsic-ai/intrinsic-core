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

"""Extended Status Specs API.

Extended Status are a way to provide error information in the system and to a
user eventually. The API in this file enables to store declaration and static
information of errors in a proto file. The proto file must contain a message of
type intrinsic_proto.assets.StatusSpecs (alternatively, this can also be
provided as a static list once during initialization).

General use is to call init_once() once per process. After that, create() can be
used to create errors easily. Referencing the respective error code, static
information such as title and recovery instructions are read from the
file. Errors which are not declared will raise a warning. The proto file can
also be used to generate documentation.

This API is intended to be used for a specific component which is run as a
process, it is not to be used in API libraries.
"""

from collections.abc import Mapping
import datetime

from google.protobuf import timestamp_pb2
import grpc

from intrinsic.assets.proto import status_spec_pb2
from intrinsic.util.status import extended_status_pb2
from intrinsic.util.status import status_exception

# Enables pre-seeding of extended status information from a proto file
_status_specs: Mapping[int, status_spec_pb2.StatusSpec] = {}
_component: str = ""


def _create_undeclared_warning(
    component: str, code: int
) -> extended_status_pb2.ExtendedStatus:
  """Creates an ExtendedStatus for an undeclared error code.

  Args:
    component: component of the missing error code
    code: numeric part of missing error code

  Returns:
    ExtendedStatus with a general warning about the missing error code.
  """
  return extended_status_pb2.ExtendedStatus(
      status_code=extended_status_pb2.StatusCode(
          component="ai.intrinsic.errors", code=604
      ),
      severity=extended_status_pb2.ExtendedStatus.Severity.WARNING,
      title="Error code not declared",
      timestamp=timestamp_pb2.Timestamp().GetCurrentTime(),
      user_report=extended_status_pb2.ExtendedStatus.UserReport(
          message=(
              f"The code {component}:{code} has not been declared by the"
              " component."
          ),
          instructions=(
              "Inform the component owner to add the missing error code to"
              " the status specs file."
          ),
      ),
  )


def init_once(
    component: str,
    *,
    filename: str | None = None,
    status_specs: list[status_spec_pb2.StatusSpec] | None = None,
) -> None:
  """Initializes extended status specs.

  Information can be loaded from a file or passed as a list of status specs
  directly. Typically, a file is easier to maintain.

  Args:
    component: Component to set on extended status in the status code field.
    filename: Name of file to read status specs from.
    status_specs: Status specs to directly inject.

  Raises:
    RuntimeError: if neither filename nor status_specs is provided.
  """
  global _status_specs
  global _component

  if filename is None and status_specs is None:
    raise RuntimeError(
        "You need to specify at least one of filename and status_specs in"
        " status_specs.init_once()."
    )

  _status_specs = {}
  _component = component

  if filename is not None:
    with open(filename, "rb") as fp:
      m = status_spec_pb2.StatusSpecs.FromString(fp.read())
    for spec in m.status_info:
      _status_specs[spec.code] = spec

  if status_specs is not None:
    _status_specs = _status_specs | {s.code: s for s in status_specs}


def create(
    code: int,
    user_message: str,
    *,
    debug_message: str = "",
    timestamp: datetime.datetime | None = None,
    grpc_code: grpc.StatusCode | None = None,
) -> status_exception.ExtendedStatusError:
  """Creates an extended status error from specs for the given code.

  This will retrieve information from the status specs when available. Provide
  a user report message with as much detail as useful to a user, e.g., add
  specific values (example: "value 34 is too large, must be smaller than 20",
  where 34 and 20 would be values that occurred during execution). Typical would
  be a format string using format-string. You can provide additional options
  with additional information, in particular context (upstream errors in the
  form of ExtendedStatus protos).

  If no information is available in the specs file, this will still emit the
  extended status with the given information, but also add a context extended
  status with a warning that the declaration is missing.

  Args:
    code: Numeric error code to reference information in the spec file.
    user_message: Information for the user to analyze the error. Be as specific
      as possible, typically this would be a formatted string.
    debug_message: An optional message to put in the debug report.
    timestamp: A timestamp to associate with the extended status, defaults to
      the current time. Set this if a more precise time is available and could
      help during debugging, e.g., for data association.
    grpc_code: Generic gRPC code to associate with this error. You may use this
      in the context of a gRPC service implementation to set a specific
      categorical error code.

  Returns:
    An extended status error prefilled with information from the specs and
    provided as arguments to the function. If no information can be found in the
    specs for the given code adds a context warning stating so.
  """
  ese = status_exception.ExtendedStatusError(
      _component,
      code,
      user_message=user_message,
      debug_message=debug_message,
      timestamp=timestamp,
      grpc_code=grpc_code,
  )

  if code in _status_specs:
    spec = _status_specs[code]

    ese.set_title(spec.title)
    if spec.recovery_instructions:
      ese.user_report.instructions = spec.recovery_instructions

  else:
    ese.set_title(f"Undeclared error {_component}:{code}")
    ese.add_context(_create_undeclared_warning(_component, code))

  return ese
