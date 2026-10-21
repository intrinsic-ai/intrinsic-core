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
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic/skills/build_defs:skill.bzl", "cc_skill", "skill_manifest")

proto_library(
    name = "create_object_proto",
    srcs = ["create_object.proto"],
    visibility = ["//intrinsic:public_asset_users"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ],
)

cc_proto_library(
    name = "create_object_cc_proto",
    visibility = [
        "//intrinsic:public_asset_users",
    ],
    deps = [
        ":create_object_proto",
    ],
)

skill_manifest(
    name = "create_object_cc_manifest",
    src = "create_object.manifest.textproto",
    visibility = [
        "//intrinsic:public_asset_users",
    ],
    deps = [
        ":create_object_proto",
    ],
)

cc_library(
    name = "create_object",
    srcs = ["create_object.cc"],
    hdrs = ["create_object.h"],
    deps = [
        ":create_object_cc_proto",
        "//intrinsic/scene/util:scene_object_from_asset",
        "//intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic/util/grpc:channel",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_macros_grpc",
        "//intrinsic/world/proto:object_world_updates_cc_proto",
        "//intrinsic_sdk/intrinsic/assets:interface_utils",
        "//intrinsic_sdk/intrinsic/assets/catalog/proto/v1:asset_catalog_cc_proto",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils_cc",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_cc_proto",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/skills/cc:execute_context",
        "//intrinsic_sdk/intrinsic/skills/cc:execute_request",
        "//intrinsic_sdk/intrinsic/skills/cc:get_footprint_context",
        "//intrinsic_sdk/intrinsic/skills/cc:get_footprint_request",
        "//intrinsic_sdk/intrinsic/skills/cc:preview_context",
        "//intrinsic_sdk/intrinsic/skills/cc:preview_request",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/objects:world_object",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/proto:view_cc_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:transformed_geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:entity_cc_proto",
        "@intrinsic_apis//intrinsic/simulation/resources:spawner_server_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/simulation/resources:spawner_server_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_skill(
    name = "create_object_skill",
    manifest = ":create_object_cc_manifest",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic/simulation/tests:__subpackages__",
        "//visibility:public",
    ],
    deps = [
        ":create_object",
        ":create_object_cc_proto",
    ],
)
