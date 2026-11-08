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

package(
    default_testonly = True,
    default_visibility = [
        "//visibility:public",
    ],
)

cc_library(
    name = "test_with_status_spec",
    srcs = ["test_with_status_spec.cc"],
    hdrs = ["test_with_status_spec.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/server:extended_status_constants",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_specs",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
        "@intrinsic_apis//intrinsic/assets/proto:status_spec_cc_proto",
    ],
)

cc_library(
    name = "session_mock",
    hdrs = ["session_mock.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_control/intrinsic/icon/common:part_group",
        "//intrinsic_control/intrinsic/icon/server:session_interface",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
    ],
)

cc_library(
    name = "fake_session",
    srcs = ["fake_session.cc"],
    hdrs = ["fake_session.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_control/intrinsic/icon/common:part_group",
        "//intrinsic_control/intrinsic/icon/server:session_interface",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:optional",
        "@abseil-cpp//absl/types:span",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:streaming_output_cc_proto",
    ],
)

cc_library(
    name = "in_process_server",
    srcs = ["in_process_server.cc"],
    hdrs = ["in_process_server.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/server:grpc_envelope",
        "//intrinsic_control/intrinsic/icon/server:icon_api_service",
        "//intrinsic_control/intrinsic/icon/server:robot_connection_interface",
        "//intrinsic_control/intrinsic/icon/server:service",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:jogging_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_grpc_proto",
    ],
)

cc_library(
    name = "fake_part_collection",
    srcs = ["fake_part_collection.cc"],
    hdrs = ["fake_part_collection.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/common:part_properties",
        "//intrinsic_control/intrinsic/icon/control/parts:payload_property",
        "//intrinsic_control/intrinsic/icon/server:part_collection",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@gloop//gloop/util/gtl:iterator_adaptors",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:safety_status_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "fake_operational_state",
    hdrs = ["fake_operational_state.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/cc_client:operational_status",
        "//intrinsic_control/intrinsic/icon/control:realtime_bridge_types",
        "//intrinsic_control/intrinsic/icon/server:operational_state_interface",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)
