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

# Simulation tools built on top of Gazebo simulation engine.
package(default_visibility = ["//intrinsic:intrinsic_internal"])

# Main function implementation for the Gazebo simulation server.
# See binary targets below that wrap this main implementation and link in
# different dependencies dependending on the target platform and deployment
# method.
cc_library(
    name = "simulation_server_main_impl",
    srcs = ["server_main_impl.cc"],
    hdrs = ["server_main_impl.h"],
    data = [
        "//intrinsic/simulation/gazebo/world_templates:default_template",
    ],
    visibility = ["//intrinsic/simulation:__subpackages__"],
    deps = [
        ":asset_instances_client",
        ":asset_instances_service_address_flag",
        ":gazebo_performance_logger",
        ":gazebo_runner",
        ":gazebo_service",
        ":gz_sim_static_systems",
        ":simulator_control_service",
        ":world_sync_system",
        ":world_util",
        "//intrinsic/geometry/storage:geometry_service_storage",
        "//intrinsic/simulation/gazebo/asset:gazebo_simulator_config_cc_proto",
        "//intrinsic/simulation/gazebo/outfeed:outfeed_manager",
        "//intrinsic/simulation/gazebo/plugins",
        "//intrinsic/simulation/gazebo/plugins/cameras:camera_services",
        "//intrinsic/simulation/gazebo/plugins/gpio:gpio_service",
        "//intrinsic/simulation/gazebo/plugins/grippers:pinch_gripper_server_impl",
        "//intrinsic/simulation/gazebo/plugins/sim_inputs:sim_inputs_service_singleton",
        "//intrinsic/simulation/gazebo/spawner:spawner_manager",
        "//intrinsic/simulation/gazebo/world_templates:world_template",
        "//intrinsic/simulation/world:world_object_plugin",
        "//intrinsic/simulation/world:world_to_sdf",
        "//intrinsic/stats:opencensus",
        "//intrinsic/world",
        "//intrinsic_control/intrinsic/icon/release:file_helpers",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/assets:interface_utils",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils_cc",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/geometry/proto:geometry_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/logging:data_logger_client",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/scene/sdf:separators",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "//intrinsic_sdk/intrinsic/util/path_resolver",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@bazel_tools//tools/cpp/runfiles",
        "@com_github_grpc_grpc//:grpc++",
        "@gz-math",
        "@gz-sim",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_cc_proto",
        "@sdformat",
    ],
)

# Statically registered Gz Sim systems that are linked into gzserver.
cc_library(
    name = "gz_sim_static_systems",
    deps = [
        "//intrinsic/simulation/gazebo/plugins:world_model_config_static",
        "@gz-sim//:gz-sim-apply-joint-force-system-static",
        "@gz-sim//:gz-sim-contact-system-static",
        "@gz-sim//:gz-sim-detachable-joint-system-static",
        "@gz-sim//:gz-sim-diff-drive-system-static",
        "@gz-sim//:gz-sim-forcetorque-system-static",
        "@gz-sim//:gz-sim-imu-system-static",
        "@gz-sim//:gz-sim-joint-controller-system-static",
        "@gz-sim//:gz-sim-joint-position-controller-system-static",
        "@gz-sim//:gz-sim-joint-state-publisher-system-static",
        "@gz-sim//:gz-sim-log-system-static",
        "@gz-sim//:gz-sim-logical-camera-system-static",
        "@gz-sim//:gz-sim-magnetometer-system-static",
        "@gz-sim//:gz-sim-physics-system-static",
        "@gz-sim//:gz-sim-pose-publisher-system-static",
        "@gz-sim//:gz-sim-scene-broadcaster-system-static",
        "@gz-sim//:gz-sim-triggered-publisher-system-static",
        "@gz-sim//:gz-sim-user-commands-system-static",
        "@gz-sim//:gz-sim-velocity-control-system-static",
        "@gz-sim//:gz-sim-wheel-slip-system-static",
    ],
    alwayslink = 1,
)

# Ogre render engine system, link-time deps and data.
cc_library(
    name = "gz_ogre2_gl3plus_render_engine_static",
    data = [
        "@ogre-next//:RenderSystem_GL3Plus.so",
    ],
    # Global symbols must be exported for all `Ogre::Singleton` objects for the Ogre-Next plugin
    # to be loaded correctly. Otherwise, the plugin re-instantiates singleton pointers and
    # crashes.
    linkopts = [
        "-Wl,--export-dynamic-symbol",
        "-Wl,_Z*Ogre*Singleton*",
    ],
    deps = [
        "@gz-rendering//ogre2:gz-rendering-ogre2-engine-static",
        "@sysroot_glvnd_stubs",
    ],
    alwayslink = 1,
)

