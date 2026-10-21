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

# For libraries and services covering the instantiation of assets.  Initial
# design was detailed in go/intrinsic-asset-instances.

load("//bazel:go_macros.bzl", "go_library")

go_library(
    name = "assetinstancesservice",
    srcs = ["asset_instances_service.go"],
    importpath = "intrinsic/assets/instances/assetinstancesservice",
    visibility = ["//intrinsic/kubernetes/workcell_spec:__pkg__"],
    deps = [
        ":instanceconversion",
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic/resources/proto:resource_instance_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic/resources/service:resourceregistryutil",
        "//intrinsic/resources/service:resourcetyperuntime",
        "//intrinsic/storage/hot_shared_state/proto:application_service_go_proto",
        "//intrinsic/util/pagination:pagetoken",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "@intrinsic_apis//intrinsic/assets/proto:asset_tag_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_instances_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:dependency_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto:go_default_library",
    ],
)

go_library(
    name = "instanceconversion",
    srcs = ["instance_conversion.go"],
    importpath = "intrinsic/assets/instances/instanceconversion",
    visibility = ["//visibility:public"],
    deps = [
        "//intrinsic/assets/deploy:render",
        "//intrinsic/assets/services:inspection",
        "//intrinsic/resources/proto:resource_instance_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_instances_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:grpc_connection_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:instance_config_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_service_definition_go_proto",
    ],
)
