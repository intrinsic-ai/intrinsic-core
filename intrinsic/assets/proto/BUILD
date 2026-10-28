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

load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

proto_library(
    name = "installation_origin_proto",
    srcs = ["installation_origin.proto"],
    visibility = [
        "//intrinsic/resources/proto:__pkg__",
        "//intrinsic/skills/proto:__pkg__",
        "//intrinsic_sdk/intrinsic/skills/proto:__pkg__",
    ],
)

go_proto_library(
    name = "installation_origin_go_proto",
    importpath = "intrinsic/assets/proto/installation_origin_go_proto",
    protos = [":installation_origin_proto"],
    visibility = [
        "//intrinsic/assets:__subpackages__",
        "//intrinsic/kubernetes/workcell_spec:__pkg__",
        "//intrinsic/resources:__pkg__",
        "//intrinsic/resources/proto:__pkg__",
        "//intrinsic/resources/service:__pkg__",
        "//intrinsic/skills/internal:__pkg__",
        "//intrinsic/skills/proto:__pkg__",
        "//intrinsic/solution_versions:__subpackages__",
        "//intrinsic_sdk/intrinsic/skills/internal:__pkg__",
        "//intrinsic_sdk/intrinsic/skills/proto:__pkg__",
    ],
)

proto_library(
    name = "page_token_data_proto",
    srcs = ["page_token_data.proto"],
)

go_proto_library(
    name = "page_token_data_go_proto",
    importpath = "intrinsic/assets/proto/page_token_data_go_proto",
    protos = [":page_token_data_proto"],
    visibility = ["//intrinsic/assets:__pkg__"],
)
