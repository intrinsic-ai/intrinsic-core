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

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

proto_library(
    name = "asset_proto",
    srcs = ["asset.proto"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":processed_asset_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:reference_proto",
    ],
)

cc_proto_library(
    name = "asset_cc_proto",
    visibility = ["//intrinsic:public_api_users"],
    deps = [":asset_proto"],
)

py_proto_library(
    name = "asset_py_pb2",
    visibility = ["//intrinsic:public_api_users"],
    deps = [":asset_proto"],
)

go_proto_library(
    name = "asset_go_proto",
    importpath = "intrinsic/assets/proto/v1/asset_go_proto",
    protos = [":asset_proto"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":processed_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:reference_go_proto",
    ],
)

proto_library(
    name = "processed_asset_proto",
    srcs = ["processed_asset.proto"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_proto",
        "@intrinsic_apis//intrinsic/assets/hardware_devices/proto/v1:hardware_device_manifest_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_proto",
    ],
)

cc_proto_library(
    name = "processed_asset_cc_proto",
    visibility = ["//intrinsic:public_api_users"],
    deps = [":processed_asset_proto"],
)

py_proto_library(
    name = "processed_asset_py_pb2",
    visibility = ["//intrinsic:public_api_users"],
    deps = [":processed_asset_proto"],
)

go_proto_library(
    name = "processed_asset_go_proto",
    importpath = "intrinsic/assets/proto/v1/processed_asset_go_proto",
    protos = [":processed_asset_proto"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/hardware_devices/proto/v1:hardware_device_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_go_proto",
    ],
)

proto_library(
    name = "solution_proto",
    srcs = ["solution.proto"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":asset_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_instances_proto",
    ],
)

cc_proto_library(
    name = "solution_cc_proto",
    visibility = ["//intrinsic:public_api_users"],
    deps = [":solution_proto"],
)

py_proto_library(
    name = "solution_py_pb2",
    visibility = ["//intrinsic:public_api_users"],
    deps = [":solution_proto"],
)

go_proto_library(
    name = "solution_go_proto",
    importpath = "intrinsic/assets/proto/v1/solution_go_proto",
    protos = [":solution_proto"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":asset_go_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_instances_go_proto",
    ],
)

proto_library(
    name = "solution_deployment_proto",
    srcs = ["solution_deployment.proto"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":solution_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_googleapis//google/longrunning:operations_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_proto",
    ],
)

go_proto_library(
    name = "solution_deployment_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/assets/proto/v1/solution_deployment_go_proto",
    protos = [":solution_deployment_proto"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":solution_go_proto",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
    ],
)

cc_proto_library(
    name = "solution_deployment_cc_proto",
    visibility = ["//intrinsic:public_api_users"],
    deps = [":solution_deployment_proto"],
)

py_proto_library(
    name = "solution_deployment_py_pb2",
    visibility = ["//intrinsic:public_api_users"],
    deps = [":solution_deployment_proto"],
)
