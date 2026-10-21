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

# This package contains the tools to handle tar archives.

package(default_visibility = [
    "//intrinsic/util:__subpackages__",
    "//intrinsic_sdk/intrinsic/util:__subpackages__",
])

go_library(
    name = "tartooling",
    srcs = ["tartooling.go"],
    importpath = "intrinsic/util/archive/tartooling",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        "@com_github_google_safearchive//tar",
        "@com_github_pkg_errors//:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)
