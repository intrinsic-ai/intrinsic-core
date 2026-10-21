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
load("//bazel:python.bzl", "py_library")
load("//intrinsic_sdk/intrinsic/skills/build_defs:skill.bzl", "py_skill", "skill_manifest")

# ----------------------------------------------------------------------------
# Skill definition for 'plan_grasp'.

# ----------------------------------------------------------------------------
# Skill definition for 'control_pinch_gripper'.

proto_library(
    name = "control_pinch_gripper_proto",
    srcs = ["control_pinch_gripper.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ],
)

go_proto_library(
    name = "control_pinch_gripper_go_proto",
    importpath = "intrinsic/manipulation/skills/control_pinch_gripper_go_proto",
    protos = [":control_pinch_gripper_proto"],
    visibility = ["//visibility:public"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
    ],
)

py_proto_library(
    name = "control_pinch_gripper_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":control_pinch_gripper_proto"],
)

cc_proto_library(
    name = "control_pinch_gripper_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":control_pinch_gripper_proto"],
)

skill_manifest(
    name = "control_pinch_gripper_manifest",
    src = "control_pinch_gripper_manifest.textproto",
    visibility = ["//visibility:public"],
    deps = [":control_pinch_gripper_proto"],
)

py_library(
    name = "control_pinch_gripper_error_codes",
    srcs = ["control_pinch_gripper_error_codes.py"],
    visibility = ["//visibility:public"],
)

py_library(
    name = "control_pinch_gripper",
    srcs = ["control_pinch_gripper.py"],
    visibility = ["//visibility:public"],
    deps = [
        ":control_pinch_gripper_error_codes",
        ":control_pinch_gripper_py_pb2",
        "//intrinsic_sdk/intrinsic/assets:interface_utils_py",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils_py",
        "//intrinsic_sdk/intrinsic/skills/python:skill_interface",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_py_pb2",
        "//intrinsic_sdk/intrinsic/world/python:object_world_resources",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl/logging",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_py_pb2",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_py_pb2",
    ],
)

py_skill(
    name = "control_pinch_gripper_skill",
    manifest = ":control_pinch_gripper_manifest",
    visibility = ["//visibility:public"],
    deps = [
        ":control_pinch_gripper",
        ":control_pinch_gripper_py_pb2",
    ],
)

# ----------------------------------------------------------------------------
# Skill definition for 'update_relative_transform'.

# ----------------------------------------------------------------------------
# Skill definition for 'get_nodes'.
