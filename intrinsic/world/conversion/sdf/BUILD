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

load("//bazel:cc_macros.bzl", "cc_binary", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "collision_component_from_sdf",
    srcs = ["collision_component_from_sdf.cc"],
    hdrs = ["collision_component_from_sdf.h"],
    deps = [
        "//intrinsic/world/component:collision_component",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/conversion/sdf:collision_surface_conversion",
        "@abseil-cpp//absl/status:statusor",
        "@sdformat",
    ],
)

cc_binary(
    name = "sdf_to_world",
    srcs = ["sdf_to_world.cc"],
    # Need to tell check_licenses that output only depends on input data licences, not those of the tool b/160225578.
    output_licenses = ["unencumbered"],
    deps = [
        ":world_from_sdf",
        "//intrinsic/world",
        "//intrinsic/world:entity",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:affine_transform_of_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:axis_aligned_bounding_box_3d",
        "//intrinsic_sdk/intrinsic/geometry/api:compute_axis_aligned_bounding_box_3d",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/gzfile",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@or_tools//ortools/base:file",
        "@sdformat",
    ],
)

cc_library(
    name = "world_from_sdf",
    srcs = ["world_from_sdf.cc"],
    hdrs = ["world_from_sdf.h"],
    deps = [
        ":collision_component_from_sdf",
        "//intrinsic/simulation/world:sim_plugins",
        "//intrinsic/world",
        "//intrinsic/world:entity",
        "//intrinsic/world:grouping",
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic/world/component:attachment_component",
        "//intrinsic/world/component:collections_component",
        "//intrinsic/world/component:collections_member_component",
        "//intrinsic/world/component:collision_component",
        "//intrinsic/world/component:projector_component",
        "//intrinsic/world/component:robot_component",
        "//intrinsic/world/component:simulation_component",
        "//intrinsic/world/component:user_data_component",
        "//intrinsic/world/proto:robot_component_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/scene:user_data_keys",
        "//intrinsic_sdk/intrinsic/scene/sdf:custom_tags",
        "//intrinsic_sdk/intrinsic/scene/sdf:sdf_path_resolver",
        "//intrinsic_sdk/intrinsic/scene/sdf:sdf_util",
        "//intrinsic_sdk/intrinsic/scene/sdf:xml_utils",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world:labels",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/component:physics_component",
        "//intrinsic_sdk/intrinsic/world/component:sensor_component",
        "//intrinsic_sdk/intrinsic/world/conversion/sdf:geometry_component_from_sdf",
        "//intrinsic_sdk/intrinsic/world/conversion/sdf:joint_conversion",
        "//intrinsic_sdk/intrinsic/world/conversion/sdf:physics_component_from_sdf",
        "//intrinsic_sdk/intrinsic/world/conversion/sdf:sensor_conversion",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:point_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_cc_proto",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:path",
        "@sdformat",
    ],
)
