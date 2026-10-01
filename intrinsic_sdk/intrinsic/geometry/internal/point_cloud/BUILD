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

""" Intrinsic package geometry/internal/legacy/point_cloud. """

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = [
    "//intrinsic/geometry:__subpackages__",
    "//intrinsic_sdk/intrinsic/geometry:__subpackages__",
])

exports_files(
    srcs = [
        "test_data/Raw_IPS.ply",
        "test_data/example_point_cloud.ply",
    ],
    visibility = [
        "//intrinsic/geometry:__subpackages__",
        "//intrinsic/world/service/benchmarks:__subpackages__",
    ],
)

cc_library(
    name = "get_bounding_box_from_point_cloud",
    srcs = ["get_bounding_box_from_point_cloud.cc"],
    hdrs = ["get_bounding_box_from_point_cloud.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/geometry/api:axis_aligned_bounding_box_3d",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
    ],
)

cc_library(
    name = "validate_pts",
    srcs = ["validate_pts.cc"],
    hdrs = ["validate_pts.h"],
    visibility = [
        "//intrinsic/geometry:__subpackages__",
        "//intrinsic_sdk/intrinsic/geometry:__subpackages__",
    ],
    deps = [
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "pts_to_ai_scene",
    srcs = ["pts_to_ai_scene.cc"],
    hdrs = ["pts_to_ai_scene.h"],
    visibility = [
        "//intrinsic/geometry:__subpackages__",
        "//intrinsic_sdk/intrinsic/geometry:__subpackages__",
    ],
    deps = [
        ":validate_pts",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:material",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@assimp",
    ],
)

cc_library(
    name = "pts_to_point_cloud",
    srcs = ["pts_to_point_cloud.cc"],
    hdrs = ["pts_to_point_cloud.h"],
    visibility = [
        "//intrinsic/geometry:__subpackages__",
        "//intrinsic_sdk/intrinsic/geometry:__subpackages__",
    ],
    deps = [
        ":validate_pts",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@assimp",
    ],
)

cc_library(
    name = "point_cloud_riegeli_coder",
    srcs = ["point_cloud_riegeli_coder.cc"],
    hdrs = ["point_cloud_riegeli_coder.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "//intrinsic_sdk/intrinsic/marshal:riegeli_proto_coder",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:point_cloud_cc_proto",
    ],
)
