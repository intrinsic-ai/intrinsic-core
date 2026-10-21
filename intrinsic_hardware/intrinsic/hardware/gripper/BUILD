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

# Gripper hardware protos and headers.

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "gripper_equipment_proto",
    srcs = [
        "gripper_equipment.proto",
    ],
    deps = [
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:pinch_gripper_server_proto",
        "@com_google_protobuf//:duration_proto",
    ],
)

cc_proto_library(
    name = "gripper_equipment_cc_proto",
    deps = [":gripper_equipment_proto"],
)

go_proto_library(
    name = "gripper_equipment_go_proto",
    importpath = "intrinsic/hardware/gripper/gripper_equipment_go_proto",
    protos = [":gripper_equipment_proto"],
    deps = ["//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:pinch_gripper_server_go_proto"],
)

cc_library(
    name = "gripper_interface",
    srcs = ["gripper.cc"],
    hdrs = ["gripper.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:optional",
        "@com_google_protobuf//:protobuf",
        "@cppregpattern//:registry",
        "@intrinsic_apis//intrinsic/hardware/gripper:gripper_cc_proto",
    ],
)
