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

# A collection of skill packages.
# This package contains a bunch of skill implementations.

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")
load("//bazel:python.bzl", "py_library")
load("//intrinsic_sdk/intrinsic/skills/build_defs:skill.bzl", "cc_skill", "py_skill", "skill_manifest")

# ----------------------------------------------------------------------------
# Skill definition for 'detach_object'

proto_library(
    name = "detach_object_proto",
    srcs = ["detach_object.proto"],
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic/manipulation/skills:__subpackages__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__pkg__",
    ],
    deps = ["@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto"],
)

cc_proto_library(
    name = "detach_object_cc_proto",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic/manipulation/skills:__subpackages__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__pkg__",
    ],
    deps = [":detach_object_proto"],
)

go_proto_library(
    name = "detach_object_go_proto",
    importpath = "intrinsic/skills/apps/detach_object_go_proto",
    protos = [":detach_object_proto"],
    deps = ["@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto"],
)

cc_library(
    name = "detach_object",
    srcs = ["detach_object.cc"],
    hdrs = ["detach_object.h"],
    visibility = [
        "//intrinsic/manipulation/skills:__subpackages__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__pkg__",
        "//incode/motion_planning/skills:__pkg__",
    ],
    deps = [
        ":detach_object_cc_proto",
        "//intrinsic/stats:scoped_span",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface_utils",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/objects:object_entity_filter",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/objects:world_object",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
    alwayslink = 1,
)

skill_manifest(
    name = "detach_object_manifest",
    src = "detach_object_manifest.textproto",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
        "//intrinsic/manipulation/skills:__subpackages__",
    ],
    deps = [":detach_object_proto"],
)

cc_skill(
    name = "detach_object_skill",
    manifest = ":detach_object_manifest",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [
        ":detach_object",
        ":detach_object_cc_proto",
    ],
)

cc_library(
    name = "attach_object_to_robot",
    srcs = ["attach_object_to_robot.cc"],
    hdrs = ["attach_object_to_robot.h"],
    visibility = [
        "//intrinsic/manipulation/skills:__subpackages__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__pkg__",
        "//incode/motion_planning/skills:__pkg__",
    ],
    deps = [
        ":attach_object_to_robot_cc_proto",
        "//intrinsic/stats:scoped_span",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface_utils",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_entity_filter",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:world_object",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_cc_proto",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

proto_library(
    name = "attach_object_to_robot_proto",
    srcs = ["attach_object_to_robot.proto"],
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic/manipulation/skills:__subpackages__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__subpackages__",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ],
)

cc_proto_library(
    name = "attach_object_to_robot_cc_proto",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic/manipulation/skills:__subpackages__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__pkg__",
    ],
    deps = [":attach_object_to_robot_proto"],
)

go_proto_library(
    name = "attach_object_to_robot_go_proto",
    importpath = "intrinsic/skills/apps/attach_object_to_robot_go_proto",
    protos = [":attach_object_to_robot_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
    ],
)

skill_manifest(
    name = "attach_object_to_robot_manifest",
    src = "attach_object_to_robot_manifest.textproto",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
        "//intrinsic/manipulation/skills:__subpackages__",
    ],
    deps = [":attach_object_to_robot_proto"],
)

cc_skill(
    name = "attach_object_to_robot_skill",
    manifest = ":attach_object_to_robot_manifest",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [
        ":attach_object_to_robot",
        ":attach_object_to_robot_cc_proto",
    ],
)

proto_library(
    name = "update_world_proto",
    srcs = ["update_world.proto"],
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__pkg__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_proto",
    ],
)

cc_proto_library(
    name = "update_world_cc_proto",
    visibility = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__pkg__",
        "//incode/motion_planning/skills:__pkg__",
    ],
    deps = [":update_world_proto"],
)

py_proto_library(
    name = "update_world_py_pb2",
    deps = [":update_world_proto"],
)

go_proto_library(
    name = "update_world_go_proto",
    importpath = "intrinsic/skills/apps/update_world_go_proto",
    protos = [":update_world_proto"],
    deps = [
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_go_proto",
    ],
)

skill_manifest(
    name = "update_world_manifest",
    src = "update_world_manifest.textproto",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [":update_world_proto"],
)

py_library(
    name = "update_world",
    srcs = ["update_world.py"],
    deps = [
        ":update_world_py_pb2",
        "//intrinsic_sdk/intrinsic/skills/python:skill_interface",
        "//intrinsic_sdk/intrinsic/util:decorators",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_py_pb2",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_py_pb2",
        "//intrinsic_sdk/intrinsic/world/python:object_world_client",
        "//intrinsic_sdk/intrinsic/world/python:object_world_resources",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_py_pb2",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_py_pb2",
    ],
)

py_skill(
    name = "update_world_skill",
    manifest = ":update_world_manifest",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [
        ":update_world",
        ":update_world_py_pb2",
    ],
)
