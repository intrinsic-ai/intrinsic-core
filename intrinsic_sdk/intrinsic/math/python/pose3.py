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

"""Pose3 class (python3).

6 degree-of-freedom (6DoF) rigid pose representation described by a rotation and
a translation.

Pose names should follow go/pose-conventions

A transform from a local frame to the world frame would be named
world_pose_local.  If p_local is a point in the local frame, we could calculate
its position in the world frame with:

  p_world = world_pose_local * p_local

If an object were posed in the local frame with the pose local_pose_object, we
could calculate its world pose as:

  world_pose_object = world_pose_local * local_pose_object

The inverse of a pose is the reverse transformation:

  local_pose_world = world_pose_local.inverse()
"""

from typing import Optional
from typing import Text

import numpy as np

from intrinsic.math.python import math_types
from intrinsic.math.python import quaternion as quaternion_class
from intrinsic.math.python import rotation3
from intrinsic.math.python import vector_util

# ----------------------------------------------------------------------------
# Pytype definitions.

# ----------------------------------------------------------------------------
# Constants
#
HOMOGENEOUS_MATRIX_FORM = (
    '\n| +-----+  tx |\n| |  R  |  ty |\n| +-----+  tz |\n| 0  0  0  1  |\n'
)

# ----------------------------------------------------------------------------
# Error messages for exceptions.
TRANSLATION_INVALID_MESSAGE = 'Translation vector in Pose3.'
MATRIX_INVALID_MESSAGE = 'Matrix should be a 4x4 homogeneous transform'


