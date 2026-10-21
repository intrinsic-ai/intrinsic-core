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

""" Intrinsic package geometry/internal/util. """

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//intrinsic:intrinsic_internal"])

cc_library(
    name = "eigen_serialization",
    srcs = [
        "eigen_serialization.cc",
    ],
    hdrs = [
        "eigen_serialization.h",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:pose2",
        "//intrinsic_sdk/intrinsic/marshal:riegeli_coder",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_riegeli//riegeli/records:record_reader",
        "@com_google_riegeli//riegeli/records:record_writer",
    ],
)

cc_library(
    name = "scale_shape",
    srcs = ["scale_shape.cc"],
    hdrs = ["scale_shape.h"],
    visibility = [
        "//intrinsic/geometry:__subpackages__",
        "//intrinsic/geometry/internal/util:__pkg__",
        "//intrinsic/scene/sdf:__pkg__",
        "//intrinsic/simulation/world:__pkg__",
        "//intrinsic/world:__subpackages__",
        "//intrinsic_sdk/intrinsic/geometry:__subpackages__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "//intrinsic_sdk/intrinsic/geometry/shapes:geometry_shape",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "export_as_gltf",
    srcs = ["export_as_gltf.cc"],
    hdrs = ["export_as_gltf.h"],
    visibility = [
        "//intrinsic/geometry:__subpackages__",
        "//intrinsic/scene:__subpackages__",
        "//intrinsic_sdk/intrinsic/geometry:__subpackages__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:material",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh/io:mesh_to_ai_scene",
        "//intrinsic_sdk/intrinsic/geometry/internal/point_cloud:pts_to_ai_scene",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@assimp",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@or_tools//ortools/base:path",
    ],
)

cc_library(
    name = "gltf_merge",
    srcs = ["gltf_merge.cc"],
    hdrs = ["gltf_merge.h"],
    deps = [
        "@abseil-cpp//absl/strings",
        "@tinygltf",
    ],
)

cc_library(
    name = "gltf_helpers",
    srcs = ["gltf_helpers.cc"],
    hdrs = ["gltf_helpers.h"],
    visibility = [
        "//intrinsic/geometry:__subpackages__",
        "//intrinsic/scene:__subpackages__",
        "//intrinsic_sdk/intrinsic/geometry:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@tinygltf",
    ],
)
