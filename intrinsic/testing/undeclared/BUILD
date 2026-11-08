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

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:protos.bzl", "proto_library")

proto_library(
    name = "undeclared_output_annotation_proto",
    srcs = ["undeclared_output_annotation.proto"],
    features = ["-proto_dynamic_mode_static_link"],
)

py_proto_library(
    name = "undeclared_output_annotation_py_pb2",
    deps = [":undeclared_output_annotation_proto"],
)

cc_proto_library(
    name = "undeclared_output_annotation_cc_proto",
    deps = [":undeclared_output_annotation_proto"],
)

cc_library(
    name = "undeclared_outputs",
    testonly = True,
    srcs = ["undeclared_outputs.cc"],
    hdrs = ["undeclared_outputs.h"],
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
    deps = [
        ":undeclared_output_annotation_cc_proto",
        "//intrinsic/util:unique_id",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
        "@com_google_riegeli//riegeli/bytes:fd_writer",
        "@com_google_riegeli//riegeli/messages:serialize_message",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:path",
    ],
)
