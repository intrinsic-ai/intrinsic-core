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

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_library", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

cc_library(
    name = "page_size",
    srcs = ["page_size.cc"],
    hdrs = ["page_size.h"],
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "page_token_manager",
    hdrs = ["page_token_manager.h"],
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
    deps = [
        ":page_token_cc_proto",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

go_library(
    name = "pagesize",
    srcs = ["pagesize.go"],
    importpath = "intrinsic/util/pagination/pagesize",
    visibility = ["//intrinsic/frontend/solution_service:__pkg__"],
)

go_library(
    name = "pagetoken",
    srcs = ["pagetoken.go"],
    importpath = "intrinsic/util/pagination/pagetoken",
    visibility = [
        "//intrinsic/assets/install:__pkg__",
        "//intrinsic/assets/instances:__pkg__",
        "//intrinsic/frontend/solution_service:__subpackages__",
        "//intrinsic/kubernetes/data_store:__subpackages__",
        "//intrinsic/longrunning/go:__pkg__",
        "//intrinsic/resources/service:__pkg__",
        "//intrinsic/skills/internal:__pkg__",
        "//intrinsic/solution_versions:__subpackages__",
        "//intrinsic_sdk/intrinsic/skills/internal:__pkg__",
    ],
    deps = [
        ":page_token_go_proto",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)

proto_library(
    name = "page_token_proto",
    srcs = ["page_token.proto"],
    deps = [
        "@com_google_protobuf//:timestamp_proto",
    ],
)

cc_proto_library(
    name = "page_token_cc_proto",
    deps = [":page_token_proto"],
)

go_proto_library(
    name = "page_token_go_proto",
    importpath = "intrinsic/util/pagination/page_token_go_proto",
    protos = [":page_token_proto"],
    visibility = ["//intrinsic/solution_versions:__subpackages__"],
)
