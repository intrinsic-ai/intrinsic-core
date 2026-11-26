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

from absl import logging
from absl.testing import absltest
from absl.testing import parameterized
import numpy as np

from intrinsic.math.python import math_test
from intrinsic.math.python import math_types
from intrinsic.math.python import quaternion
from intrinsic.math.python import vector_util

# An error of this magnitude should not trigger failures on checks for
# normalized quaternions.
_LESS_THAN_NORM_EPSILON = math_types.DEFAULT_RTOL_VALUE_FOR_NP_IS_CLOSE * 0.5

_QUAT_0 = quaternion.Quaternion([0, 0, 0, 0])
_QUAT_1 = quaternion.Quaternion.one()
_QUAT_I = quaternion.Quaternion(xyzw=vector_util.one_hot_vector(4, 0))
_QUAT_J = quaternion.Quaternion(xyzw=vector_util.one_hot_vector(4, 1))
_QUAT_K = quaternion.Quaternion(xyzw=vector_util.one_hot_vector(4, 2))
_QUAT_ONES = quaternion.Quaternion([1, 1, 1, 1])


# Named test parameters.
_UNIT_QUATERNIONS = math_test.make_named_unit_quaternions()
_NON_UNIT_QUATERNIONS = math_test.make_named_nonunit_quaternions()
_NON_ZERO_QUATERNIONS = _UNIT_QUATERNIONS + _NON_UNIT_QUATERNIONS
_TINY_QUATERNIONS = math_test.make_named_tiny_quaternions()
_TEST_QUATERNIONS = _TINY_QUATERNIONS + _NON_ZERO_QUATERNIONS


