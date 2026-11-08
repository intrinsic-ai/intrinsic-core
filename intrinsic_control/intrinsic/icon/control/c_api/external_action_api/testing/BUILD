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

package(default_visibility = [
    "//intrinsic_control/intrinsic/icon:rtcl_users",
])

cc_library(
    name = "icon_action_factory_context_fake",
    testonly = True,
    srcs = ["icon_action_factory_context_fake.cc"],
    hdrs = ["icon_action_factory_context_fake.h"],
    deps = [
        ":icon_realtime_signal_access_and_map_fake",
        ":icon_slot_map_fake",
        ":icon_streaming_io_registry_fake",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_realtime_status",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/c_api/external_action_api:icon_action_factory_context",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:string_wrapper",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "icon_action_factory_context_fake_test",
    srcs = ["icon_action_factory_context_fake_test.cc"],
    deps = [
        ":icon_action_factory_context_fake",
        ":icon_realtime_signal_access_and_map_fake",
        ":icon_slot_map_fake",
        ":icon_streaming_io_registry_fake",
        "//intrinsic_control/intrinsic/icon/actions:action_utils",
        "//intrinsic_control/intrinsic/icon/control:slot_types",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_types",
        "//intrinsic_control/intrinsic/icon/control/c_api/external_action_api:icon_action_factory_context",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "icon_realtime_signal_access_and_map_fake",
    testonly = True,
    srcs = ["icon_realtime_signal_access_and_map_fake.cc"],
    hdrs = ["icon_realtime_signal_access_and_map_fake.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control:realtime_signal_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_realtime_status",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/c_api/external_action_api:icon_realtime_signal_access",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "icon_realtime_signal_access_and_map_fake_test",
    srcs = ["icon_realtime_signal_access_and_map_fake_test.cc"],
    deps = [
        ":icon_realtime_signal_access_and_map_fake",
        "//intrinsic_control/intrinsic/icon/actions:action_utils",
        "//intrinsic_control/intrinsic/icon/control:realtime_signal_types",
        "//intrinsic_control/intrinsic/icon/control/c_api/external_action_api:icon_realtime_signal_access",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "icon_streaming_io_registry_fake",
    testonly = True,
    srcs = ["icon_streaming_io_registry_fake.cc"],
    hdrs = ["icon_streaming_io_registry_fake.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control:streaming_io_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_realtime_status",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/c_api/external_action_api:icon_streaming_io_access",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:streaming_io_wrapper",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:string_wrapper",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "icon_streaming_io_registry_fake_test",
    srcs = ["icon_streaming_io_registry_fake_test.cc"],
    deps = [
        ":icon_streaming_io_registry_fake",
        "//intrinsic_control/intrinsic/icon/actions:action_utils",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_types",
        "//intrinsic_control/intrinsic/icon/control/c_api/external_action_api:icon_streaming_io_access",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:streaming_io_wrapper",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "loopback_fake_arm",
    testonly = True,
    srcs = ["loopback_fake_arm.cc"],
    hdrs = ["loopback_fake_arm.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_realtime_status",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_cc_proto",
    ],
)

cc_test(
    name = "loopback_fake_arm_test",
    srcs = ["loopback_fake_arm_test.cc"],
    deps = [
        ":loopback_fake_arm",
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_control/intrinsic/icon/control/c_api/external_action_api:icon_feature_interfaces",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "icon_slot_map_fake",
    testonly = True,
    srcs = ["icon_slot_map_fake.cc"],
    hdrs = ["icon_slot_map_fake.h"],
    deps = [
        ":loopback_fake_arm",
        "//intrinsic_control/intrinsic/icon/control:slot_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/c_api/external_action_api:icon_realtime_slot_map",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "icon_slot_map_fake_test",
    srcs = ["icon_slot_map_fake_test.cc"],
    deps = [
        ":icon_slot_map_fake",
        "//intrinsic_control/intrinsic/icon/control:slot_types",
        "//intrinsic_control/intrinsic/icon/control/c_api/external_action_api:icon_feature_interfaces",
        "//intrinsic_control/intrinsic/icon/control/c_api/external_action_api:icon_realtime_slot_map",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "action_test_helper",
    testonly = True,
    srcs = ["action_test_helper.cc"],
    hdrs = ["action_test_helper.h"],
    deps = [
        ":icon_action_factory_context_fake",
        ":icon_realtime_signal_access_and_map_fake",
        ":icon_slot_map_fake",
        ":icon_streaming_io_registry_fake",
        "//intrinsic_control/intrinsic/icon/control/c_api/external_action_api:icon_action_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)
