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

"""Provide error handling utils for skill services/clients.

Skill service rpcs use a particular formatting of grpc.Status errors to pass
additional metadata. The calls below help translate to and from absl::Status.

See error_utils.h for more information.
"""

import grpc

from intrinsic.skills.proto import error_pb2
from intrinsic.util.grpc import error_handling

from pybind11_abseil import status  # isort: skip


# This key is taken from the grpc implementation and generates special behavior
# when sending it as trailing metadata.
_GRPC_DETAILS_METADATA_KEY = 'grpc-status-details-bin'



# TODO(keegang): figure out why the "StatusCode" typing doesn't prevent the
# passing of an integer literal (e.g. 5).

def make_grpc_status_with_error_info(
    code: status.StatusCode,
    message: str,
    skill_error_info: error_pb2.SkillErrorInfo,
) -> grpc.Status:
  """Generates a grpc status from the given data.

  This function does some special packing of the information in a way that grpc
  recognizes, ensuring that all the data shows up on the other side of the call.

  Args:
    code: status code as integer or equivalent
    message: human readable error message
    skill_error_info: information from the skill framework side

  Returns:
    a grpc.Status
  """

  # TODO(keegang): properly consolidate this against grpc_status.

  grpc_code = grpc.StatusCode.UNKNOWN
  for some_code in grpc.StatusCode:
    if some_code.value[0] == code.value:
      grpc_code = some_code

  return error_handling.make_grpc_status(
      code=grpc_code,
      message=message,
      details=[skill_error_info],
  )
