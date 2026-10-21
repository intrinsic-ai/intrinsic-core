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

# Protos for internal skill framework types.

load("//bazel:go_macros.bzl", "go_proto_library")

package(default_visibility = [
    "//visibility:public",
])

alias(
    name = "skill_registry_internal_proto",
    actual = "//intrinsic_sdk/intrinsic/skills/internal/proto:skill_registry_internal_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

go_proto_library(
    name = "skill_registry_internal_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/skills/internal/proto/skill_registry_internal_go_proto",
    protos = [":skill_registry_internal_proto"],
    deps = [
        "//intrinsic_sdk/intrinsic/skills/proto:skill_registry_config_go_proto",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
    ],
)

alias(
    name = "behavior_tree_registry_internal_proto",
    actual = "//intrinsic_sdk/intrinsic/skills/internal/proto:behavior_tree_registry_internal_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

go_proto_library(
    name = "behavior_tree_registry_internal_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/skills/internal/proto/behavior_tree_registry_internal_go_proto",
    protos = [":behavior_tree_registry_internal_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_go_proto",
    ],
)
