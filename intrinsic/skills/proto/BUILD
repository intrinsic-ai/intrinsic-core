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
    name = "skill_runtime_proto",
    srcs = ["skill_runtime.proto"],
    deps = [":skill_registry_config_proto"],
)

go_proto_library(
    name = "skill_runtime_go_proto",
    importpath = "intrinsic/skills/proto/skill_runtime_go_proto",
    protos = [":skill_runtime_proto"],
    deps = [
        ":skill_registry_config_go_proto",
    ],
)

proto_library(
    name = "behavior_tree_registry_proto",
    srcs = ["behavior_tree_registry.proto"],
    deps = [
        ":skill_registry_config_proto",
        "@com_google_protobuf//:empty_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_proto",
    ],
)

cc_proto_library(
    name = "behavior_tree_registry_cc_proto",
    deps = [":behavior_tree_registry_proto"],
)

cc_grpc_library(
    name = "behavior_tree_registry_cc_grpc_proto",
    srcs = [":behavior_tree_registry_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":behavior_tree_registry_cc_proto"],
)

py_proto_library(
    name = "behavior_tree_registry_py_pb2",
    deps = [":behavior_tree_registry_proto"],
)

py_grpc_library(
    name = "behavior_tree_registry_py_pb2_grpc",
    srcs = [":behavior_tree_registry_proto"],
    deps = [":behavior_tree_registry_py_pb2"],
)

go_proto_library(
    name = "behavior_tree_registry_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/skills/proto/behavior_tree_registry_go_proto",
    protos = [":behavior_tree_registry_proto"],
    deps = [
        ":skill_registry_config_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
    ],
)

proto_library(
    name = "skill_service_proto",
    srcs = ["skill_service.proto"],
    deps = [
        ":prediction_proto",
        "@com_google_googleapis//google/longrunning:operations_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:empty_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_proto",
        "@intrinsic_apis//intrinsic/skills/proto:error_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_proto",
    ],
)

cc_proto_library(
    name = "skill_service_cc_proto",
    deps = [":skill_service_proto"],
)

cc_grpc_library(
    name = "skill_service_cc_grpc_proto",
    srcs = [":skill_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":skill_service_cc_proto"],
)

py_proto_library(
    name = "skill_service_py_pb2",
    deps = [":skill_service_proto"],
)

py_grpc_library(
    name = "skill_service_py_pb2_grpc",
    srcs = [":skill_service_proto"],
    deps = [":skill_service_py_pb2"],
)

go_proto_library(
    name = "skill_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/skills/proto/skill_service_go_proto",
    protos = [":skill_service_proto"],
    deps = [
        ":prediction_go_proto",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/logging/proto:context_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:error_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
    ],
)

proto_library(
    name = "skill_registry_config_proto",
    srcs = ["skill_registry_config.proto"],
    deps = [
        "//intrinsic/executive/proto:behavior_tree_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_proto",
    ],
)

cc_proto_library(
    name = "skill_registry_config_cc_proto",
    deps = [":skill_registry_config_proto"],
)

py_proto_library(
    name = "skill_registry_config_py_pb2",
    deps = [":skill_registry_config_proto"],
)

go_proto_library(
    name = "skill_registry_config_go_proto",
    importpath = "intrinsic/skills/proto/skill_registry_config_go_proto",
    protos = [":skill_registry_config_proto"],
    deps = [
        "//intrinsic/executive/proto:behavior_tree_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
    ],
)

proto_library(
    name = "prediction_proto",
    srcs = ["prediction.proto"],
    deps = [
        "//intrinsic/world/proto:object_world_updates_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

py_proto_library(
    name = "prediction_py_pb2",
    deps = [":prediction_proto"],
)

go_proto_library(
    name = "prediction_go_proto",
    importpath = "intrinsic/skills/proto/prediction_go_proto",
    protos = [":prediction_proto"],
    deps = [
        "//intrinsic/world/proto:object_world_updates_go_proto",
    ],
)

cc_proto_library(
    name = "prediction_cc_proto",
    deps = [":prediction_proto"],
)
