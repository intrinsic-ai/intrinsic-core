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

# Artifacts service client for inctl
package(default_visibility = [
    "//intrinsic/storage:__subpackages__",
    "//intrinsic/storage/artifacts:__subpackages__",
    "//intrinsic/storage/artifacts/internal:__pkg__",
    "//intrinsic_sdk/intrinsic/storage:__subpackages__",
])

go_library(
    name = "utils",
    srcs = ["utils.go"],
    importpath = "intrinsic/storage/artifacts/internal/utils",
    deps = [
        "@com_github_containerd_containerd//content:go_default_library",
        "@com_github_containerd_containerd//platforms:go_default_library",
        "@com_github_opencontainers_go_digest//:go_default_library",
        "@com_github_opencontainers_image_spec//specs-go/v1:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/storage/artifacts/proto/v1:artifact_go_proto",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)
