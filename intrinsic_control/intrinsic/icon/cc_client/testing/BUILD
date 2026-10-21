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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "channel_fake",
    testonly = True,
    srcs = ["channel_fake.cc"],
    hdrs = ["channel_fake.h"],
    visibility = [
        "//intrinsic_control/intrinsic/icon:icon_client_users",
    ],
    deps = [
        ":rtcl_readonly_parts",
        "//intrinsic_control/intrinsic/icon/cc_client:condition",
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_control/intrinsic/icon/control/actions:test_helpers",
        "//intrinsic_control/intrinsic/icon/control/parts:fake_feature_interfaces",
        "//intrinsic_control/intrinsic/icon/control/parts:io_block",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:joint_limits_constants",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "rtcl_readonly_parts",
    testonly = True,
    srcs = ["rtcl_readonly_parts.cc"],
    hdrs = ["rtcl_readonly_parts.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control/parts:fake_feature_interfaces",
        "//intrinsic_control/intrinsic/icon/control/parts:realtime_part_interface",
        "//intrinsic_control/intrinsic/icon/control/parts/testing:fake_realtime_part",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_sdk/intrinsic/math:pose3",
    ],
)

cc_library(
    name = "rtcl_controller_fake_session",
    testonly = True,
    srcs = ["rtcl_controller_fake_session.cc"],
    hdrs = ["rtcl_controller_fake_session.h"],
    deps = [
        ":channel_fake",
        "//intrinsic_control/intrinsic/icon/cc_client:condition",
        "//intrinsic_control/intrinsic/icon/cc_client:operational_status",
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_control/intrinsic/icon/common:part_group",
        "//intrinsic_control/intrinsic/icon/common:slot_part_map",
        "//intrinsic_control/intrinsic/icon/control:action_factory_context",
        "//intrinsic_control/intrinsic/icon/control:mock_rtcl_action",
        "//intrinsic_control/intrinsic/icon/control:realtime_actions_reactions",
        "//intrinsic_control/intrinsic/icon/control:realtime_bridge_types",
        "//intrinsic_control/intrinsic/icon/control:realtime_condition",
        "//intrinsic_control/intrinsic/icon/control:realtime_signal_storage",
        "//intrinsic_control/intrinsic/icon/control:rtcl_action",
        "//intrinsic_control/intrinsic/icon/control:rtcl_action_factory_registry",
        "//intrinsic_control/intrinsic/icon/control:slot_types",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_helpers",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_storage",
        "//intrinsic_control/intrinsic/icon/control/parts:realtime_part_status",
        "//intrinsic_control/intrinsic/icon/proto:joint_trajectory_conversion",
        "//intrinsic_control/intrinsic/icon/proto:safety_status_conversion",
        "//intrinsic_control/intrinsic/icon/server:session_interface",
        "//intrinsic_control/intrinsic/icon/server:signature_utils",
        "//intrinsic_control/intrinsic/icon/server/testing:fake_operational_state",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_google_googletest//:gtest",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "rtcl_controller_fake_robot_connection",
    testonly = True,
    srcs = ["rtcl_controller_fake_robot_connection.cc"],
    hdrs = ["rtcl_controller_fake_robot_connection.h"],
    deps = [
        ":channel_fake",
        ":rtcl_controller_fake_session",
        "//intrinsic_control/intrinsic/icon/cc_client:operational_status",
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_control/intrinsic/icon/common:part_properties",
        "//intrinsic_control/intrinsic/icon/common:slot_part_map",
        "//intrinsic_control/intrinsic/icon/control:logging_mode",
        "//intrinsic_control/intrinsic/icon/control:rtcl_action_factory_registry",
        "//intrinsic_control/intrinsic/icon/control/parts:realtime_log_context",
        "//intrinsic_control/intrinsic/icon/server:operational_state_interface",
        "//intrinsic_control/intrinsic/icon/server:part_collection",
        "//intrinsic_control/intrinsic/icon/server:robot_connection_interface",
        "//intrinsic_control/intrinsic/icon/server:session_interface",
        "//intrinsic_control/intrinsic/icon/server:signature_utils",
        "//intrinsic_control/intrinsic/icon/server/testing:fake_operational_state",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/icon/proto:safety_status_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "rtcl_controller_channel_fake",
    testonly = True,
    srcs = ["rtcl_controller_channel_fake.cc"],
    hdrs = ["rtcl_controller_channel_fake.h"],
    visibility = [
        "//intrinsic_control/intrinsic/icon:icon_client_users",
    ],
    deps = [
        ":channel_fake",
        ":rtcl_controller_fake_robot_connection",
        ":rtcl_controller_fake_session",
        "//intrinsic_control/intrinsic/icon/cc_client:operational_status",
        "//intrinsic_control/intrinsic/icon/common:part_properties",
        "//intrinsic_control/intrinsic/icon/control/actions:rtcl_controller_default_actions",
        "//intrinsic_control/intrinsic/icon/server/testing:in_process_server",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)
