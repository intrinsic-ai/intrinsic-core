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

load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:protos.bzl", "proto_library")
load("//bazel:python.bzl", "py_library")

package(
    default_visibility = [
        "//visibility:public",
    ],
)

cc_library(
    name = "eigen_conversion",
    srcs = ["eigen_conversion.cc"],
    hdrs = ["eigen_conversion.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:matrix_cc_proto",
    ],
)

cc_library(
    name = "types_proto_conversions",
    srcs = ["types_proto_conversions.cc"],
    hdrs = ["types_proto_conversions.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_control/intrinsic/icon/common:slot_part_map",
        "//intrinsic_control/intrinsic/icon/server:session_interface",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "cart_space_conversion",
    srcs = ["cart_space_conversion.cc"],
    hdrs = ["cart_space_conversion.h"],
    deps = [
        ":eigen_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_cc_proto",
    ],
)

cc_library(
    name = "joint_state_conversion",
    srcs = ["joint_state_conversion.cc"],
    hdrs = ["joint_state_conversion.h"],
    deps = [
        ":eigen_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf_lite",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
    ],
)

cc_library(
    name = "joint_trajectory_conversion",
    srcs = ["joint_trajectory_conversion.cc"],
    hdrs = ["joint_trajectory_conversion.h"],
    deps = [
        ":joint_state_conversion",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics/types:dynamic_limits_check_mode",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
    ],
)

cc_library(
    name = "concatenate_trajectory_protos",
    srcs = ["concatenate_trajectory_protos.cc"],
    hdrs = ["concatenate_trajectory_protos.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
    ],
)

cc_library(
    name = "safety_status_conversion",
    srcs = ["safety_status_conversion.cc"],
    hdrs = ["safety_status_conversion.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control:realtime_bridge_types",
        "//intrinsic_control/intrinsic/icon/control/safety:safety_messages_fbs_cc",
        "//intrinsic_control/intrinsic/icon/control/safety/extern:safety_status_fbs_cc",
        "@intrinsic_apis//intrinsic/icon/proto:safety_status_cc_proto",
    ],
)

py_library(
    name = "matrix_conversions",
    srcs = ["matrix_conversions.py"],
    deps = [
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@intrinsic_apis//intrinsic/icon/proto:matrix_py_pb2",
    ],
)

proto_library(
    name = "rt_tracing_service_proto",
    srcs = ["rt_tracing_service.proto"],
)

cc_proto_library(
    name = "rt_tracing_service_cc_proto",
    deps = [":rt_tracing_service_proto"],
)

cc_grpc_library(
    name = "rt_tracing_service_cc_grpc_proto",
    srcs = [":rt_tracing_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":rt_tracing_service_cc_proto"],
)

proto_library(
    name = "joint_collection_prefilter_config_proto",
    srcs = [
        "joint_collection_prefilter_config.proto",
    ],
    deps = ["//intrinsic/math/proto:iir_filter_config_proto"],
)

cc_proto_library(
    name = "joint_collection_prefilter_config_cc_proto",
    deps = [":joint_collection_prefilter_config_proto"],
)
