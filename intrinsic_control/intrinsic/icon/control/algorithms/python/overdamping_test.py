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
import numpy as np

from intrinsic.icon.control.algorithms.python import overdamping


class OverdampingTest(absltest.TestCase):

  def test_overdamping_is_correct(self):
    np.testing.assert_allclose(
        overdamping.compute_overdamping(np.eye(6), 10000),
        np.eye(6) * 211.981131,
    )


if __name__ == "__main__":
  absltest.main()
