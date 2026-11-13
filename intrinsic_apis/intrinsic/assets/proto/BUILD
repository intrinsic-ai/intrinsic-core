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
    name = "id_proto",
    srcs = ["id.proto"],
)

cc_proto_library(
    name = "id_cc_proto",
    deps = [":id_proto"],
)

go_proto_library(
    name = "id_go_proto",
    importpath = "intrinsic/assets/proto/id_go_proto",
    protos = [":id_proto"],
)

py_proto_library(
    name = "id_py_pb2",
    deps = [":id_proto"],
)

proto_library(
    name = "asset_deployment_proto",
    srcs = ["asset_deployment.proto"],
    deps = [
        ":id_proto",
        "@com_google_googleapis//google/longrunning:operations_proto",
        "@com_google_protobuf//:any_proto",
        "@intrinsic_apis//intrinsic/config/proto:data_files_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ],
)

cc_proto_library(
    name = "asset_deployment_cc_proto",
    deps = [":asset_deployment_proto"],
)

cc_grpc_library(
    name = "asset_deployment_cc_grpc_proto",
    srcs = [":asset_deployment_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":asset_deployment_cc_proto"],
)

go_proto_library(
    name = "asset_deployment_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/assets/proto/asset_deployment_go_proto",
    protos = [":asset_deployment_proto"],
    deps = [
        ":id_go_proto",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/config/proto:data_files_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

py_proto_library(
    name = "asset_deployment_py_pb2",
    deps = [":asset_deployment_proto"],
)

py_grpc_library(
    name = "asset_deployment_pb2_grpc",
    srcs = [":asset_deployment_proto"],
    deps = [":asset_deployment_py_pb2"],
)

proto_library(
    name = "asset_type_proto",
    srcs = ["asset_type.proto"],
)

cc_proto_library(
    name = "asset_type_cc_proto",
    deps = [":asset_type_proto"],
)

go_proto_library(
    name = "asset_type_go_proto",
    importpath = "intrinsic/assets/proto/asset_type_go_proto",
    protos = [":asset_type_proto"],
)

py_proto_library(
    name = "asset_type_py_pb2",
    deps = [":asset_type_proto"],
)

proto_library(
    name = "asset_tag_proto",
    srcs = ["asset_tag.proto"],
    deps = [":asset_type_proto"],
)

cc_proto_library(
    name = "asset_tag_cc_proto",
    deps = [":asset_tag_proto"],
)

go_proto_library(
    name = "asset_tag_go_proto",
    importpath = "intrinsic/assets/proto/asset_tag_go_proto",
    protos = [":asset_tag_proto"],
    deps = [
        ":asset_type_go_proto",
    ],
)

py_proto_library(
    name = "asset_tag_py_pb2",
    deps = [":asset_tag_proto"],
)

proto_library(
    name = "documentation_proto",
    srcs = ["documentation.proto"],
)

cc_proto_library(
    name = "documentation_cc_proto",
    deps = [":documentation_proto"],
)

go_proto_library(
    name = "documentation_go_proto",
    importpath = "intrinsic/assets/proto/documentation_go_proto",
    protos = [":documentation_proto"],
)

py_proto_library(
    name = "documentation_py_pb2",
    deps = [":documentation_proto"],
)

proto_library(
    name = "field_metadata_proto",
    srcs = ["field_metadata.proto"],
    deps = [
        "@com_google_protobuf//:descriptor_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:dependency_proto",
    ],
)

cc_proto_library(
    name = "field_metadata_cc_proto",
    deps = [":field_metadata_proto"],
)

go_proto_library(
    name = "field_metadata_go_proto",
    importpath = "intrinsic/assets/proto/field_metadata_go_proto",
    protos = [":field_metadata_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto/v1:dependency_go_proto",
    ],
)

py_proto_library(
    name = "field_metadata_py_pb2",
    deps = [":field_metadata_proto"],
)

proto_library(
    name = "installed_assets_proto",
    srcs = ["installed_assets.proto"],
    deps = [
        ":asset_tag_proto",
        ":asset_type_proto",
        ":id_proto",
        ":metadata_proto",
        ":view_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_googleapis//google/api:field_behavior_proto",
        "@com_google_googleapis//google/longrunning:operations_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_proto",
        "@intrinsic_apis//intrinsic/assets/hardware_devices/proto/v1:hardware_device_manifest_proto",
        "@intrinsic_apis//intrinsic/assets/processes/proto:process_asset_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:reference_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:search_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:solution_validation_policy_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_inspection_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_proto",
    ],
)

cc_proto_library(
    name = "installed_assets_cc_proto",
    deps = [":installed_assets_proto"],
)

cc_grpc_library(
    name = "installed_assets_cc_grpc_proto",
    srcs = [":installed_assets_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":installed_assets_cc_proto"],
)

go_proto_library(
    name = "installed_assets_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/assets/proto/installed_assets_go_proto",
    protos = [":installed_assets_proto"],
    deps = [
        ":asset_tag_go_proto",
        ":asset_type_go_proto",
        ":id_go_proto",
        ":metadata_go_proto",
        ":view_go_proto",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/hardware_devices/proto/v1:hardware_device_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/processes/proto:process_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:reference_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:search_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:solution_validation_policy_go_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_inspection_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
    ],
)

py_proto_library(
    name = "installed_assets_py_pb2",
    deps = [":installed_assets_proto"],
)

py_grpc_library(
    name = "installed_assets_pb2_grpc",
    srcs = [":installed_assets_proto"],
    deps = [":installed_assets_py_pb2"],
)

proto_library(
    name = "vendor_proto",
    srcs = ["vendor.proto"],
)

cc_proto_library(
    name = "vendor_cc_proto",
    deps = [":vendor_proto"],
)

go_proto_library(
    name = "vendor_go_proto",
    importpath = "intrinsic/assets/proto/vendor_go_proto",
    protos = [":vendor_proto"],
)

py_proto_library(
    name = "vendor_py_pb2",
    deps = [":vendor_proto"],
)

proto_library(
    name = "metadata_proto",
    srcs = ["metadata.proto"],
    deps = [
        ":asset_tag_proto",
        ":asset_type_proto",
        ":documentation_proto",
        ":id_proto",
        ":vendor_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_googleapis//google/api:field_behavior_proto",
        "@com_google_protobuf//:descriptor_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

cc_proto_library(
    name = "metadata_cc_proto",
    deps = [":metadata_proto"],
)

go_proto_library(
    name = "metadata_go_proto",
    importpath = "intrinsic/assets/proto/metadata_go_proto",
    protos = [":metadata_proto"],
    deps = [
        ":asset_tag_go_proto",
        ":asset_type_go_proto",
        ":documentation_go_proto",
        ":id_go_proto",
        ":release_tag_go_proto",
        ":vendor_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
    ],
)

py_proto_library(
    name = "metadata_py_pb2",
    deps = [":metadata_proto"],
)

proto_library(
    name = "release_tag_proto",
    srcs = ["release_tag.proto"],
)

go_proto_library(
    name = "release_tag_go_proto",
    importpath = "intrinsic/assets/proto/release_tag_go_proto",
    protos = [":release_tag_proto"],
)

proto_library(
    name = "status_spec_proto",
    srcs = ["status_spec.proto"],
)

cc_proto_library(
    name = "status_spec_cc_proto",
    deps = [":status_spec_proto"],
)

go_proto_library(
    name = "status_spec_go_proto",
    importpath = "intrinsic/assets/proto/status_spec_go_proto",
    protos = [":status_spec_proto"],
)

py_proto_library(
    name = "status_spec_py_pb2",
    deps = [":status_spec_proto"],
)

proto_library(
    name = "view_proto",
    srcs = ["view.proto"],
)

cc_proto_library(
    name = "view_cc_proto",
    deps = [":view_proto"],
)

go_proto_library(
    name = "view_go_proto",
    importpath = "intrinsic/assets/proto/view_go_proto",
    protos = [":view_proto"],
)

py_proto_library(
    name = "view_py_pb2",
    deps = [":view_proto"],
)
