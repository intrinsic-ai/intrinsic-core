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

load("//intrinsic_sdk/bazel:go_macros.bzl", "go_library")

package(default_visibility = ["//visibility:public"])

go_library(
    name = "resolve",
    srcs = ["resolve.go"],
    importpath = "intrinsic/tools/inctl/util/resolve",
    deps = [
        "@com_github_pkg_errors//:go_default_library",
        "@io_bazel_rules_go//go/runfiles:go_default_library",
    ],
)

go_library(
    name = "introspection",
    srcs = ["introspection.go"],
    importpath = "intrinsic/tools/inctl/util/introspection",
)

go_library(
    name = "gcpauth",
    srcs = ["gcpauth.go"],
    importpath = "intrinsic/tools/inctl/util/gcpauth",
    deps = [
        "//intrinsic_sdk/intrinsic/config:environments",
        "//intrinsic_sdk/intrinsic/tools/inctl/auth",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_google_go_containerregistry//pkg/v1/google:go_default_library",
        "@com_github_google_go_containerregistry//pkg/v1/remote:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@org_golang_google_api//option:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//credentials:go_default_library",
        "@org_golang_google_grpc//credentials/oauth:go_default_library",
        "@org_golang_x_oauth2//:go_default_library",
        "@org_golang_x_oauth2//google:go_default_library",
    ],
)

go_library(
    name = "casgeometryuploader",
    srcs = ["cas_geometry_uploader.go"],
    importpath = "intrinsic/tools/inctl/util/casgeometryuploader",
    deps = [
        "//intrinsic/storage/content_addressable_storage/pkg/filetocas",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_x_sync//errgroup:go_default_library",
    ],
)
