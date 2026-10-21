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
    name = "pose3",
    hdrs = [
        "pose3.h",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:so3",
        "@abseil-cpp//absl/base:core_headers",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "almost_equals",
    hdrs = ["almost_equals.h"],
)

cc_library(
    name = "ipow",
    hdrs = ["ipow.h"],
    deps = [
        "@abseil-cpp//absl/numeric:bits",
    ],
)

cc_library(
    name = "proto_conversion",
    srcs = ["proto_conversion.cc"],
    hdrs = ["proto_conversion.h"],
    deps = [
        ":almost_equals",
        ":pose3",
        ":twist",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@intrinsic_apis//intrinsic/math/proto:accel_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:affine_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:matrix_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:point_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:twist_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_cc_proto",
    ],
)

cc_library(
    name = "gaussian_noise",
    srcs = ["gaussian_noise.cc"],
    hdrs = ["gaussian_noise.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/random:bit_gen_ref",
        "@abseil-cpp//absl/random:distributions",
    ],
)

cc_library(
    name = "twist",
    srcs = ["twist.cc"],
    hdrs = ["twist.h"],
    deps = ["//intrinsic_sdk/intrinsic/eigenmath"],
)

cc_library(
    name = "transform_utils",
    srcs = ["transform_utils.cc"],
    hdrs = ["transform_utils.h"],
    deps = [
        ":pose3",
        ":twist",
    ],
)

cc_library(
    name = "inertia_utils",
    srcs = ["inertia_utils.cc"],
    hdrs = ["inertia_utils.h"],
    deps = [
        ":almost_equals",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:to_fixed_string",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/strings:string_view",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@intrinsic_apis//intrinsic/world/proto:robot_payload_cc_proto",
    ],
)

cc_library(
    name = "units",
    hdrs = ["units.h"],
)
