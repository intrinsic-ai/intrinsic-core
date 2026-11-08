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

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_control/intrinsic/icon/testing:cc_test_and_malloc_test.bzl", "cc_test_and_malloc_test")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "sine_wave_action_proto",
    srcs = ["sine_wave_action.proto"],
)

cc_proto_library(
    name = "sine_wave_action_cc_proto",
    deps = [":sine_wave_action_proto"],
)

cc_library(
    name = "rtcl_sine_wave_action",
    srcs = ["rtcl_sine_wave_action.cc"],
    hdrs = ["rtcl_sine_wave_action.h"],
    deps = [
        ":sine_wave_action_cc_proto",
        ":sine_wave_action_info",
        "//intrinsic_control/intrinsic/icon/cc_client:condition",
        "//intrinsic_control/intrinsic/icon/control:action_factory_context",
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_control/intrinsic/icon/control:reaction",
        "//intrinsic_control/intrinsic/icon/control:rtcl_action",
        "//intrinsic_control/intrinsic/icon/control:slot_types",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/util:fixed_vector",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "rtcl_sine_wave_action_register",
    srcs = ["rtcl_sine_wave_action_register.cc"],
    deps = [
        ":rtcl_sine_wave_action",
        ":sine_wave_action_info",
        ":sine_wave_action_signature",
        "//intrinsic_control/intrinsic/icon/control:rtcl_action_factory_registry",
    ],
    alwayslink = True,
)

cc_library(
    name = "sine_wave_action_info",
    hdrs = ["sine_wave_action_info.h"],
    deps = [":sine_wave_action_cc_proto"],
)

cc_library(
    name = "sine_wave_action_signature",
    srcs = ["sine_wave_action_signature.cc"],
    hdrs = ["sine_wave_action_signature.h"],
    deps = [
        ":sine_wave_action_info",
        "//intrinsic_control/intrinsic/icon/actions:action_utils",
        "@abseil-cpp//absl/log:check",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test_and_malloc_test(
    name = "rtcl_sine_wave_action_test",
    size = "small",
    srcs = ["rtcl_sine_wave_action_test.cc"],
    deps = [
        ":rtcl_sine_wave_action_register",
        ":sine_wave_action_cc_proto",
        ":sine_wave_action_info",
        ":sine_wave_action_signature",
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_control/intrinsic/icon/control/actions:action_test_helper",
        "//intrinsic_control/intrinsic/icon/control/parts:fake_feature_interfaces",
        "//intrinsic_control/intrinsic/icon/control/parts/testing:position_loopback_fake_part",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)
