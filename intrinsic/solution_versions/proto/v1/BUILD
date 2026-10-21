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

load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_grpc_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

proto_library(
    name = "solution_version_service_proto",
    srcs = ["solution_version_service.proto"],
    deps = [
        ":solution_diff_proto",
        "//intrinsic/modified_solution/proto/v1:modified_solution_proto",
        "//intrinsic_sdk/intrinsic/assets/proto/v1:solution_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_googleapis//google/api:field_behavior_proto",
        "@com_google_googleapis//google/api:resource_proto",
        "@com_google_googleapis//google/longrunning:operations_proto",
        "@com_google_googleapis//google/rpc:status_proto",
        "@com_google_protobuf//:descriptor_proto",
        "@com_google_protobuf//:field_mask_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:branch_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:conflict_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:conflict_resolution_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:snapshot_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:snapshot_source_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:warning_proto",
    ],
)

py_proto_library(
    name = "solution_version_service_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":solution_version_service_proto"],
)

py_grpc_library(
    name = "solution_version_service_pb2_grpc",
    srcs = [":solution_version_service_proto"],
    visibility = ["//visibility:public"],
    deps = [":solution_version_service_py_pb2"],
)

go_proto_library(
    name = "solution_version_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/solution_versions/proto/v1/solution_version_service_go_proto",
    protos = [":solution_version_service_proto"],
    visibility = [
        "//intrinsic/assets/deploy:__pkg__",
        "//intrinsic/assets/install:__pkg__",
        "//intrinsic/kubernetes/workcell_spec:__pkg__",
        "//intrinsic/modified_solution:__subpackages__",
        "//intrinsic/solution_versions:__subpackages__",
        "//intrinsic/tools/inctl/cmd:__subpackages__",
        "//incode/cloud/devicemanager:__subpackages__",
        "//incode/frontend:__subpackages__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:__subpackages__",
    ],
    deps = [
        ":solution_diff_go_proto",
        "//intrinsic/modified_solution/proto/v1:modified_solution_go_proto",
        "//intrinsic_sdk/intrinsic/assets/proto/v1:solution_go_proto",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:branch_go_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:conflict_go_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:conflict_resolution_go_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:snapshot_go_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:snapshot_source_go_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:warning_go_proto",
        "@io_bazel_rules_go//proto/wkt:descriptor_go_proto",
        "@io_bazel_rules_go//proto/wkt:field_mask_go_proto",
        "@io_bazel_rules_go//proto/wkt:timestamp_go_proto",
        "@org_golang_google_genproto//googleapis/longrunning",
        "@org_golang_google_genproto_googleapis_api//annotations",
        "@org_golang_google_genproto_googleapis_rpc//status",
    ],
)

proto_library(
    name = "solution_diff_proto",
    srcs = ["solution_diff.proto"],
    deps = [
        "//intrinsic_sdk/intrinsic/assets/proto/v1:solution_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:diff_mask_proto",
    ],
)

go_proto_library(
    name = "solution_diff_go_proto",
    importpath = "intrinsic/solution_versions/proto/v1/solution_diff_go_proto",
    protos = [":solution_diff_proto"],
    visibility = [
        "//intrinsic/solution_versions:__subpackages__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution_version:__pkg__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/assets/proto/v1:solution_go_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:diff_mask_go_proto",
    ],
)

py_proto_library(
    name = "solution_diff_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":solution_diff_proto"],
)
