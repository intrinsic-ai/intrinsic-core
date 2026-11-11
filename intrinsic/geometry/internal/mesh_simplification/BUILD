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

"""Mesh simplification helpers and internal implementations."""

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//intrinsic/geometry:__subpackages__"])

cc_library(
    name = "meshopt_simplification_helpers",
    srcs = ["meshopt_simplification_helpers.cc"],
    hdrs = ["meshopt_simplification_helpers.h"],
    deps = [
        "//intrinsic/geometry/internal/conversion_utils:mesh_conversion",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/types:span",
        "@meshoptimizer",
    ],
)

cc_library(
    name = "meshopt_locality_simplifier",
    srcs = ["meshopt_locality_simplifier.cc"],
    hdrs = ["meshopt_locality_simplifier.h"],
    deps = [
        ":meshopt_simplification_helpers",
        "//intrinsic/geometry/internal/conversion_utils:mesh_conversion",
        "//intrinsic/geometry/internal/util:timeout_predicate",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "//third_party/meshoptimizer:simplifier",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
        "@meshoptimizer",
    ],
)
