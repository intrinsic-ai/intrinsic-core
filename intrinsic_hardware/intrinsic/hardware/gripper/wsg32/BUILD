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

package(default_visibility = [
    "//visibility:private",
])

cc_library(
    name = "async_buffer",
    hdrs = ["async_buffer.h"],
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
)

################################################################################
# Private values that you typically should not need to access.
################################################################################

exports_files(
    srcs = [
        "hwm_example_config.pbtxt",
    ],
    visibility = ["//intrinsic_hardware/intrinsic/hardware/gripper/wsg32:__subpackages__"],
)

cc_library(
    name = "wsg32_client",
    srcs = [
        "wsg32_client.cc",
    ],
    hdrs = [
        "wsg32_client.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@wsg32//wsg_32_driver:libwsg32",
    ],
)

cc_library(
    name = "wsg32_gripper",
    srcs = [
        "wsg32_gripper.cc",
    ],
    hdrs = [
        "wsg32_gripper.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":wsg32_client",
        "//intrinsic/util:cyclic_runner",
        "//intrinsic_hardware/intrinsic/hardware/gripper:gripper_interface",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service:generic_pinch_gripper_utils",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:generic_pinch_gripper_configs_cc_proto",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:differencer",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/hardware/gripper:gripper_cc_proto",
    ],
)

exports_files(
    srcs = ["wsg32_pinch_gripper_default.config.textproto"],
    visibility = ["//visibility:public"],
)
