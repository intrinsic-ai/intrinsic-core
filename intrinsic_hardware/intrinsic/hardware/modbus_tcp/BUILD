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

# Modbus TCP C++ wrapper

load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:container.bzl", "container_layer")

package(default_visibility = ["//intrinsic_hardware/intrinsic/hardware:__subpackages__"])

container_layer(
    name = "modbus-runfiles",
    data_path = "/",
    files = [
        "@libmodbus//:modbus_shared",
    ],
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
)

cc_library(
    name = "modbus_tcp",
    srcs = [
        "modbus_tcp.cc",
        "@libmodbus//:modbus_shared",
    ],
    hdrs = ["modbus_tcp.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
        "@libmodbus//:modbus_headers",
    ],
)
