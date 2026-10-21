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

""" Intrinsic package geometry/legacy/point_cloud. """

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//intrinsic/geometry:__subpackages__"])

exports_files(
    srcs = [
        "test_data/Raw_IPS.ply",
        "test_data/example_point_cloud.ply",
    ],
    visibility = [
        "//intrinsic/geometry:__subpackages__",
        "//intrinsic/world/service/benchmarks:__subpackages__",
        "//incode/experimental/users/sgayda2:__subpackages__",
    ],
)

cc_library(
    name = "ai_scene_to_point_cloud",
    srcs = ["ai_scene_to_point_cloud.cc"],
    hdrs = ["ai_scene_to_point_cloud.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@assimp",
    ],
)

cc_library(
    name = "load_point_cloud_from_buffer",
    srcs = ["load_point_cloud_from_buffer.cc"],
    hdrs = ["load_point_cloud_from_buffer.h"],
    deps = [
        ":ai_scene_to_point_cloud",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh/io:load_ai_scene_from_buffer",
        "//intrinsic_sdk/intrinsic/geometry/internal/point_cloud:point_cloud_riegeli_coder",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "//intrinsic_sdk/intrinsic/util/object_store",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@assimp",
    ],
)
