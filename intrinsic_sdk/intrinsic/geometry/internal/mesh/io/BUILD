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

""" Intrinsic package geometry/internal/legacy/mesh/io. """

load("//bazel:cc_macros.bzl", "cc_library")

cc_library(
    name = "ai_scene_to_mesh",
    srcs = ["ai_scene_to_mesh.cc"],
    hdrs = ["ai_scene_to_mesh.h"],
    visibility = [
        "//intrinsic/geometry:__subpackages__",
        "//intrinsic_sdk/intrinsic/geometry:__subpackages__",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh/io:__pkg__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh:concatenate_meshes",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@assimp",
    ],
)

cc_library(
    name = "load_ai_scene_from_buffer",
    srcs = ["load_ai_scene_from_buffer.cc"],
    hdrs = ["load_ai_scene_from_buffer.h"],
    visibility = [
        "//intrinsic/geometry/api:__pkg__",
        "//intrinsic/geometry/internal/legacy/mesh/io:__pkg__",
        "//intrinsic/geometry/internal/legacy/point_cloud:__pkg__",
        "//intrinsic_sdk/intrinsic/geometry/api:__pkg__",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh:__pkg__",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh/io:__pkg__",
    ],
    deps = [
        ":restrict_importer",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/internal/point_cloud:pts_to_ai_scene",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@assimp",
    ],
)

cc_library(
    name = "restrict_importer",
    srcs = ["restrict_importer.cc"],
    hdrs = ["restrict_importer.h"],
    visibility = [
        "//intrinsic/geometry:__subpackages__",
        "//intrinsic_sdk/intrinsic/geometry:__subpackages__",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh/io:__pkg__",
    ],
    deps = [
        "@abseil-cpp//absl/strings",
        "@assimp",
    ],
)

cc_library(
    name = "load_ai_scene_from_file",
    srcs = ["load_ai_scene_from_file.cc"],
    hdrs = ["load_ai_scene_from_file.h"],
    visibility = [
        "//intrinsic/geometry/api:__pkg__",
        "//intrinsic/geometry/compatibility:__pkg__",
        "//intrinsic/geometry/internal/legacy/mesh/io:__pkg__",
        "//intrinsic/geometry/internal/legacy/point_cloud:__pkg__",
        "//intrinsic_sdk/intrinsic/geometry/api:__pkg__",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh:__pkg__",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh/io:__pkg__",
    ],
    deps = [
        ":restrict_importer",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/internal/point_cloud:pts_to_ai_scene",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@assimp",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:path",
    ],
)

cc_library(
    name = "load_mesh_from_file",
    srcs = ["load_mesh_from_file.cc"],
    hdrs = ["load_mesh_from_file.h"],
    visibility = [
        "//intrinsic/geometry/internal/distance_3:__pkg__",
        "//intrinsic/geometry/internal/legacy/mesh/io:__pkg__",
        "//intrinsic/scene/sdf:__pkg__",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh:__pkg__",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh/io:__pkg__",
    ],
    deps = [
        ":ai_scene_to_mesh",
        ":load_ai_scene_from_file",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@assimp",
    ],
)

cc_library(
    name = "mesh_to_ai_scene",
    srcs = ["mesh_to_ai_scene.cc"],
    hdrs = ["mesh_to_ai_scene.h"],
    visibility = [
        "//intrinsic/geometry/internal/legacy/mesh/io:__pkg__",
        "//intrinsic/geometry/internal/util:__pkg__",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh/io:__pkg__",
        "//intrinsic_sdk/intrinsic/geometry/internal/util:__pkg__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/geometry/api:material",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@assimp",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)
