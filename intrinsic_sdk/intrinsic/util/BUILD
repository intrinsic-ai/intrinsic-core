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
load("//bazel:go_macros.bzl", "go_library")
load("//bazel:python.bzl", "py_library")

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "demangle",
    srcs = ["demangle.cc"],
    hdrs = ["demangle.h"],
    deps = [
        "@abseil-cpp//absl/cleanup",
    ],
)

cc_library(
    name = "hash",
    hdrs = ["hash.h"],
    deps = [
        "//third_party/thorough_hash",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "aggregate_type",
    hdrs = ["aggregate_type.h"],
)

cc_library(
    name = "atomic_sequence_num",
    hdrs = ["atomic_sequence_num.h"],
    deps = ["@or_tools//ortools/base:strong_int"],
)

cc_library(
    name = "fixed_vector",
    hdrs = ["fixed_vector.h"],
    deps = [
        "@abseil-cpp//absl/container:inlined_vector",
        "@abseil-cpp//absl/log",
    ],
)

cc_library(
    name = "invalid_until_set",
    hdrs = ["invalid_until_set.h"],
)

cc_library(
    name = "macros",
    hdrs = ["macros.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
    ],
)

cc_library(
    name = "page_fault_info",
    srcs = ["page_fault_info.cc"],
    hdrs = ["page_fault_info.h"],
)

py_library(
    name = "decorators",
    srcs = ["decorators.py"],
)

cc_library(
    name = "eigen",
    srcs = ["eigen.cc"],
    hdrs = ["eigen.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:pose2",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/eigenmath:so3",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
        "@boost.regex",
    ],
)

cc_library(
    name = "lru_cache",
    hdrs = ["lru_cache.h"],
    deps = [
        "//third_party/simple_lru_cache",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
    ],
)

cc_library(
    name = "memory_lock",
    hdrs = ["memory_lock.h"],
    deps = [
        "@abseil-cpp//absl/status",
    ],
)

cc_library(
    name = "string_type",
    hdrs = ["string_type.h"],
    deps = [
        "@abseil-cpp//absl/flags:marshalling",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
    ],
)

cc_library(
    name = "proto_time",
    srcs = ["proto_time.cc"],
    hdrs = ["proto_time.h"],
    deps = [
        "//third_party/ros2/ros_interfaces/jazzy/builtin_interfaces/msg:time_cc_proto",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "cloud",
    srcs = ["cloud.cc"],
    hdrs = ["cloud.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@google_cloud_cpp//:common",
    ],
)

cc_library(
    name = "log_lines",
    srcs = ["log_lines.cc"],
    hdrs = ["log_lines.h"],
    deps = [
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
    ],
)

go_library(
    name = "flag",
    srcs = ["flag.go"],
    importpath = "intrinsic/util/flag",
)

cc_library(
    name = "version",
    srcs = ["version.cc"],
    hdrs = ["version.h"],
    deps = [
        "//third_party/strings_numbers:sort",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)