class QuaternionTest(parameterized.TestCase, math_test.TestCase):

  @parameterized.named_parameters(
      ('Zero', [0, 0, 0, 0]),
      ('Identity', [0, 0, 0, 1]),
      ('Twos', np.array([2, 2, 2, 2])),
      ('Half', (0.5, 0.5, -0.5, -0.5)),
      ('Count', range(4)),
      ('Range', np.arange(-2, -1, 0.25)),
  )
  def test_init(self, xyzw):
    """Tests Quaternion.__init__ and x, y, z, w properties."""
    x, y, z, w = xyzw
    q = quaternion.Quaternion(xyzw=xyzw)
    self.assertEqual(q.x, x)
    self.assertEqual(q.y, y)
    self.assertEqual(q.z, z)
    self.assertEqual(q.w, w)
    self.assert_all_equal(q.xyzw, xyzw)

  def test_init_default(self):
    self.assertEqual(quaternion.Quaternion(), _QUAT_0)

  @parameterized.named_parameters(*_NON_ZERO_QUATERNIONS)
  def test_init_normalized(self, quat):
    """Tests Quaternion.__init__ and x, y, z, w properties."""
    q_normalized_init = quaternion.Quaternion(xyzw=quat.xyzw, normalize=True)
    q_normalized = quat.normalize()
    self.assertEqual(q_normalized_init, q_normalized)

  @parameterized.named_parameters(
      ('too many', np.arange(5)),
      ('too few', np.arange(3)),
  )
  def test_init_error_components(self, components):
    """Tests Quaternion.__init__ exceptions."""
    self.assertRaisesRegex(
        ValueError,
        vector_util.VECTOR_COMPONENTS_MESSAGE,
        quaternion.Quaternion,
        components,
    )

  def test_init_error_values(self):
    """Tests Quaternion.__init__ exceptions."""
    self.assertRaisesRegex(
        ValueError,
        vector_util.VECTOR_INFINITE_VALUES_MESSAGE,
        quaternion.Quaternion,
        [0, 0, 1, np.inf],
    )
    self.assertRaisesRegex(
        ValueError,
        vector_util.VECTOR_VALUES_MESSAGE,
        quaternion.Quaternion,
        [0, 0, 1, np.nan],
    )

  @parameterized.named_parameters(
      ('zero', np.zeros(4)),
      ('tiny', np.full(4, 1e-12)),
      ('-tiny', np.full(4, -1e-12)),
  )
  def test_init_error_cant_normalize(self, components):
    """Tests Quaternion.__init__ exceptions."""
    self.assertRaisesRegex(
        ValueError,
        'Vector has nearly zero magnitude.',
        quaternion.Quaternion,
        components,
        True,
    )

  @parameterized.named_parameters(*_TEST_QUATERNIONS)
  def test_xyzw(self, quat):
    initial_xyzw = quat.xyzw.copy()
    quat_copy = quaternion.Quaternion(initial_xyzw)
    self.assertEqual(quat_copy, quat)
    self.assert_all_equal(quat.xyzw, initial_xyzw)
    self.assert_all_equal(quat_copy.xyzw, initial_xyzw)
    quat.xyzw[0] = 123456
    quat_copy.xyzw[1] = 123456
    self.assertEqual(quat_copy, quat)
    self.assert_all_equal(quat.xyzw, initial_xyzw)
    self.assert_all_equal(quat_copy.xyzw, initial_xyzw)
    initial_xyzw[2] = 123456
    self.assertEqual(quat_copy, quat)

  @parameterized.parameters(
      (_QUAT_1, _QUAT_1),
      (_QUAT_0, _QUAT_0),
      (_QUAT_I, -_QUAT_I),
      (_QUAT_J, -_QUAT_J),
      (_QUAT_K, -_QUAT_K),
      (
          quaternion.Quaternion([1, 2, 3, 4]),
          quaternion.Quaternion([-1, -2, -3, 4]),
      ),
  )
  def test_conjugate(self, quat, quat_conjugate):
    """Tests Quaternion.conjugate property."""
    self.assertEqual(quat.conjugate(), quat_conjugate)
    self.assertEqual(quat, quat_conjugate.conjugate())

  @parameterized.parameters(
      (_QUAT_1, _QUAT_1),
      (_QUAT_I, -_QUAT_I),
      (_QUAT_J, -_QUAT_J),
      (_QUAT_K, -_QUAT_K),
      (
          quaternion.Quaternion([-0.5, 0.5, -0.5, 0.5]),
          quaternion.Quaternion([0.5, -0.5, 0.5, 0.5]),
      ),
      (
          quaternion.Quaternion([-2, -2, 2, -2]),
          quaternion.Quaternion([0.125, 0.125, -0.125, -0.125]),
      ),
  )
  def test_inverse(self, quat, quat_inverse):
    """Checks that the inverse functions are correct.

    Args:
      quat: A Quaternion to be evaluated.
      quat_inverse: The expected inverse.
    """
    logging.debug('quat=%s, quat_inverse=%s', quat, quat_inverse)
    self.assertEqual(quat_inverse, quat.inverse())
    self.assertEqual(quat, quat_inverse.inverse())
    self.assertEqual(quat * quat_inverse, _QUAT_1)
    self.assertEqual(quat_inverse * quat, _QUAT_1)

  @parameterized.named_parameters(*_NON_ZERO_QUATERNIONS)
  def test_inverse_close(self, quat):
    quat_inverse = quat.inverse()
    self.assertAlmostEqual(quat * quat_inverse, _QUAT_1)
    self.assertAlmostEqual(quat_inverse * quat, _QUAT_1)

  @parameterized.named_parameters(*_TINY_QUATERNIONS)
  def test_inverse_errors(self, quat):
    """Checks that the inverse raises correct exceptions on zero quaternions."""
    logging.debug('quat=%s', quat)
    self.assertRaisesRegex(
        ValueError, quaternion.QUATERNION_ZERO_MESSAGE, quat.inverse
    )

  @parameterized.named_parameters(*_UNIT_QUATERNIONS)
  def test_inverse_unit(self, quat):
    self.assertEqual(np.linalg.norm(quat.xyzw), 1.0)
    self.assertEqual(quat.inverse(), quat.conjugate())
    self.assertTrue(quat.is_normalized())
    self.assert_quaternion_is_normalized(quat)

  def test_equal(self):
    self.assertEqual(_QUAT_I, _QUAT_I)
    self.assertEqual(_QUAT_J, _QUAT_J)
    self.assertNotEqual(_QUAT_I, _QUAT_J)

  @parameterized.named_parameters(*_TEST_QUATERNIONS)
  def test_eq(self, quat):
    self.check_eq_and_ne_for_equal_values(quat, quat)
    self.check_eq_and_ne_for_equal_values(
        quat, quaternion.Quaternion(quat.xyzw.copy())
    )

  def test_eq_other_type(self):
    self.assertNotEqual(_QUAT_I, 'string')

  @parameterized.named_parameters(*_TEST_QUATERNIONS)
  def test_ne(self, quat):
    for _, delta in _NON_ZERO_QUATERNIONS:
      with self.subTest(delta=delta):
        self.check_eq_and_ne_for_unequal_values(quat, quat + delta)
        self.check_eq_and_ne_for_unequal_values(quat, quat - delta)
        self.check_eq_and_ne_for_unequal_values(quat, quat + delta * 1e-6)
        self.check_eq_and_ne_for_unequal_values(quat, quat - delta * 1e-6)

  def test_hash(self):
    self.assertFalse(issubclass(quaternion.Quaternion, abc.Hashable))

  @parameterized.parameters(
      (_QUAT_I, _QUAT_J, _QUAT_K),
      (_QUAT_J, _QUAT_K, _QUAT_I),
      (_QUAT_K, _QUAT_I, _QUAT_J),
      (_QUAT_I, _QUAT_K, -_QUAT_J),
      (_QUAT_J, _QUAT_I, -_QUAT_K),
      (_QUAT_K, _QUAT_J, -_QUAT_I),
      (_QUAT_I, _QUAT_I, -_QUAT_1),
      (_QUAT_J, _QUAT_J, -_QUAT_1),
      (_QUAT_K, _QUAT_K, -_QUAT_1),
      (_QUAT_ONES, _QUAT_ONES, quaternion.Quaternion([2, 2, 2, -2])),
      (
          quaternion.Quaternion([1, 2, 0, 0]),
          quaternion.Quaternion([0, 1, -3, 0]),
          quaternion.Quaternion([-6, 3, 1, -2]),
      ),
      (
          quaternion.Quaternion([1, 0, 2, 0]),
          quaternion.Quaternion([1, 3, 5, 7]),
          quaternion.Quaternion([1.0, -3.0, 17.0, -11.0]),
      ),
      (
          quaternion.Quaternion([1, 0, 0.5, 2]),
          quaternion.Quaternion([0, -1, 0.25, 0]),
          quaternion.Quaternion([0.5, -2.25, -0.5, -0.125]),
      ),
  )
  def test_multiply(self, op1, op2, product):
    """Verifies quaternion product."""
    self.assertEqual(op1 * op2, product)
    self.assertEqual(op2.conjugate() * op1.conjugate(), product.conjugate())
    self.assertEqual(-op1 * op2, -product)
    self.assertEqual(op1 * -op2, -product)
    self.assertEqual(-op1 * -op2, product)

  @parameterized.parameters(
      (_QUAT_1, 2, [0, 0, 0, 2]),
      (_QUAT_I, -1, [-1, 0, 0, 0]),
      (_QUAT_J, 3, [0, 3, 0, 0]),
      (_QUAT_K, 0.5, [0, 0, 0.5, 0]),
      (_QUAT_ONES, 0.1, [0.1, 0.1, 0.1, 0.1]),
  )
  def test_multiply_scalar(self, quat, scalar, product_xyzw):
    """Verifies quaternion product with a scalar."""
    product = quaternion.Quaternion(product_xyzw)
    self.assertEqual(quat * scalar, product)
    self.assertEqual(scalar * quat, product)

  @parameterized.named_parameters(*_TEST_QUATERNIONS)
  def test_multiply_zero(self, quat):
    self.assertEqual(quat * _QUAT_0, _QUAT_0)
    self.assertEqual(_QUAT_0 * quat, _QUAT_0)
    self.assertEqual(quat * 0.0, _QUAT_0)
    self.assertEqual(quat * 0, _QUAT_0)
    self.assertEqual(0.0 * quat, _QUAT_0)
    self.assertEqual(0 * quat, _QUAT_0)

  @parameterized.named_parameters(*_TEST_QUATERNIONS)
  def test_multiply_one(self, quat):
    self.assertEqual(quat * _QUAT_1, quat)
    self.assertEqual(_QUAT_1 * quat, quat)
    self.assertEqual(quat * 1.0, quat)
    self.assertEqual(quat * 1, quat)
    self.assertEqual(1.0 * quat, quat)
    self.assertEqual(1 * quat, quat)

  @parameterized.parameters(
      (_QUAT_I, _QUAT_J, -_QUAT_K),
      (_QUAT_J, _QUAT_K, -_QUAT_I),
      (_QUAT_K, _QUAT_I, -_QUAT_J),
      (_QUAT_I, _QUAT_K, _QUAT_J),
      (_QUAT_J, _QUAT_I, _QUAT_K),
      (_QUAT_K, _QUAT_J, _QUAT_I),
      (_QUAT_I, _QUAT_I, _QUAT_1),
      (_QUAT_J, _QUAT_J, _QUAT_1),
      (_QUAT_K, _QUAT_K, _QUAT_1),
      (
          quaternion.Quaternion([1, -1, 1, -1]),
          quaternion.Quaternion([-1, 1, 1, -1]),
          _QUAT_J,
      ),
  )
  def test_divide(self, op1, op2, quotient):
    """Verifies quaternion quotient."""
    self.assertEqual(op1 / op2, quotient)
    self.assertEqual(-op1 / op2, -quotient)
    self.assertEqual(op1 / -op2, -quotient)
    self.assertEqual(-op1 / -op2, quotient)

  @parameterized.parameters(
      (_QUAT_1, 0.5, [0, 0, 0, 2]),
      (_QUAT_I, -1, [-1, 0, 0, 0]),
      (_QUAT_J, 0.25, [0, 4, 0, 0]),
      (_QUAT_K, 2, [0, 0, 0.5, 0]),
      (_QUAT_ONES, 10.0, [0.1, 0.1, 0.1, 0.1]),
  )
  def test_divide_scalar(self, quat, scalar, product_xyzw):
    """Verifies quaternion product with a scalar."""
    product = quaternion.Quaternion(product_xyzw)
    self.assertEqual(quat / scalar, product)

  @parameterized.named_parameters(*_TEST_QUATERNIONS)
  def test_divide_zero(self, quat):
    self.assertRaisesRegex(
        ValueError, quaternion.QUATERNION_ZERO_MESSAGE, quat.divide, _QUAT_0
    )
    self.assertRaisesRegex(
        ValueError, quaternion.QUATERNION_ZERO_MESSAGE, quat.divide, 0
    )
    self.assertRaisesRegex(
        ValueError, quaternion.QUATERNION_ZERO_MESSAGE, quat.divide, 0.0
    )

  @parameterized.named_parameters(*_NON_ZERO_QUATERNIONS)
  def test_divide_one(self, quat):
    self.assertEqual(quat / _QUAT_1, quat)
    self.assertEqual(quat / 1.0, quat)
    self.assertEqual(quat / 1, quat)
    self.assertEqual(1.0 / quat, quat.inverse())
    self.assertAlmostEqual(quat / quat, _QUAT_1)

  @parameterized.named_parameters(*_TEST_QUATERNIONS)
  def test_add(self, quat):
    self.assertEqual(quat + quat, 2 * quat)
    self.assertEqual(
        quat + _QUAT_I, quaternion.Quaternion(quat.xyzw + [1, 0, 0, 0])
    )
    self.assertEqual(
        quat + _QUAT_J, quaternion.Quaternion(quat.xyzw + [0, 1, 0, 0])
    )
    self.assertEqual(
        quat + _QUAT_K, quaternion.Quaternion(quat.xyzw + [0, 0, 1, 0])
    )
    self.assertEqual(
        quat + _QUAT_1, quaternion.Quaternion(quat.xyzw + [0, 0, 0, 1])
    )

  @parameterized.named_parameters(*_TEST_QUATERNIONS)
  def test_add_zero(self, quat):
    self.assertEqual(quat + _QUAT_0, quat)
    self.assertEqual(_QUAT_0 + quat, quat)
    self.assertEqual(quat + -quat, _QUAT_0)
    self.assertEqual(-quat + quat, _QUAT_0)

  @parameterized.named_parameters(*_TEST_QUATERNIONS)
  def test_sub(self, quat):
    self.assertEqual(quat - (-quat), 2 * quat)
    self.assertEqual(
        quat - _QUAT_I, quaternion.Quaternion(quat.xyzw + [-1, 0, 0, 0])
    )
    self.assertEqual(
        quat - _QUAT_J, quaternion.Quaternion(quat.xyzw + [0, -1, 0, 0])
    )
    self.assertEqual(
        quat - _QUAT_K, quaternion.Quaternion(quat.xyzw + [0, 0, -1, 0])
    )
    self.assertEqual(
        quat - _QUAT_1, quaternion.Quaternion(quat.xyzw + [0, 0, 0, -1])
    )

  @parameterized.named_parameters(*_TEST_QUATERNIONS)
  def test_sub_zero(self, quat):
    self.assertEqual(quat - _QUAT_0, quat)
    self.assertEqual(_QUAT_0 - quat, -quat)
    self.assertEqual(quat - quat, _QUAT_0)

  @parameterized.parameters(
      ([1, 0, 0, 0], [1, 0, 0, 0]),
      ([-1, 0, 0, 0], [-1, 0, 0, 0]),
      ([1, 1, -1, 1], [0.5, 0.5, -0.5, 0.5]),
      ([10, -10, 10, 10], [0.5, -0.5, 0.5, 0.5]),
  )
  def test_normalize(self, xyzw, xyzw_normalized):
    q = quaternion.Quaternion(xyzw)
    q_normalized = quaternion.Quaternion(xyzw_normalized)
    self.assertAlmostEqual(q.normalize(), q_normalized)

  @parameterized.named_parameters(*_UNIT_QUATERNIONS)
  def test_normalize_unit_exact(self, q):
    self.assertEqual(q, q.normalize())

  @parameterized.named_parameters(*_TINY_QUATERNIONS)
  def test_normalize_errors(self, quat):
    self.assertRaisesRegex(
        ValueError, quaternion.QUATERNION_ZERO_MESSAGE, quat.normalize
    )

  @parameterized.named_parameters(*_NON_ZERO_QUATERNIONS)
  def test_check_non_zero(self, quat):
    quat.check_non_zero()

  @parameterized.named_parameters(*_TINY_QUATERNIONS)
  def test_check_non_zero_errors(self, quat):
    self.assertRaisesRegex(
        ValueError, quaternion.QUATERNION_ZERO_MESSAGE, quat.check_non_zero
    )

  @parameterized.parameters(0.0, 0.05, 0.1)
  def test_check_non_zero_rejects_at_or_below_custom_tolerance(self, magnitude):
    quat = quaternion.Quaternion([0, 0, 0, magnitude])

    self.assertRaisesRegex(
        ValueError,
        quaternion.QUATERNION_ZERO_MESSAGE + '.*custom tolerance',
        quat.check_non_zero,
        norm_epsilon=0.1,
        err_msg='custom tolerance',
    )

  @parameterized.parameters((0.2, 0.1), (1e-9, 1e-10), (1e-9, 0.0))
  def test_check_non_zero_accepts_above_custom_tolerance(
      self, magnitude, tolerance
  ):
    quat = quaternion.Quaternion([0, 0, 0, magnitude])

    quat.check_non_zero(norm_epsilon=tolerance)

  @parameterized.named_parameters(
      ('tiny_axis', [1e-200, 0, 0, 0], 1e-201),
      ('tiny_all_components', [1e-200] * 4, 1e-200),
      ('smallest_subnormal', [np.nextafter(0.0, 1.0), 0, 0, 0], 0.0),
      ('large_axis', [1e200, 0, 0, 0], 1e199),
      ('large_all_components', [1e200] * 4, 1e200),
  )
  def test_check_non_zero_accepts_extreme_magnitudes(self, xyzw, tolerance):
    quaternion.Quaternion(xyzw).check_non_zero(norm_epsilon=tolerance)

  @parameterized.parameters(1e-200, 1e200)
  def test_check_non_zero_rejects_extreme_magnitudes_below_tolerance(
      self, magnitude
  ):
    quat = quaternion.Quaternion([magnitude] * 4)
    self.assertRaisesRegex(
        ValueError,
        'extreme tolerance',
        quat.check_non_zero,
        norm_epsilon=3 * magnitude,
        err_msg='extreme tolerance',
    )

  def test_check_non_zero_reports_stable_norm(self):
    quat = quaternion.Quaternion([1e200] * 4)
    self.assertRaisesRegex(
        ValueError,
        r'= 2e\+200 <= 3e\+200.*extreme tolerance',
        quat.check_non_zero,
        norm_epsilon=3e200,
        err_msg='extreme tolerance',
    )

  @parameterized.parameters(-1.0, -np.inf, np.inf, np.nan)
  def test_check_non_zero_rejects_invalid_tolerance(self, tolerance):
    for xyzw in ([0, 0, 0, 0], [0, 0, 0, 1]):
      with self.subTest(xyzw=xyzw):
        self.assertRaisesRegex(
            ValueError,
            'norm_epsilon.*invalid tolerance',
            quaternion.Quaternion(xyzw).check_non_zero,
            norm_epsilon=tolerance,
            err_msg='invalid tolerance',
        )

  @parameterized.parameters(0.0, -0.0)
  def test_check_non_zero_rejects_zero_at_zero_tolerance(self, tolerance):
    self.assertRaisesRegex(
        ValueError,
        quaternion.QUATERNION_ZERO_MESSAGE,
        quaternion.Quaternion([0, 0, 0, 0]).check_non_zero,
        norm_epsilon=tolerance,
    )

  @parameterized.parameters(1e-8, 0.1)
  def test_check_non_zero_exact_boundary(self, tolerance):
    for magnitude in (np.nextafter(tolerance, 0.0), tolerance):
      with self.subTest(magnitude=magnitude):
        self.assertRaisesRegex(
            ValueError,
            quaternion.QUATERNION_ZERO_MESSAGE,
            quaternion.Quaternion([0, -magnitude, 0, 0]).check_non_zero,
            norm_epsilon=tolerance,
        )
    quaternion.Quaternion(
        [0, -np.nextafter(tolerance, np.inf), 0, 0]
    ).check_non_zero(norm_epsilon=tolerance)

  @parameterized.named_parameters(*_NON_ZERO_QUATERNIONS)
  def test_check_normalized_close(self, quat):
    quat_normalized = quat.normalize()
    logging.debug('quat=%s, quat_normalized=%s', quat, quat_normalized)
    self.assertAlmostEqual(np.linalg.norm(quat_normalized.xyzw), 1.0)
    quat_normalized.check_normalized()
    (quat_normalized * (1 + _LESS_THAN_NORM_EPSILON)).check_normalized()
    (quat_normalized * (1 - _LESS_THAN_NORM_EPSILON)).check_normalized()
    # If the norm is off by more than twice the epsilon value, it should trigger
    # the exception.
    self.assertRaisesRegex(
        ValueError,
        quaternion.QUATERNION_NOT_NORMALIZED_MESSAGE,
        (
            quat_normalized
            * (1 + 2 * math_types.DEFAULT_RTOL_VALUE_FOR_NP_IS_CLOSE)
        ).check_normalized,
    )
    self.assertRaisesRegex(
        ValueError,
        quaternion.QUATERNION_NOT_NORMALIZED_MESSAGE,
        (
            quat_normalized
            * (1 - 2 * math_types.DEFAULT_RTOL_VALUE_FOR_NP_IS_CLOSE)
        ).check_normalized,
    )

  @parameterized.named_parameters(*_NON_UNIT_QUATERNIONS + _TINY_QUATERNIONS)
  def test_check_normalized_errors(self, q_non_unit):
    logging.debug('q_non_unit=%s', q_non_unit)
    self.assertRaisesRegex(
        ValueError,
        quaternion.QUATERNION_NOT_NORMALIZED_MESSAGE,
        q_non_unit.check_normalized,
    )

  def test_quaternion_str(self):
    """Test function for Quaternion.__str__ function.

    Verifies output of Quaternion.__str__.

    Tests:
      Quaternion.__str__
    """
    quat = quaternion.Quaternion([0.125, -0.25, 0.375, 0.48828125])
    expected_quat_string = '[0.125i + -0.25j + 0.375k + 0.4883]'
    logging.info('Quaternion.__str__ = %s', quat)
    self.assertEqual(str(quat), expected_quat_string)
    self.assertEqual(quat.__str__(), expected_quat_string)

  def test_quaternion_repr(self):
    """Test function for Quaternion.__repr__ function.

    Verifies output of Quaternion.__repr__.

    Tests:
      Quaternion.__repr__
    """
    quat = quaternion.Quaternion([0.125, -0.25, 0.375, 0.48828125])
    logging.info('Quaternion.__repr__ = %r', quat)
    self.assertEqual(
        quat.__repr__(), 'Quaternion([0.125, -0.25, 0.375, 0.48828125])'
    )

  def test_one(self):
    self.assertEqual(_QUAT_1, quaternion.Quaternion([0, 0, 0, 1]))
    self.assertEqual(_QUAT_1.xyzw[3], 1.0)
    self.assert_all_equal(_QUAT_1.xyzw[:3], 0.0)
    self.assertEqual(_QUAT_1, _QUAT_1.inverse())
    self.assertEqual(_QUAT_1, _QUAT_1.conjugate())

  def test_random_unit(self):
    cumulative_quaternion = quaternion.Quaternion.one()
    for _ in range(100):
      random_quaternion = quaternion.Quaternion.random_unit()
      self.assertAlmostEqual(np.linalg.norm(random_quaternion.xyzw), 1.0)
      cumulative_quaternion *= random_quaternion
    self.assertAlmostEqual(np.linalg.norm(cumulative_quaternion.xyzw), 1.0)

  def test_random_seeded(self):
    rng_a = np.random.default_rng(seed=1)
    rng_a2 = np.random.default_rng(seed=1)
    rng_b = np.random.default_rng(seed=2)
    rng_c = np.random.default_rng()
    for _ in range(10):
      qa = quaternion.Quaternion.random_unit(rng=rng_a)
      self.assertEqual(qa, quaternion.Quaternion.random_unit(rng=rng_a2))
      self.assertNotEqual(qa, quaternion.Quaternion.random_unit(rng=rng_b))
      self.assertNotEqual(qa, quaternion.Quaternion.random_unit(rng=rng_c))
      self.assertNotEqual(qa, quaternion.Quaternion.random_unit())


if __name__ == '__main__':
  absltest.main()
