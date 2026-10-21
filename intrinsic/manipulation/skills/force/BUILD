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

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "move_to_contact_proto",
    srcs = ["move_to_contact.proto"],
    deps = [
        ":contact_stiffness_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ],
)

cc_proto_library(
    name = "move_to_contact_cc_proto",
    deps = [":move_to_contact_proto"],
)

py_proto_library(
    name = "move_to_contact_proto_py_pb2",
    deps = [":move_to_contact_proto"],
)

proto_library(
    name = "contact_stiffness_proto",
    srcs = ["contact_stiffness.proto"],
)

py_proto_library(
    name = "contact_stiffness_py_pb2",
    deps = [":contact_stiffness_proto"],
)

cc_proto_library(
    name = "contact_stiffness_cc_proto",
    deps = [":contact_stiffness_proto"],
)

go_proto_library(
    name = "contact_stiffness_go_proto",
    importpath = "intrinsic/manipulation/skills/force/contact_stiffness_go_proto",
    protos = [":contact_stiffness_proto"],
)

skill_manifest(
    name = "move_to_contact_manifest",
    src = "move_to_contact_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    deps = [":move_to_contact_proto"],
)

py_skill(
    name = "move_to_contact_skill",
    manifest = "move_to_contact_manifest",
    deps = [
        ":move_to_contact",
        ":move_to_contact_proto_py_pb2",
    ],
)

py_library(
    name = "move_to_contact",
    srcs = ["move_to_contact.py"],
    deps = [
        ":contact_stiffness_py_pb2",
        ":move_to_contact_proto_py_pb2",
        ":utils",
        "//intrinsic_control/intrinsic/icon/actions:force_primitive_utils",
        "//intrinsic_control/intrinsic/icon/actions:stop_utils",
        "//intrinsic_control/intrinsic/icon/control/primitives/force_control/proto:controller_params_py_pb2",
        "//intrinsic_control/intrinsic/icon/control/primitives/force_control/proto:force_primitives_py_pb2",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils_py",
        "//intrinsic_control/intrinsic/icon/python:cancelation",
        "//intrinsic_control/intrinsic/icon/python:create_action_utils",
        "//intrinsic_control/intrinsic/icon/python:icon",
        "//intrinsic_control/intrinsic/icon/python:icon_logging",
        "//intrinsic_sdk/intrinsic/logging/proto:log_item_py_pb2",
        "//intrinsic_sdk/intrinsic/math/python:data_types",
        "//intrinsic_sdk/intrinsic/math/python:proto_conversion",
        "//intrinsic_sdk/intrinsic/platform/pubsub/python:pubsub",
        "//intrinsic_sdk/intrinsic/skills/python:skill_interface",
        "//intrinsic_sdk/intrinsic/util:decorators",
        "//intrinsic_sdk/intrinsic/world/python:object_world_client",
        "//intrinsic_sdk/intrinsic/world/python:object_world_resources",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_py_pb2",
    ],
)

py_library(
    name = "utils",
    srcs = ["utils.py"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control/algorithms/python:directional_stiffness",
        "//intrinsic_control/intrinsic/icon/control/algorithms/python:overdamping",
        "//intrinsic_sdk/intrinsic/math/python:data_types",
        "//intrinsic_sdk/intrinsic/math/python:proto_conversion",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@intrinsic_apis//intrinsic/icon/equipment:force_control_settings_py_pb2",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_py_pb2",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_py_pb2",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
    ],
)
