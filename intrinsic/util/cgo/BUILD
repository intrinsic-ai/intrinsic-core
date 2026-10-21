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

package(
    default_visibility = [
        "//visibility:public",
    ],
)

cc_library(
    name = "c_types",
    hdrs = ["c_types.h"],
)

cc_library(
    name = "cpp_types",
    srcs = ["cpp_types.cc"],
    hdrs = ["cpp_types.h"],
    deps = [
        ":c_types",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:protobuf_lite",
    ],
)

go_library(
    name = "handle",
    srcs = ["handle.go"],
    cgo = 1,
    importpath = "intrinsic/util/cgo/handle",
    deps = ["@com_github_golang_glog//:go_default_library"],
)

go_library(
    name = "gotypes",
    srcs = ["go_types.go"],
    cdeps = [
        ":c_types",
    ],
    cgo = 1,
    importpath = "intrinsic/util/cgo/gotypes",
    visibility = ["//visibility:private"],
    deps = [
        ":handle",
        "@com_github_golang_glog//:go_default_library",
        "@org_golang_google_genproto_googleapis_rpc//status",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)

go_library(
    name = "cgoconvert",
    srcs = ["cgo_convert.go"],
    cdeps = [":c_types"],
    cgo = 1,
    importpath = "intrinsic/util/cgo/cgoconvert",
    deps = [
        ":gotypes",
        "//intrinsic/util/status:approximatestatus",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)
