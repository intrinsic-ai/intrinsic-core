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

# Basic math protos (e.g. matrices, vectors, and a pose)

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "bspline_proto",
    srcs = ["bspline.proto"],
)

cc_proto_library(
    name = "bspline_cc_proto",
    deps = [":bspline_proto"],
)

py_proto_library(
    name = "bspline_py_pb2",
    deps = [":bspline_proto"],
)

go_proto_library(
    name = "bspline_go_proto",
    importpath = "intrinsic/math/proto/bspline_go_proto",
    protos = [":bspline_proto"],
)

proto_library(
    name = "iir_filter_config_proto",
    srcs = [
        "iir_filter_config.proto",
    ],
)

cc_proto_library(
    name = "iir_filter_config_cc_proto",
    deps = [":iir_filter_config_proto"],
)
