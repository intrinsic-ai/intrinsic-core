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
from google.protobuf import any_pb2

from intrinsic.icon.actions import stop_utils


class StopUtilsTest(absltest.TestCase):

  def test_create_action(self):
    action = stop_utils.create_stop_action(
        action_id=17, joint_position_part_name="my_part"
    )

    self.assertEqual(action.proto.action_instance_id, 17)
    self.assertEqual(action.proto.part_name, "my_part")
    self.assertEmpty(action.reactions)
    self.assertEqual(action.proto.action_type_name, "intrinsic.stop")
    self.assertEqual(action.proto.fixed_parameters, any_pb2.Any())

  def test_is_setteld_variable_name_is_correct(self):
    self.assertEqual(stop_utils.StateVariables.IS_SETTLED, "is_settled")


if __name__ == "__main__":
  absltest.main()
