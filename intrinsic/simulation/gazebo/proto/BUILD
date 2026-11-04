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
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "spawner_service_proto",
    srcs = ["spawner_service.proto"],
    deps = [
        "@com_google_protobuf//:empty_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ],
)

cc_proto_library(
    name = "spawner_service_cc_proto",
    deps = [":spawner_service_proto"],
)

cc_grpc_library(
    name = "spawner_service_cc_grpc_proto",
    srcs = [":spawner_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":spawner_service_cc_proto"],
)

go_proto_library(
    name = "spawner_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/simulation/gazebo/proto/spawner_service_go_proto",
    protos = [":spawner_service_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
    ],
)

proto_library(
    name = "outfeed_service_proto",
    srcs = ["outfeed_service.proto"],
    deps = ["@intrinsic_apis//intrinsic/math/proto:vector3_proto"],
)

cc_proto_library(
    name = "outfeed_service_cc_proto",
    deps = [":outfeed_service_proto"],
)

cc_grpc_library(
    name = "outfeed_service_cc_grpc_proto",
    srcs = [":outfeed_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":outfeed_service_cc_proto"],
)

go_proto_library(
    name = "outfeed_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/simulation/gazebo/proto/outfeed_service_go_proto",
    protos = [":outfeed_service_proto"],
    deps = ["@intrinsic_apis//intrinsic/math/proto:vector3_go_proto"],
)

proto_library(
    name = "introspection_proto",
    srcs = [
        "introspection.proto",
    ],
    deps = ["@com_google_protobuf//:duration_proto"],
)

cc_proto_library(
    name = "introspection_cc_proto",
    deps = [":introspection_proto"],
)

go_proto_library(
    name = "introspection_go_proto",
    importpath = "intrinsic/simulation/gazebo/proto/introspection_go_proto",
    protos = [":introspection_proto"],
)
