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
    name = "motion_planner_service_migration_proto",
    srcs = ["motion_planner_service_migration.proto"],
)

go_proto_library(
    name = "motion_planner_service_migration_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/solution_versions/proto/v1alpha1/motion_planner_service_migration_go_proto",
    protos = [":motion_planner_service_migration_proto"],
    visibility = [
        "//intrinsic/solution_versions:__subpackages__",
        "//intrinsic/tools/inctl/cmd/solution/migrate:__pkg__",
    ],
)

proto_library(
    name = "solution_migration_proto",
    srcs = ["solution_migration.proto"],
    deps = [
        ":motion_planner_service_migration_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:snapshot_source_proto",
    ],
)

go_proto_library(
    name = "solution_migration_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/solution_versions/proto/v1alpha1/solution_migration_go_proto",
    protos = [":solution_migration_proto"],
    visibility = [
        "//intrinsic/solution_versions:__subpackages__",
        "//intrinsic/tools/inctl/cmd/solution/migrate:__pkg__",
    ],
    deps = [
        ":motion_planner_service_migration_go_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:snapshot_source_go_proto",
    ],
)
