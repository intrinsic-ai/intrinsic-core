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
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "client_utils",
    srcs = ["client_utils.cc"],
    hdrs = ["client_utils.h"],
    deps = [
        ":client",
        ":condition",
        ":robot_config",
        "//intrinsic_control/intrinsic/icon/common:builtins",
        "//intrinsic_control/intrinsic/icon/common:part_properties",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "state_variable_path",
    srcs = ["state_variable_path.cc"],
    hdrs = [
        "state_variable_path.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/common:state_variable_path_constants",
        "//intrinsic_control/intrinsic/icon/common:state_variable_path_util",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "state_variable_path_test",
    size = "small",
    srcs = ["state_variable_path_test.cc"],
    deps = [
        ":state_variable_path",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "client_utils_test",
    size = "small",
    srcs = ["client_utils_test.cc"],
    deps = [
        ":client",
        ":client_utils",
        ":condition",
        "//intrinsic_control/intrinsic/icon/common:builtins",
        "//intrinsic_control/intrinsic/icon/common:part_properties",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:condition_types_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "condition",
    srcs = ["condition.cc"],
    hdrs = ["condition.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:condition_types_cc_proto",
    ],
)

cc_test(
    name = "condition_test",
    size = "small",
    srcs = ["condition_test.cc"],
    deps = [
        ":condition",
        "//intrinsic/util:full_precision",
        "@abseil-cpp//absl/hash:hash_testing",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:condition_types_cc_proto",
    ],
)

cc_library(
    name = "session",
    srcs = ["session.cc"],
    hdrs = ["session.h"],
    deps = [
        ":condition",
        ":stream",
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_control/intrinsic/icon/common:slot_part_map",
        "//intrinsic_control/intrinsic/icon/proto:concatenate_trajectory_protos",
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "//intrinsic_sdk/intrinsic/util:atomic_sequence_num",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_rpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:streaming_output_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@or_tools//ortools/base:strong_int",
    ],
)

cc_test(
    name = "session_test",
    size = "small",
    srcs = ["session_test.cc"],
    deps = [
        ":client",
        ":client_utils",
        ":condition",
        ":session",
        "//intrinsic_control/intrinsic/icon/actions:point_to_point_move_info",
        "//intrinsic_control/intrinsic/icon/cc_client/testing:channel_fake",
        "//intrinsic_control/intrinsic/icon/cc_client/testing:fake_action_cc_proto",
        "//intrinsic_control/intrinsic/icon/cc_client/testing:rtcl_controller_channel_fake",
        "//intrinsic_control/intrinsic/icon/common:builtins",
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_control/intrinsic/icon/proto:concatenate_trajectory_protos",
        "//intrinsic_control/intrinsic/icon/server:fake_robot_connection",
        "//intrinsic_control/intrinsic/icon/server/testing:fake_operational_state",
        "//intrinsic_control/intrinsic/icon/server/testing:in_process_server",
        "//intrinsic_control/intrinsic/icon/server/testing:test_with_status_spec",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "//intrinsic_sdk/intrinsic/util/status:get_extended_status",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_rpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:differencer",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:streaming_output_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
    ],
)

cc_library(
    name = "client",
    srcs = ["client.cc"],
    hdrs = ["client.h"],
    deps = [
        ":operational_status",
        ":robot_config",
        "//intrinsic_control/intrinsic/icon/common:part_properties",
        "//intrinsic_control/intrinsic/icon/common:slot_part_map",
        "//intrinsic_control/intrinsic/icon/control:logging_mode",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "client_test",
    size = "small",
    srcs = ["client_test.cc"],
    deps = [
        ":client",
        ":operational_status",
        "//intrinsic_control/intrinsic/icon/cc_client/testing:channel_fake",
        "//intrinsic_control/intrinsic/icon/cc_client/testing:rtcl_controller_channel_fake",
        "//intrinsic_control/intrinsic/icon/common:part_properties",
        "//intrinsic_control/intrinsic/icon/control:logging_mode",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "operational_status",
    srcs = ["operational_status.cc"],
    hdrs = ["operational_status.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "operational_status_test",
    srcs = ["operational_status_test.cc"],
    deps = [
        ":operational_status",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "robot_config",
    srcs = ["robot_config.cc"],
    hdrs = ["robot_config.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "robot_config_test",
    size = "small",
    srcs = ["robot_config_test.cc"],
    deps = [
        ":robot_config",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "stream",
    srcs = ["stream.cc"],
    hdrs = ["stream.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_rpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
    ],
)

cc_test(
    name = "stream_test",
    srcs = ["stream_test.cc"],
    deps = [
        ":client",
        ":stream",
        "//intrinsic_control/intrinsic/icon/cc_client/testing:fake_action_cc_proto",
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_control/intrinsic/icon/server:fake_robot_connection",
        "//intrinsic_control/intrinsic/icon/server:session_interface",
        "//intrinsic_control/intrinsic/icon/server/example:noop_action",
        "//intrinsic_control/intrinsic/icon/server/testing:in_process_server",
        "//intrinsic_control/intrinsic/icon/server/testing:session_mock",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_rpc",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)
