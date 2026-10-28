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

# Common code for assets.

load("//bazel:go_macros.bzl", "go_library")

go_library(
    name = "pageutils",
    srcs = ["pageutils.go"],
    importpath = "intrinsic/assets/pageutils",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        "//intrinsic/assets/proto:page_token_data_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)

go_library(
    name = "metadatautilsinternal",
    srcs = ["metadatautilsinternal.go"],
    importpath = "intrinsic/assets/metadatautilsinternal",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        "//intrinsic/assets/hardware_devices:graph",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets:interfaceutils",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_go_proto",
        "//intrinsic_sdk/intrinsic/util/proto:descriptor",
        "//intrinsic_sdk/intrinsic/util/proto:names",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

go_library(
    name = "syncutils",
    srcs = ["syncutils.go"],
    importpath = "intrinsic/assets/syncutils",
    visibility = ["//intrinsic:internal_api_users"],
)

go_library(
    name = "orderedmap",
    srcs = ["orderedmap.go"],
    importpath = "intrinsic/assets/orderedmap",
    visibility = ["//intrinsic/assets:__subpackages__"],
)
