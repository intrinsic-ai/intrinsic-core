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

load("//bazel:cc_macros.bzl", "cc_library", "cc_test")

package(
    default_visibility = [
        "//intrinsic_control/intrinsic/icon/control:icon_rtcl",
        "//visibility:public",
    ],
)

cc_library(
    name = "icon_c_api",
    hdrs = [
        "c_action_factory_context.h",
        "c_feature_interfaces.h",
        "c_plugin_api.h",
        "c_realtime_signal_access.h",
        "c_realtime_slot_map.h",
        "c_realtime_status.h",
        "c_rtcl_action.h",
        "c_streaming_io_realtime_access.h",
        "c_types.h",
    ],
)

cc_library(
    name = "convert_c_realtime_status",
    srcs = ["convert_c_realtime_status.cc"],
    hdrs = ["convert_c_realtime_status.h"],
    deps = [
        ":icon_c_api",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "convert_c_realtime_status_test",
    size = "small",
    srcs = ["convert_c_realtime_status_test.cc"],
    deps = [
        ":convert_c_realtime_status",
        ":icon_c_api",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "convert_c_types",
    srcs = ["convert_c_types.cc"],
    hdrs = ["convert_c_types.h"],
    deps = [
        ":icon_c_api",
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_control/intrinsic/icon/control:realtime_signal_types",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:optional",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_test(
    name = "convert_c_types_test",
    size = "small",
    srcs = ["convert_c_types_test.cc"],
    deps = [
        ":convert_c_types",
        ":icon_c_api",
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
    ],
)
