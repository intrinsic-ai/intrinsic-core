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
load("//bazel:cc_macros.bzl", "cc_binary", "cc_library")
load("//bazel:cc_oci_image.bzl", "cc_oci_image")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "simulator",
    hdrs = ["simulator.h"],
    deps = [
        "//intrinsic/simulation/service/proto/first_party:simulation_service_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

cc_library(
    name = "simulator_v1",
    srcs = ["simulator_v1.cc"],
    hdrs = ["simulator_v1.h"],
    deps = [
        ":simulator",
        "//intrinsic/simulation/service/proto:conversions",
        "//intrinsic/simulation/simulator/proto/v1:simulator_control_service_cc_grpc_proto",
        "//intrinsic/simulation/simulator/proto/v1:simulator_control_service_cc_proto",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
    ],
)

cc_library(
    name = "world_visualizer",
    srcs = ["world_visualizer.cc"],
    hdrs = ["world_visualizer.h"],
    deps = [
        "//intrinsic/simulation/world:world_interpolation",
        "//intrinsic/simulation/world:world_updater",
        "//intrinsic/world/proto:world_updates_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/objects:object_entity_filter",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_cc_proto",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/world/proto:entity_search_cc_proto",
    ],
)

cc_library(
    name = "resource_registry_utils",
    srcs = ["resource_registry_utils.cc"],
    hdrs = ["resource_registry_utils.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/resources/client:resource_registry_client_cc",
        "//intrinsic_sdk/intrinsic/resources/client:resource_registry_client_interface",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "simulator_world_manager",
    srcs = ["simulator_world_manager.cc"],
    hdrs = ["simulator_world_manager.h"],
    deps = [
        "//intrinsic/world/service/updater:world_updater_cc_grpc_proto",
        "//intrinsic/world/service/updater:world_updater_cc_proto",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "simulation_runtime",
    srcs = ["simulation_runtime.cc"],
    hdrs = ["simulation_runtime.h"],
    deps = [
        ":resource_registry_utils",
        ":simulator",
        ":simulator_world_manager",
        ":world_visualizer",
        "//intrinsic/simulation/service/proto/first_party:simulation_service_cc_proto",
        "//intrinsic/simulation/world:world_service_updater",
        "//intrinsic/world/service/objects:object_world_updates_utils",
        "//intrinsic/world/service/updater:world_updater_cc_grpc_proto",
        "//intrinsic/world/service/updater:world_updater_cc_proto",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_control/intrinsic/icon/cc_client:operational_status",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_sdk/intrinsic/resources/client:resource_registry_client_interface",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/simulation/service/proto/v1:simulator_world_sync_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/simulation/service/proto/v1:simulator_world_sync_cc_proto",
    ],
)

proto_library(
    name = "runtime_state_proto",
    srcs = ["runtime_state.proto"],
)

cc_proto_library(
    name = "runtime_state_cc_proto",
    deps = [
        ":runtime_state_proto",
    ],
)

cc_library(
    name = "simulation_service_impl",
    srcs = ["simulation_service_impl.cc"],
    hdrs = ["simulation_service_impl.h"],
    deps = [
        ":runtime_state_cc_proto",
        ":simulation_runtime",
        "//intrinsic/config/proto:application_cc_proto",
        "//intrinsic/simulation/service/proto/first_party:simulation_service_cc_proto",
        "//intrinsic/storage/hot_shared_state/proto:application_service_cc_grpc_proto",
        "//intrinsic/storage/hot_shared_state/proto:application_service_cc_proto",
        "//intrinsic/world/service/updater:world_updater_cc_grpc_proto",
        "//intrinsic/world/service/updater:world_updater_cc_proto",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/resources/client:resource_registry_client_interface",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_cc_proto",
        "@intrinsic_apis//intrinsic/simulation/service/proto/v1:simulator_world_sync_cc_proto",
    ],
)

cc_library(
    name = "simulation_service_first_party",
    srcs = ["simulation_service_first_party.cc"],
    hdrs = ["simulation_service_first_party.h"],
    deps = [
        ":simulation_service_impl",
        "//intrinsic/simulation/service/proto/first_party:simulation_service_cc_grpc_proto",
        "//intrinsic/simulation/service/proto/first_party:simulation_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "@abseil-cpp//absl/base:nullability",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "simulation_service_v1",
    srcs = ["simulation_service_v1.cc"],
    hdrs = ["simulation_service_v1.h"],
    deps = [
        ":simulation_service_impl",
        "//intrinsic/simulation/service/proto:conversions",
        "//intrinsic/simulation/service/proto/first_party:simulation_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "@abseil-cpp//absl/base:nullability",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/simulation/service/proto/v1:simulation_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/simulation/service/proto/v1:simulation_service_cc_proto",
    ],
)

cc_library(
    name = "simulator_world_sync_v1",
    srcs = ["simulator_world_sync_v1.cc"],
    hdrs = ["simulator_world_sync_v1.h"],
    deps = [
        ":simulation_service_impl",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "@abseil-cpp//absl/base:nullability",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/simulation/service/proto/v1:simulator_world_sync_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/simulation/service/proto/v1:simulator_world_sync_cc_proto",
    ],
)

cc_library(
    name = "simulation_service_collection",
    srcs = ["simulation_service_collection.cc"],
    hdrs = ["simulation_service_collection.h"],
    deps = [
        ":simulation_service_first_party",
        ":simulation_service_impl",
        ":simulation_service_v1",
        ":simulator_world_sync_v1",
        "//intrinsic/simulation/service/proto/first_party:simulation_service_cc_proto",
        "//intrinsic/storage/hot_shared_state/proto:application_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/resources/client:resource_registry_client_interface",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "simulation_service_startup",
    srcs = ["simulation_service_startup.cc"],
    hdrs = ["simulation_service_startup.h"],
    visibility = ["//visibility:private"],
    deps = [
        ":resource_registry_utils",
        ":simulation_service_collection",
        ":simulation_service_impl",
        ":simulator",
        ":simulator_v1",
        ":simulator_world_sync_v1",
        "//intrinsic/simulation/service/proto/first_party:simulation_service_cc_grpc_proto",
        "//intrinsic/storage/hot_shared_state/proto:application_service_cc_grpc_proto",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/resources/client:resource_registry_client_cc",
        "//intrinsic_sdk/intrinsic/resources/client:resource_registry_client_interface",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_binary(
    name = "simulation_service_main",
    srcs = ["simulation_service_main.cc"],
    deps = [
        ":simulation_service_collection",
        ":simulation_service_startup",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc++_reflection",  # Needed for grpc_cli.
        "@com_github_grpc_grpc//:grpc_security_base",
    ],
)

cc_oci_image(
    name = "simulation-service-image",
    binary = ":simulation_service_main",
    data_path = "/",
    files = ["//intrinsic/kubernetes/waitforservices"],
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)
