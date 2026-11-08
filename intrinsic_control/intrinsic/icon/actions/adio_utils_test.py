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

from intrinsic.icon.actions import adio_pb2
from intrinsic.icon.actions import adio_utils


class AdioUtilsTest(absltest.TestCase):

  def test_set_digital_output(self):
    action = adio_utils.create_digital_output_action(
        18,
        "adio_part",
        digital_outputs={
            "do_block": adio_pb2.DigitalBlock(
                values_by_index={2: True, 3: False}
            )
        },
    )

    self.assertEqual(action.proto.action_instance_id, 18)
    self.assertEqual(action.proto.part_name, "adio_part")
    self.assertEmpty(action.reactions)
    self.assertEqual(action.proto.action_type_name, "intrinsic.adio")

    got_params = adio_pb2.ADIOFixedParams()
    self.assertTrue(action.proto.fixed_parameters.Unpack(got_params))
    self.assertEqual(
        got_params.outputs.digital_outputs["do_block"].values_by_index[2], True
    )
    self.assertEqual(
        got_params.outputs.digital_outputs["do_block"].values_by_index[3], False
    )

  def test_outputs_set_is_valid(self):
    self.assertEqual(
        adio_utils.StateVariables.OUTPUTS_SET,
        "intrinsic.outputs_set",
    )


if __name__ == "__main__":
  absltest.main()
