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

from collections import abc
import math

from absl import logging
from absl.testing import absltest
from absl.testing import parameterized
import numpy as np

from intrinsic.math.python import math_test
from intrinsic.math.python import math_types
from intrinsic.math.python import quaternion
from intrinsic.math.python import rotation3
from intrinsic.math.python import vector_util

_ARCTAN_HALF = 2 * math.degrees(math.atan(0.5))  # 26.5650512


def _normalize(axis):
  """Returns the input vector as a numpy array with magnitude 1.0."""
  axis_as_array = np.asarray(axis, dtype=np.float64)
  return axis_as_array / np.linalg.norm(axis_as_array)


# Named test parameters.
_UNIT_QUATERNIONS = math_test.make_named_unit_quaternions()
_TEST_ROTATIONS = math_test.make_named_rotations()
_TEST_TINY_ROTATIONS = math_test.make_named_tiny_rotations()
_TEST_UNIT_VECTORS = math_test.make_named_unit_vectors()

# Test axis-angle pairs with non-zero axis vector and angle in [0, pi].
_TEST_AXIS_ANGLES = (
    (np.asarray([0.0, 0.0, 1.0]), 0.2),
    (_normalize([-1, -1, 0]), 0.7),
    (_normalize([1, 2, 3]), math.pi * 0.5),
    (np.asarray([0.0, -1.0, 0.0]), math.pi * 0.9),
)

_TEST_POINTS = math_test.make_named_vectors()


