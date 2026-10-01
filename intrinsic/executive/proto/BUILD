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
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic/util/proto/build_defs:descriptor_set.bzl", "proto_source_code_info_transitive_descriptor_set")

package(default_visibility = ["//intrinsic:public_api_users"])

# behavior_call
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

py_proto_library(
    name = "behavior_call_py_pb2",
    deps = [":behavior_call_proto"],
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

# code_execution_service
proto_library(
    name = "code_execution_service_proto",
    srcs = ["code_execution_service.proto"],
    deps = [
        "@com_google_googleapis//google/longrunning:operations_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:descriptor_proto",
        "@com_google_protobuf//:empty_proto",
        "@intrinsic_apis//intrinsic/executive/proto:code_execution_proto",
    ],
)

py_proto_library(
    name = "code_execution_service_py_pb2",
    deps = [":code_execution_service_proto"],
)

py_grpc_library(
    name = "code_execution_service_py_pb2_grpc",
    srcs = [":code_execution_service_proto"],
    deps = [":code_execution_service_py_pb2"],
)

cc_proto_library(
    name = "code_execution_service_cc_proto",
    deps = [":code_execution_service_proto"],
)

cc_grpc_library(
    name = "code_execution_service_cc_grpc_proto",
    srcs = [":code_execution_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":code_execution_service_cc_proto"],
)

go_proto_library(
    name = "code_execution_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/executive/proto/code_execution_service_go_proto",
    protos = [":code_execution_service_proto"],
    deps = [
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/executive/proto:code_execution_go_proto",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)

# executive_debug_service
proto_library(
    name = "executive_debug_service_proto",
    srcs = ["executive_debug_service.proto"],
    visibility = ["//intrinsic/executive:__subpackages__"],
    deps = [
        "@opentelemetry_proto//:trace_proto",
    ],
)

cc_proto_library(
    name = "executive_debug_service_cc_proto",
    visibility = ["//intrinsic/executive:__subpackages__"],
    deps = [":executive_debug_service_proto"],
)

cc_grpc_library(
    name = "executive_debug_service_cc_grpc_proto",
    srcs = [":executive_debug_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    visibility = [
        "//intrinsic/executive:__subpackages__",
    ],
    deps = [":executive_debug_service_cc_proto"],
)

go_proto_library(
    name = "executive_debug_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/executive/proto/executive_debug_service_go_proto",
    protos = [":executive_debug_service_proto"],
    visibility = [
        "//intrinsic/executive:__subpackages__",
        "//intrinsic/tools/inctl/cmd:__pkg__",
    ],
    deps = [
        "@io_opentelemetry_go_proto_otlp//trace/v1:go_default_library",
    ],
)

# executive_config
proto_library(
    name = "executive_config_proto",
    srcs = ["executive_config.proto"],
    deps = ["@com_google_protobuf//:duration_proto"],
)

cc_proto_library(
    name = "executive_config_cc_proto",
    deps = [":executive_config_proto"],
)

# executive_flags
proto_library(
    name = "executive_flags_proto",
    srcs = ["executive_flags.proto"],
    deps = [":executive_config_proto"],
)

cc_proto_library(
    name = "executive_flags_cc_proto",
    deps = [":executive_flags_proto"],
)

# executive_service
proto_library(
    name = "executive_service_proto",
    srcs = ["executive_service.proto"],
    deps = [
        ":behavior_tree_proto",
        ":run_metadata_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_googleapis//google/longrunning:operations_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:field_mask_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_proto",
        "@intrinsic_apis//intrinsic/executive/proto:executive_execution_mode_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
    ],
)

py_proto_library(
    name = "executive_service_py_pb2",
    deps = [":executive_service_proto"],
)

py_grpc_library(
    name = "executive_service_py_pb2_grpc",
    srcs = [":executive_service_proto"],
    deps = [":executive_service_py_pb2"],
)

cc_grpc_library(
    name = "executive_service_cc_grpc_proto",
    srcs = [":executive_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":executive_service_cc_proto"],
)

cc_proto_library(
    name = "executive_service_cc_proto",
    deps = [":executive_service_proto"],
)

go_proto_library(
    name = "executive_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
        "//bazel:go_gen_grpc_gateway",
    ],
    importpath = "intrinsic/executive/proto/executive_service_go_proto",
    protos = [":executive_service_proto"],
    deps = [
        ":behavior_tree_go_proto",
        ":run_metadata_go_proto",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/executive/proto:executive_execution_mode_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/emptypb",
        "@org_golang_google_protobuf//types/known/fieldmaskpb",
    ],
)

# run_metadata
proto_library(
    name = "run_metadata_proto",
    srcs = ["run_metadata.proto"],
    deps = [
        ":behavior_tree_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/executive/proto:executive_execution_mode_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_proto",
    ],
)

py_proto_library(
    name = "run_metadata_py_pb2",
    deps = [":run_metadata_proto"],
)

cc_proto_library(
    name = "run_metadata_cc_proto",
    deps = [":run_metadata_proto"],
)

go_proto_library(
    name = "run_metadata_go_proto",
    importpath = "intrinsic/executive/proto/run_metadata_go_proto",
    protos = [":run_metadata_proto"],
    deps = [
        ":behavior_tree_go_proto",
        "@intrinsic_apis//intrinsic/executive/proto:executive_execution_mode_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
    ],
)

proto_library(
    name = "executive_events_proto",
    srcs = ["executive_events.proto"],
    deps = [
        ":behavior_tree_proto",
        ":run_metadata_proto",
        "@com_google_googleapis//google/rpc:status_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/executive/proto:executive_execution_mode_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_proto",
    ],
)

py_proto_library(
    name = "executive_events_py_pb2",
    deps = [":executive_events_proto"],
)

cc_proto_library(
    name = "executive_events_cc_proto",
    deps = [":executive_events_proto"],
)

go_proto_library(
    name = "executive_events_go_proto",
    importpath = "intrinsic/executive/proto/executive_events_go_proto",
    protos = [":executive_events_proto"],
    deps = [
        ":behavior_tree_go_proto",
        ":run_metadata_go_proto",
        "@intrinsic_apis//intrinsic/executive/proto:executive_execution_mode_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
        "@org_golang_google_genproto_googleapis_rpc//status",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)

# behavior tree
proto_library(
    name = "behavior_tree_proto",
    srcs = ["behavior_tree.proto"],
    deps = [
        ":behavior_call_proto",
        "@com_google_cel_spec//proto/cel/expr:syntax_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_googleapis//google/api:field_behavior_proto",
        "@com_google_protobuf//:any_proto",
        "@intrinsic_apis//intrinsic/executive/proto:any_list_proto",
        "@intrinsic_apis//intrinsic/executive/proto:any_with_assignments_proto",
        "@intrinsic_apis//intrinsic/executive/proto:code_execution_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_proto",
    ],
)

cc_proto_library(
    name = "behavior_tree_cc_proto",
    deps = [":behavior_tree_proto"],
)

py_proto_library(
    name = "behavior_tree_py_pb2",
    deps = [":behavior_tree_proto"],
)

go_proto_library(
    name = "behavior_tree_go_proto",
    importpath = "intrinsic/executive/proto/behavior_tree_go_proto",
    protos = [":behavior_tree_proto"],
    deps = [
        ":behavior_call_go_proto",
        "@com_google_cel_spec//:expr",
        "@intrinsic_apis//intrinsic/executive/proto:any_list_go_proto",
        "@intrinsic_apis//intrinsic/executive/proto:any_with_assignments_go_proto",
        "@intrinsic_apis//intrinsic/executive/proto:code_execution_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
    ],
)

proto_source_code_info_transitive_descriptor_set(
    name = "world_query_proto_descriptor_set",
    deps = ["@intrinsic_apis//intrinsic/executive/proto:world_query_proto"],
)

proto_library(
    name = "process_tree_proto",
    srcs = ["process_tree.proto"],
    deps = [":behavior_tree_proto"],
)

# process_tree
cc_proto_library(
    name = "process_tree_cc_proto",
    deps = [":process_tree_proto"],
)

py_proto_library(
    name = "process_tree_py_pb2",
    deps = [":process_tree_proto"],
)
