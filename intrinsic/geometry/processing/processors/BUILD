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

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//intrinsic:intrinsic_internal"])

cc_library(
    name = "noop_processor",
    srcs = ["noop_processor.cc"],
    hdrs = ["noop_processor.h"],
    deps = [
        "//intrinsic/geometry/processing:pipeline_config_cc_proto",
        "//intrinsic/geometry/processing:processor",
        "//intrinsic/longrunning/cc:operation_context",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "convex_hull_processor",
    srcs = ["convex_hull_processor.cc"],
    hdrs = ["convex_hull_processor.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        "//intrinsic/geometry/api:compute_convex_hull",
        "//intrinsic/geometry/processing:pipeline_config_cc_proto",
        "//intrinsic/geometry/processing:processor",
        "//intrinsic/longrunning/cc:operation_context",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "alpha_wrap_processor",
    srcs = ["alpha_wrap_processor.cc"],
    hdrs = ["alpha_wrap_processor.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        "//intrinsic/geometry/api:construct_alpha_hull",
        "//intrinsic/geometry/processing:pipeline_config_cc_proto",
        "//intrinsic/geometry/processing:processor",
        "//intrinsic/longrunning/cc:operation_context",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "mesh_simplification_processor",
    srcs = ["mesh_simplification_processor.cc"],
    hdrs = ["mesh_simplification_processor.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        "//intrinsic/geometry/api:construct_simplified_mesh",
        "//intrinsic/geometry/processing:pipeline_config_cc_proto",
        "//intrinsic/geometry/processing:processor",
        "//intrinsic/longrunning/cc:operation_context",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "isotropic_remeshing_processor",
    srcs = ["isotropic_remeshing_processor.cc"],
    hdrs = ["isotropic_remeshing_processor.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        "//intrinsic/geometry/api:construct_isotropic_remeshing",
        "//intrinsic/geometry/processing:pipeline_config_cc_proto",
        "//intrinsic/geometry/processing:processor",
        "//intrinsic/longrunning/cc:operation_context",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "scale_processor",
    srcs = ["scale_processor.cc"],
    hdrs = ["scale_processor.h"],
    deps = [
        "//intrinsic/geometry/processing:pipeline_config_cc_proto",
        "//intrinsic/geometry/processing:processor",
        "//intrinsic/longrunning/cc:operation_context",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:apply_transform",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "coacd_processor",
    srcs = ["coacd_processor.cc"],
    hdrs = ["coacd_processor.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        "//intrinsic/geometry/api:compute_coacd",
        "//intrinsic/geometry/processing:pipeline_config_cc_proto",
        "//intrinsic/geometry/processing:processor",
        "//intrinsic/longrunning/cc:operation_context",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)
