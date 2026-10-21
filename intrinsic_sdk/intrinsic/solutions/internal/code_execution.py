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

"""Python wrappers around code execution."""

from __future__ import annotations

import abc
import inspect
import io
import textwrap
import tokenize
from typing import Any
from typing import Callable
import uuid

from intrinsic.executive.proto import code_execution_pb2
from intrinsic.solutions import blackboard_value
from intrinsic.solutions import proto_building
from intrinsic.solutions.internal import skill_utils

_DEFAULT_SCRIPT_NODE_PROTO_FILE = "node.proto"
# Expected to be filled with a UUID.
_DEFAULT_RETURN_VALUE_KEY_PATTERN = "pynode_%s"


class CodeExecution(abc.ABC):
  """Corresponds to a CodeExecution proto."""

  @property
  @abc.abstractmethod
  def proto(self) -> code_execution_pb2.CodeExecution:
    """Returns the proto representation."""

  @property
  @abc.abstractmethod
  def result(self) -> blackboard_value.BlackboardValue | None:
    """Returns a reference to the return value of the code execution."""


class PythonScript(CodeExecution):
  """Represents a Python script node in a behavior tree."""

  _signature_with_args: proto_building.SignatureWithArgs
  _function_body: str
  _return_value_key: str

  def __init__(
      self,
      signature_with_args: proto_building.SignatureWithArgs | None = None,
      *,
      function_body: str,
      return_value_key: str | None = None,
  ):
    """Creates a PythonScript.

    The given code to be executed ('function_body') is the body of a function
    and must not contain a function header. For inputs and outputs, it can
    assume a proto module corresponding to the "main proto file" of the given
    signature (the file in which parameter and return value message are defined)
    to be already imported. For example, if the signature was created with the
    help of `solution.proto_builder`, the code can assume `node_pb2` to be
    defined. The code has to fit into a template similar to the following:

    ```
    import numpy as np
    from intrinsic.skills.python import basic_compute_context
    from <node_pb2 parent module> import node_pb2

    def compute(
        params: node_pb2.Params,
        context: basic_compute_context.BasicComputeContext,
    ) -> node_pb2.ReturnValue:
      {function_body}
    ```

    Several details in the template depend on the given signature. You can get a
    preview of the exact code that will be executed for a PythonScript by
    querying the code execution backend, e.g.:

    ```
    # Pass a placeholder function body if necessary
    script = bt.PythonScript(signature_with_args=..., function_body="pass")
    print(solution.code_execution_info.preview_executed_code(script))
    ```

    Args:
      signature_with_args: Signature and arguments for the script node. If not
        set, an empty signature will be created (=no parameter and return value
        message).
      function_body: Function body of the Python code to execute. Can be passed
        with arbitrary indentation (as long at it is consistent for all lines).
      return_value_key: Optional blackboard key under which to store the return
        value. If not provided, a unique key will be generated if the signature
        defines a return value message.
    """
    if signature_with_args is None:
      signature_with_args = proto_building.Signature().with_args()

    self._signature_with_args = signature_with_args

    if not function_body.strip():
      raise ValueError("function_body must not be empty")
    # Normalize indentation. The code execution service expects indented code.
    self._function_body = textwrap.indent(
        textwrap.dedent(function_body).strip(), "  "
    )

    if self._signature_with_args.return_value_message_full_name:
      if return_value_key is None:
        self._return_value_key = (
            _DEFAULT_RETURN_VALUE_KEY_PATTERN % uuid.uuid4().hex
        )
      else:
        if not return_value_key:
          raise ValueError("return_value_key must not be empty")
        self._return_value_key = return_value_key
    else:
      if return_value_key is not None:
        raise ValueError(
            "return_value_key provided but signature does not define return"
            " value"
        )
      self._return_value_key = ""

  @property
  def signature_with_args(self) -> proto_building.SignatureWithArgs:
    """Returns the SignatureWithArgs object for this Python script node."""
    return self._signature_with_args

  @property
  def function_body(self) -> str:
    """Returns the function body string for this Python script node."""
    return self._function_body

  @property
  def proto(self) -> code_execution_pb2.CodeExecution:
    result = code_execution_pb2.CodeExecution(
        python_code=code_execution_pb2.PythonCode(
            function_body=self._function_body
        ),
        return_value_key=self._return_value_key,
        parameter_message_full_name=(
            self._signature_with_args.parameter_message_full_name
        ),
        return_value_message_full_name=(
            self._signature_with_args.return_value_message_full_name
        ),
    )

    if self._signature_with_args.file_descriptor_set.file:
      result.file_descriptor_set.CopyFrom(
          self._signature_with_args.file_descriptor_set
      )

    if self._signature_with_args.params_message is not None:
      result.parameters.proto.Pack(self._signature_with_args.params_message)

    for (
        path,
        cel_expression,
    ) in self._signature_with_args.blackboard_params.items():
      assignment = result.parameters.assign.add()
      assignment.path = path
      assignment.cel_expression = cel_expression

    return result

  @property
  def result(self) -> blackboard_value.BlackboardValue | None:
    if not self._signature_with_args.return_value_message_full_name:
      return None

    msg = skill_utils.create_message_from_file_descriptor_set(
        self._signature_with_args.file_descriptor_set,
        self._signature_with_args.return_value_message_full_name,
    )
    return blackboard_value.BlackboardValue(
        msg.DESCRIPTOR.fields_by_name,
        self._return_value_key,
        type(msg),
        None,
    )

  @classmethod
  def _create_from_proto(
      cls, proto_object: code_execution_pb2.CodeExecution
  ) -> PythonScript:
    """Creates a PythonScript instance from a proto."""
    # Reconstruct Signature
    signature = proto_building.Signature(
        parameter_message_full_name=proto_object.parameter_message_full_name,
        return_value_message_full_name=proto_object.return_value_message_full_name,
        file_descriptor_set=proto_object.file_descriptor_set,
    )

    # Reconstruct SignatureWithArgs
    if proto_object.parameter_message_full_name:
      params_message = skill_utils.create_message_from_file_descriptor_set(
          proto_object.file_descriptor_set,
          proto_object.parameter_message_full_name,
      )
      if proto_object.parameters.proto.type_url:
        proto_object.parameters.proto.Unpack(params_message)

      signature_with_args = proto_building.SignatureWithArgs(
          signature=signature,
          params_message=params_message,
          blackboard_params={
              assignment.path: assignment.cel_expression
              for assignment in proto_object.parameters.assign
          },
      )
    else:
      signature_with_args = signature.with_args()

    return_value_key = (
        proto_object.return_value_key if proto_object.return_value_key else None
    )

    return cls(
        signature_with_args=signature_with_args,
        function_body=proto_object.python_code.function_body,
        return_value_key=return_value_key,
    )


