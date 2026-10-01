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

# This directory should contain code that is used internally to our platform.
# Some files may still be exported as part of the Skills SDK, but they should
# not needed to be included directly when authoring a Skill.

load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_library")
load("//bazel:python.bzl", "py_library")

package(default_visibility = [
    "//intrinsic/apps:__subpackages__",
    "//intrinsic/assets/services:__subpackages__",
    "//intrinsic/executive:__subpackages__",
    "//intrinsic/frontend:__subpackages__",
    "//intrinsic/manipulation:__subpackages__",
    "//intrinsic/resources:__subpackages__",
    "//intrinsic/skills:__subpackages__",
    "//intrinsic/testing:__subpackages__",
    "//intrinsic_control/intrinsic/icon/skills:__subpackages__",
    "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__pkg__",
    "//intrinsic_runtime/intrinsic/proto_tools/registry:__subpackages__",
    "//intrinsic_sdk/intrinsic/executive:__subpackages__",
    "//intrinsic_sdk/intrinsic/skills:__subpackages__",
    "//intrinsic_sdk/intrinsic/tools/inbuild:__subpackages__",
    "//incode/motion_planning/skills:__subpackages__",
])

py_library(
    name = "default_parameters_py",
    srcs = ["default_parameters.py"],
    deps = [
        # TODO(b/319078587) Use relative target name once fixed
        "//intrinsic/skills/internal:error_bindings",
        "@com_google_protobuf//:protobuf_python",
        "@pybind11_abseil//pybind11_abseil:import_status_module",
    ],
)

