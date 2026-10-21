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

load("//bazel:go_macros.bzl", "go_library", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = [
    "//intrinsic/tools/inctl:__subpackages__",
    "//intrinsic_sdk/intrinsic/tools/inctl:__subpackages__",
])

proto_library(
    name = "config_proto",
    srcs = ["config.proto"],
)

go_proto_library(
    name = "config_go_proto",
    importpath = "intrinsic/tools/inctl/config/config_go_proto",
    protos = [":config_proto"],
)

go_library(
    name = "config",
    srcs = ["store.go"],
    importpath = "intrinsic/tools/inctl/config/config",
    deps = [
        ":config_go_proto",
        "@com_github_pkg_errors//:go_default_library",
        "@org_golang_google_protobuf//encoding/protojson:go_default_library",
    ],
)
