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
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/intrinsic/skills/build_defs:skill.bzl", "cc_skill", "skill_manifest")

proto_library(
    name = "homing_proto",
    srcs = ["homing.proto"],
    visibility = [
        "//intrinsic:public_asset_users",
    ],
    deps = [
        "@com_google_protobuf//:duration_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_parameter_metadata_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ],
)

cc_proto_library(
    name = "homing_cc_proto",
    deps = [":homing_proto"],
)

go_proto_library(
    name = "homing_go_proto",
    importpath = "intrinsic/skills/apps/icon2/homing_go_proto",
    protos = [":homing_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/skills/proto:skill_parameter_metadata_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
    ],
)

skill_manifest(
    name = "homing_manifest",
    src = "homing_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [":homing_proto"],
)

cc_library(
    name = "homing",
    srcs = ["homing.cc"],
    hdrs = ["homing.h"],
    deps = [
        ":homing_cc_proto",
        "//intrinsic_control/intrinsic/icon/actions:homing_info",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_control/intrinsic/icon/cc_client:client_utils",
        "//intrinsic_control/intrinsic/icon/cc_client:session",
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_control/intrinsic/icon/utils:arm_utils",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:point_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

cc_skill(
    name = "homing_skill",
    manifest = ":homing_manifest",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [
        ":homing",
        ":homing_cc_proto",
    ],
)
