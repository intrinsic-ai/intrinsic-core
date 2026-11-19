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
    name = "ioc_export_targets",
    srcs = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:motion_planner_service_asset",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:clear_motion_planner_service_cache_skill",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:move_robot_skill",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:preplan_motion_skill",
    ],
)

test_suite(
    name = "ioc_export_test_targets",
    tests = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner:motion_planner_test",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:motion_planner_service_test",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:clear_motion_planner_service_cache_test",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:move_robot_test",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:preplan_motion_test",
    ],
)
