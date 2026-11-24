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
    name = "sequence_list_proto",
    srcs = ["sequence_list.proto"],
    deps = [
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_proto",
    ],
)

cc_proto_library(
    name = "sequence_list_cc_proto",
    deps = [":sequence_list_proto"],
)

go_proto_library(
    name = "sequence_list_go_proto",
    importpath = "intrinsic/frontend/solution_service/proto/sequence_list_go_proto",
    protos = [":sequence_list_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_go_proto",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)

py_proto_library(
    name = "sequence_list_py_pb2",
    deps = [":sequence_list_proto"],
)

proto_library(
    name = "solution_service_proto",
    srcs = ["solution_service.proto"],
    deps = [
        ":sequence_list_proto",
        ":status_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
    ],
)

cc_proto_library(
    name = "solution_service_cc_proto",
    deps = [":solution_service_proto"],
)

cc_grpc_library(
    name = "solution_service_cc_grpc_proto",
    srcs = [":solution_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":solution_service_cc_proto"],
)

go_proto_library(
    name = "solution_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//protoc-gen-grpc-gateway:go_gen_grpc_gateway",
    ],
    importpath = "intrinsic/frontend/solution_service/proto/solution_service_go_proto",
    protos = [":solution_service_proto"],
    deps = [
        ":sequence_list_go_proto",
        ":status_go_proto",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
        "@org_golang_google_protobuf//types/known/emptypb",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)

py_proto_library(
    name = "solution_service_py_pb2",
    deps = [":solution_service_proto"],
)

py_grpc_library(
    name = "solution_service_py_pb2_grpc",
    srcs = [":solution_service_proto"],
    deps = [":solution_service_py_pb2"],
)

proto_library(
    name = "status_proto",
    srcs = ["status.proto"],
)

cc_proto_library(
    name = "status_cc_proto",
    deps = [":status_proto"],
)

go_proto_library(
    name = "status_go_proto",
    importpath = "intrinsic/frontend/solution_service/proto/status_go_proto",
    protos = [":status_proto"],
)

py_proto_library(
    name = "status_py_pb2",
    deps = [":status_proto"],
)
