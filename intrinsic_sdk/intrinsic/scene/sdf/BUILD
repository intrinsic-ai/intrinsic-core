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

package(
    default_visibility =
        [
            "//intrinsic/scene:__subpackages__",
            "//intrinsic/simulation:__subpackages__",
            "//intrinsic/world/conversion/sdf:__subpackages__",
            "//intrinsic_sdk/intrinsic/scene:__subpackages__",
            "//intrinsic_sdk/intrinsic/simulation:__subpackages__",
            "//intrinsic_sdk/intrinsic/world/conversion/sdf:__subpackages__",
        ],
)

cc_library(
    name = "sdf_path_resolver",
    srcs = ["sdf_path_resolver.cc"],
    hdrs = ["sdf_path_resolver.h"],
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/util/path_resolver",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@or_tools//ortools/base:path",
    ],
)

cc_library(
    name = "custom_tags",
    hdrs = ["custom_tags.h"],
    deps = ["@abseil-cpp//absl/strings:string_view"],
)

cc_library(
    name = "separators",
    hdrs = ["separators.h"],
    deps = ["@abseil-cpp//absl/strings:string_view"],
)

cc_library(
    name = "sdf_sensor_pose",
    srcs = ["sdf_sensor_pose.cc"],
    hdrs = ["sdf_sensor_pose.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@sdformat",
    ],
)

cc_library(
    name = "sdf_util",
    srcs = ["sdf_util.cc"],
    hdrs = ["sdf_util.h"],
    deps = [
        ":custom_tags",
        ":sdf_path_resolver",
        ":separators",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:affine_transform_of_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:axis_aligned_bounding_box_3d",
        "//intrinsic_sdk/intrinsic/geometry/api:compute_axis_aligned_bounding_box_3d",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "//intrinsic_sdk/intrinsic/geometry/api:material",
        "//intrinsic_sdk/intrinsic/geometry/api:shapes",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/simulation/gazebo:type_conversion",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@gz-math",
        "@or_tools//ortools/base:file",
        "@sdformat",
    ],
)

cc_library(
    name = "xml_utils",
    srcs = ["xml_utils.cc"],
    hdrs = ["xml_utils.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@sdformat",
        "@tinyxml2",
    ],
)

cc_library(
    name = "entity_from_sdf",
    srcs = ["entity_from_sdf.cc"],
    hdrs = ["entity_from_sdf.h"],
    deps = [
        ":custom_tags",
        ":sdf_path_resolver",
        ":sdf_util",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/simulation/gazebo:type_conversion",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/component:physics_component",
        "//intrinsic_sdk/intrinsic/world/conversion/sdf:geometry_component_from_sdf",
        "//intrinsic_sdk/intrinsic/world/conversion/sdf:joint_conversion",
        "//intrinsic_sdk/intrinsic/world/conversion/sdf:physics_component_from_sdf",
        "//intrinsic_sdk/intrinsic/world/conversion/sdf:sensor_conversion",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@gz-math",
        "@intrinsic_apis//intrinsic/scene/proto/v1:entity_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
        "@sdformat",
    ],
)

cc_library(
    name = "scene_object_from_sdf",
    srcs = ["scene_object_from_sdf.cc"],
    hdrs = ["scene_object_from_sdf.h"],
    visibility = [
        "//intrinsic/scene/sdf:__pkg__",
        "//intrinsic/scene/service:__subpackages__",
        "//intrinsic/world/conversion:__subpackages__",
        "//intrinsic_sdk/intrinsic/world/conversion:__subpackages__",
    ],
    deps = [
        ":custom_tags",
        ":entity_from_sdf",
        ":sdf_path_resolver",
        ":sdf_util",
        ":sim_spec_from_sdf",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/scene:user_data_keys",
        "//intrinsic_sdk/intrinsic/scene/conversion:object_properties_conversion",
        "//intrinsic_sdk/intrinsic/scene/conversion:user_data",
        "//intrinsic_sdk/intrinsic/scene/util:scene_object_updates",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/conversion/sdf:joint_conversion",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:protobuf_lite",
        "@intrinsic_apis//intrinsic/scene/proto/v1:entity_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:object_properties_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:simulation_spec_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
        "@or_tools//ortools/base:path",
        "@sdformat",
    ],
)

cc_binary(
    name = "sdf_to_scene_object",
    srcs = ["sdf_to_scene_object.cc"],
    visibility = [
        "//intrinsic/scene/build_defs:__subpackages__",
        "//intrinsic/scene/sdf:__pkg__",
        "//intrinsic_sdk/intrinsic/scene/build_defs:__subpackages__",
    ],
    deps = [
        ":scene_object_from_sdf",
        ":sdf_path_resolver",
        ":sim_spec_from_sdf",
        "//intrinsic/geometry/internal/kernel_3:exact_predicates_inexact_constructions_kernel",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/geometry/api:axis_aligned_bounding_box_3d",
        "//intrinsic_sdk/intrinsic/geometry/api:compute_axis_aligned_bounding_box_3d",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_deserializer",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/geometry/storage:gzf_storage",
        "//intrinsic_sdk/intrinsic/geometry/storage:in_memory_storage",
        "//intrinsic_sdk/intrinsic/scene/util:object_user_data",
        "//intrinsic_sdk/intrinsic/scene/util:scene_object_gzf",
        "//intrinsic_sdk/intrinsic/scene/validate:large_mesh",
        "//intrinsic_sdk/intrinsic/scene/validate:scene_object_validate_geo",
        "//intrinsic_sdk/intrinsic/scene/validate:scene_object_validation",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/gzfile",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_storage_refs_cc_proto",
        "@or_tools//ortools/base:file",
    ],
)

cc_library(
    name = "sim_spec_from_sdf",
    srcs = ["sim_spec_from_sdf.cc"],
    hdrs = ["sim_spec_from_sdf.h"],
    visibility = [
        "//intrinsic/scene/sdf:__pkg__",
        "//intrinsic/scene/service:__subpackages__",
    ],
    deps = [
        ":sdf_util",
        ":xml_utils",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@intrinsic_apis//intrinsic/scene/proto/v1:entity_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:simulation_spec_cc_proto",
        "@sdformat",
    ],
)
