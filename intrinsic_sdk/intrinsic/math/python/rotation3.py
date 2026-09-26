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

"""Rotation3 class (python3).

This library implements 3D rotations about an axis through the origin.

A rotation is represented internally as a normalized quaternion.
"""

import math
from typing import Optional
from typing import Text
from typing import Tuple

import numpy as np

from intrinsic.math.python import math_types
from intrinsic.math.python import quaternion as quaternion_class
from intrinsic.math.python import vector_util

# ----------------------------------------------------------------------------
# Error messages for exceptions.
ROTATION3_INIT_MESSAGE = 'Rotation3 initialization'
INVALID_AXIS_MESSAGE = 'Invalid rotation axis vector.'
INVALID_ANGULAR_VELOCITY_MESSAGE = 'Invalid angular velocity.'
INVALID_ROTATION_QUATERNION_MESSAGE = (
    'A quaternion representing a rotation should have magnitude 1.'
)
MATRIX_NOT_ORTHOGONAL_MESSAGE = 'Rotation matrix should be orthogonal'
MATRIX_WRONG_SHAPE_MESSAGE = 'Matrix should be 3x3 or 4x4'
MATRIX_NOT_PROPER_ROTATION_MESSAGE = (
    'Rotation matrix should not be a reflection, determinant must be 1'
)

# ----------------------------------------------------------------------------
# Default values.

# Axis returned when the rotation angle is too small for the axis to be
# determined.
_DEFAULT_ROTATION3_AXIS = (0, 0, 1)


def check_rotation_matrix(
    matrix: np.ndarray,
    rtol: float = math_types.DEFAULT_RTOL_VALUE_FOR_NP_IS_CLOSE,
    atol: float = math_types.DEFAULT_ATOL_VALUE_FOR_NP_IS_CLOSE,
    err_msg: Text = '',
) -> None:
  """Verifies that the matrix is (or contains) a 3x3 rotation matrix.

  Raises a ValueError if the upper 3x3 submatrix of the matrix is not a valid
  rotation matrix.  The matrix must have two dimensions and contain a 3x3
  submatrix.  The 3x3 submatrix must be orthogonal and have determinant +1
  (a proper rotation, not a reflection).

  This function does not check any values outside of the 3x3 submatrix.

  Args:
    matrix: A 3x3 or 4x4 matrix whose upper 3x3 corner should be orthogonal.
    rtol: relative error tolerance, passed through to np.allclose.
    atol: absolute error tolerance, passed through to np.allclose.
    err_msg: Error message string added to exception in case of invalid input.

  Raises:
    ValueError: If the matrix has the wrong shape, does not have an orthogonal
    upper 3x3 submatrix, or the submatrix has determinant -1 (a reflection).
  """
  if len(matrix.shape) != 2 or matrix.shape[0] < 3 or matrix.shape[1] < 3:
    raise ValueError(
        '%s: %s\n %s\n%s'
        % (MATRIX_WRONG_SHAPE_MESSAGE, matrix.shape, matrix, err_msg)
    )
  matrix3x3 = np.array(matrix, dtype=np.float64, copy=False)[:3, :3]
  # If matrix3x3 is a rotation matrix, matrix3x3 * matrix3x3' should be the
  # identity matrix.
  eye = np.matmul(matrix3x3, np.transpose(matrix3x3))
  if not np.allclose(eye, np.identity(3), rtol=rtol, atol=atol):
    raise ValueError(
        "%s: shape=%s\n%s\nr * r' = %s\n%s"
        % (MATRIX_NOT_ORTHOGONAL_MESSAGE, matrix.shape, matrix, eye, err_msg)
    )
  # An orthogonal matrix has determinant +1 (rotation) or -1 (reflection).
  # A quaternion can only represent a rotation.
  determinant = np.linalg.det(matrix3x3)
  if not np.isclose(determinant, 1.0, rtol=rtol, atol=atol):
    raise ValueError(
        '%s: shape=%s\n%s\ndet(r) = %s\n%s'
        % (
            MATRIX_NOT_PROPER_ROTATION_MESSAGE,
            matrix.shape,
            matrix,
            determinant,
            err_msg,
        )
    )


