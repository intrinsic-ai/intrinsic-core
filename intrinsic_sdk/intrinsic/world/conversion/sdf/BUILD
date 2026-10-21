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
    name = "collision_surface_conversion",
    srcs = ["collision_surface_conversion.cc"],
    hdrs = ["collision_surface_conversion.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/scene/sdf:sdf_util",
        "//intrinsic_sdk/intrinsic/scene/sdf:xml_utils",
        "//intrinsic_sdk/intrinsic/simulation/gazebo:type_conversion",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@gz-math",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_cc_proto",
        "@sdformat",
    ],
)

cc_library(
    name = "geometry_component_from_sdf",
    srcs = ["geometry_component_from_sdf.cc"],
    hdrs = ["geometry_component_from_sdf.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/geometry/api:affine_transform_of_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:io",
        "//intrinsic_sdk/intrinsic/geometry/api:material",
        "//intrinsic_sdk/intrinsic/scene/sdf:sdf_path_resolver",
        "//intrinsic_sdk/intrinsic/scene/sdf:sdf_util",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_cc_proto",
        "@sdformat",
    ],
)

cc_library(
    name = "joint_conversion",
    srcs = ["joint_conversion.cc"],
    hdrs = ["joint_conversion.h"],
    deps = [
        ":kinematics_component_from_sdf",
        ":sensor_conversion",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/scene/sdf:sdf_util",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
        "@sdformat",
    ],
)

cc_library(
    name = "kinematics_component_from_sdf",
    srcs = ["kinematics_component_from_sdf.cc"],
    hdrs = ["kinematics_component_from_sdf.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/scene:constants",
        "//intrinsic_sdk/intrinsic/scene/sdf:custom_tags",
        "//intrinsic_sdk/intrinsic/scene/sdf:sdf_util",
        "//intrinsic_sdk/intrinsic/simulation/gazebo:type_conversion",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@gz-math",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
        "@sdformat",
    ],
)

cc_library(
    name = "physics_component_from_sdf",
    srcs = ["physics_component_from_sdf.cc"],
    hdrs = ["physics_component_from_sdf.h"],
    deps = [
        ":collision_surface_conversion",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/simulation/gazebo:type_conversion",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/component:physics_component",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
        "@gz-math",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_cc_proto",
        "@sdformat",
    ],
)

cc_library(
    name = "sensor_conversion",
    srcs = ["sensor_conversion.cc"],
    hdrs = ["sensor_conversion.h"],
    deps = [
        "//intrinsic/simulation/world:sim_plugins",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/scene/sdf:sdf_sensor_pose",
        "//intrinsic_sdk/intrinsic/scene/sdf:sdf_util",
        "//intrinsic_sdk/intrinsic/scene/sdf:xml_utils",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/component:sensor_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_cc_proto",
        "@sdformat",
    ],
)
