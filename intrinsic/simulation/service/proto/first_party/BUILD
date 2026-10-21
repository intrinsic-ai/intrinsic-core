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

# Protos for the simulation service.

load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "simulation_service_proto",
    srcs = [
        "simulation_service.proto",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_proto",
        "@com_google_googleapis//google/rpc:status_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:wrappers_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
    ],
)

cc_proto_library(
    name = "simulation_service_cc_proto",
    deps = [":simulation_service_proto"],
)

cc_grpc_library(
    name = "simulation_service_cc_grpc_proto",
    srcs = [":simulation_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":simulation_service_cc_proto"],
)

py_proto_library(
    name = "simulation_service_py_pb2",
    deps = [":simulation_service_proto"],
)

py_grpc_library(
    name = "simulation_service_py_pb2_grpc",
    srcs = [":simulation_service_proto"],
    deps = [":simulation_service_py_pb2"],
)

go_proto_library(
    name = "simulation_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/simulation/service/proto/first_party/simulation_service_go_proto",
    protos = [":simulation_service_proto"],
    deps = [
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_go_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@org_golang_google_genproto_googleapis_rpc//status",
        "@org_golang_google_protobuf//types/known/durationpb",
        "@org_golang_google_protobuf//types/known/emptypb",
        "@org_golang_google_protobuf//types/known/wrapperspb",
    ],
)
