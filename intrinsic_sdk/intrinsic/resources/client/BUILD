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
load("//bazel:python.bzl", "py_library")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "resource_registry_client",
    srcs = ["resource_registry_client.py"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/grpc:error_handling",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_py_pb2",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_py_pb2_grpc",
    ],
)

cc_library(
    name = "resource_registry_client_interface",
    hdrs = ["resource_registry_client_interface.h"],
    deps = [
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_cc_proto",
    ],
)

cc_library(
    name = "resource_registry_client_cc",
    srcs = ["resource_registry_client.cc"],
    hdrs = ["resource_registry_client.h"],
    deps = [
        ":resource_registry_client_interface",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_cc_grpc",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_cc_proto",
    ],
)
