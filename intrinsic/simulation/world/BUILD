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

# Intrinsic World integration for Gazebo.

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "world_updater",
    srcs = ["world_updater.cc"],
    hdrs = ["world_updater.h"],
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        "//intrinsic/world/proto:world_updates_cc_proto",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:concurrent_queue",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_cc_proto",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "world_service_updater",
    srcs = ["world_service_updater.cc"],
    hdrs = ["world_service_updater.h"],
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        ":world_updater",
        "//intrinsic/world/proto:world_updates_cc_proto",
        "//intrinsic/world/service:world_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

cc_library(
    name = "world_to_sdf",
    srcs = ["world_to_sdf.cc"],
    hdrs = ["world_to_sdf.h"],
    # Add models required for auto-injection (e.g. model://sun)
    data = [
        "//intrinsic/models/gazebo:base_models",
    ],
    deps = [
        ":gen_node",
        ":generate_collision_bitmasks",
        ":gripper_plugin_spec",
        ":inlined_plugins_util",
        ":multi_camera_plugin_spec",
        ":sim_plugins",
        ":world_object_plugin",
        "//intrinsic/geometry/internal/legacy/mesh/io:save_mesh_to_stl_file",
        "//intrinsic/scene/sdf:convert_to_sdf",
        "//intrinsic/simulation/gazebo:gz_topic_info",
        "//intrinsic/simulation/gazebo/plugins:world_model_config_plugin_constants",
        "//intrinsic/simulation/gazebo/world_templates:world_template",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/util:full_precision",
        "//intrinsic/world",
        "//intrinsic/world:entity",
        "//intrinsic/world/component:attachment_component",
        "//intrinsic/world/component:collections_component",
        "//intrinsic/world/component:collections_member_component",
        "//intrinsic/world/component:collision_component",
        "//intrinsic/world/component:ppr_component",
        "//intrinsic/world/component:projector_component",
        "//intrinsic/world/component:robot_component",
        "//intrinsic/world/component:simulation_component",
        "//intrinsic/world/component:user_data_component",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:object_world_creation_utils",
        "//intrinsic/world/proto:generic_action_cc_proto",
        "//intrinsic/world/proto:robot_component_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_fingerprint",
        "//intrinsic_sdk/intrinsic/geometry/api:renderable_generation",
        "//intrinsic_sdk/intrinsic/geometry/internal/util:scale_shape",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "//intrinsic_sdk/intrinsic/geometry/shapes:geometry_shape",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/scene:user_data_keys",
        "//intrinsic_sdk/intrinsic/scene/sdf:sdf_sensor_pose",
        "//intrinsic_sdk/intrinsic/scene/sdf:sdf_util",
        "//intrinsic_sdk/intrinsic/simulation/gazebo:gz_topic_constants",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/component:physics_component",
        "//intrinsic_sdk/intrinsic/world/component:sensor_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//third_party/thorough_hash",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@assimp",
        "@boost.bimap",
        "@com_google_protobuf//:protobuf",
        "@gz-transport",
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:point_cc_proto",
        "@intrinsic_apis//intrinsic/simulation/gazebo/proto/v1:gazebo_service_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_cc_proto",
        "@io_opentelemetry_cpp//api",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:path",
        "@sdformat",
        "@tinyxml2",
    ],
)

cc_library(
    name = "gen_node",
    srcs = ["gen_node.cc"],
    hdrs = ["gen_node.h"],
    deps = [
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "generate_collision_bitmasks",
    srcs = ["generate_collision_bitmasks.cc"],
    hdrs = ["generate_collision_bitmasks.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "gripper_plugin_spec",
    srcs = ["gripper_plugin_spec.cc"],
    hdrs = ["gripper_plugin_spec.h"],
    deps = [
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:pinch_gripper_server_cc_proto",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:gripper_config_cc_proto",
        "@tinyxml2",
    ],
)

cc_library(
    name = "multi_camera_plugin_spec",
    srcs = ["multi_camera_plugin_spec.cc"],
    hdrs = ["multi_camera_plugin_spec.h"],
    deps = [
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@tinyxml2",
    ],
)

cc_library(
    name = "inlined_plugins_util",
    srcs = ["inlined_plugins_util.cc"],
    hdrs = ["inlined_plugins_util.h"],
    deps = [
        ":multi_camera_plugin_spec",
        ":sim_plugins",
        "//intrinsic/world:entity",
        "//intrinsic/world/component:user_data_component",
        "//intrinsic_sdk/intrinsic/scene:user_data_keys",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_cc_proto",
        "@tinyxml2",
    ],
)

cc_library(
    name = "sim_plugins",
    srcs = ["sim_plugins.cc"],
    hdrs = ["sim_plugins.h"],
    deps = [
        "//intrinsic/world/proto:generic_action_cc_proto",
        "//intrinsic/world/proto:robot_component_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:simulation_spec_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_cc_proto",
        "@tinyxml2",
    ],
)

cc_library(
    name = "world_interpolation",
    srcs = ["world_interpolation.cc"],
    hdrs = ["world_interpolation.h"],
    deps = [
        "//intrinsic/eigenmath:interpolation",
        "//intrinsic/world",
        "//intrinsic/world/proto:world_updates_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/objects:object_entity_filter",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_cc_proto",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "world_object_plugin",
    srcs = ["world_object_plugin.cc"],
    hdrs = ["world_object_plugin.h"],
    deps = [
        ":gripper_plugin_spec",
        ":inlined_plugins_util",
        ":multi_camera_plugin_spec",
        "//intrinsic/world",
        "//intrinsic/world/proto:robot_component_cc_proto",
        "//intrinsic/world/util:walk_attachment_tree",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_config_cc_proto",
        "//intrinsic_hardware/intrinsic/hardware/gripper:gripper_equipment_cc_proto",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service_asset:gripper_service_utils",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service_asset:pinch_gripper_opcua_service_config_cc_proto",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service_asset:pinch_gripper_realtime_control_service_config_cc_proto",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service_asset:suction_gripper_opcua_service_config_cc_proto",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service_asset:suction_gripper_realtime_control_service_config_cc_proto",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/scene:user_data_keys",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/component:sensor_component",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:gripper_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_cc_proto",
    ],
)