cc_library(
    name = "world_sync_system",
    srcs = ["world_sync_system.cc"],
    hdrs = ["world_sync_system.h"],
    deps = [
        "//intrinsic/simulation/gazebo/components:world_component",
        "//intrinsic/world/proto/v1:pubsub_world_updates_cc_proto",
        "//intrinsic/world/util:tf_frame_util",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/simulation/gazebo:type_conversion",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_rpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:world_object",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@gz-common//profiler",
        "@gz-sim",
        "@intrinsic_apis//intrinsic/simulation/service/proto/v1:simulator_world_sync_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/simulation/service/proto/v1:simulator_world_sync_cc_proto",
    ],
)

cc_library(
    name = "gazebo_runner",
    srcs = ["gazebo_runner.cc"],
    hdrs = ["gazebo_runner.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@gz-common//profiler",
        "@gz-sim",
        "@gz-transport",
    ],
)

cc_library(
    name = "simulator_control_service",
    srcs = ["simulator_control_service.cc"],
    hdrs = ["simulator_control_service.h"],
    deps = [
        ":asset_instances_service_address_flag",
        ":gazebo_runner",
        ":profiler",
        "//intrinsic/simulation/gazebo/plugins:add_hardware_module_launchers_system",
        "//intrinsic/simulation/simulator/proto/v1:simulator_control_service_cc_grpc_proto",
        "//intrinsic/simulation/simulator/proto/v1:simulator_control_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:ret_check_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@gz-sim",
    ],
)

cc_library(
    name = "world_util",
    srcs = ["world_util.cc"],
    hdrs = ["world_util.h"],
    deps = [
        "//intrinsic/simulation/world:world_to_sdf",
        "//intrinsic/world",
        "//intrinsic/world/util:download_world_util",
        "//intrinsic_sdk/intrinsic/assets:interface_utils",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils_cc",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_deserializer",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:path",
    ],
)

cc_library(
    name = "profiler",
    srcs = ["profiler.cc"],
    hdrs = ["profiler.h"],
    visibility = ["//visibility:private"],
    deps = [
        "//intrinsic/simulation/gazebo/proto:introspection_cc_proto",
        "//intrinsic/stats:metrics_utils",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//src/google/protobuf/io",
        "@gz-common//profiler",
        "@gz-common//profiler:ProfilerImplInterface",
        "@io_opentelemetry_cpp//api",
    ],
)

cc_library(
    name = "asset_instances_service_address_flag",
    srcs = ["asset_instances_service_address_flag.cc"],
    hdrs = ["asset_instances_service_address_flag.h"],
    deps = [
        "@abseil-cpp//absl/flags:flag",
    ],
)

cc_library(
    name = "gazebo_performance_logger",
    srcs = ["gazebo_performance_logger.cc"],
    hdrs = ["gazebo_performance_logger.h"],
    deps = [
        "//intrinsic/simulation/gazebo/proto:introspection_cc_proto",
        "//intrinsic_sdk/intrinsic/logging:data_logger_client",
        "//intrinsic_sdk/intrinsic/logging/proto:log_item_cc_proto",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
        "@gz-msgs",
        "@gz-transport",
    ],
)

cc_library(
    name = "gz_topic_info",
    srcs = ["gz_topic_info.cc"],
    hdrs = ["gz_topic_info.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/simulation/gazebo/proto/v1:gazebo_service_cc_proto",
    ],
)

cc_library(
    name = "gazebo_service",
    srcs = ["gazebo_service.cc"],
    hdrs = ["gazebo_service.h"],
    deps = [
        ":gz_topic_info",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "@com_github_grpc_grpc//:grpc++",
        "@gz-transport",
        "@intrinsic_apis//intrinsic/simulation/gazebo/proto/v1:gazebo_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/simulation/gazebo/proto/v1:gazebo_service_cc_proto",
    ],
)

cc_library(
    name = "asset_instances_client",
    srcs = ["asset_instances_client.cc"],
    hdrs = ["asset_instances_client.h"],
    visibility = ["//intrinsic/simulation:__subpackages__"],
    deps = [
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_instances_cc_grpc",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_instances_cc_proto",
    ],
)
