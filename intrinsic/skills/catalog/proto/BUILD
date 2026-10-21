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
    name = "skill_deployment_data_proto",
    srcs = ["skill_deployment_data.proto"],
    visibility = ["//intrinsic/modified_solution/proto/v1:__pkg__"],
    deps = [
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_proto",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:image_proto",
    ],
)

go_proto_library(
    name = "skill_deployment_data_go_proto",
    importpath = "intrinsic/skills/catalog/proto/skill_deployment_data_go_proto",
    protos = [":skill_deployment_data_proto"],
    visibility = [
        "//intrinsic/assets/deploy:__pkg__",
        "//intrinsic/kubernetes/workcell_spec:__pkg__",
        "//intrinsic/modified_solution:__subpackages__",
        "//intrinsic/resources:__pkg__",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:image_go_proto",
    ],
)
