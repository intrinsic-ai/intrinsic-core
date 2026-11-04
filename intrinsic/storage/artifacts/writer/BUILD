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

# Writers for Artifacts service
package(default_visibility = ["//intrinsic/storage/artifacts:__subpackages__"])

go_library(
    name = "writer",
    srcs = [
        "file.go",
        "image_tar.go",
        "oci_blob.go",
        "oci_image.go",
        "writer.go",
    ],
    importpath = "intrinsic/storage/artifacts/writer/writer",
    deps = [
        "@com_github_containerd_containerd//:go_default_library",
        "@com_github_containerd_containerd//content:go_default_library",
        "@com_github_containerd_containerd//errdefs:go_default_library",
        "@com_github_containerd_containerd//images:go_default_library",
        "@com_github_containerd_containerd//leases:go_default_library",
        "@com_github_containerd_containerd//platforms:go_default_library",
        "@com_github_containerd_containerd//reference:go_default_library",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_opencontainers_go_digest//:go_default_library",
        "@com_github_opencontainers_image_spec//specs-go:go_default_library",
        "@com_github_opencontainers_image_spec//specs-go/v1:go_default_library",
        "@com_github_pborman_uuid//:go_default_library",
        "@intrinsic_apis//intrinsic/storage/artifacts/proto/v1:artifact_go_proto",
        "@io_opencensus_go//trace:go_default_library",
        "@org_uber_go_atomic//:go_default_library",
    ],
)
