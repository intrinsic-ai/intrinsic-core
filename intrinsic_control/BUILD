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

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "export_targets",
    srcs = [
        ":robot_descriptions",
        "//intrinsic/manipulation/skills/force:move_to_contact_skill",
        "//intrinsic/math/spline:uniform_between_knots_bspline_sampler",
        "//intrinsic/math/spline:uniform_bspline_sampler",
        "//intrinsic/simulation/gazebo/plugins:gazebo_hwm",
        "//intrinsic/skills/apps/icon2:homing_skill",
        "//intrinsic_control/intrinsic/icon/actions:joint_jogging_py_pb2",
        "//intrinsic_control/intrinsic/icon/equipment:resolved_dependency_utils",
        "//intrinsic_control/intrinsic/icon/equipment:resolved_dependency_utils_py",
        "//intrinsic_control/intrinsic/icon/hal/tools:hardware_module_introspection",
        "//intrinsic_control/intrinsic/icon/hardware_modules/abb:abb_robots_core",
        "//intrinsic_control/intrinsic/icon/hardware_modules/fake:fake_module",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi:kuka_rsi_hardware_module",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi:kuka_rsi_loopback",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi:kuka_rsi_robots_core",
        "//intrinsic_control/intrinsic/icon/hardware_modules/loopback:loopback_hardware_module",
        "//intrinsic_control/intrinsic/icon/hardware_modules/strawbot:strawbot_hal_module",
        "//intrinsic_control/intrinsic/icon/hardware_modules/universal_robots:universal_robots_core",
        "//intrinsic_control/intrinsic/icon/machines/common:generic_icon_mainloop_type",
        "//intrinsic_control/intrinsic/icon/skills:aio_read_input_skill",
        "//intrinsic_control/intrinsic/icon/skills:aio_set_output_skill",
        "//intrinsic_control/intrinsic/icon/skills:dio_read_input_skill",
        "//intrinsic_control/intrinsic/icon/skills:dio_set_output_skill",
        "//intrinsic_control/intrinsic/icon/skills:dio_wait_for_input_skill",
        "//intrinsic_control/intrinsic/icon/skills:disable_realtime_control_skill",
        "//intrinsic_control/intrinsic/icon/skills:enable_realtime_control_skill",
        "//intrinsic_control/intrinsic/icon/skills:move_gripper_joints_skill",
        "//intrinsic_control/intrinsic/icon/skills:set_payload_skill",
        "//intrinsic_control/intrinsic/icon/skills:update_robot_joint_positions_skill",
        "//intrinsic_hardware/intrinsic/hardware/force_torque/axia80_net:axia80_net_hardware_device",
    ],
)

filegroup(
    name = "robot_descriptions",
    srcs = [
        "//intrinsic_control/intrinsic/models/robot_definitions/abb:irb1300_10_115_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/fanuc:crx_10ia_l_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/fanuc:crx_10ia_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/fanuc:crx_20ia_l_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/fanuc:crx_30ia_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/fanuc:lr_10ia_10",
        "//intrinsic_control/intrinsic/models/robot_definitions/fanuc:lr_mate_200id_4s_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/fanuc:lr_mate_200id_7l",
        "//intrinsic_control/intrinsic/models/robot_definitions/fanuc:lr_mate_200id_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/fanuc:m_10id_12_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/fanuc:m_20id_25_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/fanuc:m_20id_35_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/fanuc:m_710id_50m_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/fanuc:r_2000ic_210l",
        "//intrinsic_control/intrinsic/models/robot_definitions/kuka:kr10_r1100_2_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/kuka:kr16_r2010_2_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/kuka:kr20_r1810_2_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/kuka:kr50_r2500_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/kuka:kr6_r900_2_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/ur:ur10e_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/ur:ur3e_scene_object",
        "//intrinsic_control/intrinsic/models/robot_definitions/ur:ur5e_scene_object",
    ],
)

test_suite(
    name = "export_test_targets",
    tests = [
        "//intrinsic/math/spline:uniform_between_knots_bspline_sampler_test",
        "//intrinsic/math/spline:uniform_bspline_sampler_test",
        "//intrinsic/simulation/gazebo/plugins:gazebo_hwm_test",
        "//intrinsic_control/intrinsic/icon/hardware_modules/fake:fake_module_test",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi:kuka_rsi_hardware_module_test",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi:kuka_rsi_proto_tests",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi:kuka_rsi_util_test",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi/kuka_system_control:kuka_eki_system_control_test",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi/kuka_system_control:kuka_plc_client_test",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi/kuka_system_control:kuka_plc_system_control_test",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi/kuka_system_control:kuka_system_control_factory_test",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi/kuka_system_status:kuka_eki_system_status_test",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi/kuka_system_status:kuka_system_status_factory_test",
        "//third_party/abb_hardware_module/abb_hwm/tests:abb_elog_test",
        "//third_party/abb_hardware_module/abb_hwm/tests:egm_serialization_test",
        "//third_party/abb_hardware_module/abb_hwm/tests:egm_server_test",
        "//third_party/abb_hardware_module/abb_hwm/tests:rws_egm_utils_test",
    ],
)

package_group(
    name = "visibility_packages",
    packages = [
        "//intrinsic_control/intrinsic/icon/...",
    ],
)
