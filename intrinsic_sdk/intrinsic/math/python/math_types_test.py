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

"""Tests for intrinsic.math.python.math_types module (python3)."""

from absl.testing import absltest
from absl.testing import parameterized
import numpy as np

from intrinsic.math.python import math_test
from intrinsic.math.python import math_types

# Test error message for verification of err_msg parameter.
_TEST_ERROR_MESSAGE = 'This is a test error message.'


class MathTypesTest(math_test.TestCase, parameterized.TestCase):

  def test_is_scalar(self):
    self.assertTrue(math_types.is_scalar(3))
    self.assertTrue(math_types.is_scalar(1.5))
    self.assertTrue(math_types.is_scalar((1.5)))
    self.assertFalse(math_types.is_scalar([1.5]))
    self.assertFalse(math_types.is_scalar([1.5, 2]))
    self.assertFalse(math_types.is_scalar((1.5, 1.5)))
    self.assertFalse(math_types.is_scalar(np.zeros(3)))
    self.assertFalse(math_types.is_scalar([]))
    self.assertFalse(math_types.is_scalar(''))
    self.assertFalse(math_types.is_scalar('h'))
    self.assertFalse(math_types.is_scalar('hello'))

  def _get_matching_arrays_check_equal_shape(self, arbitrary, zeros):
    """Checks that _GetMatchingArrays returns arrays of the correct size.

    TestCase._get_matching_arrays should always return two numpy arrays, which
    have the same shape and whose shape and values do not depend on the order of
    the arguments.

    Args:
      arbitrary: An arbitrary array or scalar input.
      zeros: An array or scalar input that must have zero values.

    Raises:
      AssertionError internal to _GetMatchingArrays if the inputs are
      not proper.
    """
    x1, y1 = math_types.get_matching_arrays(arbitrary, zeros)
    self.assert_equal_shape(x1, y1)
    self.assert_all_equal(y1, 0)

    y2, x2 = math_types.get_matching_arrays(zeros, arbitrary)
    self.assert_equal_shape(x2, y2)
    self.assert_all_equal(y2, 0)

    self.assert_equal_shape(x1, x2)
    self.assert_equal_shape(y1, y2)
    self.assert_all_equal(x1, x2)
    self.assert_all_equal(y1, y2)

  def test_get_matching_arrays(self):
    self._get_matching_arrays_check_equal_shape(np.arange(3), np.zeros(3))
    self._get_matching_arrays_check_equal_shape(
        np.ones((3, 2)), np.zeros((3, 2))
    )
    self._get_matching_arrays_check_equal_shape(
        np.ones((2, 3, 2)), np.zeros((2, 3, 2))
    )

  def test_get_matching_arrays_list(self):
    self._get_matching_arrays_check_equal_shape(np.arange(3), [0, 0, 0])
    self._get_matching_arrays_check_equal_shape(
        [[[1, 2], [3, 4], [5, 6]]], np.zeros((1, 3, 2))
    )
    self._get_matching_arrays_check_equal_shape(
        [[1, 2], [3, 4], [5, 6]], [[0, 0], [0, 0], [0, 0]]
    )

  def test_get_matching_arrays_tuple(self):
    self._get_matching_arrays_check_equal_shape(np.arange(3), (0, 0, 0))
    self._get_matching_arrays_check_equal_shape(
        ((1, 2), (3, 4), (5, 6)), np.zeros((3, 2))
    )
    self._get_matching_arrays_check_equal_shape((1, 2, 3), (0, 0, 0))
    self._get_matching_arrays_check_equal_shape((1, 2), [0, 0])

  def test_get_matching_arrays_scalar(self):
    self._get_matching_arrays_check_equal_shape(np.arange(3), 0)
    self._get_matching_arrays_check_equal_shape(1.5, np.zeros((3, 2)))
    self._get_matching_arrays_check_equal_shape([[[1, 2], [3, 4], [5, 6]]], 0)
    self._get_matching_arrays_check_equal_shape([[1, 2], [3, 4]], 0)
    self._get_matching_arrays_check_equal_shape(1, 0)
    self._get_matching_arrays_check_equal_shape((1, 2, 3, 4), 0)

  @parameterized.named_parameters(
      ('integer_list', [1, 2, 3]),
      ('integer_matrix', np.array([[1, 2], [3, 4]])),
      ('boolean_list', [True, False]),
  )
  def test_get_matching_arrays_preserves_fractional_scalar(self, values):
    original = np.array(values, copy=True)
    expected = np.full(original.shape, 0.5)

    array, scalar = math_types.get_matching_arrays(values, 0.5)
    self.assert_all_equal(array, original)
    self.assert_all_equal(scalar, expected)

    scalar, array = math_types.get_matching_arrays(0.5, values)
    self.assert_all_equal(scalar, expected)
    self.assert_all_equal(array, original)
    self.assert_all_equal(values, original)

  def test_get_matching_arrays_preserves_large_integer_scalar(self):
    values = np.array([1, 2], dtype=np.int8)
    expected = np.array([1000, 1000])

    array, scalar = math_types.get_matching_arrays(values, 1000)
    self.assert_all_equal(array, values)
    self.assert_all_equal(scalar, expected)

    scalar, array = math_types.get_matching_arrays(1000, values)
    self.assert_all_equal(scalar, expected)
    self.assert_all_equal(array, values)

  @parameterized.parameters((1, 0.5), (0.5, 1))
  def test_get_matching_arrays_preserves_mixed_scalars(self, lhs, rhs):
    lhs_array, rhs_array = math_types.get_matching_arrays(lhs, rhs)

    self.assert_all_equal(lhs_array, np.asarray(lhs))
    self.assert_all_equal(rhs_array, np.asarray(rhs))

  def test_get_matching_arrays_wrong_size(self):
    """Checks when inputs have different numbers of elements."""
    self.assertRaisesRegex(
        ValueError,
        math_types.SHAPE_MISMATCH_MESSAGE,
        math_types.get_matching_arrays,
        np.arange(3),
        np.zeros(2),
    )
    self.assertRaisesRegex(
        ValueError,
        math_types.SHAPE_MISMATCH_MESSAGE,
        math_types.get_matching_arrays,
        [[1, 2], [3, 4]],
        [1, 2, 3],
    )

  def test_get_matching_arrays_wrong_shape(self):
    """Checks when inputs have different shapes."""
    self.assertRaisesRegex(
        ValueError,
        math_types.SHAPE_MISMATCH_MESSAGE,
        math_types.get_matching_arrays,
        np.ones([10, 2]).T,
        np.zeros(20),
    )
    self.assertRaisesRegex(
        ValueError,
        math_types.SHAPE_MISMATCH_MESSAGE,
        math_types.get_matching_arrays,
        np.ones([10, 2]),
        np.zeros(20),
    )
    self.assertRaisesRegex(
        ValueError,
        math_types.SHAPE_MISMATCH_MESSAGE,
        math_types.get_matching_arrays,
        [1, 2],
        [[3], [4]],
    )

  def test_get_matching_arrays_err_msg(self):
    """Checks that err_msg is passed through correctly."""
    self.assertRaisesRegex(
        ValueError,
        math_types.SHAPE_MISMATCH_MESSAGE + '.*' + _TEST_ERROR_MESSAGE,
        math_types.get_matching_arrays,
        np.arange(3),
        np.zeros(2),
        _TEST_ERROR_MESSAGE,
    )


if __name__ == '__main__':
  np.random.seed(0)
  absltest.main()
