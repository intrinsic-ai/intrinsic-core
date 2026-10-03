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

load("//bazel:cc_macros.bzl", "cc_library", "cc_test")
load("//bazel:python.bzl", "py_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "source_location",
    hdrs = ["source_location.h"],
    deps = ["@abseil-cpp//absl/base:core_headers"],
)

cc_library(
    name = "file_helpers",
    hdrs = ["file_helpers.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//src/google/protobuf/io",
    ],
)

py_library(
    name = "file_helpers_py",
    srcs = ["file_helpers.py"],
    deps = [
        "@com_google_protobuf//:protobuf_python",
    ],
)

cc_library(
    name = "grpc_time_support",
    hdrs = ["grpc_time_support.h"],
    deps = ["@com_github_grpc_grpc//:grpc++"],
)

cc_test(
    name = "grpc_time_support_test",
    size = "small",
    srcs = ["grpc_time_support_test.cc"],
    deps = [
        ":grpc_time_support",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:gpr",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googletest//:gtest_main",
    ],
)
