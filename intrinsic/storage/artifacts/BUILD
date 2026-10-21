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

load("//bazel:container.bzl", "container_image")
load("//bazel:go_macros.bzl", "go_binary", "go_library")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "artifacts-service-template",
    srcs = ["templates/artifacts-service.yaml"],
    visibility = ["//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__"],
)

container_image(
    name = "artifacts-service-image",
    base = "//intrinsic/kubernetes:base-image-oci",
    data_path = "/",
    files = [
        ":artifact_service",
    ],
    visibility = [
        "//intrinsic/kubernetes:intrinsic_kubernetes_users",
    ],
)

go_binary(
    name = "artifact_service",
    srcs = ["artifacts.go"],
    importpath = "intrinsic/storage/artifacts/artifact_service",
    deps = [
        ":artifacts",
        "//intrinsic/kubernetes:intrinsic",
        "//intrinsic/storage/artifacts/internal",
        "//intrinsic_sdk/intrinsic/stats/go:telemetry",
        "@com_github_containerd_containerd//:go_default_library",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/storage/artifacts/proto/v1:artifact_go_proto",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@io_opencensus_go//plugin/ochttp:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//reflection:go_default_library",
    ],
)

go_library(
    name = "artifacts",
    srcs = ["artifacts_service.go"],
    importpath = "intrinsic/storage/artifacts/artifacts",
    deps = [
        "//intrinsic/storage/artifacts/internal",
        "//intrinsic_sdk/intrinsic/storage/artifacts/internal:utils",
        "@com_github_containerd_containerd//content:go_default_library",
        "@com_github_containerd_containerd//namespaces:go_default_library",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_opencontainers_go_digest//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/storage/artifacts/proto/v1:artifact_go_proto",
        "@io_opencensus_go//plugin/ochttp:go_default_library",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//metadata:go_default_library",
        "@org_golang_google_grpc//peer:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)
