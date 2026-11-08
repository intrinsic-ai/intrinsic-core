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

from intrinsic.icon.actions import point_to_point_move_pb2
from intrinsic.icon.actions import point_to_point_move_utils
from intrinsic.kinematics.types import joint_limits_pb2


class PointToPointMoveUtilsTest(absltest.TestCase):

  def test_create_action(self):
    action = point_to_point_move_utils.create_point_to_point_move_action(
        14, "arm_part", [1.0, 2.0, 3.0, 4.0, 5.0, 6.0]
    )

    self.assertEqual(action.proto.action_instance_id, 14)
    self.assertEqual(action.proto.part_name, "arm_part")
    self.assertEmpty(action.reactions)
    self.assertEqual(
        action.proto.action_type_name, "intrinsic.point_to_point_move"
    )

    got_params = point_to_point_move_pb2.PointToPointMoveFixedParams()
    self.assertTrue(action.proto.fixed_parameters.Unpack(got_params))
    self.assertEqual(
        got_params.goal_position.joints, [1.0, 2.0, 3.0, 4.0, 5.0, 6.0]
    )
    self.assertEqual(
        got_params.goal_velocity.joints, [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    )
    self.assertFalse(got_params.HasField("joint_limits"))

  def test_create_with_velocity(self):
    action = point_to_point_move_utils.create_point_to_point_move_action(
        14,
        "arm_part",
        goal_position=[1.0, 2.0, 3.0, 4.0, 5.0, 6.0],
        goal_velocity=[0.1, 0.2, 0.3, 0.4, 0.5, 0.6],
    )

    self.assertEqual(action.proto.action_instance_id, 14)
    self.assertEqual(action.proto.part_name, "arm_part")
    self.assertEmpty(action.reactions)
    self.assertEqual(
        action.proto.action_type_name, "intrinsic.point_to_point_move"
    )

    got_params = point_to_point_move_pb2.PointToPointMoveFixedParams()
    self.assertTrue(action.proto.fixed_parameters.Unpack(got_params))
    self.assertEqual(
        got_params.goal_position.joints, [1.0, 2.0, 3.0, 4.0, 5.0, 6.0]
    )
    self.assertEqual(
        got_params.goal_velocity.joints, [0.1, 0.2, 0.3, 0.4, 0.5, 0.6]
    )
    self.assertFalse(got_params.HasField("joint_limits"))

  def test_create_action_with_limits(self):
    action = point_to_point_move_utils.create_point_to_point_move_action(
        14,
        "arm_part",
        goal_position=[1.0, 2.0, 3.0, 4.0, 5.0, 6.0],
        joint_limits=joint_limits_pb2.JointLimits(
            min_position=joint_limits_pb2.RepeatedDouble(
                values=[-1.0, -2.0, -3.0, -4.0, -5.0, -6.0]
            ),
            max_position=joint_limits_pb2.RepeatedDouble(
                values=[1.0, 2.0, 3.0, 4.0, 5.0, 6.0]
            ),
        ),
    )

    self.assertEqual(action.proto.action_instance_id, 14)
    self.assertEqual(action.proto.part_name, "arm_part")
    self.assertEmpty(action.reactions)
    self.assertEqual(
        action.proto.action_type_name, "intrinsic.point_to_point_move"
    )

    got_params = point_to_point_move_pb2.PointToPointMoveFixedParams()
    self.assertTrue(action.proto.fixed_parameters.Unpack(got_params))
    self.assertEqual(
        got_params.goal_position.joints, [1.0, 2.0, 3.0, 4.0, 5.0, 6.0]
    )
    self.assertEqual(
        got_params.goal_velocity.joints, [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    )
    self.assertEqual(
        got_params.joint_limits.min_position.values,
        [-1.0, -2.0, -3.0, -4.0, -5.0, -6.0],
    )
    self.assertEqual(
        got_params.joint_limits.max_position.values,
        [1.0, 2.0, 3.0, 4.0, 5.0, 6.0],
    )


if __name__ == "__main__":
  absltest.main()
