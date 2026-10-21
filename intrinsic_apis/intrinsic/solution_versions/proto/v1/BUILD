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

load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "branch_proto",
    srcs = ["branch.proto"],
    deps = [
        "@com_google_googleapis//google/api:field_behavior_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_proto",
    ],
)

go_proto_library(
    name = "branch_go_proto",
    importpath = "intrinsic/solution_versions/proto/v1/branch_go_proto",
    protos = [":branch_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
    ],
)

py_proto_library(
    name = "branch_py_pb2",
    deps = [":branch_proto"],
)

proto_library(
    name = "conflict_proto",
    srcs = ["conflict.proto"],
)

go_proto_library(
    name = "conflict_go_proto",
    importpath = "intrinsic/solution_versions/proto/v1/conflict_go_proto",
    protos = [":conflict_proto"],
)

py_proto_library(
    name = "conflict_py_pb2",
    deps = [":conflict_proto"],
)

proto_library(
    name = "conflict_resolution_proto",
    srcs = ["conflict_resolution.proto"],
)

go_proto_library(
    name = "conflict_resolution_go_proto",
    importpath = "intrinsic/solution_versions/proto/v1/conflict_resolution_go_proto",
    protos = [":conflict_resolution_proto"],
)

py_proto_library(
    name = "conflict_resolution_py_pb2",
    deps = [":conflict_resolution_proto"],
)

proto_library(
    name = "diff_mask_proto",
    srcs = ["diff_mask.proto"],
)

go_proto_library(
    name = "diff_mask_go_proto",
    importpath = "intrinsic/solution_versions/proto/v1/diff_mask_go_proto",
    protos = [":diff_mask_proto"],
)

py_proto_library(
    name = "diff_mask_py_pb2",
    deps = [":diff_mask_proto"],
)

proto_library(
    name = "snapshot_proto",
    srcs = ["snapshot.proto"],
    deps = ["@com_google_protobuf//:timestamp_proto"],
)

go_proto_library(
    name = "snapshot_go_proto",
    importpath = "intrinsic/solution_versions/proto/v1/snapshot_go_proto",
    protos = [":snapshot_proto"],
)

py_proto_library(
    name = "snapshot_py_pb2",
    deps = [":snapshot_proto"],
)

proto_library(
    name = "snapshot_source_proto",
    srcs = ["snapshot_source.proto"],
)

go_proto_library(
    name = "snapshot_source_go_proto",
    importpath = "intrinsic/solution_versions/proto/v1/snapshot_source_go_proto",
    protos = [":snapshot_source_proto"],
)

py_proto_library(
    name = "snapshot_source_py_pb2",
    deps = [":snapshot_source_proto"],
)

proto_library(
    name = "warning_proto",
    srcs = ["warning.proto"],
    deps = ["@intrinsic_apis//intrinsic/assets/proto:id_proto"],
)

go_proto_library(
    name = "warning_go_proto",
    importpath = "intrinsic/solution_versions/proto/v1/warning_go_proto",
    protos = [":warning_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
    ],
)

py_proto_library(
    name = "warning_py_pb2",
    deps = [":warning_proto"],
)
