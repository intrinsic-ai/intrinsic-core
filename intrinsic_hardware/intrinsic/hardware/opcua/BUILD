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
    default_visibility = ["//visibility:private"],
)

exports_files(
    srcs = [
        "controller_cert.der",
        "server_cert.der",
        "server_key.der",
    ],
    visibility = ["//intrinsic:intrinsic_internal"],
)

cc_library(
    name = "opcua_server",
    # Currently, this server exists mainly to test opcua client implementations.
    testonly = 1,
    srcs = ["opcua_server.cc"],
    hdrs = ["opcua_server.h"],
    visibility = [
        "//intrinsic_control/intrinsic/icon:__subpackages__",
        "//intrinsic_hardware/intrinsic/hardware/gpio:__subpackages__",
    ],
    deps = [
        ":opcua_raii_types",
        "//intrinsic_sdk/intrinsic/util/thread",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@open62541",
        "@open62541//:types",
    ],
)

cc_library(
    name = "opcua_raii_types",
    srcs = [
        "opcua_read_request.cc",
        "opcua_write_request.cc",
    ],
    hdrs = [
        "opcua_arrays.h",
        "opcua_raii_types.h",
        "opcua_read_request.h",
        "opcua_read_response.h",
        "opcua_type_traits.h",
        "opcua_variant.h",
        "opcua_write_request.h",
        "opcua_write_response.h",
    ],
    visibility = [
        "//intrinsic_control/intrinsic/icon:__subpackages__",
        "//intrinsic_hardware/intrinsic/hardware/gpio:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@open62541",
        "@open62541//:types",
    ],
)

cc_library(
    name = "opcua_client",
    srcs = ["opcua_client.cc"],
    hdrs = ["opcua_client.h"],
    visibility = [
        "//intrinsic_control/intrinsic/icon:__subpackages__",
        "//intrinsic_hardware/intrinsic/hardware/gpio:__subpackages__",
    ],
    deps = [
        ":opcua_raii_types",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@open62541",
        "@open62541//:types",
    ],
)
