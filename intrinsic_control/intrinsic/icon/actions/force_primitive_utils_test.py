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

from intrinsic.icon.actions import force_primitive_pb2
from intrinsic.icon.actions import force_primitive_utils
from intrinsic.icon.control.primitives.force_control.proto import force_primitives_pb2
from intrinsic.manipulation.skills.force import contact_stiffness_pb2
from intrinsic.manipulation.skills.force import utils
from intrinsic.math.proto import point_pb2
from intrinsic.math.proto import pose_pb2
from intrinsic.math.proto import quaternion_pb2
from intrinsic.math.python import data_types
from intrinsic.solutions.testing import compare

MEDIUM_CONTACT_STIFFNESS = contact_stiffness_pb2.ContactStiffnessParams(
    default_contact_stiffness=contact_stiffness_pb2.CONTACT_STIFFNESS_MEDIUM
)

DEFAULT_TASK_PARAMS = force_primitive_utils.build_task_params(
    robot_tip_t_robot_tool=data_types.Pose3(translation=[1.0, 2.0, 3.0]),
    robot_base_t_task=data_types.Pose3(translation=[4.0, 5.0, 6.0]),
    contact_stiffness=MEDIUM_CONTACT_STIFFNESS,
)

FORCE_CONTROL_SETTINGS = utils.ForceControlParams(
    excessive_force_threshold=123.0,
    excessive_torque_threshold=456.0,
    virtual_translational_inertia=12.0,
    virtual_rotational_inertia=13.0,
    sensed_wrench_deadband=data_types.Wrench(
        force=[1.1, 1.2, 1.3], torque=[1.4, 1.5, 1.6]
    ),
)


class ForcePrimitiveUtilsTest(absltest.TestCase):

  def test_build_task_params_returns_correct_vakue(self):
    task_params = force_primitive_utils.build_task_params(
        robot_tip_t_robot_tool=data_types.Pose3(translation=[1.0, 2.0, 3.0]),
        robot_base_t_task=data_types.Pose3(translation=[4.0, 5.0, 6.0]),
        contact_stiffness=MEDIUM_CONTACT_STIFFNESS,
    )

    compare.assertProto2Equal(
        self,
        task_params,
        force_primitive_pb2.TaskParams(
            robot_tip_t_robot_tool=pose_pb2.Pose(
                position=point_pb2.Point(x=1.0, y=2.0, z=3.0),
                orientation=quaternion_pb2.Quaternion(
                    x=0.0, y=0.0, z=0.0, w=1.0
                ),
            ),
            robot_base_t_task=pose_pb2.Pose(
                position=point_pb2.Point(x=4.0, y=5.0, z=6.0),
                orientation=quaternion_pb2.Quaternion(
                    x=0.0, y=0.0, z=0.0, w=1.0
                ),
            ),
            environment_stiffness=MEDIUM_CONTACT_STIFFNESS,
        ),
    )

  def test_make_contact_action(self):
    make_contact = force_primitives_pb2.MakeContact(
        max_contact_force=10.0,
    )
    action = force_primitive_utils.create_make_contact_action(
        action_id=14,
        joint_position_part_name="robot_arm",
        ft_sensor_part_name="my_ft_sensor",
        primitive=make_contact,
        force_control_settings=FORCE_CONTROL_SETTINGS,
        task_params=DEFAULT_TASK_PARAMS,
    )

    self.assertEqual(action.id, 14)
    self.assertEqual(action.proto.action_type_name, "intrinsic.force_primitive")
    self.assertEqual(
        action.proto.slot_part_map.slot_name_to_part_name["arm"], "robot_arm"
    )
    self.assertEqual(
        action.proto.slot_part_map.slot_name_to_part_name["ft_sensor"],
        "my_ft_sensor",
    )
    got_params = force_primitive_pb2.ForcePrimitiveFixedParams()
    self.assertTrue(action.proto.fixed_parameters.Unpack(got_params))

    compare.assertProto2Equal(
        self, got_params.force_primitive.make_contact, make_contact
    )
    compare.assertProto2Equal(
        self, got_params.force_control_settings, FORCE_CONTROL_SETTINGS.proto()
    )
    compare.assertProto2Equal(self, got_params.task_params, DEFAULT_TASK_PARAMS)


if __name__ == "__main__":
  absltest.main()
