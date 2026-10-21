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

# This package contains the tools to handle tar archives.

package(default_visibility = ["//visibility:private"])

cc_library(
    name = "unzip",
    srcs = ["unzip.cc"],
    hdrs = ["unzip.h"],
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        "//intrinsic/util:file_helpers",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@libzip",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:path",
    ],
)

# Test data for utilities. See README for details.
#
# This is a Fileset, and not a filegroup, so that we can pass a single `rlocationpath`
# to `untar_test` (see below).
#
# Doing that makes the test robust against running in different environments
# (locally, on cloud build farms, as part of another bazel workspace...).
