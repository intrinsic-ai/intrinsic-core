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

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "geometry_service_proto",
    srcs = ["geometry_service.proto"],
    deps = [
        "//intrinsic/geometry/processing:pipeline_config_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_googleapis//google/longrunning:operations_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_service_types_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_storage_refs_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
    ],
)

cc_proto_library(
    name = "geometry_service_cc_proto",
    deps = [":geometry_service_proto"],
)

cc_grpc_library(
    name = "geometry_service_cc_grpc_proto",
    srcs = [":geometry_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":geometry_service_cc_proto"],
)

py_proto_library(
    name = "geometry_service_py_pb2",
    deps = [":geometry_service_proto"],
)

py_grpc_library(
    name = "geometry_service_py_pb2_grpc",
    srcs = [":geometry_service_proto"],
    deps = [":geometry_service_py_pb2"],
)

go_proto_library(
    name = "geometry_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
        "//bazel:go_gen_grpc_gateway",
    ],
    importpath = "intrinsic/geometry/proto/geometry_service_go_proto",
    protos = [":geometry_service_proto"],
    deps = [
        "//intrinsic/geometry/processing:pipeline_config_go_proto",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_service_types_go_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_storage_refs_go_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_go_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)
