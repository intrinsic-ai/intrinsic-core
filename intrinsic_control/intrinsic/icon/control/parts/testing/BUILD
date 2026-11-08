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

load("//bazel:cc_macros.bzl", "cc_library")

cc_library(
    name = "position_loopback_fake_part",
    testonly = True,
    srcs = ["position_loopback_fake_part.cc"],
    hdrs = ["position_loopback_fake_part.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":fake_realtime_part",
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_control/intrinsic/icon/control/parts:fake_feature_interfaces",
        "//intrinsic_control/intrinsic/icon/control/parts:realtime_part_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "fake_realtime_part",
    testonly = True,
    hdrs = ["fake_realtime_part.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        "//intrinsic_control/intrinsic/icon/cc_client:operational_status",
        "//intrinsic_control/intrinsic/icon/common:part_properties",
        "//intrinsic_control/intrinsic/icon/control:realtime_bridge_types",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interface_registry",
        "//intrinsic_control/intrinsic/icon/control/parts:realtime_part_interface",
        "//intrinsic_control/intrinsic/icon/control/parts:realtime_part_property_access",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/log:check",
    ],
)
