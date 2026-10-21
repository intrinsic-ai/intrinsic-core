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

load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "any_list_proto",
    srcs = ["any_list.proto"],
    deps = ["@com_google_protobuf//:any_proto"],
)

cc_proto_library(
    name = "any_list_cc_proto",
    deps = [":any_list_proto"],
)

go_proto_library(
    name = "any_list_go_proto",
    importpath = "intrinsic/executive/proto/any_list_go_proto",
    protos = [":any_list_proto"],
)

py_proto_library(
    name = "any_list_py_pb2",
    deps = [":any_list_proto"],
)

proto_library(
    name = "any_with_assignments_proto",
    srcs = ["any_with_assignments.proto"],
    deps = ["@com_google_protobuf//:any_proto"],
)

cc_proto_library(
    name = "any_with_assignments_cc_proto",
    deps = [":any_with_assignments_proto"],
)

go_proto_library(
    name = "any_with_assignments_go_proto",
    importpath = "intrinsic/executive/proto/any_with_assignments_go_proto",
    protos = [":any_with_assignments_proto"],
)

py_proto_library(
    name = "any_with_assignments_py_pb2",
    deps = [":any_with_assignments_proto"],
)

proto_library(
    name = "behavior_call_proto",
    srcs = ["behavior_call.proto"],
    deps = [
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_googleapis//google/api:field_behavior_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:duration_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_proto",
    ],
)

cc_proto_library(
    name = "behavior_call_cc_proto",
    deps = [":behavior_call_proto"],
)

go_proto_library(
    name = "behavior_call_go_proto",
    importpath = "intrinsic/executive/proto/behavior_call_go_proto",
    protos = [":behavior_call_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/skills/proto:footprint_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
    ],
)

py_proto_library(
    name = "behavior_call_py_pb2",
    deps = [":behavior_call_proto"],
)

proto_library(
    name = "code_execution_proto",
    srcs = ["code_execution.proto"],
    deps = [
        ":any_with_assignments_proto",
        "@com_google_googleapis//google/api:field_behavior_proto",
        "@com_google_protobuf//:descriptor_proto",
    ],
)

cc_proto_library(
    name = "code_execution_cc_proto",
    deps = [":code_execution_proto"],
)

go_proto_library(
    name = "code_execution_go_proto",
    importpath = "intrinsic/executive/proto/code_execution_go_proto",
    protos = [":code_execution_proto"],
    deps = [
        ":any_with_assignments_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
    ],
)

py_proto_library(
    name = "code_execution_py_pb2",
    deps = [":code_execution_proto"],
)

proto_library(
    name = "behavior_tree_proto",
    srcs = ["behavior_tree.proto"],
    deps = [
        ":any_list_proto",
        ":any_with_assignments_proto",
        ":behavior_call_proto",
        ":code_execution_proto",
        "@com_google_cel_spec//proto/cel/expr:syntax_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_googleapis//google/api:field_behavior_proto",
        "@com_google_protobuf//:any_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_proto",
    ],
)

cc_proto_library(
    name = "behavior_tree_cc_proto",
    deps = [":behavior_tree_proto"],
)

go_proto_library(
    name = "behavior_tree_go_proto",
    importpath = "intrinsic/executive/proto/behavior_tree_go_proto",
    protos = [":behavior_tree_proto"],
    deps = [
        ":any_list_go_proto",
        ":any_with_assignments_go_proto",
        ":behavior_call_go_proto",
        ":code_execution_go_proto",
        "@com_google_cel_spec//:expr",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
    ],
)

py_proto_library(
    name = "behavior_tree_py_pb2",
    deps = [":behavior_tree_proto"],
)

proto_library(
    name = "blackboard_service_proto",
    srcs = ["blackboard_service.proto"],
    deps = [
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_proto",
    ],
)

cc_proto_library(
    name = "blackboard_service_cc_proto",
    deps = [":blackboard_service_proto"],
)

