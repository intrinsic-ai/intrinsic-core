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

package(default_visibility = [
    "//visibility:public",
])

proto_library(
    name = "conductor_proto",
    srcs = ["conductor.proto"],
    deps = [
        "//intrinsic/conductor/proto:execution_context_proto",
        "//intrinsic/conductor/proto:save_status_proto",
        "//intrinsic_sdk/intrinsic/executive/proto:run_metadata_proto",
        "@com_google_googleapis//google/longrunning:operations_proto",
        "@com_google_protobuf//:empty_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_proto",
    ],
)

go_proto_library(
    name = "conductor_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/conductor/proto/conductor_go_proto",
    protos = [":conductor_proto"],
    deps = [
        "//intrinsic/conductor/proto:execution_context_go_proto",
        "//intrinsic/conductor/proto:save_status_go_proto",
        "//intrinsic_sdk/intrinsic/executive/proto:run_metadata_go_proto",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_go_proto",
    ],
)

cc_proto_library(
    name = "conductor_cc_proto",
    deps = [":conductor_proto"],
)

cc_grpc_library(
    name = "conductor_cc_grpc",
    srcs = [":conductor_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":conductor_cc_proto"],
)

py_proto_library(
    name = "conductor_py_pb2",
    deps = [":conductor_proto"],
)

py_grpc_library(
    name = "conductor_py_pb2_grpc",
    srcs = [":conductor_proto"],
    deps = [":conductor_py_pb2"],
)
