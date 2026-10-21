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

load("//bazel:go_macros.bzl", "go_library")

go_library(
    name = "runtime",
    srcs = [
        "runtime.go",
    ],
    importpath = "intrinsic/assets/conversion/runtime",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":metadata",
        "//intrinsic/assets:metadatautilsinternal",
        "//intrinsic/assets/catalog/proto/v1:asset_catalog_internal_go_proto",
        "//intrinsic/assets/hardware_devices:graph",
        "//intrinsic/assets/proto:installation_origin_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets/data:datavalidate",
        "//intrinsic_sdk/intrinsic/assets/processes:processmanifest",
        "//intrinsic_sdk/intrinsic/assets/processes:processvalidate",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_go_proto",
        "//intrinsic_sdk/intrinsic/assets/proto/v1:asset_go_proto",
        "//intrinsic_sdk/intrinsic/assets/proto/v1:processed_asset_go_proto",
        "//intrinsic_sdk/intrinsic/scene/validate/go:sceneobjectvalidation",
        "//intrinsic_sdk/intrinsic/util/go:validate",
        "//intrinsic_sdk/intrinsic/util/status:extstatus",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/hardware_devices/proto/v1:hardware_device_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_tag_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_graph_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:reference_go_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:env_var_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_permissions_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_volume_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:image_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_permissions_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_service_definition_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_volume_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_x_exp//maps:go_default_library",
    ],
)

go_library(
    name = "applicationasset",
    srcs = [
        "application_asset.go",
    ],
    importpath = "intrinsic/assets/conversion/applicationasset",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic_sdk/intrinsic/assets/catalog/proto/v1:asset_catalog_go_proto",
        "//intrinsic_sdk/intrinsic/assets/proto/v1:asset_go_proto",
        "//intrinsic_sdk/intrinsic/assets/proto/v1:processed_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:reference_go_proto",
    ],
)

go_library(
    name = "solutionconversion",
    srcs = [
        "solution_conversion.go",
    ],
    importpath = "intrinsic/assets/conversion/solutionconversion",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":applicationasset",
        ":runtime",
        "//intrinsic/assets/instances:instanceconversion",
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic/config/proto:common_go_proto",
        "//intrinsic/config/proto:process_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets/proto/v1:asset_go_proto",
        "//intrinsic_sdk/intrinsic/assets/proto/v1:solution_deployment_go_proto",
        "//intrinsic_sdk/intrinsic/assets/proto/v1:solution_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_instances_go_proto",
    ],
)

go_library(
    name = "metadata",
    srcs = [
        "metadata.go",
    ],
    importpath = "intrinsic/assets/conversion/metadata",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic_sdk/intrinsic/assets/proto/v1:asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/hardware_devices/proto/v1:hardware_device_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_tag_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_go_proto",
    ],
)
