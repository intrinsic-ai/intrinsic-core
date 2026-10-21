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

load("//bazel:go_macros.bzl", "go_library")

package(default_visibility = ["//visibility:public"])

go_library(
    name = "directupload",
    srcs = [
        "discovery.go",
        "monitor.go",
        "transfer.go",
    ],
    importpath = "intrinsic/skills/tools/skill/cmd/directupload/directupload",
    deps = [
        "//intrinsic_sdk/intrinsic/assets:imagetransfer",
        "//intrinsic_sdk/intrinsic/storage/artifacts/client",
        "@com_github_cenkalti_backoff_v4//:go_default_library",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_google_go_containerregistry//pkg/name:go_default_library",
        "@com_github_google_go_containerregistry//pkg/v1:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:image_go_proto",
        "@intrinsic_apis//intrinsic/storage/artifacts/proto/v1:articat_go_proto",
        "@intrinsic_apis//intrinsic/storage/artifacts/proto/v1:artifact_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_x_sync//singleflight:go_default_library",
        "@org_uber_go_atomic//:go_default_library",
    ],
)
