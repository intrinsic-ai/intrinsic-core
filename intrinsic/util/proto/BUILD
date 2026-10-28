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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "compile_utils",
    srcs = ["compile_utils.cc"],
    hdrs = ["compile_utils.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
    ],
)

go_library(
    name = "descriptorcompatibility",
    srcs = ["descriptor_compatibility.go"],
    importpath = "intrinsic/util/proto/descriptorcompatibility",
    deps = [
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

cc_library(
    name = "pb_hash",
    hdrs = ["pb_hash.h"],
    deps = [
        "@com_google_protobuf//:differencer",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//src/google/protobuf/io",
    ],
)

cc_library(
    name = "walk_messages",
    srcs = ["walk_messages.cc"],
    hdrs = ["walk_messages.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_protobuf//:protobuf",
    ],
)
