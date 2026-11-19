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
    name = "dynamic_reconfiguration_proto",
    srcs = ["dynamic_reconfiguration.proto"],
    deps = ["@com_google_protobuf//:any_proto"],
)

cc_proto_library(
    name = "dynamic_reconfiguration_cc_proto",
    deps = [":dynamic_reconfiguration_proto"],
)

cc_grpc_library(
    name = "dynamic_reconfiguration_cc_grpc_proto",
    srcs = [":dynamic_reconfiguration_proto"],
    grpc_only = True,
    deps = [":dynamic_reconfiguration_cc_proto"],
)

go_proto_library(
    name = "dynamic_reconfiguration_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/assets/services/proto/v1/dynamic_reconfiguration_go_proto",
    protos = [":dynamic_reconfiguration_proto"],
    deps = [
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)

py_proto_library(
    name = "dynamic_reconfiguration_py_pb2",
    deps = [":dynamic_reconfiguration_proto"],
)

py_grpc_library(
    name = "dynamic_reconfiguration_py_pb2_grpc",
    srcs = [":dynamic_reconfiguration_proto"],
    deps = [":dynamic_reconfiguration_py_pb2"],
)

proto_library(
    name = "service_inspection_proto",
    srcs = ["service_inspection.proto"],
    deps = [
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

cc_proto_library(
    name = "service_inspection_cc_proto",
    deps = [":service_inspection_proto"],
)

go_proto_library(
    name = "service_inspection_go_proto",
    importpath = "intrinsic/assets/services/proto/v1/service_inspection_go_proto",
    protos = [":service_inspection_proto"],
)

py_proto_library(
    name = "service_inspection_py_pb2",
    deps = [":service_inspection_proto"],
)

proto_library(
    name = "service_state_proto",
    srcs = ["service_state.proto"],
    deps = ["@intrinsic_apis//intrinsic/util/status:extended_status_proto"],
)

cc_proto_library(
    name = "service_state_cc_proto",
    deps = [":service_state_proto"],
)

cc_grpc_library(
    name = "service_state_cc_grpc_proto",
    srcs = [":service_state_proto"],
    grpc_only = True,
    deps = [":service_state_cc_proto"],
)

go_proto_library(
    name = "service_state_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/assets/services/proto/v1/service_state_go_proto",
    protos = [":service_state_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
    ],
)

py_proto_library(
    name = "service_state_py_pb2",
    deps = [":service_state_proto"],
)

py_grpc_library(
    name = "service_state_py_pb2_grpc",
    srcs = [":service_state_proto"],
    deps = [":service_state_py_pb2"],
)

proto_library(
    name = "system_service_state_proto",
    srcs = ["system_service_state.proto"],
    deps = [
        "@com_google_googleapis//google/api:annotations_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_proto",
    ],
)

go_proto_library(
    name = "system_service_state_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//protoc-gen-grpc-gateway:go_gen_grpc_gateway",
    ],
    importpath = "intrinsic/assets/services/proto/v1/system_service_state_go_proto",
    protos = [":system_service_state_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
    ],
)

py_proto_library(
    name = "system_service_state_py_proto",
    deps = [":system_service_state_proto"],
)

py_grpc_library(
    name = "system_service_state_py_proto_grpc",
    srcs = [":system_service_state_proto"],
    deps = [":system_service_state_py_proto"],
)
