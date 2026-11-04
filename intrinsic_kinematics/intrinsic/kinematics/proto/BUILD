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
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "frame_cartesian_limits_proto",
    srcs = ["frame_cartesian_limits.proto"],
    # keep
    strip_import_prefix = "/intrinsic_kinematics",
    deps = [
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
    ],
)

cc_proto_library(
    name = "frame_cartesian_limits_cc_proto",
    deps = [":frame_cartesian_limits_proto"],
)

go_proto_library(
    name = "frame_cartesian_limits_go_proto",
    importpath = "intrinsic/kinematics/proto/frame_cartesian_limits_go_proto",
    proto = ":frame_cartesian_limits_proto",
    deps = [
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
    ],
)

cc_library(
    name = "frame_cartesian_limits_conversion",
    srcs = ["frame_cartesian_limits_conversion.cc"],
    hdrs = ["frame_cartesian_limits_conversion.h"],
    deps = [
        ":frame_cartesian_limits_cc_proto",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "kinematics_conversion",
    srcs = [
        "kinematics_conversion.cc",
    ],
    hdrs = [
        "kinematics_conversion.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/kinematics/proto:kinematics_cc_proto",
        "@intrinsic_apis//intrinsic/kinematics/proto:skeleton_cc_proto",
    ],
)
