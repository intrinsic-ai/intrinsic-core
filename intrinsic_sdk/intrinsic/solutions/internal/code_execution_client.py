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

"""SBL specific clients for the code execution services."""

from __future__ import annotations

import grpc

from intrinsic.executive.proto import code_execution_info_service_pb2
from intrinsic.executive.proto import code_execution_info_service_pb2_grpc
from intrinsic.executive.proto import code_execution_pb2
from intrinsic.solutions import behavior_tree
from intrinsic.util.grpc import error_handling


class InfoClient:
  """Client for the CodeExecutionInfoService."""

  def __init__(
      self,
      stub: code_execution_info_service_pb2_grpc.CodeExecutionInfoServiceStub,
  ):
    """Constructs a new InfoClient object.

    Args:
      stub: The gRPC stub to be used for communication with the service.
    """
    self._stub = stub

  @classmethod
  def connect(cls, grpc_channel: grpc.Channel) -> InfoClient:
    """Connects to CodeExecutionInfoService using the given channel.

    Args:
      grpc_channel: Channel to the gRPC service.

    Returns:
      A newly created InfoClient instance.
    """
    stub = code_execution_info_service_pb2_grpc.CodeExecutionInfoServiceStub(
        grpc_channel
    )
    return cls(stub)

  @error_handling.retry_on_grpc_unavailable
  def preview_executed_code(
      self, script: behavior_tree.PythonScript | behavior_tree.Task
  ) -> str:
    """Returns the executed Python code for the given script or script node.

    This method is useful for checking whether the user-provided 'function_body'
    of the given Python script or script node is correct and can be executed. It
    queries the code execution backend for a preview of the actual Python code
    which will be executed for the Python script node in the code execution
    backend. This preview combines the users code (= the 'function_body' of the
    PythonScript node) with the actual code context added by the code execution
    backend. This context includes, e.g., a function header and imports for the
    proto module which defines the parameter and return value message (if the
    script has a non-empty signature).

    Args:
      script: A PythonScript or a Task containing a PythonScript.

    Returns:
      A preview of the Python code which will be executed in the code execution
      backend.

    Raises:
      TypeError: If script is not a PythonScript or Task.
      ValueError: If a Task is given and it does not contain a PythonScript.
    """
    if isinstance(script, behavior_tree.Task):
      if not isinstance(script.code_execution, behavior_tree.PythonScript):
        raise ValueError(
            f"Given Task node does not contain a PythonScript: {script}"
        )
      python_script = script.code_execution
    elif isinstance(script, behavior_tree.PythonScript):
      python_script = script
    else:
      raise TypeError(
          f"Expected a PythonScript or Task, got {type(script).__name__}"
      )

    sig = python_script.signature_with_args
    request = code_execution_info_service_pb2.GetPythonTemplateRequest(
        parameter_message_full_name=sig.parameter_message_full_name,
        return_value_message_full_name=sig.return_value_message_full_name,
        file_descriptor_set=sig.file_descriptor_set,
        python_code=code_execution_pb2.PythonCode(
            function_body=python_script.function_body
        ),
    )

    response = self._stub.GetPythonTemplate(request)
    return response.code_template