cc_grpc_library(
    name = "blackboard_service_cc_grpc_proto",
    srcs = [":blackboard_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":blackboard_service_cc_proto"],
)

go_proto_library(
    name = "blackboard_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//protoc-gen-grpc-gateway:go_gen_grpc_gateway",
    ],
    importpath = "intrinsic/executive/proto/blackboard_service_go_proto",
    protos = [":blackboard_service_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/emptypb",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)

py_proto_library(
    name = "blackboard_service_py_pb2",
    deps = [":blackboard_service_proto"],
)

py_grpc_library(
    name = "blackboard_service_py_pb2_grpc",
    srcs = [":blackboard_service_proto"],
    deps = [":blackboard_service_py_pb2"],
)

proto_library(
    name = "clips_snapshot_proto",
    srcs = ["clips_snapshot.proto"],
)

cc_proto_library(
    name = "clips_snapshot_cc_proto",
    deps = [":clips_snapshot_proto"],
)

go_proto_library(
    name = "clips_snapshot_go_proto",
    importpath = "intrinsic/executive/proto/clips_snapshot_go_proto",
    protos = [":clips_snapshot_proto"],
)

py_proto_library(
    name = "clips_snapshot_py_pb2",
    deps = [":clips_snapshot_proto"],
)

proto_library(
    name = "code_execution_info_service_proto",
    srcs = ["code_execution_info_service.proto"],
    deps = [
        ":code_execution_proto",
        "@com_google_protobuf//:descriptor_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
    ],
)

cc_proto_library(
    name = "code_execution_info_service_cc_proto",
    deps = [":code_execution_info_service_proto"],
)

cc_grpc_library(
    name = "code_execution_info_service_cc_grpc_proto",
    srcs = [":code_execution_info_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":code_execution_info_service_cc_proto"],
)

go_proto_library(
    name = "code_execution_info_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/executive/proto/code_execution_info_service_go_proto",
    protos = [":code_execution_info_service_proto"],
    deps = [
        ":code_execution_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

py_proto_library(
    name = "code_execution_info_service_py_pb2",
    deps = [":code_execution_info_service_proto"],
)

py_grpc_library(
    name = "code_execution_info_service_py_pb2_grpc",
    srcs = [":code_execution_info_service_proto"],
    deps = [":code_execution_info_service_py_pb2"],
)

proto_library(
    name = "executive_execution_mode_proto",
    srcs = ["executive_execution_mode.proto"],
)

cc_proto_library(
    name = "executive_execution_mode_cc_proto",
    deps = [":executive_execution_mode_proto"],
)

go_proto_library(
    name = "executive_execution_mode_go_proto",
    importpath = "intrinsic/executive/proto/executive_execution_mode_go_proto",
    protos = [":executive_execution_mode_proto"],
)

py_proto_library(
    name = "executive_execution_mode_py_pb2",
    deps = [":executive_execution_mode_proto"],
)

proto_library(
    name = "log_items_proto",
    srcs = ["log_items.proto"],
    deps = ["@com_google_googleapis//google/longrunning:operations_proto"],
)

cc_proto_library(
    name = "log_items_cc_proto",
    deps = [":log_items_proto"],
)

go_proto_library(
    name = "log_items_go_proto",
    importpath = "intrinsic/executive/proto/log_items_go_proto",
    protos = [":log_items_proto"],
    deps = [
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
    ],
)

py_proto_library(
    name = "log_items_py_pb2",
    deps = [":log_items_proto"],
)

proto_library(
    name = "proto_builder_proto",
    srcs = ["proto_builder.proto"],
    deps = [
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_protobuf//:descriptor_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
    ],
)

cc_proto_library(
    name = "proto_builder_cc_proto",
    deps = [":proto_builder_proto"],
)

cc_grpc_library(
    name = "proto_builder_cc_grpc_proto",
    srcs = [":proto_builder_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":proto_builder_cc_proto"],
)

go_proto_library(
    name = "proto_builder_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//protoc-gen-grpc-gateway:go_gen_grpc_gateway",
    ],
    importpath = "intrinsic/executive/proto/proto_builder_go_proto",
    protos = [":proto_builder_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

py_proto_library(
    name = "proto_builder_py_pb2",
    deps = [":proto_builder_proto"],
)

py_grpc_library(
    name = "proto_builder_py_pb2_grpc",
    srcs = [":proto_builder_proto"],
    deps = [":proto_builder_py_pb2"],
)

proto_library(
    name = "run_response_proto",
    srcs = ["run_response.proto"],
    deps = ["@com_google_protobuf//:any_proto"],
)

cc_proto_library(
    name = "run_response_cc_proto",
    deps = [":run_response_proto"],
)

go_proto_library(
    name = "run_response_go_proto",
    importpath = "intrinsic/executive/proto/run_response_go_proto",
    protos = [":run_response_proto"],
)

py_proto_library(
    name = "run_response_py_pb2",
    deps = [":run_response_proto"],
)

proto_library(
    name = "world_query_proto",
    srcs = ["world_query.proto"],
    deps = ["@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto"],
)

cc_proto_library(
    name = "world_query_cc_proto",
    deps = [":world_query_proto"],
)

go_proto_library(
    name = "world_query_go_proto",
    importpath = "intrinsic/executive/proto/world_query_go_proto",
    protos = [":world_query_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
    ],
)

py_proto_library(
    name = "world_query_py_pb2",
    deps = [":world_query_proto"],
)
