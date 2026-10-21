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

go_library(
    name = "imagepublisher",
    srcs = [
        "containerd_publisher.go",
        "image_publisher.go",
    ],
    embedsrcs = [
    ],
    importpath = "intrinsic/production/imagepublisher",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        "//intrinsic/kubernetes/workcell_spec:imagetags",
        "//intrinsic/kubernetes/workcell_spec:workcellspec",
        "//intrinsic/util/go:set",
        "@com_github_cenkalti_backoff_v4//:go_default_library",
        "@com_github_containerd_containerd//:go_default_library",
        "@com_github_containerd_containerd//images:go_default_library",
        "@com_github_containerd_containerd//namespaces:go_default_library",
        "@com_github_containerd_containerd//platforms:go_default_library",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_google_go_containerregistry//pkg/name:go_default_library",
        "@com_github_google_go_containerregistry//pkg/v1:go_default_library",
        "@com_github_google_go_containerregistry//pkg/v1/remote:go_default_library",
        "@com_github_google_go_containerregistry//pkg/v1/remote/transport:go_default_library",
        "@com_github_google_go_containerregistry//pkg/v1/tarball:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@com_google_cloud_go_storage//:storage",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_api//cloudbuild/v1:go_default_library",
        "@org_golang_x_sync//errgroup:go_default_library",
    ],
)

alias(
    name = "intrinsic",
    actual = "//intrinsic_sdk/intrinsic/production:intrinsic",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//intrinsic:intrinsic_internal"],
)
