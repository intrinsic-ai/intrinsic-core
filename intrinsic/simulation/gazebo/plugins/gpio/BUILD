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

# Simulated GPIO gRPC service

load("//bazel:cc_macros.bzl", "cc_library")

package(
    default_visibility = [
        "//intrinsic/simulation:__subpackages__",
    ],
)

cc_library(
    name = "gpio_service",
    srcs = ["gpio_service.cc"],
    hdrs = ["gpio_service.h"],
    deps = [
        ":gpio_connection_interface",
        "//intrinsic_hardware/intrinsic/hardware/gpio:gpio_service_proto_utils",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_rpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
        "@gloop//gloop/util/gtl:iterator_adaptors",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:signal_cc_proto",
    ],
)

cc_library(
    name = "gpio_connection_interface",
    hdrs = ["gpio_connection_interface.h"],
    deps = [
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
    ],
)
