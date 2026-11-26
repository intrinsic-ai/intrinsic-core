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

"""Quaternion class (python3).

This library implements a Quaternion class that may or may not represent a
rotation.
"""

import math
from typing import Optional, Text, Union

import numpy as np

from intrinsic.math.python import math_types
from intrinsic.math.python import vector_util

# ----------------------------------------------------------------------------
# Pytype definitions.
QuaternionOrScalarType = Union['Quaternion', float, int]

# ----------------------------------------------------------------------------
# Error messages for exceptions.
QUATERNION_INVALID_MESSAGE = (
    'Quaternion component vector should have four values'
)
QUATERNION_ZERO_MESSAGE = 'Quaternion has zero magnitude'
QUATERNION_NOT_NORMALIZED_MESSAGE = 'Quaternion is not normalized'

# ----------------------------------------------------------------------------
# Numpy constant matrix for computing the conjugate.
_QUATERNION_CONJUGATE_SCALE_FACTORS = np.array(
    [-1, -1, -1, 1], dtype=np.float64
)


class Quaternion(object):
  """A quaternion represented as an array of four values [x,y,z,w].

  This class defines standard Hamiltonian quaternions.
  The matrix convention is for column vector multiplication.

  A quaternion is a number with three imaginary components, i, j, and k and one
  real component.

      q = x i + y j + z k + w

  We represent the quaternion with the vector [x, y, z, w].

  Attributes:
    _xyzw: [x, y, z, w], the quaternion representation in a numpy array.

  Properties:
    xyzw: All coefficients as a numpy array.
    x: Coefficient of imaginary component i.
    y: Coefficient of imaginary component j.
    z: Coefficient of imaginary component k.
    w: Real component of quaternion.
  Factory functions:
    one: Returns the multiplicative identity, 1.0.
    random_unit: Returns a random Quaternion with magnitude one.
  """

  def __init__(
      self,
      xyzw: Optional[math_types.VectorType] = None,
      normalize: bool = False,
  ):
    """Initializes the quaternion with the xyzw component values.

    Sets the components to the values xyzw.

    If no components are given, the default is the zero quaternion.  The zero
    quaternion does not represent any rotation.

    If normalize is True, the quaternion will be scaled by 1/norm to give it a
    magnitude of 1.0.  This will result in an exception if the quaternion is
    close to zero.

    Quaternion.one() returns the multiplicative identity.

    Args:
      xyzw: Quaternion component values in a vector.
      normalize: Indicates whether to normalize the quaternion.

    Raises:
      ValueError: If xyzw has wrong shape or if normalization fails.
    """
    if xyzw is None:
      xyzw = (0, 0, 0, 0)
    self._xyzw = vector_util.as_finite_vector(
        xyzw,
        dimension=4,
        normalize=normalize,
        dtype=np.float64,
        err_msg='Quaternion.__init__',
    ).copy()

  # --------------------------------------------------------------------------
  # Properties
  # --------------------------------------------------------------------------

  @property
  def xyzw(self) -> np.ndarray:
    """Returns the x, y, z, w component values of the quaternion.

    Returns:
      The xyzw coefficients of the quaternion as a numpy array.
    """
    return self._xyzw.copy()

  @property
  def x(self) -> float:
    """Returns x component of quaternion."""
    return self._xyzw[0]

  @property
  def y(self) -> float:
    """Returns y component of quaternion."""
    return self._xyzw[1]

  @property
  def z(self) -> float:
    """Returns z component of quaternion."""
    return self._xyzw[2]

  @property
  def w(self) -> float:
    """Returns w component of quaternion."""
    return self._xyzw[3]

  # --------------------------------------------------------------------------
  # Utility functions
  # --------------------------------------------------------------------------

  def conjugate(self) -> 'Quaternion':
    """Returns the complex conjugate of the quaternion.

    The conjugate of a quaternion is the same quaternion with the three
    imaginary components negated:

      q' = -qx i + -qy j + -qz k + qw

      q * q' = |q|^2, so if |q| = 1, then q' = q^-1

    Returns:
      The complex conjugate of the quaternion.
    """
    return Quaternion(xyzw=self._xyzw * _QUATERNION_CONJUGATE_SCALE_FACTORS)

  def is_normalized(
      self, norm_epsilon: float = math_types.DEFAULT_RTOL_VALUE_FOR_NP_IS_CLOSE
  ) -> bool:
    """Returns True if the quaternion is normalized within norm_epsilon.

    Returns True if abs(1 - |q|) <= norm_epsilon

    Args:
      norm_epsilon: Error tolerance on magnitude.

    Returns:
      True if the quaternion has magnitude close to 1.0.
    """
    return vector_util.is_vector_normalized(
        self._xyzw, norm_epsilon=norm_epsilon
    )

  def inverse(self) -> 'Quaternion':
    """Returns the multiplicative inverse of a non-zero quaternion.

    q * q.inverse() = q.inverse() * q = 1

    Returns:
      Inverse of the quaternion.

    Raises:
      ValueError: If the other quaternion cannot be inverted, i.e. |q| == 0.
    """
    self.check_non_zero(err_msg='cannot be inverted')
    return self.conjugate() / (np.linalg.norm(self._xyzw) ** 2)

  def multiply(self, other_quaternion: 'Quaternion') -> 'Quaternion':
    """Returns the quaternion product (self * other_quaternion).

    Using equation (2.24) from Computer Animation (3rd Edition), 2012 by Rick
    Parent.  This assumes neither quaternion has rotation properties, such that
    the zero quaternion is valid.

    Args:
      other_quaternion: Right hand side operand of quaternion product.

    Returns:
      The quaternion product: self * other_quaternion.
    """
    xyzw = np.zeros(4, dtype=np.float64)
    xyzw[3] = self._xyzw[3] * other_quaternion.xyzw[3] - np.dot(
        self._xyzw[:3], other_quaternion.xyzw[:3]
    )
    xyzw[:3] = (
        self._xyzw[3] * other_quaternion.xyzw[:3]
        + other_quaternion.xyzw[3] * self._xyzw[:3]
        + np.cross(self._xyzw[:3], other_quaternion.xyzw[:3])
    )

    return Quaternion(xyzw=xyzw)

  def divide(self, other: QuaternionOrScalarType) -> 'Quaternion':
    """Returns the quaternion quotient (self * other.inverse()).

    If the other value is a scalar, the result is a scale of the quaternion.

    Args:
      other: Right hand side operand of quaternion product.

    Returns:
      The quaternion product: self * other_quaternion.

    Raises:
      ValueError: If the other quaternion cannot be inverted.
    """
    if isinstance(other, Quaternion):
      return self.multiply(other.inverse())
    elif other <= math_types.DEFAULT_ATOL_VALUE_FOR_NP_IS_CLOSE:
      # Calls the quaternion version to get the correct exception message.
      return self.divide(Quaternion(xyzw=[0.0, 0.0, 0.0, other]))
    else:
      # If the operand is a real scalar, scales the quaternion components by the
      # inverse of the operand.
      return Quaternion(self._xyzw / other)

  def normalize(self, err_msg: Text = '') -> 'Quaternion':
    """Returns a normalized copy of this quaternion.

    Calculates q / |q|

    Args:
      err_msg: Message to be added to error in case of failure.

    Returns:
      A quaternion with the same direction but magnitude 1.

    Raises:
      ValueError: If the quaternion cannot be normalized, i.e. |q| == 0.
    """
    norm = np.linalg.norm(self._xyzw)
    if norm <= math_types.DEFAULT_ATOL_VALUE_FOR_NP_IS_CLOSE:
      raise ValueError(
          self._zero_magnitude_message(
              norm_epsilon=math_types.DEFAULT_ATOL_VALUE_FOR_NP_IS_CLOSE,
              err_msg=err_msg,
          )
      )
    return Quaternion(self._xyzw / norm)

  # --------------------------------------------------------------------------
  # Operators
  # --------------------------------------------------------------------------

  def __eq__(self, other: 'Quaternion') -> bool:
    """Returns True iff the quaternions are identical."""
    if not isinstance(other, type(self)):
      return NotImplemented
    return np.array_equal(self._xyzw, other.xyzw)

  def __ne__(self, other: 'Quaternion') -> bool:
    """Returns True iff the quaternions are not identical."""
    return not self == other

  __hash__ = None  # This class is not hashable.

  def __neg__(self) -> 'Quaternion':
    """Returns the negative (additive inverse) of this quaternion."""
    return Quaternion(xyzw=-(self._xyzw))

  def __mul__(self, other: QuaternionOrScalarType) -> 'Quaternion':
    """Returns the quaternion product of self * other."""
    if isinstance(other, Quaternion):
      return self.multiply(other)
    else:
      # If the operand is a scalar, scale the components.
      return Quaternion(self._xyzw * other)

  def __rmul__(self, scale_factor: float) -> 'Quaternion':
    """Returns the quaternion product of other * self."""
    # Multiplication with real scalar values is commutative.
    return Quaternion(self._xyzw * scale_factor)

  def __div__(self, other: QuaternionOrScalarType) -> 'Quaternion':
    """Returns quotient of self / other (python2).

    other / self == other * self^-1

    Args:
      other: Left hand operand of quotient.

    Returns:
      Quaternion quotient, other / self.

    Raises:
      ValueError: If the other quaternion or value cannot be inverted,
        i.e. |q| == 0.
    """
    return self.divide(other)

  def __truediv__(self, other: QuaternionOrScalarType) -> 'Quaternion':
    """Returns quotient of self / other (python3)."""
    return self.__div__(other)

  def __rdiv__(self, other: float) -> 'Quaternion':
    """Returns quotient of other / self (python2).

    other / self == other * self^-1

    This is equal to a scalar multiple of self.inverse().

    Args:
      other: Left hand operand of quotient.

    Returns:
      Quaternion quotient, other / self.

    Raises:
      ValueError: If the other quaternion cannot be inverted, i.e. |q| == 0.
    """
    return other * self.inverse()

  def __rtruediv__(self, other: float) -> 'Quaternion':
    """Returns quotient of other / self (python3)."""
    return self.__rdiv__(other)

  def __add__(self, other_quaternion: 'Quaternion') -> 'Quaternion':
    """Returns the sum of two quaternions.

    Quaternion addition works like complex addition, by adding the components:

      q = qx i + qy j + qz k + qw
      p = px i + py j + pz k + pw
      q + p = (qx + px) i + (qy + py) j + (qz + pz) k + (qw + pw).

    Args:
      other_quaternion: Right hand operand to addition.

    Returns:
      A quaternion equal to the sum of self + other_quaternion.
    """
    return Quaternion(self.xyzw + other_quaternion.xyzw)

  def __sub__(self, other_quaternion: 'Quaternion') -> 'Quaternion':
    """Returns the difference of two quaternions.

    Args:
      other_quaternion: Right hand operand to subtraction.

    Returns:
      A quaternion equal to the difference, self - other_quaternion.
    """
    return Quaternion(self.xyzw - other_quaternion.xyzw)

  def __abs__(self) -> float:
    """Returns the magnitude of the quaternion."""
    return np.linalg.norm(self._xyzw)

  # --------------------------------------------------------------------------
  # Checks
  # --------------------------------------------------------------------------

  def _zero_magnitude_message(
      self,
      norm_epsilon: float,
      err_msg: Text = '',
      norm: Optional[float] = None,
  ):
    if norm is None:
      norm = np.linalg.norm(self._xyzw)
    return '%s: |%r| = %g <= %g  %s' % (
        QUATERNION_ZERO_MESSAGE,
        self,
        norm,
        norm_epsilon,
        err_msg,
    )

  def check_non_zero(
      self,
      norm_epsilon: float = math_types.DEFAULT_ATOL_VALUE_FOR_NP_IS_CLOSE,
      err_msg: Text = '',
  ) -> None:
    """Raises a ValueError exception if the quaternion is close to zero.

    Args:
      norm_epsilon: Finite, non-negative error tolerance on magnitude.
      err_msg: Message to be added to error in case of failure.

    Raises:
      ValueError: If norm_epsilon is negative or non-finite, or if
        |q| <= norm_epsilon.
    """
    if not math.isfinite(norm_epsilon) or norm_epsilon < 0:
      raise ValueError(
          'norm_epsilon must be finite and non-negative: %r. %s'
          % (norm_epsilon, err_msg)
      )
    norm = math.hypot(*self._xyzw)
    if norm <= norm_epsilon:
      raise ValueError(
          self._zero_magnitude_message(
              norm_epsilon=norm_epsilon, err_msg=err_msg, norm=norm
          )
      )

  def check_normalized(
      self,
      norm_epsilon: float = math_types.DEFAULT_RTOL_VALUE_FOR_NP_IS_CLOSE,
      err_msg: Text = '',
  ) -> None:
    """Raises a ValueError exception if the quaternion is not normalized.

    Args:
      norm_epsilon: Error tolerance on magnitude.
      err_msg: Message to be added to error in case of failure.

    Raises:
      ValueError: If |q| != 1.
    """
    if not self.is_normalized(norm_epsilon):
      raise ValueError(
          '%s: |%r| = %g not within %g of 1.0  %s'
          % (
              QUATERNION_NOT_NORMALIZED_MESSAGE,
              self,
              np.linalg.norm(self._xyzw),
              norm_epsilon,
              err_msg,
          )
      )

  # --------------------------------------------------------------------------
  # Factory functions
  # --------------------------------------------------------------------------

  @classmethod
  def one(cls) -> 'Quaternion':
    """Returns 1, the multiplicative identity quaternion."""
    return cls(xyzw=vector_util.one_hot_vector(4, 3))

  @classmethod
  def random_unit(
      cls, rng: Optional[np.random.Generator] = None
  ) -> 'Quaternion':
    """Returns a random unit quaternion uniformly distributed over SO(3).

    Using Shoemake algorithm on go/graphic-gems-3, page 129 using quaternion
    definition from go/shoemake-quaternions.

    Args:
      rng: A random number generator.

    Returns:
      A random Quaternion with magnitude one.
    """
    if not rng:
      rng = np.random.default_rng()

    x0 = rng.uniform(low=0.0, high=1.0)
    x1 = rng.uniform(low=0.0, high=1.0)
    x2 = rng.uniform(low=0.0, high=1.0)
    theta1 = 2 * math.pi * x1
    theta2 = 2 * math.pi * x2
    s1 = math.sin(theta1)
    s2 = math.sin(theta2)
    c1 = math.cos(theta1)
    c2 = math.cos(theta2)
    r1 = math.sqrt(1 - x0)
    r2 = math.sqrt(x0)
    x = s1 * r1
    y = c1 * r1
    z = s2 * r2
    w = c2 * r2
    return cls(xyzw=np.array([x, y, z, w]))

  # --------------------------------------------------------------------------
  # String representations
  # --------------------------------------------------------------------------

  def __str__(self) -> Text:
    """Returns a string that describes the quaternion."""
    return '[%.4gi + %.4gj + %.4gk + %.4g]' % (self.x, self.y, self.z, self.w)

  def __repr__(self) -> Text:
    """Returns a string representation of the quaternion.

    This representation can be used to construct the quaternion.

    Returns:
      Returns the string, 'Quaternion([x, y, z, w])', which can be used to
      regenerate the quaternion.
    """
    return 'Quaternion([%r, %r, %r, %r])' % (
        float(self.x),
        float(self.y),
        float(self.z),
        float(self.w),
    )
