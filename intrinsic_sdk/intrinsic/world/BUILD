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

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "entity_id",
    srcs = ["entity_id.cc"],
    hdrs = ["entity_id.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util:aggregate_type",
        "@or_tools//ortools/base:strong_int",
    ],
)

cc_library(
    name = "labels",
    hdrs = ["labels.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util:string_type",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "geometry_types",
    hdrs = ["geometry_types.h"],
)