cc_library(
    name = "equipment_utilities",
    srcs = ["equipment_utilities.cc"],
    hdrs = ["equipment_utilities.h"],
    deps = [
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

cc_library(
    name = "error_utils",
    srcs = ["error_utils.cc"],
    hdrs = ["error_utils.h"],
    deps = [
        ":runtime_data",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/proto:type_url",
        "//intrinsic/util/status:get_extended_status",
        "//intrinsic/util/status:status_conversion_rpc",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/proto:status_spec_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:error_cc_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_cc_proto",
    ],
)

py_library(
    name = "error_utils_py",
    srcs = ["error_utils.py"],
    deps = [
        "//intrinsic/util/grpc:error_handling",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/skills/proto:error_py_pb2",
        "@pybind11_abseil//pybind11_abseil:import_status_module",
    ],
)

py_library(
    name = "error_bindings",
    srcs = ["error_bindings.py"],
    deps = ["@pybind11_abseil//pybind11_abseil:import_status_module"],
)

cc_library(
    name = "conflicts",
    srcs = ["conflicts.cc"],
    hdrs = ["conflicts.h"],
    deps = [
        "//intrinsic/util/grpc",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic/world/proto:object_world_service_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

cc_library(
    name = "skill_service_client_interface",
    hdrs = ["skill_service_client_interface.h"],
    deps = [
        "//intrinsic/skills/proto:skill_service_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
    ],
)

cc_library(
    name = "skill_service_client",
    srcs = ["skill_service_client.cc"],
    hdrs = ["skill_service_client.h"],
    deps = [
        ":error_utils",
        ":skill_service_client_interface",
        "//intrinsic/logging/proto:log_item_cc_proto",
        "//intrinsic/logging/utils:logger_labels",
        "//intrinsic/skills/proto:skill_service_cc_grpc_proto",
        "//intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/stats:tracing_utils",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/grpc",
        "//intrinsic/util/proto:any",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_conversion_rpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/logging:data_logger_client",
        "//intrinsic_sdk/intrinsic/skills/cc:client_common",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:concurrent_queue",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googleapis//google/longrunning:longrunning_cc_proto",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/logging/errors/proto:error_report_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
        "@io_opentelemetry_cpp//api",
    ],
)

cc_library(
    name = "kinematic_object_for_position_part",
    srcs = ["kinematic_object_for_position_part.cc"],
    hdrs = ["kinematic_object_for_position_part.h"],
    deps = [
        "//intrinsic/util/status:status_macros",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
    ],
)

cc_library(
    name = "get_footprint_context_impl",
    srcs = ["get_footprint_context_impl.cc"],
    hdrs = ["get_footprint_context_impl.h"],
    deps = [
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/world/objects:frame",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/objects:world_object",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

py_library(
    name = "get_footprint_context_impl_py",
    srcs = ["get_footprint_context_impl.py"],
    deps = [
        "//intrinsic/geometry/proto:geometry_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client_py",
        "//intrinsic_sdk/intrinsic/skills/python:get_footprint_context",
        "//intrinsic_sdk/intrinsic/world/python:object_world_client",
        "//intrinsic_sdk/intrinsic/world/python:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/python:object_world_resources",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
    ],
)

cc_library(
    name = "execute_context_impl",
    hdrs = ["execute_context_impl.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_canceller",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_logging_context",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "@abseil-cpp//absl/status",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
    ],
)

py_library(
    name = "execute_context_impl_py",
    srcs = ["execute_context_impl.py"],
    deps = [
        "//intrinsic/geometry/proto:geometry_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client_py",
        "//intrinsic_sdk/intrinsic/skills/python:execute_context",
        "//intrinsic_sdk/intrinsic/skills/python:skill_canceller",
        "//intrinsic_sdk/intrinsic/skills/python:skill_logging_context",
        "//intrinsic_sdk/intrinsic/world/python:object_world_client",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
    ],
)

cc_library(
    name = "execute_context_view",
    hdrs = ["execute_context_view.h"],
    visibility = [
        "//intrinsic/apps:__subpackages__",
        "//intrinsic/assets/services:__subpackages__",
        "//intrinsic/executive:__subpackages__",
        "//intrinsic/frontend:__subpackages__",
        "//intrinsic/manipulation:__subpackages__",
        "//intrinsic/resources:__subpackages__",
        "//intrinsic/skills:__subpackages__",
        "//intrinsic/testing:__subpackages__",
        "//intrinsic_control/intrinsic/icon/skills:__subpackages__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__pkg__",
        "//intrinsic_runtime/intrinsic/proto_tools/registry:__subpackages__",
        "//intrinsic_sdk/intrinsic/skills/cc:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inbuild:__subpackages__",
        "//incode/motion_planning/skills:__subpackages__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_canceller",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_logging_context",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
    ],
)

cc_library(
    name = "preview_context_impl",
    srcs = ["preview_context_impl.cc"],
    hdrs = ["preview_context_impl.h"],
    visibility = [
        "//intrinsic/apps:__subpackages__",
        "//intrinsic/assets/services:__subpackages__",
        "//intrinsic/executive:__subpackages__",
        "//intrinsic/frontend:__subpackages__",
        "//intrinsic/manipulation:__subpackages__",
        "//intrinsic/resources:__subpackages__",
        "//intrinsic/skills:__subpackages__",
        "//intrinsic/testing:__subpackages__",
        "//intrinsic_control/intrinsic/icon/skills:__subpackages__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__pkg__",
        "//intrinsic_runtime/intrinsic/proto_tools/registry:__subpackages__",
        "//intrinsic_sdk/intrinsic/skills/cc:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inbuild:__subpackages__",
        "//incode/motion_planning/skills:__subpackages__",
    ],
    deps = [
        "//intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/proto:object_world_updates_cc_proto",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:preview_context",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_canceller",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_logging_context",
        "//intrinsic_sdk/intrinsic/world/objects:frame",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/objects:world_object",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
    ],
)

py_library(
    name = "preview_context_impl_py",
    srcs = ["preview_context_impl.py"],
    deps = [
        "//intrinsic/geometry/proto:geometry_service_py_pb2_grpc",
        "//intrinsic/world/proto:object_world_updates_py_pb2",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client_py",
        "//intrinsic_sdk/intrinsic/skills/python:preview_context",
        "//intrinsic_sdk/intrinsic/skills/python:skill_canceller",
        "//intrinsic_sdk/intrinsic/skills/python:skill_logging_context",
        "//intrinsic_sdk/intrinsic/world/python:object_world_client",
        "//intrinsic_sdk/intrinsic/world/python:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/python:object_world_resources",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
    ],
)

py_library(
    name = "skill_service_impl_py",
    srcs = ["skill_service_impl.py"],
    deps = [
        ":default_parameters_py",
        ":error_bindings",
        ":error_utils_py",
        ":execute_context_impl_py",
        ":get_footprint_context_impl_py",
        ":preview_context_impl_py",
        ":runtime_data_py",
        ":skill_repository_py",
        "//intrinsic/assets:id_utils_py",
        "//intrinsic/geometry/proto:geometry_service_py_pb2_grpc",
        "//intrinsic/skills/proto:skill_service_py_pb2",
        "//intrinsic/skills/proto:skill_service_py_pb2_grpc",
        "//intrinsic/util/status:status_exception",
        "//intrinsic/world/proto:object_world_service_py_pb2_grpc",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client_py",
        "//intrinsic_sdk/intrinsic/skills/python:proto_utils",
        "//intrinsic_sdk/intrinsic/skills/python:skill_canceller",
        "//intrinsic_sdk/intrinsic/skills/python:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/python:skill_logging_context",
        "//intrinsic_sdk/intrinsic/world/python:object_world_client",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl/logging",
        "@com_google_googleapis//google/longrunning:operations_py_proto",
        "@com_google_googleapis//google/rpc:status_py_proto",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/logging/proto:context_py_pb2",
        "@intrinsic_apis//intrinsic/skills/proto:error_py_pb2",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_py_pb2",
        "@intrinsic_apis//intrinsic/skills/proto:skills_py_pb2",
        "@intrinsic_apis//intrinsic/util/status:extended_status_py_pb2",
        "@pybind11_abseil//pybind11_abseil:import_status_module",
    ],
)

py_library(
    name = "proto_utils",
    srcs = ["proto_utils.py"],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//intrinsic/assets:id_utils_py",
        "//intrinsic/util/proto:source_code_info_view_py",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_py_pb2",
        "@intrinsic_apis//intrinsic/skills/proto:skills_py_pb2",
    ],
)

cc_library(
    name = "skill_repository",
    hdrs = ["skill_repository.h"],
    deps = [
        ":runtime_data",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

py_library(
    name = "skill_repository_py",
    srcs = ["skill_repository.py"],
    deps = [
        ":runtime_data_py",
        "//intrinsic_sdk/intrinsic/skills/python:skill_interface",
    ],
)

cc_library(
    name = "runtime_data",
    srcs = ["runtime_data.cc"],
    hdrs = ["runtime_data.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//intrinsic/util:proto_time",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/skills/cc:client_common",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/proto:status_spec_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_service_config_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
    ],
)

py_library(
    name = "runtime_data_py",
    srcs = ["runtime_data.py"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/proto:status_spec_py_pb2",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_py_pb2",
        "@intrinsic_apis//intrinsic/skills/proto:skill_service_config_py_pb2",
        "@intrinsic_apis//intrinsic/skills/proto:skills_py_pb2",
    ],
)

cc_library(
    name = "single_skill_factory",
    srcs = ["single_skill_factory.cc"],
    hdrs = ["single_skill_factory.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":runtime_data",
        ":skill_repository",
        "//intrinsic/assets:id_utils",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
    ],
)

py_library(
    name = "single_skill_factory_py",
    srcs = ["single_skill_factory.py"],
    visibility = ["//visibility:public"],
    deps = [
        ":runtime_data_py",
        ":skill_repository_py",
        "//intrinsic/assets:id_utils_py",
        "//intrinsic/util:decorators",
        "//intrinsic_sdk/intrinsic/skills/python:skill_interface",
    ],
)

cc_library(
    name = "skill_init",
    srcs = ["skill_init.cc"],
    hdrs = ["skill_init.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":skill_registry_client",
        ":skill_repository",
        ":skill_service_impl",
        "//intrinsic/geometry/proto:geometry_service_cc_grpc_proto",
        "//intrinsic/geometry/storage:geometry_service_storage",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/logging:data_logger_client",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/skills/proto:skill_service_config_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
    ],
)

py_library(
    name = "skill_init_py",
    srcs = ["skill_init.py"],
    visibility = ["//visibility:public"],
    deps = [
        ":skill_repository_py",
        ":skill_service_impl_py",
        "//intrinsic/geometry/proto:geometry_service_py_pb2_grpc",
        "//intrinsic/skills/proto:skill_service_py_pb2_grpc",
        "//intrinsic/world/proto:object_world_service_py_pb2_grpc",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_py_pb2_grpc",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl/logging",
        "@intrinsic_apis//intrinsic/skills/proto:skill_service_config_py_pb2",
    ],
)

cc_library(
    name = "skill_service_config_utils",
    srcs = ["skill_service_config_utils.cc"],
    hdrs = ["skill_service_config_utils.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":skill_proto_utils",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_control/intrinsic/icon/release:file_helpers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_service_config_cc_proto",
    ],
)

py_library(
    name = "skill_service_config_utils_py",
    srcs = ["skill_service_config_utils.py"],
    visibility = ["//visibility:public"],
    deps = [
        ":proto_utils",
        "@com_google_absl_py//absl/logging",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_py_pb2",
        "@intrinsic_apis//intrinsic/skills/proto:skill_service_config_py_pb2",
    ],
)

cc_library(
    name = "skill_service_impl",
    srcs = ["skill_service_impl.cc"],
    hdrs = ["skill_service_impl.h"],
    deps = [
        ":equipment_utilities",
        ":error_utils",
        ":execute_context_impl",
        ":get_footprint_context_impl",
        ":preview_context_impl",
        ":runtime_data",
        ":skill_registry_client_interface",
        ":skill_repository",
        "//intrinsic/assets:id_utils",
        "//intrinsic/skills/proto:skill_service_cc_grpc_proto",
        "//intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_conversion_rpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_macros_grpc",
        "//intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_canceller",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_logging_context",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googleapis//google/longrunning:longrunning_cc_proto",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_cc_proto",
        "@io_opentelemetry_cpp//api",
    ],
)

cc_library(
    name = "skill_registry_client",
    srcs = ["skill_registry_client.cc"],
    hdrs = ["skill_registry_client.h"],
    deps = [
        ":skill_registry_client_interface",
        "//intrinsic/skills/internal/proto:behavior_tree_registry_internal_cc_grpc_proto",
        "//intrinsic/skills/internal/proto:behavior_tree_registry_internal_cc_proto",
        "//intrinsic/skills/internal/proto:skill_registry_internal_cc_grpc_proto",
        "//intrinsic/skills/internal/proto:skill_registry_internal_cc_proto",
        "//intrinsic/skills/proto:behavior_tree_registry_cc_grpc_proto",
        "//intrinsic/skills/proto:behavior_tree_registry_cc_proto",
        "//intrinsic/skills/proto:skill_registry_config_cc_proto",
        "//intrinsic/util/status:annotate",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/skills/cc:client_common",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_registry_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_registry_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
    ],
)

cc_library(
    name = "skill_registry_client_interface",
    hdrs = ["skill_registry_client_interface.h"],
    visibility = [
        "//intrinsic/apps:__subpackages__",
        "//intrinsic/assets/services:__subpackages__",
        "//intrinsic/executive:__subpackages__",
        "//intrinsic/frontend:__subpackages__",
        "//intrinsic/manipulation:__subpackages__",
        "//intrinsic/resources:__subpackages__",
        "//intrinsic/skills:__subpackages__",
        "//intrinsic/testing:__subpackages__",
        "//intrinsic_control/intrinsic/icon/skills:__subpackages__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__pkg__",
        "//intrinsic_runtime/intrinsic/proto_tools/registry:__subpackages__",
        "//intrinsic_sdk/intrinsic/skills/cc:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inbuild:__subpackages__",
        "//incode/motion_planning/skills:__subpackages__",
    ],
    deps = [
        "//intrinsic/skills/proto:skill_registry_config_cc_proto",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
    ],
)

cc_library(
    name = "skill_proto_utils",
    srcs = ["skill_proto_utils.cc"],
    hdrs = ["skill_proto_utils.h"],
    deps = [
        "//intrinsic/util/proto:source_code_info_view",
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@com_googlesource_code_re2//:re2",
        "@intrinsic_apis//intrinsic/assets/proto:documentation_cc_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
    ],
)

cc_library(
    name = "world_service_utils",
    srcs = ["world_service_utils.cc"],
    hdrs = ["world_service_utils.h"],
    # Upload/download of full world protos should not be used widely.
    visibility = [
        "//intrinsic/skills/apps/icon2:__pkg__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:__pkg__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service/debug:__pkg__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service/nonvolatile_cache:__pkg__",
    ],
    deps = [
        "//intrinsic/util/grpc",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world",
        "//intrinsic/world:cc_proto",
        "//intrinsic/world/conversion:world_from_object_proto",
        "//intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic/world/proto:object_world_service_cc_proto",
        "//intrinsic/world/service:world_service_cc_grpc_proto",
        "//intrinsic/world/service:world_service_cc_proto",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_deserializer",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
    ],
)

go_library(
    name = "configmapwatcher",
    srcs = ["configmapwatcher.go"],
    importpath = "intrinsic/skills/internal/configmapwatcher",
    deps = [
        "@io_k8s_api//core/v1:go_default_library",
        "@io_k8s_apimachinery//pkg/apis/meta/v1:go_default_library",
        "@io_k8s_apimachinery//pkg/fields:go_default_library",
        "@io_k8s_apimachinery//pkg/labels:go_default_library",
        "@io_k8s_client_go//informers:go_default_library",
        "@io_k8s_client_go//kubernetes:go_default_library",
        "@io_k8s_client_go//tools/cache:go_default_library",
    ],
)

go_library(
    name = "skillregistryservice",
    srcs = ["skillregistryservice.go"],
    importpath = "intrinsic/skills/internal/skillregistryservice",
    deps = [
        ":skillcomposer",
        ":skillfilter",
        ":skillruntime",
        "//intrinsic/assets:idutils",
        "//intrinsic/skills/internal/proto:behavior_tree_registry_internal_go_proto",
        "//intrinsic/skills/internal/proto:skill_registry_internal_go_proto",
        "//intrinsic/skills/proto:behavior_tree_registry_go_proto",
        "//intrinsic/skills/proto:skill_registry_config_go_proto",
        "//intrinsic/skills/proto:skill_runtime_go_proto",
        "//intrinsic/skills/proto:skill_service_go_proto",
        "//intrinsic/util/pagination:pagetoken",
        "//intrinsic/util/proto:descriptorcompatibility",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pborman_uuid//:go_default_library",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_registry_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)

go_library(
    name = "skillinfo",
    srcs = ["skillinfo.go"],
    importpath = "intrinsic/skills/internal/skillinfo",
    deps = [
        "//intrinsic/skills/proto:skill_service_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)

go_library(
    name = "skillfilter",
    srcs = ["skillfilter.go"],
    importpath = "intrinsic/skills/internal/skillfilter",
    deps = [
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
        "@org_golang_google_protobuf//proto",
    ],
)

go_library(
    name = "skillcomposer",
    srcs = ["skillcomposer.go"],
    importpath = "intrinsic/skills/internal/skillcomposer",
    visibility = [
        "//intrinsic/assets/internal:__pkg__",
        "//intrinsic/resources/service:__pkg__",
        "//intrinsic/skills/tools:__pkg__",
    ],
    deps = [
        ":configmapwatcher",
        ":skillinfo",
        "//intrinsic/assets:idutils",
        "//intrinsic/assets:syncutils",
        "//intrinsic/assets/proto:installation_origin_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic/resources/service:resourcetyperuntime",
        "//intrinsic/util/proto:sourcecodeinfoview",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
        "@io_k8s_api//core/v1:go_default_library",
        "@io_k8s_client_go//informers:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/descriptorpb",
    ],
)

go_library(
    name = "skillruntime",
    srcs = ["skillruntime.go"],
    importpath = "intrinsic/skills/internal/skillruntime",
    visibility = [
        "//intrinsic/kubernetes/workcell_spec:__subpackages__",
        "//intrinsic/skills/tools:__subpackages__",
    ],
    deps = [
        "//intrinsic/resources/proto:runtime_db_go_proto",
        "//intrinsic/skills/proto:skill_runtime_go_proto",
        "//intrinsic/util/grpc:statusutil",
        "@org_golang_google_protobuf//proto",
    ],
)
