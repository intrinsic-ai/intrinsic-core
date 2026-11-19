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

package_group(
    name = "public_api_users",
    packages = ["public"],
)

package_group(
    name = "internal_api_users",
    packages = [
        "//intrinsic/...",
        "//incode/...",
        "//intrinsic_apis/...",
        "//intrinsic_control/...",
        "//intrinsic_hardware/...",
        "//intrinsic_inference/...",
        "//intrinsic_kinematics/...",
        "//intrinsic_motion_planning/...",
        "//intrinsic_perception/...",
        "//intrinsic_runtime/...",
        "//intrinsic_sdk/...",
        "//intrinsic_simulation/...",
    ],
)

package_group(
    name = "intrinsic_internal",
    packages = [
        "//intrinsic/...",
        "//incode/...",
        "//intrinsic_apis/...",
        "//intrinsic_control/...",
        "//intrinsic_hardware/...",
        "//intrinsic_inference/...",
        "//intrinsic_kinematics/...",
        "//intrinsic_motion_planning/...",
        "//intrinsic_perception/...",
        "//intrinsic_runtime/...",
        "//intrinsic_sdk/...",
        "//intrinsic_simulation/...",
    ],
)

package_group(
    name = "public_asset_users",
    packages = ["public"],
)

package_group(
    name = "release_tooling",
    packages = [
        "//intrinsic/production/...",
        "//incode/production/...",
    ],
)
