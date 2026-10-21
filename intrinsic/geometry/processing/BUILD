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

""" Intrinsic package geometry/api/processing. """

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = ["//intrinsic:intrinsic_internal"])

cc_library(
    name = "pipeline",
    srcs = ["pipeline.cc"],
    hdrs = ["pipeline.h"],
    deps = [
        ":processor",
        "//intrinsic/longrunning/cc:operation_context",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_fingerprint",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "pipeline_builder",
    srcs = ["pipeline_builder.cc"],
    hdrs = ["pipeline_builder.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        ":pipeline",
        ":pipeline_config_cc_proto",
        ":processor",
        "//intrinsic/geometry/processing/processors:alpha_wrap_processor",
        "//intrinsic/geometry/processing/processors:coacd_processor",
        "//intrinsic/geometry/processing/processors:convex_hull_processor",
        "//intrinsic/geometry/processing/processors:isotropic_remeshing_processor",
        "//intrinsic/geometry/processing/processors:mesh_simplification_processor",
        "//intrinsic/geometry/processing/processors:noop_processor",
        "//intrinsic/geometry/processing/processors:scale_processor",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

proto_library(
    name = "pipeline_config_proto",
    srcs = ["pipeline_config.proto"],
    deps = ["@com_google_protobuf//:duration_proto"],
)

cc_proto_library(
    name = "pipeline_config_cc_proto",
    deps = [":pipeline_config_proto"],
)

go_proto_library(
    name = "pipeline_config_go_proto",
    importpath = "intrinsic/geometry/processing/pipeline_config_go_proto",
    protos = [":pipeline_config_proto"],
)

cc_library(
    name = "processor",
    hdrs = ["processor.h"],
    deps = [
        "//intrinsic/longrunning/cc:operation_context",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "@abseil-cpp//absl/status:statusor",
    ],
)
