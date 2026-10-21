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

""" Intrinsic package geometry/legacy/mesh/io. """

load("//bazel:cc_macros.bzl", "cc_library")

cc_library(
    name = "save_mesh_to_stl_file",
    srcs = ["save_mesh_to_stl_file.cc"],
    hdrs = ["save_mesh_to_stl_file.h"],
    visibility = [
        "//intrinsic/scene/sdf:__pkg__",
        "//intrinsic/simulation/world:__pkg__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@or_tools//ortools/base:file",
    ],
)