def create_from_proto(
    proto_object: code_execution_pb2.CodeExecution,
) -> CodeExecution:
  """Creates a CodeExecution instance from a proto."""
  match proto_object.WhichOneof("code"):
    case "python_code":
      return PythonScript._create_from_proto(proto_object)
    case _:
      raise ValueError(
          f"Unsupported code execution type: {proto_object.WhichOneof('code')}"
      )


def _find_def_header_colon_token(
    tokens: list[tokenize.TokenInfo],
    func: Callable[..., Any],
) -> tokenize.TokenInfo:
  """Returns the colon token finishing the "def" header."""

  # Find the first "def" token
  def_idx = -1
  for i, token in enumerate(tokens):
    if token.type == tokenize.NAME and token.string == "def":
      def_idx = i
      break
  if def_idx == -1:
    raise ValueError(f"Could not find def header for function {func}")

  # Find the matching ":" for the "def" token. We need to count parens since ":"
  # can also appear, e.g., in type annotations.
  paren_depth = bracket_depth = brace_depth = 0
  for i in range(def_idx + 1, len(tokens)):
    token = tokens[i]
    if token.type == tokenize.OP:
      match token.string:
        case "(":
          paren_depth += 1
        case "[":
          bracket_depth += 1
        case "{":
          brace_depth += 1
        case ")":
          paren_depth -= 1
        case "]":
          bracket_depth -= 1
        case "}":
          brace_depth -= 1
        case ":" if paren_depth == bracket_depth == brace_depth == 0:
          return token
  raise ValueError(f"Could not find colon in header for function {func}")


def get_function_body_as_str(func: Callable[..., Any]) -> str:
  """Returns the source code of the body of a function or method as a string.

  This function can be used to generate a function body string to pass into a
  PythonScript. E.g.:

  ```
  def compute(
      params: node_pb2.Params,
      context: basic_compute_context.BasicComputeContext,
  ) -> node_pb2.ReturnValue:
    # Some comment
    print('hello')
    return node_pb2.ReturnValue(y=params.x)

  s = PythonScript(
    signature_with_args=...,
    function_body=get_function_body_as_str(compute),
  )
  ```

  CAUTION: This will generate and pass the string
    "# Some comment\nprint('hello')\nreturn node_pb2.ReturnValue(y=params.x)"
  to PythonScript() for remote execution. This will NOT pass the function object
  itself to the PythonScript. The function object is just a helper to generate
  the code string in a readable way. The function won't get executed in the
  local environment, it cannot access any variables from the local environment
  and the function header (name and signature) gets completely ignored.

  Args:
    func: The Python function or method object to inspect.

  Returns:
    The source code of the body of the function as a string.

  Raises:
    ValueError: If the source code cannot be retrieved or parsed.
  """
  try:
    source = inspect.getsource(func)
  except (TypeError, OSError) as e:
    raise ValueError(f"Could not get source code for {func}: {e}") from e

  # Find the "header colon" in "... def x(...) ...: ..."
  tokens = list(tokenize.generate_tokens(io.StringIO(source).readline))
  header_colon_token = _find_def_header_colon_token(tokens, func)

  # Return everything after the header colon
  line_no, col_no = header_colon_token.start
  lines = source.splitlines()
  # line_no is 1-based, col_no is 0-based
  first_line_remainder = lines[line_no - 1][col_no + 1 :]
  if first_line_remainder.strip():
    body_lines = [first_line_remainder] + lines[line_no:]
  else:
    body_lines = lines[line_no:]

  return textwrap.dedent("\n".join(body_lines))
