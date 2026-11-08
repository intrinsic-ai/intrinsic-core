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

from intrinsic.icon.actions import tare_force_torque_sensor_pb2
from intrinsic.icon.actions import tare_force_torque_sensor_utils


class TareForceTorqueSensorUtilsTest(absltest.TestCase):

  def test_create_action(self):
    action = (
        tare_force_torque_sensor_utils.create_tare_force_torque_sensor_action(
            35, "sensor_part", 13
        )
    )

    self.assertEqual(action.proto.action_instance_id, 35)
    self.assertEqual(action.proto.part_name, "sensor_part")
    self.assertEmpty(action.reactions)
    self.assertEqual(
        action.proto.action_type_name, "intrinsic.tare_force_torque_sensor"
    )

    got_params = tare_force_torque_sensor_pb2.TareForceTorqueSensorParams()
    self.assertTrue(action.proto.fixed_parameters.Unpack(got_params))
    self.assertEqual(got_params.num_taring_cycles, 13)

  def test_create_action_with_default(self):
    action = (
        tare_force_torque_sensor_utils.create_tare_force_torque_sensor_action(
            35, "sensor_part"
        )
    )

    self.assertEqual(action.proto.action_instance_id, 35)
    self.assertEqual(action.proto.part_name, "sensor_part")
    self.assertEmpty(action.reactions)
    self.assertEqual(
        action.proto.action_type_name, "intrinsic.tare_force_torque_sensor"
    )

    got_params = tare_force_torque_sensor_pb2.TareForceTorqueSensorParams()
    self.assertTrue(action.proto.fixed_parameters.Unpack(got_params))
    self.assertFalse(got_params.HasField("num_taring_cycles"))


if __name__ == "__main__":
  absltest.main()
