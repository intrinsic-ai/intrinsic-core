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

# An acl engine using Authzed SpiceDB.
load("//bazel:go_macros.bzl", "go_library")

package(default_visibility = [
    "//visibility:public",
])

go_library(
    name = "cookies",
    srcs = ["cookies.go"],
    importpath = "intrinsic/kubernetes/acl/cookies",
    visibility = [
        ":__subpackages__",
        "//intrinsic/kubernetes:__subpackages__",
        "//intrinsic/kubernetes/acl:__pkg__",
        "//intrinsic/tools/inctl/util:__subpackages__",
        "//intrinsic_sdk/intrinsic/kubernetes:__subpackages__",
        "//intrinsic_sdk/intrinsic/skills/tools/skill/cmd:__subpackages__",
        "//intrinsic_sdk/intrinsic/tools/inctl/auth:__subpackages__",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:__subpackages__",
    ],
    deps = [
        "@com_github_golang_glog//:go_default_library",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//metadata:go_default_library",
    ],
)

go_library(
    name = "clientcontext",
    srcs = ["clientcontext.go"],
    importpath = "intrinsic/kubernetes/acl/clientcontext",
    visibility = ["//visibility:public"],
    deps = [
        ":cookies",
        "//intrinsic_sdk/intrinsic/stats/go:telemetry",
        "@com_github_golang_glog//:go_default_library",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//metadata:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)

go_library(
    name = "jwt",
    srcs = ["jwt.go"],
    importpath = "intrinsic/kubernetes/acl/jwt",
    deps = [
        "@com_github_golang_jwt_jwt_v5//:jwt",
        "@com_github_pkg_errors//:go_default_library",
    ],
)

go_library(
    name = "org",
    srcs = ["org.go"],
    importpath = "intrinsic/kubernetes/acl/org",
    deps = [
        "@com_github_golang_glog//:go_default_library",
        "@com_github_rs_xid//:go_default_library",
    ],
)

go_library(
    name = "identity",
    srcs = [
        "identity.go",
        "//intrinsic/kubernetes/acl:identity_internal.go",
    ],
    importpath = "intrinsic/kubernetes/acl/identity",
    deps = [
        ":clientcontext",
        ":cookies",
        ":jwt",
        ":org",
        "//intrinsic_sdk/intrinsic/frontend/go:origin",
        "//intrinsic_sdk/intrinsic/stats/go:telemetry",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_google_go_containerregistry//pkg/authn:go_default_library",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//metadata:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)
