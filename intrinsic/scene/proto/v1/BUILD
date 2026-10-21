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

package(
    default_visibility = [
        "//visibility:public",
    ],
)

proto_library(
    name = "imported_scene_proto",
    srcs = ["imported_scene.proto"],
    deps = [
        ":imported_scene_updates_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_proto",
    ],
)

cc_proto_library(
    name = "imported_scene_cc_proto",
    deps = [":imported_scene_proto"],
)

go_proto_library(
    name = "imported_scene_go_proto",
    importpath = "intrinsic/scene/proto/v1/imported_scene_go_proto",
    protos = [":imported_scene_proto"],
    deps = [
        "//intrinsic/scene/proto/v1:imported_scene_updates_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_go_proto",
    ],
)

py_proto_library(
    name = "imported_scene_py_pb2",
    visibility = [
        "//visibility:public",
    ],
    deps = [":imported_scene_proto"],
)

proto_library(
    name = "imported_scene_updates_proto",
    srcs = ["imported_scene_updates.proto"],
    deps = ["@intrinsic_apis//intrinsic/math/proto:pose_proto"],
)

cc_proto_library(
    name = "imported_scene_updates_cc_proto",
    deps = [":imported_scene_updates_proto"],
)

go_proto_library(
    name = "imported_scene_updates_go_proto",
    importpath = "intrinsic/scene/proto/v1/imported_scene_updates_go_proto",
    protos = [":imported_scene_updates_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
    ],
)

py_proto_library(
    name = "imported_scene_updates_py_pb2",
    visibility = [
        "//visibility:public",
    ],
    deps = [":imported_scene_updates_proto"],
)

alias(
    name = "scene_object_import_proto",
    actual = "//intrinsic_sdk/intrinsic/scene/proto/v1:scene_object_import_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "scene_object_import_go_proto",
    actual = "//intrinsic_sdk/intrinsic/scene/proto/v1:scene_object_import_go_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

proto_library(
    name = "scene_object_internal_proto",
    srcs = ["scene_object_internal.proto"],
    deps = [
        ":imported_scene_proto",
        ":scene_object_import_proto",
        "@com_google_googleapis//google/longrunning:operations_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_deployment_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:material_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:export_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:geometry_stats_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_proto",
    ],
)

go_proto_library(
    name = "scene_object_internal_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/scene/proto/v1/scene_object_internal_go_proto",
    protos = [":scene_object_internal_proto"],
    deps = [
        ":imported_scene_go_proto",
        ":scene_object_import_go_proto",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/proto:asset_deployment_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_go_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:material_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:export_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:geometry_stats_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_go_proto",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)

py_proto_library(
    name = "scene_object_internal_py_pb2",
    deps = [":scene_object_internal_proto"],
)

py_grpc_library(
    name = "scene_object_internal_py_pb2_grpc",
    srcs = [":scene_object_internal_proto"],
    deps = [":scene_object_internal_py_pb2"],
)

cc_proto_library(
    name = "scene_object_internal_cc_proto",
    deps = [":scene_object_internal_proto"],
)

cc_grpc_library(
    name = "scene_object_internal_cc_grpc_proto",
    srcs = [":scene_object_internal_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":scene_object_internal_cc_proto"],
)

# TODO(b/317911125): Enable scene_object_edit_service in the SDK after the service satisfies user facing requirements.

proto_library(
    name = "scene_object_edit_service_proto",
    srcs = ["scene_object_edit_service.proto"],
    deps = [
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_proto",
    ],
)

cc_proto_library(
    name = "scene_object_edit_service_cc_proto",
    deps = [":scene_object_edit_service_proto"],
)

py_proto_library(
    name = "scene_object_edit_service_py_pb2",
    deps = [":scene_object_edit_service_proto"],
)

go_proto_library(
    name = "scene_object_edit_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/scene/proto/v1/scene_object_edit_service_go_proto",
    protos = [":scene_object_edit_service_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_go_proto",
        "@org_golang_google_protobuf//types/known/emptypb",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)

cc_grpc_library(
    name = "scene_object_edit_service_cc_grpc_proto",
    srcs = [":scene_object_edit_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":scene_object_edit_service_cc_proto"],
)

py_grpc_library(
    name = "scene_object_edit_service_py_pb2_grpc",
    srcs = [":scene_object_edit_service_proto"],
    deps = [":scene_object_edit_service_py_pb2"],
)
