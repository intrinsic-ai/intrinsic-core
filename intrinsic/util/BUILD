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

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "cyclic_runner",
    srcs = ["cyclic_runner.cc"],
    hdrs = ["cyclic_runner.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/time",
        "//intrinsic_sdk/intrinsic/util/time:timer",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "file_helpers",
    srcs = ["file_helpers.cc"],
    hdrs = ["file_helpers.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@or_tools//ortools/base:file",
    ],
)

cc_library(
    name = "unique_id",
    srcs = ["unique_id.cc"],
    hdrs = ["unique_id.h"],
    deps = [
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "functional",
    hdrs = [
        "functional.h",
        "functional_internal.h",
    ],
    # This is legacy code that is hard to read. Semi-deprecated and restricted to existing users.
    visibility = ["//intrinsic_control/intrinsic/icon/reflexxes:__pkg__"],
)

cc_library(
    name = "full_precision",
    hdrs = ["full_precision.h"],
    deps = ["@abseil-cpp//absl/strings:str_format"],
)

cc_library(
    name = "testcallback",
    srcs = ["testcallback.cc"],
    hdrs = ["testcallback.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
    ],
)
