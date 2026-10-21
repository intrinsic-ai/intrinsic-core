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
    name = "condition_types_proto",
    srcs = ["condition_types.proto"],
)

cc_proto_library(
    name = "condition_types_cc_proto",
    deps = [":condition_types_proto"],
)

go_proto_library(
    name = "condition_types_go_proto",
    importpath = "intrinsic/icon/proto/v1/condition_types_go_proto",
    protos = [":condition_types_proto"],
)

py_proto_library(
    name = "condition_types_py_pb2",
    deps = [":condition_types_proto"],
)

proto_library(
    name = "field_metadata_proto",
    srcs = ["field_metadata.proto"],
    deps = [
        "@com_google_protobuf//:descriptor_proto",
    ],
)

cc_proto_library(
    name = "field_metadata_cc_proto",
    deps = [":field_metadata_proto"],
)

go_proto_library(
    name = "field_metadata_go_proto",
    importpath = "intrinsic/icon/proto/v1/field_metadata_go_proto",
    protos = [":field_metadata_proto"],
)

py_proto_library(
    name = "field_metadata_py_pb2",
    deps = [":field_metadata_proto"],
)

proto_library(
    name = "jogging_service_proto",
    srcs = ["jogging_service.proto"],
    deps = [
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_protobuf//:duration_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ],
)

cc_proto_library(
    name = "jogging_service_cc_proto",
    deps = [":jogging_service_proto"],
)

cc_grpc_library(
    name = "jogging_service_cc_grpc_proto",
    srcs = [":jogging_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":jogging_service_cc_proto"],
)

go_proto_library(
    name = "jogging_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//protoc-gen-grpc-gateway:go_gen_grpc_gateway",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/icon/proto/v1/jogging_service_go_proto",
    protos = [":jogging_service_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_go_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
        "@org_golang_google_protobuf//types/known/durationpb",
    ],
)

proto_library(
    name = "types_proto",
    srcs = ["types.proto"],
    deps = [
        ":condition_types_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:descriptor_proto",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_proto",
    ],
)

cc_proto_library(
    name = "types_cc_proto",
    deps = [":types_proto"],
)

go_proto_library(
    name = "types_go_proto",
    importpath = "intrinsic/icon/proto/v1/types_go_proto",
    protos = [":types_proto"],
    deps = [
        ":condition_types_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_go_proto",
    ],
)

py_proto_library(
    name = "types_py_pb2",
    deps = [":types_proto"],
)

proto_library(
    name = "service_proto",
    srcs = ["service.proto"],
    deps = [
        ":types_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_googleapis//google/rpc:status_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_proto",
        "@intrinsic_apis//intrinsic/icon/proto:logging_mode_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_proto",
        "@intrinsic_apis//intrinsic/icon/proto:safety_status_proto",
        "@intrinsic_apis//intrinsic/icon/proto:streaming_output_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_proto",
        "@intrinsic_apis//intrinsic/world/proto:robot_payload_proto",
    ],
)

cc_proto_library(
    name = "service_cc_proto",
    deps = [":service_proto"],
)

cc_grpc_library(
    name = "service_cc_grpc_proto",
    srcs = [":service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":service_cc_proto"],
)

go_proto_library(
    name = "service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//protoc-gen-grpc-gateway:go_gen_grpc_gateway",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/icon/proto/v1/service_go_proto",
    protos = [":service_proto"],
    deps = [
        ":types_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:logging_mode_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:safety_status_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:streaming_output_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:robot_payload_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
        "@org_golang_google_genproto_googleapis_rpc//status",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/durationpb",
        "@org_golang_google_protobuf//types/known/emptypb",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)

py_proto_library(
    name = "service_py_pb2",
    deps = [":service_proto"],
)

py_grpc_library(
    name = "service_py_pb2_grpc",
    srcs = [":service_proto"],
    deps = [":service_py_pb2"],
)
