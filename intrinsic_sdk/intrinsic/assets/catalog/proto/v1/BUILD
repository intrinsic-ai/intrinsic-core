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

# AssetCatalog protos.

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "asset_catalog_proto",
    srcs = ["asset_catalog.proto"],
    deps = [
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_googleapis//google/api:field_behavior_proto",
        "@com_google_protobuf//:descriptor_proto",
        "@com_google_protobuf//:field_mask_proto",
        "@intrinsic_apis//intrinsic/assets/catalog/proto/v1:release_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:referenced_data_proto",
        "@intrinsic_apis//intrinsic/assets/hardware_devices/proto/v1:hardware_device_manifest_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_tag_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto:view_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:search_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_proto",
    ],
)

cc_proto_library(
    name = "asset_catalog_cc_proto",
    deps = [":asset_catalog_proto"],
)

cc_grpc_library(
    name = "asset_catalog_cc_grpc_proto",
    srcs = [":asset_catalog_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":asset_catalog_cc_proto"],
)

go_proto_library(
    name = "asset_catalog_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
        "//bazel/grpc_gateway:go_gen_grpc_gateway",
    ],
    importpath = "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto",
    protos = [":asset_catalog_proto"],
    deps = [
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/catalog/proto/v1:release_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:referenced_data_go_proto",
        "@intrinsic_apis//intrinsic/assets/hardware_devices/proto/v1:hardware_device_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_tag_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:view_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:search_go_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

py_proto_library(
    name = "asset_catalog_py_pb2",
    deps = [":asset_catalog_proto"],
)

py_grpc_library(
    name = "asset_catalog_py_pb2_grpc",
    srcs = [":asset_catalog_proto"],
    deps = [":asset_catalog_py_pb2"],
)
