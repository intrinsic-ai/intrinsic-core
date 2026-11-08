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

from intrinsic.icon.control.algorithms.python import directional_stiffness


class DirectionalStiffnessTest(absltest.TestCase):

  def test_computes_correct_stiffness(self):
    stiffness = directional_stiffness.compute_directional_stiffness(
        motion_direction=np.array([0.0, 0.0, 1.0]),
        translational_stiffness_orthogonal_to_motion_direction=20.0,
        rotational_stiffness_orthogonal_to_motion_direction=30.0,
    )
    np.testing.assert_allclose(
        stiffness,
        np.array([
            [20.0, 0.0, 0.0, 0.0, 0.0, 0.0],
            [0.0, 20.0, 0.0, 0.0, 0.0, 0.0],
            [0.0, 0.0, 0.0, 0.0, 0.0, 0.0],
            [0.0, 0.0, 0.0, 30.0, 0.0, 0.0],
            [0.0, 0.0, 0.0, 0.0, 30.0, 0.0],
            [0.0, 0.0, 0.0, 0.0, 0.0, 30.0],
        ]),
    )


if __name__ == "__main__":
  absltest.main()
