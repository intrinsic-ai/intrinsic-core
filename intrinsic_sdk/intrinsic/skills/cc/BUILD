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

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "client_common",
    hdrs = ["client_common.h"],
    deps = [
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "equipment_pack",
    srcs = ["equipment_pack.cc"],
    hdrs = ["equipment_pack.h"],
    deps = [
        "//intrinsic/skills/proto:skill_service_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
    ],
)

cc_library(
    name = "skill_interface",
    hdrs = ["skill_interface.h"],
    deps = [
        ":execute_context",
        ":execute_request",
        ":get_footprint_context",
        ":get_footprint_request",
        ":preview_context",
        ":preview_request",
        "//intrinsic/skills/proto:skill_service_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

cc_library(
    name = "skill_interface_utils",
    srcs = ["skill_interface_utils.cc"],
    hdrs = ["skill_interface_utils.h"],
    deps = [
        ":equipment_pack",
        ":skill_interface",
        "//intrinsic/skills/internal:execute_context_view",
        "//intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/proto:any",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/proto:object_world_updates_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "get_footprint_context",
    hdrs = ["get_footprint_context.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client",
        "//intrinsic_sdk/intrinsic/world/objects:frame",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:world_object",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

cc_library(
    name = "get_footprint_request",
    hdrs = ["get_footprint_request.h"],
    deps = [
        "//intrinsic/util/proto:any",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "execute_context",
    hdrs = ["execute_context.h"],
    deps = [
        ":equipment_pack",
        ":skill_canceller",
        ":skill_logging_context",
        "//intrinsic/skills/internal:skill_registry_client_interface",
        "//intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
    ],
)

cc_library(
    name = "execute_request",
    hdrs = ["execute_request.h"],
    deps = [
        "//intrinsic/util/proto:any",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "preview_context",
    hdrs = ["preview_context.h"],
    deps = [
        ":equipment_pack",
        ":skill_canceller",
        ":skill_logging_context",
        "//intrinsic/world/proto:object_world_updates_cc_proto",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client",
        "//intrinsic_sdk/intrinsic/world/objects:frame",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:world_object",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
    ],
)

cc_library(
    name = "preview_request",
    hdrs = ["preview_request.h"],
    deps = [
        "//intrinsic/util/proto:any",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "skill_logging_context",
    hdrs = ["skill_logging_context.h"],
    deps = [
        "@abseil-cpp//absl/strings:string_view",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
    ],
)

cc_library(
    name = "skill_canceller",
    srcs = ["skill_canceller.cc"],
    hdrs = ["skill_canceller.h"],
    deps = [
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "skill_utils",
    srcs = ["skill_utils.cc"],
    hdrs = ["skill_utils.h"],
    deps = [
        "//intrinsic/util/grpc:channel",
        "//intrinsic/util/grpc:connection_params",
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
    ],
)

cc_library(
    name = "skill_data",
    srcs = ["skill_data.cc"],
    hdrs = ["skill_data.h"],
    deps = [
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
    ],
)
