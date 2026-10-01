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

"""Pytype definitions and conversion utilities.

(python3).

Utility functions:
  is_scalar - Distinguishes between scalar and vector values.
  get_matching_arrays - Gets two arrays with the same shape from scalar or
    matrix arguments.
"""

from collections import abc
from typing import Iterable
from typing import Text
from typing import Tuple
from typing import Union

import numpy as np

# ---------------------------------------------------------------------------
# PyType Definitions
#
# A list or vector of floating point values.
ScalarType = Union[float, int]
Float2Type = Tuple[float, float]
Float3Type = Tuple[float, float, float]
Float4Type = Tuple[float, float, float, float]
Vector2Type = Union[np.ndarray, Iterable[float], Float2Type]
Vector3Type = Union[np.ndarray, Iterable[float], Float3Type]
Vector4Type = Union[np.ndarray, Iterable[float], Float4Type]
VectorType = Union[
    np.ndarray, Iterable[float], Float2Type, Float3Type, Float4Type
]
# A scalar or a vector of floating point values.
VectorOrValueType = Union[VectorType, float]
Vector3OrValueType = Union[Vector3Type, float]
Vector4OrValueType = Union[Vector4Type, float]

# Error messages for exceptions.
SHAPE_MISMATCH_MESSAGE = 'lhs and rhs should have the same dimension'
DIMENSION_INVALID_MESSAGE = 'Dimension is undefined or invalid'
DIMENSION_MISMATCH_MESSAGE = 'Dimension is defined, but does not match inputs'

# ---------------------------------------------------------------------------
# Global Constants
#
# Default values for rtol and atol arguments to np.isclose are taken from
#   third_party/py/numpy/core/numeric.py
DEFAULT_RTOL_VALUE_FOR_NP_IS_CLOSE = 1e-5  # Relative tolerance
DEFAULT_ATOL_VALUE_FOR_NP_IS_CLOSE = 1e-8  # Absolute tolerance


def is_scalar(scalar_or_array: VectorOrValueType) -> bool:
  """Returns true if the argument has a scalar type.

  Args:
    scalar_or_array: A scalar floating point value or an array of values.

  Returns:
    True if the value is a scalar (single value).
  """
  return not isinstance(scalar_or_array, abc.Sized)


def get_matching_arrays(
    lhs: VectorOrValueType, rhs: VectorOrValueType, err_msg: Text = ''
) -> Tuple[np.ndarray, np.ndarray]:
  """Converts both inputs to numpy arrays with the same shape.

  If either input is a scalar, it will be converted to a constant array with
  the same shape as the other input.  If both inputs are scalars, they will
  both be converted to arrays containing a single value.

  If they are not scalars, the inputs must have the same shape.

  If they are both scalars, the inputs will be converted to arrays with a
  single element.

  Args:
    lhs: Left hand side scalar or array argument.
    rhs: Right hand side scalar or array argument.
    err_msg: String error message added to exception in case of failure.

  Returns:
    lhs, rhs: The arguments converted to numpy arrays.

  Raises:
    ValueError: If the lhs and rhs arrays not have the same shape.
  """
  if is_scalar(rhs):
    lhs = np.asarray(lhs)
    rhs = np.full(lhs.shape, rhs)
  else:
    rhs = np.asarray(rhs)
    if is_scalar(lhs):
      lhs = np.full(rhs.shape, lhs)
    else:
      lhs = np.asarray(lhs)
  if lhs.shape != rhs.shape:
    raise ValueError(
        'lhs and rhs should have the same dimension: %s != %s; %s'
        % (lhs.shape, rhs.shape, err_msg)
    )
  return lhs, rhs
