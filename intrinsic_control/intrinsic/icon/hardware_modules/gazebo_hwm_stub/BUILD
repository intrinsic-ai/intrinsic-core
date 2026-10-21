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

load("//bazel:cc_macros.bzl", "cc_binary", "cc_library")
load("//intrinsic_control/intrinsic/icon/hal/bzl:hardware_module_image.bzl", "hardware_module_image")

package(default_visibility = [
    "//visibility:public",
])

cc_binary(
    name = "gazebo_hwm_stub_main",
    srcs = ["gazebo_hwm_stub_main.cc"],
    deps = [
        ":health_forwarder",
        "//intrinsic/simulation/gazebo/plugins:aggregated_resource_health_cc_grpc_proto",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_config_cc_proto",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_main_util",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_registry",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_runtime",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_util",
        "//intrinsic_control/intrinsic/icon/hal:module_config",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_control/intrinsic/icon/utils:shutdown_signals",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/util:memory_lock",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

hardware_module_image(
    name = "gazebo_hwm_stub_image",
    hardware_module_binary = "gazebo_hwm_stub_main",
    visibility = [
        "//intrinsic:intrinsic_internal",
        "//intrinsic_control/intrinsic/icon:hardware_module_authors",
    ],
)

cc_library(
    name = "health_forwarder",
    srcs = ["health_forwarder.cc"],
    hdrs = ["health_forwarder.h"],
    deps = [
        "//intrinsic/simulation/gazebo/plugins:aggregated_resource_health_cc_grpc_proto",
        "//intrinsic/simulation/gazebo/plugins:aggregated_resource_health_cc_proto",
        "//intrinsic_control/intrinsic/icon/utils:aggregated_health_to_state",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_proto",
    ],
)