class Rotation3Test(parameterized.TestCase, math_test.TestCase):

  def _rotation_matrix(self, axis, angle):
    """Returns the 3x3 rotation matrix defined by axis and angle."""
    self.assert_vector_is_normalized(axis)
    sina = math.sin(angle)
    cosa = math.cos(angle)
    rotation_matrix = np.identity(3) * cosa
    rotation_matrix += np.outer(axis, axis) * (1.0 - cosa)
    sina_axis = sina * axis
    rotation_matrix += np.array(
        (
            (0.0, -sina_axis[2], sina_axis[1]),
            (sina_axis[2], 0.0, -sina_axis[0]),
            (-sina_axis[1], sina_axis[0], 0.0),
        ),
        dtype=np.float64,
    )
    return rotation_matrix

  def test_identity(self):
    rotation_def = rotation3.Rotation3()
    rotation_id = rotation3.Rotation3.identity()
    rotation_1 = rotation3.Rotation3(
        quaternion.Quaternion.one(), normalize=False
    )
    self.assertEqual(rotation_def, rotation_id)
    self.assertEqual(rotation_def, rotation_1)

  @parameterized.named_parameters(*_UNIT_QUATERNIONS)
  def test_init(self, quat):
    rotation = rotation3.Rotation3(quat, normalize=False)
    logging.info('%r %r', quat, rotation)
    self.assertEqual(rotation.quaternion, quat)
    rotation.quaternion.check_normalized()

  @parameterized.named_parameters(*_UNIT_QUATERNIONS)
  def test_init_normalize(self, quat):
    rotation = rotation3.Rotation3(quat, normalize=True)
    self.assertEqual(rotation.quaternion, quat.normalize())
    rotation.quaternion.check_normalized()

  @parameterized.named_parameters(('ones', np.ones(4)), ('1234', (1, 2, 3, 4)))
  def test_check_normalized_failure(self, xyzw):
    rot_q = rotation3.Rotation3(quaternion.Quaternion(xyzw))
    self.assertRaisesRegex(
        ValueError,
        quaternion.QUATERNION_NOT_NORMALIZED_MESSAGE,
        rot_q.quaternion.check_normalized,
    )
    rot_xyzw = rotation3.Rotation3.from_xyzw(xyzw)
    self.assertRaisesRegex(
        ValueError,
        quaternion.QUATERNION_NOT_NORMALIZED_MESSAGE,
        rot_xyzw.quaternion.check_normalized,
    )

  def test_init_zero_quaternion(self):
    q0 = quaternion.Quaternion(xyzw=np.zeros(4, dtype=np.float64))
    q1 = quaternion.Quaternion.one()
    r0 = rotation3.Rotation3(q0)
    self.assertEqual(r0, rotation3.Rotation3.identity())
    self.assertEqual(r0.quaternion, q1)

  @parameterized.named_parameters(
      ('tiny', np.full(4, 1e-12)), ('-tiny', np.full(4, -1e-12))
  )
  def test_init_error_tiny_quaternion(self, xyzw):
    expected_err_msg = '%s.*%s' % (
        quaternion.QUATERNION_ZERO_MESSAGE,
        rotation3.ROTATION3_INIT_MESSAGE,
    )
    self.assertRaisesRegex(
        ValueError,
        expected_err_msg,
        rotation3.Rotation3,
        quaternion.Quaternion(xyzw),
        True,
    )
    self.assertRaisesRegex(
        ValueError, expected_err_msg, rotation3.Rotation3.from_xyzw, xyzw, True
    )

  @parameterized.named_parameters(
      ('too many', np.arange(5)),
      ('too few', np.arange(3)),
  )
  def test_init_error_quaternion_components(self, components):
    self.assertRaisesRegex(
        ValueError,
        vector_util.VECTOR_COMPONENTS_MESSAGE,
        rotation3.Rotation3.from_xyzw,
        components,
    )

  def test_init_error_quaternion_values(self):
    self.assertRaisesRegex(
        ValueError,
        vector_util.VECTOR_INFINITE_VALUES_MESSAGE,
        rotation3.Rotation3.from_xyzw,
        [0, 0, 1, np.inf],
        False,
    )
    self.assertRaisesRegex(
        ValueError,
        vector_util.VECTOR_VALUES_MESSAGE,
        rotation3.Rotation3.from_xyzw,
        [0, np.nan, 1, 1],
        False,
    )

  @parameterized.parameters(*_TEST_AXIS_ANGLES)
  def test_axis_angle(self, axis, angle):
    rotation = rotation3.Rotation3.from_axis_angle(axis, angle)
    self.assert_all_close(rotation.axis(), axis)
    self.assertAlmostEqual(rotation.angle(), angle)
    self.assert_rotation_close(
        rotation, rotation3.Rotation3.from_axis_angle(-axis, -angle)
    )
    axis_out = rotation.axis()
    angle_out = rotation.angle()
    self.assert_all_equal(axis_out, rotation.axis())
    self.assertEqual(angle_out, rotation.angle())
    self.assert_rotation_close(
        rotation, rotation3.Rotation3.from_axis_angle(axis_out, angle_out)
    )
    # The negative of the quaternion represents the same rotation, so axis() and
    # angle() should return the identical result.
    rotation_neg = rotation3.Rotation3(-rotation.quaternion)
    self.assert_all_equal(rotation_neg.axis(), rotation.axis())
    self.assertEqual(rotation_neg.angle(), rotation.angle())
    axis_out = rotation_neg.axis()
    angle_out = rotation_neg.angle()
    self.assert_all_equal(axis_out, rotation.axis())
    self.assertEqual(angle_out, rotation.angle())

  @parameterized.named_parameters(*_TEST_TINY_ROTATIONS)
  def test_axis_angle_default_vector(self, rotation):
    axis = rotation.axis()
    angle = rotation.angle()
    self.assert_all_equal(axis, rotation3._DEFAULT_ROTATION3_AXIS)
    for default_name, default_axis in _TEST_UNIT_VECTORS:
      with self.subTest(default_name=default_name, default_axis=default_axis):
        axis_with_default = rotation.axis(default_axis=default_axis)
        angle_with_default = rotation.angle(default_axis=default_axis)
        self.assert_all_close(axis_with_default, default_axis)
        self.assertEqual(angle_with_default, angle)
        self.assert_all_equal(
            axis_with_default, rotation.axis(default_axis=default_axis)
        )
        self.assertEqual(
            angle_with_default, rotation.angle(default_axis=default_axis)
        )
        # Check behavior when both default_axis and direction_axis are
        # specified.
        self.assert_all_equal(
            axis_with_default,
            rotation.axis(
                default_axis=default_axis, direction_axis=default_axis
            ),
        )
        self.assert_all_equal(
            -axis_with_default,
            rotation.axis(
                default_axis=default_axis, direction_axis=-default_axis
            ),
        )

  @parameterized.named_parameters(*_TEST_ROTATIONS)
  def test_axis_angle_direction_vector(self, rotation):
    axis = rotation.axis()
    angle = rotation.angle()
    for direction_name, direction_axis in _TEST_UNIT_VECTORS:
      with self.subTest(
          direction_name=direction_name, direction_axis=direction_axis
      ):
        axis_with_direction = rotation.axis(direction_axis=direction_axis)
        angle_with_direction = rotation.angle(direction_axis=direction_axis)
        self.assertGreaterEqual(np.dot(axis_with_direction, direction_axis), 0)
        self.assert_all_equal(
            axis_with_direction, rotation.axis(direction_axis=direction_axis)
        )
        self.assertEqual(
            angle_with_direction, rotation.angle(direction_axis=direction_axis)
        )
        if np.dot(axis_with_direction, axis) < 0:
          self.assert_all_equal(axis_with_direction, -axis)
          self.assertEqual(angle_with_direction, -angle)
        else:
          self.assert_all_equal(axis_with_direction, axis)
          self.assertEqual(angle_with_direction, angle)

  @parameterized.parameters(*_TEST_AXIS_ANGLES)
  def test_axis_negative_angle(self, axis, angle):
    rotation = rotation3.Rotation3.from_axis_angle(axis, -angle)
    self.assert_all_close(-rotation.axis(), axis)
    self.assertAlmostEqual(rotation.angle(), angle)

  def test_default_rotation3_axis(self):
    """Checks that the default axis is a copy."""
    identity = rotation3.Rotation3.identity()
    axis = identity.axis()
    self.assert_all_equal(axis, (0, 0, 1))
    axis[0] = 1
    axis2 = identity.axis()
    self.assert_all_equal(axis2, (0, 0, 1))

  def check_tiny_rotation(self, rotation_tiny_angle, expected_angle):
    self.assertAlmostEqual(rotation_tiny_angle.angle(), expected_angle)
    self.assert_all_close(
        rotation_tiny_angle.axis(), rotation3._DEFAULT_ROTATION3_AXIS
    )
    axis_out = rotation_tiny_angle.axis()
    angle_out = rotation_tiny_angle.angle()
    self.assert_all_close(axis_out, [0, 0, 1])
    self.assertAlmostEqual(angle_out, expected_angle)
    self.assert_rotation_close(
        rotation_tiny_angle,
        rotation3.Rotation3.from_axis_angle(axis_out, angle_out),
    )

  @parameterized.named_parameters(*_TEST_ROTATIONS)
  def test_assert_rotation_close(self, rotation):
    self.assert_rotation_close(rotation, rotation)
    eps_angle = math_types.DEFAULT_ATOL_VALUE_FOR_NP_IS_CLOSE * 0.5
    tiny_rotation = rotation3.Rotation3.from_axis_angle([0, 0, 1], eps_angle)
    self.assert_rotation_close(rotation, rotation * tiny_rotation)
    self.assert_rotation_close(rotation, tiny_rotation * rotation)
    rotation_i = rotation3.Rotation3(
        quaternion.Quaternion(xyzw=vector_util.one_hot_vector(4, 0))
    )
    self.assertRaisesRegex(
        AssertionError,
        math_test.ROTATION_NOT_CLOSE_MESSAGE,
        self.assert_rotation_close,
        rotation,
        rotation * rotation_i,
    )

  @parameterized.named_parameters(_TEST_UNIT_VECTORS)
  def test_axis_tiny_angle(self, axis):
    rotation_0 = rotation3.Rotation3.from_axis_angle(axis, 0.0)
    self.check_tiny_rotation(rotation_0, 0.0)

    eps_angle = math_types.DEFAULT_ATOL_VALUE_FOR_NP_IS_CLOSE * 0.5
    rotation_eps = rotation3.Rotation3.from_axis_angle(axis, eps_angle)
    self.check_tiny_rotation(rotation_eps, eps_angle)

    rotation_eps = rotation3.Rotation3.from_axis_angle(axis, -eps_angle)
    self.check_tiny_rotation(rotation_eps, eps_angle)

  def test_axis_angle_errors(self):
    self.assertRaisesRegex(
        ValueError,
        rotation3.INVALID_AXIS_MESSAGE,
        rotation3.Rotation3.from_axis_angle,
        [0, 1],
        1,
    )
    self.assertRaisesRegex(
        ValueError,
        rotation3.INVALID_AXIS_MESSAGE,
        rotation3.Rotation3.from_axis_angle,
        [0, 0, 0],
        1,
    )
    self.assertRaisesRegex(
        ValueError,
        rotation3.INVALID_AXIS_MESSAGE,
        rotation3.Rotation3.from_axis_angle,
        [1e-12, 0, 0],
        1,
    )

  @parameterized.parameters(*_TEST_AXIS_ANGLES)
  def test_equivalent_rotations(self, axis, angle):
    rotation = rotation3.Rotation3.from_axis_angle(axis, angle)
    angle_plus_2pi = angle + 2 * math.pi
    angle_minus_2pi = angle - 2 * math.pi
    self.assert_rotation_close(
        rotation, rotation3.Rotation3.from_axis_angle(axis, angle_plus_2pi)
    )
    self.assert_rotation_close(
        rotation, rotation3.Rotation3.from_axis_angle(axis, angle_minus_2pi)
    )
    self.assertEqual(
        rotation, rotation3.Rotation3.from_axis_angle(-axis, -angle)
    )
    self.assert_rotation_close(
        rotation, rotation3.Rotation3.from_axis_angle(-axis, -angle_minus_2pi)
    )
    self.assert_rotation_close(
        rotation, rotation3.Rotation3.from_axis_angle(-axis, -angle_plus_2pi)
    )

  @parameterized.parameters(*_TEST_AXIS_ANGLES)
  def test_rotate_point(self, axis, angle):
    rotation = rotation3.Rotation3.from_axis_angle(axis, angle)
    matrix = self._rotation_matrix(axis, angle)
    for point_name, point in _TEST_POINTS:
      with self.subTest(point_name=point_name, point=point):
        p_rotated = rotation.rotate_point(point)
        p_by_matrix = np.matmul(matrix, np.asarray(point))
        self.assert_all_close(p_rotated, p_by_matrix)

  @parameterized.named_parameters(*_TEST_ROTATIONS)
  def test_inverse(self, rotation):
    inverse = rotation.inverse()
    self.assert_rotation_close(
        rotation * inverse, rotation3.Rotation3.identity()
    )
    self.assert_rotation_close(
        inverse * rotation, rotation3.Rotation3.identity()
    )
    self.assert_rotation_close(rotation, rotation.inverse().inverse())
    self.assertEqual(rotation, rotation.inverse().inverse())
    for point_name, point in _TEST_POINTS:
      with self.subTest(point_name=point_name, point=point):
        self.assert_all_close(
            rotation.rotate_point(inverse.rotate_point(point)), point
        )
        self.assert_all_close(
            inverse.rotate_point(rotation.rotate_point(point)), point
        )

  @parameterized.parameters(*_TEST_AXIS_ANGLES)
  def test_inverse_axis_angle(self, axis, angle):
    rotation = rotation3.Rotation3.from_axis_angle(axis, angle)
    inverse = rotation.inverse()
    self.assert_all_close(rotation.inverse().axis(), -axis)
    self.assertAlmostEqual(rotation.inverse().angle(), angle)
    self.assert_rotation_close(
        inverse * rotation, rotation3.Rotation3.identity()
    )
    self.assert_rotation_close(
        rotation * inverse, rotation3.Rotation3.identity()
    )
    self.assert_rotation_close(
        inverse, rotation3.Rotation3.from_axis_angle(axis, -angle)
    )
    self.assert_rotation_close(
        inverse, rotation3.Rotation3.from_axis_angle(-axis, angle)
    )

  @parameterized.parameters(*_TEST_AXIS_ANGLES)
  def test_matrix3x3(self, axis, angle):
    rotation = rotation3.Rotation3.from_axis_angle(axis, angle)
    rotation_matrix = self._rotation_matrix(axis, angle)
    logging.debug('q: %r\nmatrix: %r\n', rotation.quaternion, rotation_matrix)
    self.assert_all_close(rotation_matrix, rotation.matrix3x3())

  @parameterized.named_parameters(*_TEST_ROTATIONS)
  def test_from_matrix(self, rotation):
    rotation_matrix = rotation.matrix3x3()
    rotation3.check_rotation_matrix(rotation_matrix)
    rotation_from_matrix = rotation3.Rotation3.from_matrix(rotation_matrix)
    self.assertGreaterEqual(
        rotation_from_matrix.quaternion.w, 0, rotation_from_matrix
    )
    self.assert_rotation_close(rotation, rotation_from_matrix)

  def test_from_matrix_identity(self):
    rotation3.check_rotation_matrix(np.identity(3))
    self.assertEqual(
        rotation3.Rotation3.from_matrix(np.identity(3)),
        rotation3.Rotation3.identity(),
    )

    rotation3.check_rotation_matrix(np.identity(4))
    self.assertEqual(
        rotation3.Rotation3.from_matrix(np.identity(4)),
        rotation3.Rotation3.identity(),
    )

  @parameterized.parameters([
      (np.identity(3) * 2.0,),
      (np.ones((3, 3)),),
      (np.ones((4, 4)),),
  ])
  def test_from_matrix_not_orthogonal(self, not_orthogonal_matrix):
    self.assertRaisesRegex(
        ValueError,
        rotation3.MATRIX_NOT_ORTHOGONAL_MESSAGE,
        rotation3.check_rotation_matrix,
        not_orthogonal_matrix,
    )
    self.assertRaisesRegex(
        ValueError,
        rotation3.MATRIX_NOT_ORTHOGONAL_MESSAGE,
        rotation3.Rotation3.from_matrix,
        not_orthogonal_matrix,
    )

  @parameterized.parameters([
      (np.diag([-1.0, 1.0, 1.0]),),
      (np.diag([1.0, -1.0, 1.0]),),
      (np.diag([1.0, 1.0, -1.0]),),
      (-np.identity(3),),
      (np.diag([-1.0, 1.0, 1.0, 1.0]),),
      (-np.identity(4),),
  ])
  def test_from_matrix_reflection(self, reflection_matrix):
    self.assertRaisesRegex(
        ValueError,
        rotation3.MATRIX_NOT_PROPER_ROTATION_MESSAGE,
        rotation3.check_rotation_matrix,
        reflection_matrix,
    )
    self.assertRaisesRegex(
        ValueError,
        rotation3.MATRIX_NOT_PROPER_ROTATION_MESSAGE,
        rotation3.Rotation3.from_matrix,
        reflection_matrix,
    )

  def test_from_matrix_reflection_keeps_err_msg(self):
    with self.assertRaisesRegex(ValueError, 'caller context'):
      rotation3.check_rotation_matrix(
          np.diag([-1.0, 1.0, 1.0]), err_msg='caller context'
      )
    with self.assertRaisesRegex(ValueError, 'caller context'):
      rotation3.Rotation3.from_matrix(
          np.diag([-1.0, 1.0, 1.0]), err_msg='caller context'
      )

  @parameterized.parameters([
      (np.zeros((1, 2, 3)),),
      (np.zeros(7),),
      (np.identity(2),),
      (np.zeros((2, 4)),),
      (np.zeros((3, 1)),),
  ])
  def test_from_matrix_wrong_shape(self, wrong_shape_matrix):
    wrong_shape_matrix = np.asarray(wrong_shape_matrix)
    self.assertRaisesRegex(
        ValueError,
        rotation3.MATRIX_WRONG_SHAPE_MESSAGE,
        rotation3.check_rotation_matrix,
        wrong_shape_matrix,
    )
    self.assertRaisesRegex(
        ValueError,
        rotation3.MATRIX_WRONG_SHAPE_MESSAGE,
        rotation3.Rotation3.from_matrix,
        wrong_shape_matrix,
    )

  @parameterized.parameters(
      ((0, 4, 0, 0),),
      ((1, -1, 1, -1),),
      ((1, 2, 3, 4),),
  )
  def test_normalized_vs_not(self, xyzw):
    rotation_unnormalized = rotation3.Rotation3.from_xyzw(xyzw, normalize=False)
    rotation_normalized = rotation3.Rotation3.from_xyzw(xyzw, normalize=True)
    for v_name, v in _TEST_UNIT_VECTORS:
      with self.subTest(v_name=v_name, v=v):
        self.assert_all_close(
            rotation_unnormalized.rotate_point(v),
            rotation_normalized.rotate_point(v),
        )

  @parameterized.parameters(
      ((90, 0, 0), (1, 0, 0, 1)),
      ((0, 90, 0), (0, 1, 0, 1)),
      ((0, 0, 90), (0, 0, 1, 1)),
      ((-90, 0, 0), (-1, 0, 0, 1)),
      ((0, -90, 0), (0, -1, 0, 1)),
      ((0, 0, -90), (0, 0, -1, 1)),
      ((30, 30, 0), (0.25, 0.25, -0.0669872981, 0.933012702)),
      ((0, 30, 30), (-0.0669872981, 0.25, 0.25, 0.933012702)),
      ((30, 0, 30), (0.25, 0.0669872981, 0.25, 0.933012702)),
      ((-30, -30, 0), (-0.25, -0.25, -0.0669872981, 0.933012702)),
      ((0, -30, -30), (-0.0669872981, -0.25, -0.25, 0.933012702)),
      ((-30, 0, -30), (-0.25, 0.0669872981, -0.25, 0.933012702)),
      ((_ARCTAN_HALF, 0, 0), (1, 0, 0, 2)),
      ((0, _ARCTAN_HALF, 0), (0, 1, 0, 2)),
      ((0, 0, _ARCTAN_HALF), (0, 0, 1, 2)),
      ((180 - _ARCTAN_HALF, 0, 0), (2, 0, 0, 1)),
      ((180, _ARCTAN_HALF, 180), (0, 2, 0, 1)),
      ((0, 0, 180 - _ARCTAN_HALF), (0, 0, 2, 1)),
      ((_ARCTAN_HALF, _ARCTAN_HALF, _ARCTAN_HALF), (2, 6, 2, 9)),
  )
  def test_euler_angles(self, rpy_degrees, xyzw):
    rpy_radians = np.radians(rpy_degrees)
    rotation = rotation3.Rotation3.from_xyzw(xyzw, normalize=True)
    rotation_rpy = rotation3.Rotation3.from_euler_angles(
        rpy_degrees=rpy_degrees
    )
    self.assert_rotation_close(rotation, rotation_rpy)
    self.assert_all_close(rpy_degrees, rotation_rpy.euler_angles(radians=False))
    self.assert_all_close(rpy_degrees, rotation.euler_angles(radians=False))
    self.assert_all_close(rpy_radians, rotation_rpy.euler_angles(radians=True))
    self.assert_all_close(rpy_radians, rotation.euler_angles(radians=True))
    rotation_rpy_radians = rotation3.Rotation3.from_euler_angles(
        rpy_radians=np.radians(rpy_degrees)
    )
    self.assert_rotation_close(rotation, rotation_rpy_radians)
    self.assert_all_close(rpy_degrees, rotation_rpy_radians.euler_angles())

  @parameterized.named_parameters(*_TEST_ROTATIONS)
  def test_euler_angles_correctness(self, rotation):
    rpy_degrees = rotation.euler_angles(radians=False)
    rotation_from_rpy = rotation3.Rotation3.from_euler_angles(
        rpy_degrees=rpy_degrees
    )
    self.assert_rotation_close(rotation, rotation_from_rpy)
    rpy_radians = rotation.euler_angles(radians=True)
    self.assert_all_close(rpy_degrees, np.degrees(rpy_radians))
    self.assert_rotation_close(
        rotation, rotation3.Rotation3.from_euler_angles(rpy_radians=rpy_radians)
    )
    roll, pitch, yaw = rpy_radians[:]
    rotation_neg_roll = rotation3.Rotation3.from_axis_angle((1, 0, 0), -roll)
    rotation_neg_pitch = rotation3.Rotation3.from_axis_angle((0, 1, 0), -pitch)
    rotation_neg_yaw = rotation3.Rotation3.from_axis_angle((0, 0, 1), -yaw)
    rotation_roll = rotation_neg_pitch * rotation_neg_yaw * rotation
    roll_axis = rotation_roll.axis(
        default_axis=(1, 0, 0), direction_axis=(1, 0, 0)
    )
    roll_out = rotation_roll.angle(
        default_axis=(1, 0, 0), direction_axis=(1, 0, 0)
    )
    self.assert_all_close(roll_axis, (1, 0, 0))
    self.assert_angles_in_radians_close(roll_out, roll)
    rotation_pitch = rotation_neg_yaw * rotation * rotation_neg_roll
    pitch_axis = rotation_pitch.axis(
        default_axis=(0, 1, 0), direction_axis=(0, 1, 0)
    )
    pitch_out = rotation_pitch.angle(
        default_axis=(0, 1, 0), direction_axis=(0, 1, 0)
    )
    self.assert_all_close(pitch_axis, (0, 1, 0))
    self.assert_angles_in_radians_close(pitch_out, pitch)
    rotation_yaw = rotation * rotation_neg_roll * rotation_neg_pitch
    yaw_axis = rotation_yaw.axis(
        default_axis=(0, 0, 1), direction_axis=(0, 0, 1)
    )
    yaw_out = rotation_yaw.angle(
        default_axis=(0, 0, 1), direction_axis=(0, 0, 1)
    )
    self.assert_all_close(yaw_axis, (0, 0, 1))
    self.assert_angles_in_radians_close(yaw_out, yaw)

  def test_from_euler_angles_errors(self):
    self.assertRaisesRegex(
        ValueError,
        'Rotation3.from_euler_angles requires rpy_degrees or rpy_radians.',
        rotation3.Rotation3.from_euler_angles,
    )

  @parameterized.named_parameters(*_TEST_ROTATIONS)
  def test_multiply(self, rotation1):
    """Verifies correctness of multiply according to its definition."""
    for rotation2_name, rotation2 in _TEST_ROTATIONS:
      with self.subTest(rotation2_name=rotation2_name, rotation2=rotation2):
        rotation12 = rotation1 * rotation2
        self.assertEqual(
            rotation12.quaternion, rotation1.quaternion * rotation2.quaternion
        )
        for point_name, point in _TEST_POINTS:
          with self.subTest(point_name=point_name, point=point):
            self.assert_all_close(
                rotation12.rotate_point(point),
                rotation1.rotate_point(rotation2.rotate_point(point)),
            )

  @parameterized.parameters(*_TEST_AXIS_ANGLES)
  def test_multiply_axis_angle(self, axis, angle):
    rotation1 = rotation3.Rotation3.from_axis_angle(axis, angle)
    rotation2 = rotation3.Rotation3.from_axis_angle(axis, angle * -0.3)
    self.assert_all_close(rotation1.axis(), axis)
    self.assert_all_close(rotation2.axis(), -axis)
    self.assertAlmostEqual(rotation1.angle(), angle)
    self.assertAlmostEqual(rotation2.angle(), angle * 0.3)
    rotation12 = rotation1 * rotation2
    self.assert_all_close(rotation12.axis(), axis)
    self.assertAlmostEqual(rotation12.angle(), angle * 0.7)

  @parameterized.parameters(*_TEST_AXIS_ANGLES)
  def test_multiply_identity(self, axis, angle):
    rotation1 = rotation3.Rotation3.from_axis_angle(axis, angle)
    rotation2 = rotation3.Rotation3.from_axis_angle(axis, angle * -0.3)
    rotation_identity = rotation3.Rotation3.identity()
    self.assert_rotation_close(rotation1, rotation1 * rotation_identity)
    self.assert_rotation_close(rotation1, rotation_identity * rotation1)
    self.assert_rotation_close(rotation2, rotation2 * rotation_identity)
    self.assert_rotation_close(rotation2, rotation_identity * rotation2)

  @parameterized.parameters(
      (
          (np.array([0, 0, 1]), math.pi * 0.5),
          (np.array([0, 1, 0]), math.pi * 0.5),
          (_normalize([-1, 1, 1]), math.pi * (2.0 / 3.0)),
      ),
  )
  def test_multiply_pair(self, axis_angle1, axis_angle2, axis_angle12):
    rotation1 = rotation3.Rotation3.from_axis_angle(*axis_angle1)
    rotation2 = rotation3.Rotation3.from_axis_angle(*axis_angle2)
    rotation12 = rotation3.Rotation3.from_axis_angle(*axis_angle12)
    rotation1m2 = rotation1 * rotation2
    logging.debug(
        '%s * %s = %s (%s, %s)',
        rotation1,
        rotation2,
        rotation1m2,
        rotation1m2.axis(),
        rotation1m2.angle() / math.pi,
    )
    self.assertAlmostEqual(rotation12.angle(), rotation1m2.angle())
    self.assert_all_close(rotation12.axis(), rotation1m2.axis())
    self.assert_rotation_close(rotation12, rotation1 * rotation2)

  @parameterized.named_parameters(*_TEST_ROTATIONS)
  def test_divide(self, rotation1):
    for name2, rotation2 in _TEST_ROTATIONS:
      with self.subTest(name2=name2, rotation2=rotation2):
        rotation12 = rotation1 / rotation2
        self.assertEqual(
            rotation12.quaternion,
            rotation1.quaternion * rotation2.quaternion.conjugate(),
        )
        for point_name, point in _TEST_POINTS:
          self.assert_all_close(
              rotation12.rotate_point(point),
              rotation1.rotate_point(rotation2.inverse().rotate_point(point)),
              err_msg='point: %s %r' % (point_name, point),
          )

  @parameterized.parameters(*_TEST_AXIS_ANGLES)
  def test_divide_axis_angle(self, axis, angle):
    rotation1 = rotation3.Rotation3.from_axis_angle(axis, angle * 0.6)
    rotation2 = rotation3.Rotation3.from_axis_angle(axis, angle * -0.3)
    self.assert_all_close(rotation1.axis(), axis)
    self.assert_all_close(rotation2.axis(), -axis)
    self.assertAlmostEqual(rotation1.angle(), angle * 0.6)
    self.assertAlmostEqual(rotation2.angle(), angle * 0.3)
    rotation12 = rotation1 / rotation2
    self.assert_all_close(rotation12.axis(), axis)
    self.assertAlmostEqual(rotation12.angle(), angle * 0.9)

  @parameterized.parameters(*_TEST_AXIS_ANGLES)
  def test_divide_identity(self, axis, angle):
    rotation1 = rotation3.Rotation3.from_axis_angle(axis, angle)
    rotation2 = rotation3.Rotation3.from_axis_angle(axis, angle * -0.3)
    rotation_identity = rotation3.Rotation3.identity()
    self.assertTrue(rotation1.almost_equal(rotation1 / rotation_identity))
    self.assertTrue(
        rotation1.inverse().almost_equal(rotation_identity / rotation1)
    )
    self.assertTrue(rotation2.almost_equal(rotation2 / rotation_identity))
    self.assertTrue(
        rotation2.inverse().almost_equal(rotation_identity / rotation2)
    )

  @parameterized.parameters(
      (
          (np.array([0, 0, 1]), math.pi * 0.5),
          (np.array([0, 1, 0]), math.pi * 0.5),
          (_normalize([1, -1, 1]), math.pi * (2.0 / 3.0)),
      ),
  )
  def test_divide_pair(self, axis_angle1, axis_angle2, axis_angle12):
    rotation1 = rotation3.Rotation3.from_axis_angle(*axis_angle1)
    rotation2 = rotation3.Rotation3.from_axis_angle(*axis_angle2)
    rotation12 = rotation3.Rotation3.from_axis_angle(*axis_angle12)
    rotation1d2 = rotation1 / rotation2
    logging.debug(
        '%s / %s = %s (%s, %s)',
        rotation1,
        rotation2,
        rotation1d2,
        rotation1d2.axis(),
        rotation1d2.angle() / math.pi,
    )
    self.assertAlmostEqual(rotation12.angle(), rotation1d2.angle())
    self.assert_all_close(rotation12.axis(), rotation1d2.axis())
    self.assertTrue(rotation12.almost_equal(rotation1 / rotation2))

  def test_random(self):
    for _ in range(10):
      random_rotation = rotation3.Rotation3.random()
      self.assertNotEqual(random_rotation, rotation3.Rotation3.identity())

  def test_random_seeded(self):
    rng_a = np.random.default_rng(seed=1)
    rng_a2 = np.random.default_rng(seed=1)
    rng_b = np.random.default_rng(seed=2)
    rng_c = np.random.default_rng()
    for _ in range(10):
      rota = rotation3.Rotation3.random(rng=rng_a)
      self.assertEqual(rota, rotation3.Rotation3.random(rng=rng_a2))
      self.assertNotEqual(rota, rotation3.Rotation3.random(rng=rng_b))
      self.assertNotEqual(rota, rotation3.Rotation3.random(rng=rng_c))
      self.assertNotEqual(rota, rotation3.Rotation3.random())

  def check_equal_rotations(self, rotation1, rotation2):
    self.check_eq_and_ne_for_equal_values(rotation1, rotation2)
    # The definition of equal rotations is that all points are transformed
    # identically.
    for point_name, point in _TEST_POINTS:
      with self.subTest(point_name=point_name, point=point):
        self.assert_all_equal(
            rotation1.rotate_point(point), rotation2.rotate_point(point)
        )

  @parameterized.named_parameters(*_TEST_ROTATIONS)
  def test_eq(self, rotation):
    self.check_equal_rotations(rotation, rotation)
    self.check_equal_rotations(
        rotation, rotation3.Rotation3(rotation.quaternion)
    )
    self.check_equal_rotations(
        rotation, rotation3.Rotation3(-rotation.quaternion)
    )

  def test_eq_other_type(self):
    self.assertNotEqual(rotation3.Rotation3.identity(), 'string')

  def check_unequal_rotations(self, rotation1, rotation2):
    self.check_eq_and_ne_for_unequal_values(rotation1, rotation2)
    # At least one point should have a different result from the two rotations.
    all_equal = True
    for _, point in _TEST_POINTS:
      if not np.all(
          rotation1.rotate_point(point) == rotation2.rotate_point(point)
      ):
        all_equal = False
    self.assertFalse(all_equal, '%r != %r' % (rotation1, rotation2))

  @parameterized.named_parameters(*_TEST_ROTATIONS)
  def test_ne(self, rotation):
    self.check_unequal_rotations(
        rotation,
        rotation
        * rotation3.Rotation3(
            quaternion.Quaternion(xyzw=vector_util.one_hot_vector(4, 0))
        ),
    )
    self.check_unequal_rotations(
        rotation3.Rotation3.from_axis_angle([0, 0, 1], 1e-8) * rotation,
        rotation,
    )

  def test_hash(self):
    self.assertFalse(issubclass(rotation3.Rotation3, abc.Hashable))

  def test_str(self):
    """Test function for Rotation3.__str__ function."""
    quat = quaternion.Quaternion([-0.1, 0.7, -0.5, 0.5])
    rotation = rotation3.Rotation3(quat)
    expected_string = 'Rotation3([-0.1i + 0.7j + -0.5k + 0.5])'
    logging.info('Rotation3.__str__ = %s', rotation)
    self.assertEqual(str(rotation), expected_string)
    self.assertEqual(rotation.__str__(), expected_string)

  def test_str_normalized(self):
    """Test function for Rotation3.__str__ function."""
    quat = quaternion.Quaternion([1, -1, -1, 1])
    rotation = rotation3.Rotation3(quat)
    expected_string = 'Rotation3([0.5i + -0.5j + -0.5k + 0.5])'
    logging.info('Rotation3.__str__ = %s', rotation)
    self.assertEqual(str(rotation), expected_string)
    self.assertEqual(rotation.__str__(), expected_string)

  def test_repr(self):
    """Test function for Rotation3.__repr__ function."""
    quat = quaternion.Quaternion([-0.5, 0.5, -0.5, 0.5])
    rotation = rotation3.Rotation3(quat)
    expected_string = 'Rotation3(Quaternion([-0.5, 0.5, -0.5, 0.5]))'
    logging.info('Rotation3.__repr__ = %r', rotation)
    self.assertEqual(rotation.__repr__(), expected_string)

  def test_repr_normalized(self):
    """Test function for Rotation3.__repr__ function."""
    quat = quaternion.Quaternion([1, -1, -1, 1])
    rotation = rotation3.Rotation3(quat)
    expected_string = 'Rotation3(Quaternion([0.5, -0.5, -0.5, 0.5]))'
    logging.info('Rotation3.__repr__ = %s', rotation)
    self.assertEqual(repr(rotation), expected_string)
    self.assertEqual(rotation.__repr__(), expected_string)

  @parameterized.named_parameters(_TEST_UNIT_VECTORS)
  def test_angular_velocity(self, axis):
    omega = axis
    self.assertEqual(
        rotation3.Rotation3.from_angular_velocity(omega, 0.0),
        rotation3.Rotation3.identity(),
    )
    for scale in [0.0, 1.0, 2.0, 1000.0, 1e-2, 1e-4, 1e-8]:
      rotation_scaled_time = rotation3.Rotation3.from_angular_velocity(
          omega, scale
      )
      rotation_scaled_omega = rotation3.Rotation3.from_angular_velocity(
          omega * scale, 1.0
      )
      self.assert_rotation_close(rotation_scaled_time, rotation_scaled_omega)
      self.assert_rotation_close(
          rotation_scaled_time,
          rotation3.Rotation3.from_axis_angle(omega, scale),
      )
      self.assert_rotation_close(
          rotation_scaled_time,
          rotation3.Rotation3.from_angular_velocity(omega, -scale).inverse(),
      )
      self.assert_rotation_close(
          rotation_scaled_time,
          rotation3.Rotation3.from_angular_velocity(-scale * omega).inverse(),
      )
      self.assert_rotation_close(
          rotation_scaled_time,
          rotation3.Rotation3.from_angular_velocity(-scale * omega, -1.0),
      )

  def test_angular_velocity_zero(self):
    self.assertEqual(
        rotation3.Rotation3.from_angular_velocity(np.zeros(3), 0.0),
        rotation3.Rotation3.identity(),
    )
    self.assertEqual(
        rotation3.Rotation3.from_angular_velocity(np.zeros(3), 1000.0),
        rotation3.Rotation3.identity(),
    )

  def test_angular_velocity_errors(self):
    self.assert_rotation_close(
        rotation3.Rotation3.from_angular_velocity([2, 1, 2]),
        rotation3.Rotation3.from_axis_angle(
            [2.0 / 3.0, 1.0 / 3.0, 2.0 / 3.0], 3
        ),
    )
    self.assertRaisesRegex(
        ValueError,
        rotation3.INVALID_ANGULAR_VELOCITY_MESSAGE,
        rotation3.Rotation3.from_angular_velocity,
        [2, 1, 2, 3],
    )
    self.assertRaisesRegex(
        ValueError,
        rotation3.INVALID_ANGULAR_VELOCITY_MESSAGE,
        rotation3.Rotation3.from_angular_velocity,
        np.ones(2),
    )


if __name__ == '__main__':
  absltest.main()
