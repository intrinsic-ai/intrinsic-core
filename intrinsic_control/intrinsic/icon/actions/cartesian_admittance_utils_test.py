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

from intrinsic.icon.actions import cartesian_admittance_action_pb2
from intrinsic.icon.actions import cartesian_admittance_utils
from intrinsic.icon.actions import cartesian_impedance_pb2
from intrinsic.icon.actions import cartesian_impedance_reference_generators_pb2 as reference_generators_pb2
from intrinsic.icon.proto import cart_space_pb2
from intrinsic.icon.proto import matrix_conversions
from intrinsic.kinematics.types import joint_limits_pb2
from intrinsic.math.python import data_types

_ARRAY_EQUAL_PRECISION = 5


class CartesianAdmittanceUtilsTest(absltest.TestCase):

  def test_create_action(self):
    action = cartesian_admittance_utils.create_cartesian_admittance_action(
        74,
        "arm_part",
        "ft_sensor_part",
        virtual_cartesian_inertia=np.diag([1.0, 1.1, 1.2, 1.3, 1.4, 1.5]),
        cartesian_stiffness=np.diag([2.0, 2.1, 2.2, 2.3, 2.4, 2.5]),
        cartesian_damping=np.diag([3.0, 3.1, 3.2, 3.3, 3.4, 3.5]),
        robot_tip_t_robot_tool=data_types.Pose3(translation=[4.0, 4.1, 4.2]),
        task_t_tool_reference_pose=data_types.Pose3(
            translation=[5.0, 5.1, 5.2]
        ),
        tool_reference_twist=data_types.Twist(
            linear=[6.0, 6.1, 6.2], angular=[6.3, 6.4, 6.5]
        ),
        tool_reference_wrench=data_types.Wrench(
            force=[7.0, 7.1, 7.2], torque=[7.3, 7.4, 7.5]
        ),
        joint_limits=joint_limits_pb2.JointLimits(
            min_position=joint_limits_pb2.RepeatedDouble(
                values=[-8.0, -8.1, -8.2, -8.3, -8.4, -8.5]
            ),
            max_position=joint_limits_pb2.RepeatedDouble(
                values=[8.0, 8.1, 8.2, 8.3, 8.4, 8.5]
            ),
        ),
        cartesian_limits=cart_space_pb2.CartesianLimits(
            min_translational_position=[-9.0, -9.1, -9.2],
            max_translational_position=[9.0, 9.1, 9.2],
        ),
        sensed_wrench_deadband=data_types.Wrench(
            force=[10.0, 10.1, 10.2], torque=[10.3, 10.4, 10.5]
        ),
        state_variable_config=(
            cartesian_impedance_pb2.StateVariableConfiguration(
                translation_velocity_error_threshold=1e-2
            )
        ),
        lowpass_filter_constant=0.34,
    )

    self.assertEqual(action.proto.action_instance_id, 74)
    self.assertEqual(
        action.proto.slot_part_map.slot_name_to_part_name["arm"], "arm_part"
    )
    self.assertEqual(
        action.proto.slot_part_map.slot_name_to_part_name["ft_sensor"],
        "ft_sensor_part",
    )
    self.assertEqual(
        action.proto.action_type_name, "intrinsic.cartesian_admittance_action"
    )

    got_params = (
        cartesian_admittance_action_pb2.CartesianAdmittanceActionFixedParams()
    )
    self.assertTrue(action.proto.fixed_parameters.Unpack(got_params))
    got_impedance_params = got_params.cartesian_impedance_parameters

    self.assertEqual(
        got_impedance_params.cartesian_target.virtual_cartesian_inertia,
        matrix_conversions.from_ndarray(
            np.diag([1.0, 1.1, 1.2, 1.3, 1.4, 1.5])
        ),
    )

    self.assertEqual(
        got_impedance_params.cartesian_target.cartesian_stiffness,
        matrix_conversions.from_ndarray(
            np.diag([2.0, 2.1, 2.2, 2.3, 2.4, 2.5])
        ),
    )

    self.assertEqual(
        got_impedance_params.cartesian_target.cartesian_damping,
        matrix_conversions.from_ndarray(
            np.diag([3.0, 3.1, 3.2, 3.3, 3.4, 3.5])
        ),
    )

    self.assertEqual(
        got_impedance_params.cartesian_target.robot_tip_t_robot_tool.pos.x, 4.0
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.robot_tip_t_robot_tool.pos.y, 4.1
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.robot_tip_t_robot_tool.pos.z, 4.2
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.robot_tip_t_robot_tool.rot.qw,
        1.0,
    )

    self.assertEqual(
        got_impedance_params.cartesian_target.task_t_tool_reference_pose.pos.x,
        5.0,
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.task_t_tool_reference_pose.pos.y,
        5.1,
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.task_t_tool_reference_pose.pos.z,
        5.2,
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.task_t_tool_reference_pose.rot.qw,
        1.0,
    )

    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_twist.x, 6.0
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_twist.y, 6.1
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_twist.z, 6.2
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_twist.rx, 6.3
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_twist.ry, 6.4
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_twist.rz, 6.5
    )

    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.x, 7.0
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.y, 7.1
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.z, 7.2
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.rx, 7.3
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.ry, 7.4
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.rz, 7.5
    )

    self.assertEqual(
        got_impedance_params.constraints.joint_limits.min_position.values,
        [-8.0, -8.1, -8.2, -8.3, -8.4, -8.5],
    )
    self.assertEqual(
        got_impedance_params.constraints.joint_limits.max_position.values,
        [8.0, 8.1, 8.2, 8.3, 8.4, 8.5],
    )

    self.assertEqual(
        got_impedance_params.constraints.cartesian_limits.min_translational_position,
        [-9.0, -9.1, -9.2],
    )
    self.assertEqual(
        got_impedance_params.constraints.cartesian_limits.max_translational_position,
        [9.0, 9.1, 9.2],
    )

    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.x,
        10.0,
    )
    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.y,
        10.1,
    )
    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.z,
        10.2,
    )
    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.rx,
        10.3,
    )
    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.ry,
        10.4,
    )
    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.rz,
        10.5,
    )

    got_state_variable_configuration = (
        got_impedance_params.state_variable_configuration
    )
    self.assertEqual(
        got_state_variable_configuration.translation_velocity_error_threshold,
        1e-2,
    )

    got_reference_generators = got_impedance_params.reference_generators
    self.assertEqual(
        got_reference_generators.cartesian.simple_lowpass.lowpass_filter_constant,
        0.34,
    )
    self.assertEqual(
        got_reference_generators.nullspace.simple_lowpass.lowpass_filter_constant,
        0.34,
    )

  def test_simple_lowpass_is_default_reference_generator(self):
    action = cartesian_admittance_utils.create_cartesian_admittance_action(
        74,
        "arm_part",
        "ft_sensor_part",
        virtual_cartesian_inertia=np.diag([1.0, 1.1, 1.2, 1.3, 1.4, 1.5]),
        cartesian_stiffness=np.diag([2.0, 2.1, 2.2, 2.3, 2.4, 2.5]),
    )

    got_params = (
        cartesian_admittance_action_pb2.CartesianAdmittanceActionFixedParams()
    )
    self.assertTrue(action.proto.fixed_parameters.Unpack(got_params))
    got_impedance_params = got_params.cartesian_impedance_parameters

    got_reference_generators = got_impedance_params.reference_generators
    self.assertEqual(
        got_reference_generators.cartesian.WhichOneof("reference_generator"),
        "simple_lowpass",
    )
    self.assertEqual(
        got_reference_generators.cartesian.simple_lowpass,
        reference_generators_pb2.SimpleImpedanceReferenceGenerator(),
    )
    self.assertEqual(
        got_reference_generators.nullspace.WhichOneof("reference_generator"),
        "simple_lowpass",
    )
    self.assertEqual(
        got_reference_generators.nullspace.simple_lowpass,
        reference_generators_pb2.SimpleNullspaceReferenceGenerator(),
    )

  def test_sensed_force_variable_is_valid(self):
    self.assertEqual(
        cartesian_admittance_utils.StateVariables.SENSED_FORCE,
        "intrinsic.sensed_force",
    )

  def test_settled_for_seconds_is_valid(self):
    self.assertEqual(
        cartesian_admittance_utils.StateVariables.SETTLED_FOR_SECONDS,
        "intrinsic.settled_for_seconds",
    )

  def test_is_settled_is_valid(self):
    self.assertEqual(
        cartesian_admittance_utils.StateVariables.IS_SETTLED,
        "intrinsic.is_settled",
    )

  def test_elapsed_time_seconds_is_valid(self):
    self.assertEqual(
        cartesian_admittance_utils.StateVariables.ELAPSED_TIME_SECONDS,
        "elapsed_time_seconds",
    )

  def test_translational_distance_traveled_is_valid(self):
    self.assertEqual(
        cartesian_admittance_utils.StateVariables.DISTANCE_TRAVELED,
        "intrinsic.translational_distance_traveled",
    )

  def test_max_dispacement_in_window_is_valid(self):
    self.assertEqual(
        cartesian_admittance_utils.StateVariables.MAX_DISPLACEMENT_IN_WINDOW,
        "maximum_translational_displacement_in_time_window",
    )

  def test_create_approach_action_returns_valid_action(self):
    action = cartesian_admittance_utils.create_approach_contact_action(
        56,
        "my_robot_arm",
        "my_ft_sensor",
        virtual_cartesian_inertia=np.diag([1.0] * 6),
        contact_force=17.3,
        motion_direction_in_base=np.array([0.0, 0.0, 1.0]),
        mechanical_stiffness_at_contact=80000.0,
        tool_translational_stiffness_in_nullspace=300,
        tool_rotational_stiffness_in_nullspace=400,
        robot_tip_t_robot_tool=data_types.Pose3(translation=[3.0, 3.1, 3.2]),
        joint_limits=joint_limits_pb2.JointLimits(
            min_position=joint_limits_pb2.RepeatedDouble(
                values=[-4.0, -4.1, -4.2, -4.3, -4.4, -4.5]
            ),
            max_position=joint_limits_pb2.RepeatedDouble(
                values=[4.0, 4.1, 4.2, 4.3, 4.4, 4.5]
            ),
        ),
        cartesian_limits=cart_space_pb2.CartesianLimits(
            min_translational_position=[-5.0, -5.1, -5.2],
            max_translational_position=[5.0, 5.1, 5.2],
        ),
        sensed_wrench_deadband=data_types.Wrench(
            force=[6.0, 6.1, 6.2], torque=[6.3, 6.4, 6.5]
        ),
        state_variable_config=(
            cartesian_impedance_pb2.StateVariableConfiguration(
                translation_velocity_error_threshold=2e-2
            )
        ),
    )

    self.assertEqual(action.proto.action_instance_id, 56)
    self.assertEqual(
        action.proto.slot_part_map.slot_name_to_part_name["arm"], "my_robot_arm"
    )
    self.assertEqual(
        action.proto.slot_part_map.slot_name_to_part_name["ft_sensor"],
        "my_ft_sensor",
    )
    self.assertEqual(
        action.proto.action_type_name, "intrinsic.cartesian_admittance_action"
    )

    got_params = (
        cartesian_admittance_action_pb2.CartesianAdmittanceActionFixedParams()
    )
    self.assertTrue(action.proto.fixed_parameters.Unpack(got_params))
    got_impedance_params = got_params.cartesian_impedance_parameters

    self.assertEqual(
        got_impedance_params.cartesian_target.virtual_cartesian_inertia,
        matrix_conversions.from_ndarray(np.diag([1.0] * 6)),
    )

    self.assertEqual(
        got_impedance_params.cartesian_target.cartesian_stiffness,
        matrix_conversions.from_ndarray(
            np.diag([300.0, 300.0, 0.0, 400.0, 400.0, 400.0])
        ),
    )
    np.testing.assert_array_almost_equal(
        matrix_conversions.to_ndarray(
            got_impedance_params.cartesian_target.cartesian_damping
        ),
        np.diag([570.03157807] * 6),
        decimal=_ARRAY_EQUAL_PRECISION,
    )

    self.assertEqual(
        got_impedance_params.cartesian_target.robot_tip_t_robot_tool.pos.x, 3.0
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.robot_tip_t_robot_tool.pos.y, 3.1
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.robot_tip_t_robot_tool.pos.z, 3.2
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.robot_tip_t_robot_tool.rot.qw,
        1.0,
    )

    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.x, 0.0
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.y, 0.0
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.z, 17.3
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.rx, 0.0
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.ry, 0.0
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.rz, 0.0
    )

    self.assertEqual(
        got_impedance_params.constraints.joint_limits.min_position.values,
        [-4.0, -4.1, -4.2, -4.3, -4.4, -4.5],
    )
    self.assertEqual(
        got_impedance_params.constraints.joint_limits.max_position.values,
        [4.0, 4.1, 4.2, 4.3, 4.4, 4.5],
    )

    self.assertEqual(
        got_impedance_params.constraints.cartesian_limits.min_translational_position,
        [-5.0, -5.1, -5.2],
    )
    self.assertEqual(
        got_impedance_params.constraints.cartesian_limits.max_translational_position,
        [5.0, 5.1, 5.2],
    )

    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.x,
        6.0,
    )
    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.y,
        6.1,
    )
    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.z,
        6.2,
    )
    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.rx,
        6.3,
    )
    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.ry,
        6.4,
    )
    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.rz,
        6.5,
    )

    got_state_variable_configuration = (
        got_impedance_params.state_variable_configuration
    )
    self.assertEqual(
        got_state_variable_configuration.translation_velocity_error_threshold,
        2e-2,
    )

  def test_create_stabilize_action_returns_valid_action(self):
    action = cartesian_admittance_utils.create_stabilize_contact_action(
        57,
        "my_robot_arm",
        "my_ft_sensor",
        virtual_cartesian_inertia=np.diag([1.0] * 6),
        contact_force=17.3,
        motion_direction_in_base=np.array([0.0, 0.0, 1.0]),
        mechanical_stiffness_at_contact=80000.0,
        tool_translational_stiffness_in_nullspace=300,
        tool_rotational_stiffness_in_nullspace=400,
        robot_tip_t_robot_tool=data_types.Pose3(translation=[3.0, 3.1, 3.2]),
        joint_limits=joint_limits_pb2.JointLimits(
            min_position=joint_limits_pb2.RepeatedDouble(
                values=[-4.0, -4.1, -4.2, -4.3, -4.4, -4.5]
            ),
            max_position=joint_limits_pb2.RepeatedDouble(
                values=[4.0, 4.1, 4.2, 4.3, 4.4, 4.5]
            ),
        ),
        cartesian_limits=cart_space_pb2.CartesianLimits(
            min_translational_position=[-5.0, -5.1, -5.2],
            max_translational_position=[5.0, 5.1, 5.2],
        ),
        sensed_wrench_deadband=data_types.Wrench(
            force=[6.0, 6.1, 6.2], torque=[6.3, 6.4, 6.5]
        ),
        state_variable_config=(
            cartesian_impedance_pb2.StateVariableConfiguration(
                translation_velocity_error_threshold=2e-2
            )
        ),
    )

    self.assertEqual(action.proto.action_instance_id, 57)
    self.assertEqual(
        action.proto.slot_part_map.slot_name_to_part_name["arm"], "my_robot_arm"
    )
    self.assertEqual(
        action.proto.slot_part_map.slot_name_to_part_name["ft_sensor"],
        "my_ft_sensor",
    )
    self.assertEqual(
        action.proto.action_type_name, "intrinsic.cartesian_admittance_action"
    )

    got_params = (
        cartesian_admittance_action_pb2.CartesianAdmittanceActionFixedParams()
    )
    self.assertTrue(action.proto.fixed_parameters.Unpack(got_params))
    got_impedance_params = got_params.cartesian_impedance_parameters

    self.assertEqual(
        got_impedance_params.cartesian_target.virtual_cartesian_inertia,
        matrix_conversions.from_ndarray(np.diag([1.0] * 6)),
    )

    self.assertEqual(
        got_impedance_params.cartesian_target.cartesian_stiffness,
        matrix_conversions.from_ndarray(
            np.diag([300.0, 300.0, 0.0, 400.0, 400.0, 400.0])
        ),
    )
    np.testing.assert_array_almost_equal(
        matrix_conversions.to_ndarray(
            got_impedance_params.cartesian_target.cartesian_damping
        ),
        np.diag([570.03157807] * 6),
        decimal=_ARRAY_EQUAL_PRECISION,
    )

    self.assertEqual(
        got_impedance_params.cartesian_target.robot_tip_t_robot_tool.pos.x, 3.0
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.robot_tip_t_robot_tool.pos.y, 3.1
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.robot_tip_t_robot_tool.pos.z, 3.2
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.robot_tip_t_robot_tool.rot.qw,
        1.0,
    )

    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.x, 0.0
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.y, 0.0
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.z, 17.3
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.rx, 0.0
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.ry, 0.0
    )
    self.assertEqual(
        got_impedance_params.cartesian_target.tool_reference_wrench.rz, 0.0
    )

    self.assertEqual(
        got_impedance_params.constraints.joint_limits.min_position.values,
        [-4.0, -4.1, -4.2, -4.3, -4.4, -4.5],
    )
    self.assertEqual(
        got_impedance_params.constraints.joint_limits.max_position.values,
        [4.0, 4.1, 4.2, 4.3, 4.4, 4.5],
    )

    self.assertEqual(
        got_impedance_params.constraints.cartesian_limits.min_translational_position,
        [-5.0, -5.1, -5.2],
    )
    self.assertEqual(
        got_impedance_params.constraints.cartesian_limits.max_translational_position,
        [5.0, 5.1, 5.2],
    )

    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.x,
        6.0,
    )
    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.y,
        6.1,
    )
    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.z,
        6.2,
    )
    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.rx,
        6.3,
    )
    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.ry,
        6.4,
    )
    self.assertEqual(
        got_impedance_params.algorithm_configuration.sensed_wrench_deadband.rz,
        6.5,
    )

    got_state_variable_configuration = (
        got_impedance_params.state_variable_configuration
    )
    self.assertEqual(
        got_state_variable_configuration.translation_velocity_error_threshold,
        2e-2,
    )

    got_reference_generators = got_impedance_params.reference_generators
    self.assertEqual(
        got_reference_generators.cartesian.simple_lowpass.lowpass_filter_constant,
        1.0,
    )
    self.assertEqual(
        got_reference_generators.nullspace.simple_lowpass.lowpass_filter_constant,
        1.0,
    )

  def test_create_action_with_too_low_velocity_fails(self):

    with self.assertRaisesRegex(
        ValueError,
        "The defined force can result in a velocity of 0.030 m/s, but the"
        " max_translational_velocity is 0.010 m/s.",
    ):
      cartesian_admittance_utils.create_approach_contact_action(
          57,
          "my_robot_arm",
          "my_ft_sensor",
          virtual_cartesian_inertia=np.diag([1.0] * 6),
          contact_force=17.3,
          motion_direction_in_base=np.array([0.0, 0.0, 1.0]),
          mechanical_stiffness_at_contact=80000.0,
          tool_translational_stiffness_in_nullspace=300,
          tool_rotational_stiffness_in_nullspace=400,
          robot_tip_t_robot_tool=data_types.Pose3(translation=[3.0, 3.1, 3.2]),
          cartesian_limits=cart_space_pb2.CartesianLimits(
              max_translational_velocity=[0.01, 0.01, 0.01]
          ),
      )


if __name__ == "__main__":
  absltest.main()
