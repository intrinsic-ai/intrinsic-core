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
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "sim_inputs_service",
    srcs = ["sim_inputs_service.cc"],
    hdrs = ["sim_inputs_service.h"],
    deps = [
        "//intrinsic/simulation/gazebo/components:digital_io_components",
        "//intrinsic/simulation/gazebo/components:ppr_component",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@gz-sim",
        "@intrinsic_apis//intrinsic/simulation/gazebo/plugins/sim_inputs/v1:set_simulated_inputs_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/simulation/gazebo/plugins/sim_inputs/v1:set_simulated_inputs_cc_proto",
    ],
)

cc_library(
    name = "sim_inputs_service_singleton",
    srcs = ["sim_inputs_service_singleton.cc"],
    hdrs = ["sim_inputs_service_singleton.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
        "@intrinsic_apis//intrinsic/simulation/gazebo/plugins/sim_inputs/v1:set_simulated_inputs_cc_grpc_proto",
    ],
)

cc_library(
    name = "sim_inputs_system",
    srcs = ["sim_inputs_system.cc"],
    hdrs = ["sim_inputs_system.h"],
    deps = [
        ":sim_inputs_service",
        ":sim_inputs_service_singleton",
        "@abseil-cpp//absl/log",
        "@gz-sim",
        "@sdformat",
    ],
)

cc_library(
    name = "sim_inputs_system_register",
    srcs = ["sim_inputs_system_register.cc"],
    deps = [
        ":sim_inputs_system",
        "@gz-plugin//:register",
        "@gz-sim",
    ],
    alwayslink = True,
)
