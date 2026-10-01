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

"""Provides way to define allowlists used in starlark rules"""

_LISTS = {
    # TODO: b/402283805 - migrate users of resource_set_shim to other
    # alternatives, such as intrinsic_solution or other offline processing.
    "resource_set_shim": [
        # keep-sorted start
        # keep-sorted end
    ],
    # TODO(b/406857001): migrate users of the old equipment-based dependencies
    # to the new model where dependencies are declared in the skill's parameter.
    "skill_manifest_deps": [
        # keep-sorted start
        "//intrinsic/manipulation/skills/force:move_to_contact_manifest",
        "//intrinsic/skills/apps/icon2:homing_manifest",
        "//intrinsic/skills/examples:adder_skill_manifest",
        "//intrinsic_control/intrinsic/icon/skills:aio_read_input_cc_manifest",
        "//intrinsic_control/intrinsic/icon/skills:aio_set_output_cc_manifest",
        "//intrinsic_control/intrinsic/icon/skills:dio_read_input_cc_manifest",
        "//intrinsic_control/intrinsic/icon/skills:dio_set_output_cc_manifest",
        "//intrinsic_control/intrinsic/icon/skills:dio_wait_for_input_cc_manifest",
        "//intrinsic_control/intrinsic/icon/skills:enable_realtime_control_manifest",
        "//intrinsic_control/intrinsic/icon/skills:move_gripper_joints_manifest_cc",
        "//intrinsic_control/intrinsic/icon/skills:set_payload_manifest",
        "//intrinsic_control/intrinsic/icon/skills:update_robot_joint_positions_manifest",
        "//intrinsic_perception/intrinsic/perception/skills/calibration:collect_calibration_data_manifest",
        "//intrinsic_perception/intrinsic/perception/skills/calibration:initialize_calibration_manifest",
        "//intrinsic_perception/intrinsic/perception/skills/calibration:sample_calibration_poses_manifest",
        "//intrinsic_perception/intrinsic/perception/skills/multi_view:estimate_pose_multi_view_manifest",
        "//incode/motion_planning/skills:move_robot_manifest",
        "//incode/motion_planning/skills:preplan_motion_manifest",
        # keep-sorted end
    ],
}

def _abs_target(ctx):
    return "//" + ctx.label.package + ":" + ctx.label.name

def in_allowlist(ctx, list_name):
    return _abs_target(ctx) in _LISTS[list_name]
