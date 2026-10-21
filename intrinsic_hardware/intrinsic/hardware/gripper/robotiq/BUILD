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

# Gripper driver for the robotiq gripper

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "hardware_registers",
    srcs = ["hardware_registers.cc"],
    hdrs = ["hardware_registers.h"],
    visibility = ["//visibility:private"],
    deps = [
        "@abseil-cpp//absl/log",
    ],
)

cc_library(
    name = "robotiq_gripper",
    srcs = ["robotiq_gripper.cc"],
    hdrs = ["robotiq_gripper.h"],
    deps = [
        ":hardware_registers",
        "//intrinsic/util:cyclic_runner",
        "//intrinsic_hardware/intrinsic/hardware/gripper:gripper_interface",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service:generic_pinch_gripper_utils",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:generic_pinch_gripper_configs_cc_proto",
        "//intrinsic_hardware/intrinsic/hardware/modbus_tcp",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/platform/pubsub:subscription",
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
