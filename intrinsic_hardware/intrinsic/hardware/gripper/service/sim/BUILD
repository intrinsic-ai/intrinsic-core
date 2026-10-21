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
    default_visibility = [
        "//visibility:public",
    ],
)

cc_library(
    name = "sim_pinch_gripper_impl",
    srcs = ["sim_pinch_gripper_impl.cc"],
    hdrs = ["sim_pinch_gripper_impl.h"],
    deps = [
        "//intrinsic/resources/health:health_state_machine",
        "//intrinsic/resources/health:operational_status",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:pinch_gripper_server_cc_grpc_proto",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:pinch_gripper_server_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/synchronization",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_proto",
    ],
)

cc_library(
    name = "pinch_gripper_service",
    srcs = ["pinch_gripper_service.cc"],
    hdrs = ["pinch_gripper_service.h"],
    deps = [
        ":sim_pinch_gripper_impl",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:pinch_gripper_server_cc_grpc_proto",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:pinch_gripper_server_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

cc_library(
    name = "health_service",
    srcs = ["health_service.cc"],
    hdrs = ["health_service.h"],
    deps = [
        ":sim_pinch_gripper_impl",
        "//intrinsic/resources/proto:resource_health_cc_grpc_proto",
        "//intrinsic/resources/proto:resource_health_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "@abseil-cpp//absl/strings",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_proto",
    ],
)
