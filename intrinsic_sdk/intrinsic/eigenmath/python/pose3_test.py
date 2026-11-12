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

from absl.testing import absltest

from intrinsic.eigenmath.python import pose3_test_util
from intrinsic.math.python import data_types


class Pose3Test(absltest.TestCase):

  def test_identity(self):
    self.assertEqual(pose3_test_util.identity(), data_types.Pose3.identity())

  def test_translate(self):
    self.assertEqual(
        pose3_test_util.translate(
            data_types.Pose3.from_vec7([1, 2, 3, 0, 0, 0, 1])
        ),
        data_types.Pose3.from_vec7([2, 4, 6, 0, 0, 0, 1]),
    )

  def test_rotate(self):
    expected = data_types.Pose3.from_vec7([1, 2, 3, 1, 0, 0, 0])
    actual = pose3_test_util.rotate(
        data_types.Pose3.from_vec7([1, 2, 3, 0, 0, 0, 1])
    )
    self.assertTrue(
        actual.almost_equal(expected), f'expected: {expected}, actual: {actual}'
    )


if __name__ == '__main__':
  absltest.main()
