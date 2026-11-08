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

load("//bazel:cc_macros.bzl", "cc_binary", "cc_library", "cc_test")
load("//intrinsic_control/intrinsic/icon/testing:cc_test_and_malloc_test.bzl", "cc_test_and_malloc_test")

package(default_visibility = [
    "//intrinsic_control/intrinsic/icon:rtcl_users",
])

cc_library(
    name = "icon_action_interface",
    hdrs = ["icon_action_interface.h"],
    deps = [
        ":icon_action_factory_context",
        ":icon_realtime_signal_access",
        ":icon_realtime_slot_map",
        ":icon_streaming_io_access",
        "//intrinsic_control/intrinsic/icon/cc_client:condition",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "icon_feature_interfaces",
    srcs = ["icon_feature_interfaces.cc"],
    hdrs = ["icon_feature_interfaces.h"],
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
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
    ],
)

cc_test_and_malloc_test(
    name = "icon_feature_interfaces_test",
    srcs = ["icon_feature_interfaces_test.cc"],
    deps = [
        ":icon_feature_interfaces",
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:feature_interface_wrappers",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:force_torque_sensor_wrapper",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:joint_limits_wrapper",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:joint_position_sensor_wrapper",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:joint_position_wrapper",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:joint_velocity_estimator_wrapper",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:manipulator_kinematics_wrapper",
        "//intrinsic_control/intrinsic/icon/control/parts:fake_feature_interfaces",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:manifolds",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "icon_realtime_slot_map",
    srcs = ["icon_realtime_slot_map.cc"],
    hdrs = ["icon_realtime_slot_map.h"],
    deps = [
        ":icon_feature_interfaces",
        "//intrinsic_control/intrinsic/icon/control:slot_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
    ],
)

cc_test_and_malloc_test(
    name = "icon_realtime_slot_map_test",
    srcs = ["icon_realtime_slot_map_test.cc"],
    deps = [
        ":icon_feature_interfaces",
        ":icon_realtime_slot_map",
        "//intrinsic_control/intrinsic/icon/control:fake_realtime_slot_map_impl",
        "//intrinsic_control/intrinsic/icon/control:realtime_slot_map",
        "//intrinsic_control/intrinsic/icon/control:slot_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:feature_interface_wrappers",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:realtime_slot_map_wrapper",
        "//intrinsic_control/intrinsic/icon/control/parts:fake_feature_interfaces",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interface_registry",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
    ],
)

cc_library(
    name = "icon_realtime_signal_access",
    srcs = ["icon_realtime_signal_access.cc"],
    hdrs = ["icon_realtime_signal_access.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control:realtime_signal_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_realtime_status",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
    ],
)

cc_library(
    name = "icon_action_factory_context",
    srcs = ["icon_action_factory_context.cc"],
    hdrs = ["icon_action_factory_context.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control:realtime_signal_types",
        "//intrinsic_control/intrinsic/icon/control:slot_types",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_realtime_status",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:streaming_io_wrapper",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:string_wrapper",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "icon_action_factory_context_test",
    srcs = ["icon_action_factory_context_test.cc"],
    deps = [
        ":icon_action_factory_context",
        "//intrinsic_control/intrinsic/icon/actions:action_utils",
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_control/intrinsic/icon/control:action_factory_context",
        "//intrinsic_control/intrinsic/icon/control:realtime_signal_types",
        "//intrinsic_control/intrinsic/icon/control:slot_types",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_storage",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:action_factory_context_wrapper",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:streaming_io_wrapper",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "icon_streaming_io_access",
    hdrs = ["icon_streaming_io_access.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control:streaming_io_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_realtime_status",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:streaming_io_wrapper",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
    ],
)

cc_library(
    name = "icon_plugin_register_macro",
    hdrs = ["icon_plugin_register_macro.h"],
    deps = [
        ":make_icon_action_vtable",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
    ],
)

cc_library(
    name = "make_icon_action_vtable",
    hdrs = ["make_icon_action_vtable.h"],
    deps = [
        ":icon_action_factory_context",
        ":icon_action_interface",
        ":icon_realtime_signal_access",
        ":icon_realtime_slot_map",
        ":icon_streaming_io_access",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_realtime_status",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:string_wrapper",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "sine_wave_plugin_action",
    srcs = ["sine_wave_plugin_action.cc"],
    hdrs = ["sine_wave_plugin_action.h"],
    deps = [
        ":icon_action_factory_context",
        ":icon_action_interface",
        ":icon_feature_interfaces",
        ":icon_realtime_signal_access",
        ":icon_realtime_slot_map",
        ":icon_streaming_io_access",
        "//intrinsic_control/intrinsic/icon/actions:action_utils",
        "//intrinsic_control/intrinsic/icon/cc_client:condition",
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_control/intrinsic/icon/control:slot_types",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_types",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/control/c_api/external_action_api:sine_wave_action_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
    alwayslink = True,
)

cc_test(
    name = "sine_wave_plugin_action_test",
    srcs = ["sine_wave_plugin_action_test.cc"],
    deps = [
        ":sine_wave_plugin_action",
        "//intrinsic_control/intrinsic/icon/cc_client:condition",
        "//intrinsic_control/intrinsic/icon/control/c_api/external_action_api/testing:action_test_helper",
        "//intrinsic_control/intrinsic/icon/control/c_api/external_action_api/testing:loopback_fake_arm",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_binary(
    name = "sine_wave_plugin.so",
    srcs = ["sine_wave_plugin.cc"],
    linkshared = 1,
    linkstatic = 1,
    deps = [
        ":icon_plugin_register_macro",
        ":sine_wave_plugin_action",
    ],
)

cc_test(
    name = "load_sine_wave_plugin_test",
    srcs = ["load_sine_wave_plugin_test.cc"],
    data = [
        ":sine_wave_plugin.so",
    ],
    tags = [
        "noasan",
        "nomsan",
        "noubsan",
    ],
    deps = [
        ":sine_wave_plugin_action",
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_control/intrinsic/icon/control:action_factory_context",
        "//intrinsic_control/intrinsic/icon/control:fake_realtime_slot_map_impl",
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_control/intrinsic/icon/control:plugin_action",
        "//intrinsic_control/intrinsic/icon/control:realtime_signal_access",
        "//intrinsic_control/intrinsic/icon/control:realtime_signal_storage",
        "//intrinsic_control/intrinsic/icon/control:realtime_slot_map",
        "//intrinsic_control/intrinsic/icon/control:rtcl_action",
        "//intrinsic_control/intrinsic/icon/control:rtcl_action_factory_registry",
        "//intrinsic_control/intrinsic/icon/control:slot_types",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_helpers",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_realtime",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_storage",
        "//intrinsic_control/intrinsic/icon/control/actions:test_helpers",
        "//intrinsic_control/intrinsic/icon/control/parts:fake_feature_interfaces",
        "//intrinsic_control/intrinsic/icon/control/parts:realtime_part_interface",
        "//intrinsic_control/intrinsic/icon/control/parts/testing:position_loopback_fake_part",
        "//intrinsic_control/intrinsic/icon/server:custom_action_plugin_loader",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/path_resolver",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/control/c_api/external_action_api:sine_wave_action_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test_and_malloc_test(
    name = "icon_streaming_io_access_test",
    srcs = ["icon_streaming_io_access_test.cc"],
    deps = [
        ":icon_streaming_io_access",
        "//intrinsic_control/intrinsic/icon/actions:action_utils",
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_helpers",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_realtime",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_registry",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_storage",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:action_factory_context_wrapper",
        "//intrinsic_control/intrinsic/icon/control/c_api/wrappers:streaming_io_realtime_access_wrapper",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
    ],
)
