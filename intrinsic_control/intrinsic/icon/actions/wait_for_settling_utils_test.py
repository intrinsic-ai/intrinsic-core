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

from intrinsic.icon.actions import wait_for_settling_action_pb2
from intrinsic.icon.actions import wait_for_settling_utils


class WaitForSettlingUtilsTest(absltest.TestCase):

  def test_create(self):
    action = wait_for_settling_utils.create_wait_for_settling_action(
        action_id=238, arm_part_name="my_arm_part", uncertainty_threshold=0.3
    )

    self.assertEqual(action.proto.action_instance_id, 238)
    self.assertEqual(action.proto.part_name, "my_arm_part")
    self.assertEqual(
        action.proto.action_type_name, "intrinsic.wait_for_settling_action"
    )

    got_params = wait_for_settling_action_pb2.WaitForSettlingActionFixedParams()
    self.assertTrue(action.proto.fixed_parameters.Unpack(got_params))
    self.assertEqual(got_params.uncertainty_threshold, 0.3)

  def test_create_default(self):
    action = wait_for_settling_utils.create_wait_for_settling_action(
        action_id=238, arm_part_name="my_arm_part"
    )

    self.assertEqual(action.proto.action_instance_id, 238)
    self.assertEqual(action.proto.part_name, "my_arm_part")
    self.assertEqual(
        action.proto.action_type_name, "intrinsic.wait_for_settling_action"
    )

    got_params = wait_for_settling_action_pb2.WaitForSettlingActionFixedParams()
    self.assertTrue(action.proto.fixed_parameters.Unpack(got_params))
    self.assertFalse(got_params.HasField("uncertainty_threshold"))


if __name__ == "__main__":
  absltest.main()
