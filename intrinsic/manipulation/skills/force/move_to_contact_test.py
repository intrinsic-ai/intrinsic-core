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

"""Tests for the move_to_contact skill."""

from unittest import mock

from absl.testing import absltest
import numpy as np

from intrinsic.manipulation.skills.force import move_to_contact
from intrinsic.manipulation.skills.force import move_to_contact_pb2
from intrinsic.math.python import data_types


class MoveToContactTest(absltest.TestCase):

  def _params_with_tool(self) -> move_to_contact_pb2.MoveToContactParams:
    params = move_to_contact_pb2.MoveToContactParams(
        contact_force=10.0,
        timeout_sec=60.0,
    )
    params.tool.by_name.object.object_name = "robot"
    return params

  def test_validate_parameters_allows_unset_default_direction(self):
    params = self._params_with_tool()
    params.fixed_vector.reference.by_name.object.object_name = "base"

    self.assertFalse(params.fixed_vector.HasField("direction"))
    move_to_contact.MoveToContact().validate_parameters(params, max_force=100.0)

  def test_get_motion_direction_defaults_to_tool_z_axis(self):
    params = self._params_with_tool()
    params.fixed_vector.SetInParent()

    world = mock.Mock()
    world.get_transform_node.return_value = object()
    world.get_transform.return_value = data_types.Pose3()

    direction = move_to_contact._get_motion_direction(params, world, object())

    np.testing.assert_allclose(direction, [0.0, 0.0, 1.0])

  def test_validate_parameters_rejects_explicit_zero_direction(self):
    params = self._params_with_tool()
    params.fixed_vector.reference.by_name.object.object_name = "base"
    params.fixed_vector.direction.SetInParent()

    self.assertTrue(params.fixed_vector.HasField("direction"))
    with self.assertRaises(move_to_contact.SkillError):
      move_to_contact.MoveToContact().validate_parameters(
          params, max_force=100.0
      )

  def test_validate_parameters_allows_explicit_nonzero_direction(self):
    params = self._params_with_tool()
    params.fixed_vector.reference.by_name.object.object_name = "base"
    params.fixed_vector.direction.z = 1.0

    self.assertTrue(params.fixed_vector.HasField("direction"))
    move_to_contact.MoveToContact().validate_parameters(params, max_force=100.0)


if __name__ == "__main__":
  absltest.main()