class Pose3(object):
  """A class which represents a pose as a rotation and a translation.

  The pose is a rigid 6 degree-of-freedom transformation represented
  as a Rotation3 object and a 3D translation vector.

  Properties:
    rotation: Rotation as a Rotation3 object.
    quaternion: Rotation as a quaternion.
    translation: 3D translation vector as numpy array.
    vec7: Seven-value representation used in blue, [tx, ty, tz, qx, qy, qz, qw].

  Factory functions:
    identity
    from_matrix_4x4
    from_vec7
  """

  def __init__(
      self,
      rotation: Optional[rotation3.Rotation3] = None,
      translation: Optional[math_types.VectorType] = None,
  ):
    """Constructs a new pose.

    Initializes a new pose object.

    If no parameters are specified the default will be an identity
    transformation.

    Args:
      rotation: 3D rotation component of pose, identity by default.
      translation: translation vector component of pose, zero by default.
    """
    self._rotation = rotation or rotation3.Rotation3.identity()
    if translation is None:
      self._translation = np.zeros(3, dtype=np.float64)
    else:
      self._translation = vector_util.as_vector3(
          translation, err_msg=TRANSLATION_INVALID_MESSAGE
      ).copy()

  # --------------------------------------------------------------------------
  # Properties
  # --------------------------------------------------------------------------

  @property
  def rotation(self) -> rotation3.Rotation3:
    """Accessor to extract the rotation property."""
    return self._rotation

  @property
  def quaternion(self) -> quaternion_class.Quaternion:
    """Accessor to extract the quaternion property.

    Returns the quaternion representation of the rotation component of the pose.

    Returns:
      A unit quaternion.
    """
    return quaternion_class.Quaternion(xyzw=self.rotation.quaternion.xyzw)

  @property
  def translation(self) -> np.ndarray:
    """Accessor to extract the translation property.

    Returns the translation components from the pose.

    Returns:
     A 3D vector in a numpy array.
    """
    return self._translation.copy()

  @property
  def vec7(self) -> np.ndarray:
    """Returns seven-value representation: [tx, ty, tz, qx, qy, qz, qw]."""
    return np.hstack((self.translation, self.quaternion.xyzw))

  # --------------------------------------------------------------------------
  # Utility functions
  # --------------------------------------------------------------------------

  def transform_point(self, point: math_types.VectorType) -> np.ndarray:
    """Transforms the point by the pose.

    The transform operates on the point by rotating it and then adding the
    translation.

      T(v) = R(v) + p

    If we have a transform from coordinate frame A to the world coordinate
    frame, world_pose_A, and a point, point_A, in the coordinate frame A, we can
    calculate the point in the world coordinate frame as:
    world_pose_A.transform_point(point_A) = point_world

    Args:
      point: 3D vector to be transformed.

    Returns:
     A 3D vector in a numpy array.

    Raises:
      ValueError: Raised by rotate_point if the point is not a finite 3D vector.
    """
    return self.rotation.rotate_point(point) + self.translation

  def inverse(self) -> 'Pose3':
    """Calculates the inverse of this transform.

    If this pose calculates the transform from coordinate system A to
    B, the result will be the transform from B to A.

      (B_pose_A)' = A_pose_B
       B  <-  A     A  <-  B

    If we let ' denote the inverse of an operator:

      T(v) = R(v) + p
      T'(v) = R'(v - p) = R'(v) - R'(p)  because rotation is linear
      (T o T')(v) == v

    Returns:
      A pose representing the inverse.
    """
    rotation_inverse = self.rotation.inverse()
    translation_inverse = -(rotation_inverse.rotate_point(self.translation))
    return Pose3(rotation=rotation_inverse, translation=translation_inverse)

  def multiply(self, other: 'Pose3') -> 'Pose3':
    """Calculates the product (or composition) of the two transforms.

    Multiplies this transform by the other (self * other).

    If this pose calculates the transform from coordinate frame B to C and the
    other represents the transform from frame A to B, the result will be
    the transform from frame A to frame C.
      C_pose_B * B_pose_A  =  C_pose_A
        C  <-  B  <-  A       C  <-  A

    T1(v) = q1 v q1' + p1
    T2(v) = q2 v q2' + p2
    (T1 o T2)(v) = q2 (q1 v q1' + p1) q2' + p2
                 = q2 q1 v q1' q2' + q2 p1 q2' + p2
                 = (q2 q1) v (q2 q1)' + T2(p1)

    Args:
      other: Right hand operand as a Pose3.

    Returns:
      Product of the two poses as a Pose3.
    """

    result_translation = self.transform_point(other.translation)
    result_rotation = self.rotation * other.rotation
    return Pose3(rotation=result_rotation, translation=result_translation)

  def multiply_by_inverse(self, other: 'Pose3') -> 'Pose3':
    """Calculates the product of the inverse of this transform with the other.

    Multiplies the inverse of this transform by the other (self^-1 * other).

    This is more stable than computing the inverse and then the product, because
    the translation components of the two poses are subtracted directly without
    rotation.

    This operation is commonly used when two transforms are given with respect
    to a world coordinate frame and we want a direct transform from one frame to
    the other.  Given world_pose_A and world_pose_B, we calculate A_pose_B as
      (world_pose_A)^-1 * world_pose_B,
    which would be calculated with:
      world_pose_A.multiply_by_inverse(world_pose_B)

    Args:
      other: Right hand operand as a Pose3.

    Returns:
      Product of the inverse of this transform with the other as a Pose3.
    """
    rotation_inverse = self.rotation.inverse()
    result_translation = rotation_inverse.rotate_point(
        other.translation - self.translation
    )
    result_rotation = rotation_inverse * other.rotation
    return Pose3(rotation=result_rotation, translation=result_translation)

  def almost_equal(
      self,
      other: 'Pose3',
      rtol: float = math_types.DEFAULT_RTOL_VALUE_FOR_NP_IS_CLOSE,
      atol: float = math_types.DEFAULT_ATOL_VALUE_FOR_NP_IS_CLOSE,
  ) -> bool:
    """Tests whether the two poses are equivalent within tolerances.

    The two poses are equivalent if the maximum absolute difference between the
    elements of the two poses is <= atol and the relative difference is <= rtol.
    The negative of a quaternion performs the same rotation as the original
    quaternion.

    Args:
      other: another pose to test against this one
      rtol: relative tolerance
      atol: absolute tolerance

    Returns:
      True if the two poses are equivalent.
    """
    return np.allclose(
        self.translation, other.translation, rtol=rtol, atol=atol
    ) and self.rotation.almost_equal(other.rotation, rtol=rtol, atol=atol)

  def matrix4x4(self) -> np.ndarray:
    """Converts the pose to a 4x4 homogeneous transformation matrix.

    Retrieves the matrix4x4 representation of the pose.

    Returns:
      The 4x4 matrix transformation.
    """
    matrix4x4 = np.identity(4)
    matrix4x4[:3, :3] = self.rotation.matrix3x3()
    matrix4x4[:3, 3] = self.translation
    return matrix4x4

  # --------------------------------------------------------------------------
  # Operators
  # --------------------------------------------------------------------------

  def __mul__(self, other: 'Pose3') -> 'Pose3':
    """Returns the product: self * other."""
    return self.multiply(other)

  def __eq__(self, other: 'Pose3') -> bool:
    """Returns true iff the two poses are precisely equivalent."""
    if not isinstance(other, type(self)):
      return NotImplemented
    return (
        np.all(self.translation == other.translation)
        and self.rotation == other.rotation
    )

  def __ne__(self, other: 'Pose3') -> bool:
    """Returns true iff the two poses are not precisely equivalent."""
    return not self == other

  # --------------------------------------------------------------------------
  # Factory functions
  # --------------------------------------------------------------------------

  @classmethod
  def identity(cls) -> 'Pose3':
    return cls()

  @classmethod
  def from_matrix4x4(
      cls,
      matrix4x4: np.ndarray,
      rtol: float = math_types.DEFAULT_RTOL_VALUE_FOR_NP_IS_CLOSE,
      atol: float = math_types.DEFAULT_ATOL_VALUE_FOR_NP_IS_CLOSE,
      err_msg='',
  ) -> 'Pose3':
    """Constructor from 4x4 transformation matrix.

    Sets the pose from its matrix4x4 representation.

      | +-----+  tx |
      | |  R  |  ty |
      | +-----+  tz |
      | 0  0  0  1  |

    Args:
      matrix4x4: The 4x4 matrix transformation.
      rtol: relative error tolerance, passed through to np.allclose.
      atol: absolute error tolerance, passed through to np.allclose.
      err_msg: Error message that is added to exception in case of failure.

    Returns:
      A Pose3 that computes the same rigid transformation as the matrix.

    Raises:
      ValueError: If input has wrong shape or its rotation part is not a proper
        rotation (not orthogonal, or a reflection with determinant -1).
    """
    if matrix4x4.shape != (4, 4):
      raise ValueError(
          'Matrix should be a 4x4 homogeneous transform: %s Actual: %s\n%r\n%s'
          % (HOMOGENEOUS_MATRIX_FORM, matrix4x4.shape, matrix4x4, err_msg)
      )
    if not np.all([0, 0, 0, 1] == matrix4x4[3, :]):
      raise ValueError(
          '%s: %s Actual:\n%r\n%s'
          % (
              MATRIX_INVALID_MESSAGE,
              HOMOGENEOUS_MATRIX_FORM,
              matrix4x4,
              err_msg,
          )
      )
    rotation = rotation3.Rotation3.from_matrix(
        matrix4x4[:3, :3], rtol=rtol, atol=atol, err_msg=err_msg
    )
    translation = matrix4x4[:3, 3]
    return cls(rotation=rotation, translation=translation)

  @classmethod
  def from_vec7(
      cls, vec7_values: math_types.VectorType, normalize: bool = False
  ) -> 'Pose3':
    """Constructs pose from [tx, ty, tz, qx, qy, qz, qw].

    Constructs pose from the seven-value representation used in blue:
      [tx, ty, tz, qx, qy, qz, qw]

    Args:
      vec7_values: The vec7 vector [tx, ty, tz, qx, qy, qz, qw].
      normalize: Indicates whether to normalize the quaternion.

    Returns:
      Pose3 constructed from translation vector, [tx, ty, tz], and rotation
      quaternion, [qx, qy, qz, qw].
    """
    vec7 = vector_util.as_finite_vector(
        vec7_values, dimension=7, dtype=np.float64
    )
    return Pose3(
        rotation=rotation3.Rotation3.from_xyzw(
            xyzw=vec7[3:7], normalize=normalize
        ),
        translation=vec7[:3],
    )

  # --------------------------------------------------------------------------
  # String representations
  # --------------------------------------------------------------------------

  def __str__(self) -> Text:
    """Generates a string that describes the pose.

    Generates a human-readable string that describes the translation
    and rotation of the pose.

    Returns:
      A string describing the pose.
    """
    return 'Pose3(%s,%s)' % (self.rotation, self.translation)

  def __repr__(self) -> Text:
    return 'Pose3(%r,%r)' % (self.rotation, self.translation)
