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
load("//bazel:go_macros.bzl", "go_binary")

go_binary(
    name = "operations_service",
    srcs = ["main.go"],
    deps = [
        "//intrinsic/kubernetes:intrinsic",
        "//intrinsic/longrunning/go/proxy",
        "//intrinsic/util/go:shutdown",
        "//intrinsic_sdk/intrinsic/stats/go:telemetry",
        "//intrinsic_sdk/intrinsic/util:flag",
        "@com_github_golang_glog//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//reflection:go_default_library",
    ],
)

container_image(
    name = "operations-image",
    base = "//intrinsic/kubernetes:base-image-oci",
    data_path = "/",
    files = [
        "//intrinsic/longrunning/service:operations_service",
    ],
    visibility = ["//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__"],
)

filegroup(
    name = "operations-template",
    srcs = ["operations.yaml"],
    visibility = ["//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__"],
)
