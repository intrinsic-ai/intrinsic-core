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

# Generic action plugin related functionalities

load("//bazel:cc_macros.bzl", "cc_library")

cc_library(
    name = "generic_action",
    srcs = [
        "generic_action_plugin.cc",
    ],
    hdrs = [
        "generic_action_plugin.h",
    ],
    deps = [
        ":generic_action_gpio_connection",
        "//intrinsic/simulation/gazebo/plugins/gpio:gpio_service",
        "//intrinsic/world/proto:generic_action_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@com_google_googletest//:gtest_prod",
        "@com_google_protobuf//:protobuf",
        "@gloop//gloop/util/gtl:iterator_adaptors",
        "@gz-sim",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@sdformat",
    ],
)

cc_library(
    name = "generic_action_gpio_connection",
    srcs = [
        "generic_action_gpio_connection.cc",
    ],
    hdrs = [
        "generic_action_gpio_connection.h",
    ],
    deps = [
        "//intrinsic/simulation/gazebo/plugins/gpio:gpio_connection_interface",
        "//intrinsic/util/proto:pb_hash",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@gloop//gloop/util/gtl:iterator_adaptors",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:signal_cc_proto",
    ],
)

cc_library(
    name = "generic_action_plugin",
    srcs = [
        "generic_action_plugin_register.cc",
    ],
    visibility = [
        "//intrinsic/simulation/gazebo:__subpackages__",
    ],
    deps = [
        ":generic_action",
        "@gz-plugin//:register",
        "@gz-sim",
    ],
    alwayslink = 1,
)
