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

"""Utilities for creating and unpacking open inference protocol messages."""

import struct
from typing import Sequence

import numpy as np
from specification.protocol import open_inference_grpc_pb2

from intrinsic_inference.core.utils import oip_mappings

_STRING_DATATYPE = "BYTES"
_STRING_SHAPE = [1]


def convert_bytes_to_str(buffer: bytes) -> str:
  """Converts an encoded OIP bytes buffer to a UTF-8 string."""
  buffer_len = struct.unpack("<I", buffer[:4])[0]
  return buffer[4 : 4 + buffer_len].decode("utf-8")


def convert_str_to_bytes(tensor_str: str) -> bytes:
  """Converts a UTF-8 string to an encoded OIP bytes buffer."""
  tensor_encoded = tensor_str.encode("utf-8")
  return struct.pack("<I", len(tensor_encoded)) + tensor_encoded


def get_index_of_tensor(
    tensor_name: str,
    tensors: Sequence[
        open_inference_grpc_pb2.ModelInferRequest.InferInputTensor
        | open_inference_grpc_pb2.ModelInferResponse.InferOutputTensor
    ],
) -> int:
  """Gets the matching index of a tensor by name."""
  for idx, tensor in enumerate(tensors):
    if tensor.name == tensor_name:
      return idx
  return -1


def extract_str_tensor(
    tensor_name: str,
    request_or_response: (
        open_inference_grpc_pb2.ModelInferRequest
        | open_inference_grpc_pb2.ModelInferResponse
    ),
) -> str | None:
  """Extracts a string tensor by name from an OIP request or response, or None if not found."""
  if isinstance(request_or_response, open_inference_grpc_pb2.ModelInferRequest):
    tensors = request_or_response.inputs
    raw_contents = request_or_response.raw_input_contents
  else:
    tensors = request_or_response.outputs
    raw_contents = request_or_response.raw_output_contents

  idx = get_index_of_tensor(tensor_name, tensors)
  if idx == -1:
    return None
  return convert_bytes_to_str(raw_contents[idx])


def set_string_tensor_and_contents(
    request_or_response: (
        open_inference_grpc_pb2.ModelInferRequest
        | open_inference_grpc_pb2.ModelInferResponse
    ),
    tensor_name: str,
    tensor_str: str,
    existing_idx_to_replace: int | None = None,
) -> (
    open_inference_grpc_pb2.ModelInferRequest
    | open_inference_grpc_pb2.ModelInferResponse
):
  """Packs a tensor of type string into the request or response.

  If existing_idx_to_replace is not None, the tensor at that index is replaced.
  Otherwise the tensor is appended to inputs/outputs and raw contents.
  """
  if isinstance(request_or_response, open_inference_grpc_pb2.ModelInferRequest):
    tensors = request_or_response.inputs
    raw_contents = request_or_response.raw_input_contents
  elif isinstance(
      request_or_response, open_inference_grpc_pb2.ModelInferResponse
  ):
    tensors = request_or_response.outputs
    raw_contents = request_or_response.raw_output_contents
  else:
    raise TypeError(
        f"Invalid request or response type: {type(request_or_response)}"
    )

  if existing_idx_to_replace is None:
    tensors.add()
    raw_contents.append(b"")
    idx = -1
  else:
    if existing_idx_to_replace >= len(tensors):
      raise IndexError(
          f"Index {existing_idx_to_replace} exceeded tensors list length of "
          f"{len(tensors)}."
      )
    idx = existing_idx_to_replace

  tensors[idx].name = tensor_name
  tensors[idx].datatype = _STRING_DATATYPE
  tensors[idx].shape[:] = _STRING_SHAPE
  raw_contents[idx] = convert_str_to_bytes(tensor_str)

  return request_or_response


def extract_np_tensor_at_index(
    idx: int,
    request_or_response: (
        open_inference_grpc_pb2.ModelInferRequest
        | open_inference_grpc_pb2.ModelInferResponse
    ),
) -> np.ndarray:
  """Extracts a NumPy array from an OIP tensor."""
  if isinstance(request_or_response, open_inference_grpc_pb2.ModelInferRequest):
    tensors = request_or_response.inputs
    raw_contents = request_or_response.raw_input_contents
  else:
    tensors = request_or_response.outputs
    raw_contents = request_or_response.raw_output_contents

  if idx >= len(tensors):
    raise IndexError(f"Index {idx} does not exist in the tensors.")

  tensor = tensors[idx]
  if tensor.HasField("contents"):
    np_dtype = oip_mappings.oip_to_numpy_type(tensor.datatype)
    field_name = oip_mappings.oip_type_to_field(tensor.datatype)
    return np.array(
        getattr(tensor.contents, field_name), dtype=np_dtype
    ).reshape(tensor.shape)

  if not raw_contents or idx >= len(raw_contents):
    raise IndexError(f"Index {idx} doesn't exist in contents or raw_contents.")

  return np.frombuffer(
      raw_contents[idx],
      dtype=oip_mappings.oip_to_numpy_type(tensor.datatype),
  ).reshape(tensor.shape)


def extract_np_tensor_from_oip_request(
    tensor_name: str,
    infer_request: open_inference_grpc_pb2.ModelInferRequest,
) -> np.ndarray:
  """Extracts a NumPy array from an OIP ModelInferRequest."""
  idx = get_index_of_tensor(tensor_name, infer_request.inputs)
  if idx == -1:
    raise ValueError(f"Tensor {tensor_name} not found in inference request.")
  return extract_np_tensor_at_index(idx, infer_request)


def extract_np_tensor_from_oip_response(
    tensor_name: str,
    infer_response: open_inference_grpc_pb2.ModelInferResponse,
) -> np.ndarray:
  """Extracts a NumPy array from an OIP ModelInferResponse."""
  idx = get_index_of_tensor(tensor_name, infer_response.outputs)
  if idx == -1:
    raise ValueError(f"Tensor {tensor_name} not found in inference response.")
  return extract_np_tensor_at_index(idx, infer_response)