class Rotation3(object):
  """Represents a 3D rigid rotation about an axis through the origin.

  The rotation is represented as a quaternion.

  Properties:
    quaternion: The quaternion that performs the rotation.

  Factory functions:
    from_axis_angle: Generates the rotation by the given angle about the given
      axis.
    from_matrix: Extracts rotation from 3x3 rotation matrix.
  """

  def __init__(
      self,
      quat: quaternion_class.Quaternion = quaternion_class.Quaternion.one(),
      normalize: bool = False,
  ):
    """Constructs Rotation3 from a quaternion.

    Constructs a Rotation3 object from a quaternion.

    If the normalize flag is False, the input quaternion should already be
    normalized (have magnitude approximately 1.0).

    To improve stability, the normalize flag should be False when the Rotation3
    is being constructed from a quaternion that ought to be normalized already,
    such as the inverse or product of Rotation3 objects.

    Args:
      quat: Non-zero quaternion representing the rotation.
      normalize: Indicates whether to normalize the quaternion.

    Raises:
      ValueError: If the quaternion is not normalized or cannot be normalized.
    """
    if np.array_equal(quat.xyzw, np.array([0, 0, 0, 0])):
      # If the quaternion is exactly zero, use identity instead.
      quat = quaternion_class.Quaternion.one()
    elif normalize:
      quat = quat.normalize(err_msg=ROTATION3_INIT_MESSAGE)
    self._quaternion = quat
    self._quaternion.check_non_zero(
        err_msg='%s %s'
        % (ROTATION3_INIT_MESSAGE, INVALID_ROTATION_QUATERNION_MESSAGE)
    )

  # --------------------------------------------------------------------------
  # Properties
  # --------------------------------------------------------------------------

  @property
  def quaternion(self) -> quaternion_class.Quaternion:
    """Returns the quaternion that represents the rotation."""
    return self._quaternion

  # --------------------------------------------------------------------------
  # Utility functions
  # --------------------------------------------------------------------------

  def rotate_point(self, point: math_types.Vector3Type) -> np.ndarray:
    """Performs a rotation by this quaternion.

    Rotates the point by the quaternion using quaternion multiplication,
      (q * p * q^-1), without constructing the rotation matrix.

    Args:
      point: The point to be rotated.

    Returns:
      A 3D vector in a numpy array.
    """
    q_point = quaternion_class.Quaternion(
        xyzw=[point[0], point[1], point[2], 0.0]
    )
    q_point_rotated = self._quaternion * q_point * self._quaternion.inverse()
    return q_point_rotated.xyzw[:3].copy()

  def inverse(self) -> 'Rotation3':
    """Returns the inverse of the rotation.

    rotation.rotate_point(p2) == p1
    rotation.inverse().rotate_point(p1) == p2

    rotation * rotation.inverse() == Rotation3.identity()

    The inverse rotation is performed by the conjugate of the quaternion.

    This inverse operation is exact:
      r.inverse().inverse() is exactly equal to r.

    Returns:
      A rotation that is the inverse of this rotation.
    """
    return Rotation3(self._quaternion.conjugate(), normalize=False)

  def almost_equal(
      self,
      other: 'Rotation3',
      rtol: float = math_types.DEFAULT_RTOL_VALUE_FOR_NP_IS_CLOSE,
      atol: float = math_types.DEFAULT_ATOL_VALUE_FOR_NP_IS_CLOSE,
  ) -> bool:
    """Returns true if the rotations are equivalent within tolerances.

    The error in rotations corresponds roughly to the positional error of a
    point at unit distance from the origin when subjected to these rotations.

    Args:
      other: Rotation to compare against self.
      rtol: relative error tolerance, passed through to np.allclose.
      atol: absolute error tolerance, passed through to np.allclose.

    Returns:
      True if the two rotations are equivalent within tolerances.
    """
    q1_normalized = self.quaternion.normalize()
    q2_normalized = other.quaternion.normalize()
    return np.allclose(
        q1_normalized.xyzw, q2_normalized.xyzw, rtol=rtol, atol=atol
    ) or np.allclose(
        q1_normalized.xyzw, -q2_normalized.xyzw, rtol=rtol, atol=atol
    )

  def _axis_and_half_angle_values(
      self,
      default_axis: math_types.Vector3Type,
      direction_axis: math_types.Vector3Type,
  ) -> Tuple[np.ndarray, float, float]:
    """Calculates axis vector and sine and cosine values of the half angle.

    If both default_axis and direction_axis are specified and they have a
    negative inner product, the negative of the default_axis may be returned to
    ensure that the direction_axis is specified.

    Args:
      default_axis: Axis returned if the angle is too small to resolve the real
        axis.
      direction_axis: Direction vector used to select between positive and
        negative rotations.  If this value is specified, the axis will have a
        non-negative inner product with this vector.

    Returns:
      axis: Axis of rotation.
      sin_half_angle: sine of half angle of rotation.
      cos_half_angle: Cosine of half angle of rotation.
    """
    axis = self.quaternion.xyzw[:3].copy()
    sin_half_angle = np.linalg.norm(axis)
    cos_half_angle = self.quaternion.xyzw[3]
    if cos_half_angle < 0:
      # Select the quaternion representation of the rotation that has a positive
      # real component.
      axis = -axis
      cos_half_angle = -cos_half_angle
    if sin_half_angle > math_types.DEFAULT_ATOL_VALUE_FOR_NP_IS_CLOSE:
      axis = axis / sin_half_angle
    else:
      # Rotation angle is too small to resolve the axis.  Use the default axis
      # instead.
      axis = vector_util.as_unit_vector3(default_axis)
    if direction_axis is not None and np.inner(axis, direction_axis) < 0:
      # If direction_axis is specified, select the axis sign that has positive
      # inner product with the direction_axis.
      sin_half_angle = -sin_half_angle
      axis = -axis
    return axis, sin_half_angle, cos_half_angle

  def axis(
      self,
      default_axis: math_types.Vector3Type = _DEFAULT_ROTATION3_AXIS,
      direction_axis: Optional[math_types.Vector3Type] = None,
  ) -> np.ndarray:
    """Returns the axis of rotation as a normalized vector.

    If the rotation is too small to resolve the axis, returns the z-axis vector
    instead.

    If the angle of rotation is exactly pi (180 degrees), the real value will be
    zero and the axis is ambiguous.  It could lie equivalently in the positive
    or negative direction.

    Args:
      default_axis: Axis returned if the angle is too small.
      direction_axis: Selects between positive and negative rotations.

    Returns:
      Axis vector with magnitude 1 as a numpy array.
    """
    axis, _, _ = self._axis_and_half_angle_values(
        default_axis=default_axis, direction_axis=direction_axis
    )
    return axis

  def angle(
      self,
      default_axis: math_types.Vector3Type = _DEFAULT_ROTATION3_AXIS,
      direction_axis: Optional[math_types.Vector3Type] = None,
  ) -> float:
    """Returns the angle of rotation in radians.

    The angle is always in [0, pi] if direction_axis is not specified
    and in [-pi, pi] if direction_axis is specified.

    Args:
      default_axis: Axis returned if the angle is too small.
      direction_axis: Selects between positive and negative rotations.

    Returns:
      Rotation angle in radians.
    """
    _, sin_half_angle, cos_half_angle = self._axis_and_half_angle_values(
        default_axis=default_axis, direction_axis=direction_axis
    )
    return 2 * math.atan2(sin_half_angle, cos_half_angle)

  def matrix3x3(self) -> np.ndarray:
    """Returns the 3x3 matrix representation of the rotation.

    This matrix should be applied to column vectors.

    Returns:
      3x3 rotation matrix as a numpy array.
    """
    q = quaternion_class.Quaternion(
        xyzw=self._quaternion.xyzw / np.linalg.norm(self._quaternion.xyzw)
    )
    # There are two equivalent conversions for quaternion <==> rotation matrix.
    return 2 * np.array([
        [
            q.x * q.x + q.w * q.w,
            q.x * q.y - q.z * q.w,
            q.x * q.z + q.y * q.w,
        ],
        [
            q.x * q.y + q.z * q.w,
            q.y * q.y + q.w * q.w,
            q.y * q.z - q.x * q.w,
        ],
        [
            q.x * q.z - q.y * q.w,
            q.y * q.z + q.x * q.w,
            q.z * q.z + q.w * q.w,
        ],
    ]) - np.identity(3)

  def euler_angles(self, radians: bool = False) -> np.ndarray:
    """Returns the roll-pitch-yaw Euler angle representation of the rotation.

    The rotation by Euler angles is calculated by performing these three
    rotations in this order:

      1. Rotate about fixed X-axis by roll.
      2. Rotate about fixed Y-axis by pitch.
      3. Rotate about fixed Z-axis by yaw.

    The Euler angles for a rotation are not uniquely defined as multiple
    (roll, pitch, yaw) combinations can result in the same overall rotation.

    Args:
      radians: Indicates whether the result should be returns as radians or
        degrees.

    Returns:
      roll, pitch, and yaw in degrees or radians.
    """
    matrix = self.matrix3x3()
    check_rotation_matrix(matrix)
    cos_pitch = np.linalg.norm(matrix[2][1:])
    pitch = math.atan2(-matrix[2][0], cos_pitch)
    if cos_pitch < 1e-3:
      # Rotations that are pitched close to 90 degrees can be represented with
      # just pitch and yaw, because yaw after rotation by 90 degrees about y is
      # equivalent to roll.
      roll = 0
      yaw = math.atan2(-matrix[0][1], matrix[1][1])
    else:
      yaw = math.atan2(matrix[1][0], matrix[0][0])
      roll = math.atan2(matrix[2][1], matrix[2][2])

    rpy_radians = np.array((roll, pitch, yaw))
    if radians:
      return rpy_radians
    else:
      return np.degrees(rpy_radians)

  # --------------------------------------------------------------------------
  # Operators
  # --------------------------------------------------------------------------

  def __eq__(self, other: 'Rotation3') -> bool:
    """Returns True iff the rotations are identical.

    The negative of a quaternion performs the same rotation as the quaternion.

    Args:
      other: Another rotation.

    Returns:
      True iff the rotations are identical.
    """
    if not isinstance(other, type(self)):
      return NotImplemented
    if self.quaternion == other.quaternion:
      return True
    if self.quaternion == -other.quaternion:
      return True
    q1_normalized = self.quaternion.normalize()
    q2_normalized = other.quaternion.normalize()
    return q1_normalized == q2_normalized or q1_normalized == -q2_normalized

  def __ne__(self, other: 'Rotation3') -> bool:
    """Returns True iff the rotations are not identical."""
    return not self == other

  def __mul__(self, other: 'Rotation3') -> 'Rotation3':
    """Returns the product of the two rotations: (self * other).

    The product of two rotations is their function composition, (self o other).

    For all 3D points, p:
      (self o other).rotate_point(p) == self.rotate_point(other.rotate_point(p))

    The composition of two rotations is calculated by taking the product of
    their representative quaternions.  Because the original rotations are
    normalized, the product will not need to be normalized.

    Args:
      other: Right hand operand of product.

    Returns:
      The composite rotation (self * other).
    """
    return Rotation3(self._quaternion * other.quaternion, normalize=False)

  def __div__(self, other: 'Rotation3') -> 'Rotation3':
    """Returns quotient of this / other (python2).

    Equivalent to self * other^-1

    Args:
      other: Right hand operand of quotient.

    Returns:
      The quotient, self / other
    """
    return Rotation3(self.quaternion * other.quaternion.conjugate())

  def __truediv__(self, other: 'Rotation3') -> 'Rotation3':
    """Returns quotient of this / other (python3)."""
    return self.__div__(other)

  # --------------------------------------------------------------------------
  # Checks
  # --------------------------------------------------------------------------

  def check_valid(
      self,
      norm_epsilon: float = math_types.DEFAULT_ATOL_VALUE_FOR_NP_IS_CLOSE,
      err_msg: Text = '',
  ) -> None:
    """Checks that the rotation has valid values.

    Args:
      norm_epsilon: Error tolerance on magnitude of quaternion.
      err_msg: Message to be added to error in case of failure.

    Returns:
      None

    Raises:
      ValueError: If |q| <= norm_epsilon.
    """
    self.quaternion.check_non_zero(norm_epsilon, err_msg)

  # --------------------------------------------------------------------------
  # Factory functions
  # --------------------------------------------------------------------------

  @classmethod
  def identity(cls) -> 'Rotation3':
    """Returns the identity rotation."""
    return cls(quat=quaternion_class.Quaternion.one(), normalize=False)

  @classmethod
  def from_xyzw(
      cls, xyzw: math_types.Vector4Type, normalize: bool = False
  ) -> 'Rotation3':
    """Returns rotation with the given quaternion components.

    Constructs a Rotation3 object from a quaternion with the given component
    values.

    If the normalize flag is False, the input quaternion should already be
    normalized (have magnitude approximately 1.0).

    Args:
      xyzw: Components of quaternion with real component last.
      normalize: Indicates whether to normalize the quaternion.
    """
    return cls(quat=quaternion_class.Quaternion(xyzw=xyzw), normalize=normalize)

  @classmethod
  def from_axis_angle(
      cls, axis: math_types.Vector3Type, angle: float, err_msg: Text = ''
  ) -> 'Rotation3':
    """Returns a rotation about the axis by the angle.

    Args:
      axis: Direction vector of axis of rotation.
      angle: Angle of rotation in radians.
      err_msg: Error message string added to exception in case of invalid input.

    Returns:
      The rotation defined by the axis and angle.

    Raises:
      ValueError: If the axis has magnitude zero or does not have three
        components.
    """
    half_angle = angle * 0.5
    axis = vector_util.as_unit_vector3(
        axis, err_msg='%s %s' % (err_msg, INVALID_AXIS_MESSAGE)
    )
    xyzw = np.zeros(4)
    xyzw[:3] = axis
    xyzw[:3] *= math.sin(half_angle)
    xyzw[3] = math.cos(half_angle)
    return cls(quaternion_class.Quaternion(xyzw=xyzw), normalize=False)

  @classmethod
  def from_matrix(
      cls,
      matrix: np.ndarray,
      rtol: float = math_types.DEFAULT_RTOL_VALUE_FOR_NP_IS_CLOSE,
      atol: float = math_types.DEFAULT_ATOL_VALUE_FOR_NP_IS_CLOSE,
      err_msg: Text = '',
  ) -> 'Rotation3':
    """Construct a quaternion from a transformation matrix.

    Calculate the quaternion that performs the same rotation as
    this transformation matrix.

    Args:
      matrix: A transformation matrix of dimensions at least 3x3.
      rtol: relative error tolerance, passed through to np.allclose.
      atol: absolute error tolerance, passed through to np.allclose.
      err_msg: Error message string added to exception in case of invalid input.

    Returns:
      A Quaternion representing the same rotation as the matrix.

    Raises:
      ValueError: If input is not a rotation matrix.
    """
    check_rotation_matrix(matrix, rtol=rtol, atol=atol, err_msg=err_msg)
    matrix3x3 = np.array(matrix, dtype=np.float64, copy=False)[:3, :3]
    q_xyzw = np.zeros(4, dtype=np.float64)
    trace = np.trace(matrix3x3) + 1
    # Solve for the largest quaternion component first.
    if trace > 1:
      # The real component is the largest.
      q_xyzw[3] = trace
      q_xyzw[2] = matrix3x3[1, 0] - matrix3x3[0, 1]
      q_xyzw[1] = matrix3x3[0, 2] - matrix3x3[2, 0]
      q_xyzw[0] = matrix3x3[2, 1] - matrix3x3[1, 2]
      q_xyzw *= 0.5 / math.sqrt(trace)
    else:
      # i will be the index of the diagonal element with the largest value.
      # j and k will be the following indices in order.
      i, j, k = (np.arange(3) + matrix3x3.diagonal().argmax()) % 3
      scale = 1 + matrix3x3[i, i] - (matrix3x3[j, j] + matrix3x3[k, k])
      q_xyzw[i] = scale
      q_xyzw[j] = matrix3x3[i, j] + matrix3x3[j, i]
      q_xyzw[k] = matrix3x3[k, i] + matrix3x3[i, k]
      q_xyzw[3] = matrix3x3[k, j] - matrix3x3[j, k]
      q_xyzw *= 0.5 / math.sqrt(scale)
    if q_xyzw[3] < 0:
      q_xyzw *= -1  # Selects the quaternion with positive real coefficient.
    return cls(quaternion_class.Quaternion(xyzw=q_xyzw))

  @classmethod
  def from_euler_angles(
      cls,
      rpy_degrees: Optional[math_types.Vector3Type] = None,
      rpy_radians: Optional[math_types.Vector3Type] = None,
  ) -> 'Rotation3':
    """Constructs a rotation from Euler angles (roll, pitch, yaw).

    The rotation performed by a given set of Euler angles is uniquely defined.

    The rotation is calculated by performing these three rotations in this
    order:

      1. Rotate about fixed X-axis by roll.
      2. Rotate about fixed Y-axis by pitch.
      3. Rotate about fixed Z-axis by yaw.

    This is consistent with the definition of rpy in ROS.

    Args:
      rpy_degrees: roll, pitch, and yaw in degrees.
      rpy_radians: roll, pitch, and yaw in radians.

    Returns:
      The equivalent rotation.

    Raises:
      ValueError: If the inputs are invalid.
    """
    if rpy_degrees is not None:
      rpy_radians = np.radians(rpy_degrees)
    if rpy_radians is None:
      raise ValueError(
          'Rotation3.from_euler_angles requires rpy_degrees or rpy_radians.'
      )
    rpy_radians = vector_util.as_vector3(rpy_radians)
    roll, pitch, yaw = rpy_radians[:]
    roll_rotation = cls.from_axis_angle((1, 0, 0), roll)
    pitch_rotation = cls.from_axis_angle((0, 1, 0), pitch)
    yaw_rotation = cls.from_axis_angle((0, 0, 1), yaw)
    return yaw_rotation * pitch_rotation * roll_rotation

  @classmethod
  def from_angular_velocity(
      cls,
      angular_velocity: Optional[math_types.Vector3Type] = None,
      delta_time: float = 1.0,
      err_msg: Text = '',
  ) -> 'Rotation3':
    """Returns the rotation resulting from angular velocity over time.

    Returns the rotation that results from applying the given angular velocity
    (a vector with units radians/second) over a period of time, delta_time.

    Args:
      angular_velocity: An angular velocity vector (radians/second).
      delta_time: A duration in seconds.
      err_msg: Error message string added to exception in case of invalid input.

    Returns:
      The rotation as Rotation3 that results from applying the given angular
      velocity over delta_time.
    """
    angular_velocity = vector_util.as_vector3(
        angular_velocity,
        err_msg='%s %s' % (err_msg, INVALID_ANGULAR_VELOCITY_MESSAGE),
    )
    angle_radians = np.linalg.norm(angular_velocity)
    total_angle = angle_radians * delta_time
    if total_angle == 0.0:
      return Rotation3.identity()

    epsilon_angle = 1e-6
    if np.abs(total_angle) < epsilon_angle:
      # Use sin(x) = x for very small values of x.
      q = quaternion_class.Quaternion(
          xyzw=np.hstack((0.5 * delta_time * angular_velocity, [1]))
      )
      return cls(quat=q, normalize=True)

    axis = angular_velocity / angle_radians
    q = quaternion_class.Quaternion(
        xyzw=np.hstack(
            ((math.sin(total_angle / 2) * axis), [math.cos(total_angle / 2)])
        )
    )
    return cls(quat=q, normalize=False)

  @classmethod
  def random(cls, rng: Optional[np.random.Generator] = None) -> 'Rotation3':
    """Returns a random rotation generated using a RNG.

    Note that these are not uniformly distributed over SO(3).

    Args:
      rng: A random number generator.

    Returns:
      A random Rotation3.
    """
    return cls(quat=quaternion_class.Quaternion.random_unit(rng=rng))

  # --------------------------------------------------------------------------
  # String representations
  # --------------------------------------------------------------------------

  def __str__(self) -> Text:
    """Returns a string that describes the rotation as a quaternion."""
    return 'Rotation3(%s)' % self._quaternion.normalize()

  def __repr__(self) -> Text:
    """Returns a string representation of the rotation.

    This representation can be used to construct the rotation.

    Returns:
      Returns the string, 'Rotation3(Quaternion([x, y, z, w]))'.
    """
    return 'Rotation3(%r)' % self._quaternion.normalize()
