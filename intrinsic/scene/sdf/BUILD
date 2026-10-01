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

package(
    default_visibility =
        [
            "//intrinsic/scene:__subpackages__",
            "//intrinsic/simulation:__subpackages__",
            "//intrinsic/world/conversion/sdf:__subpackages__",
        ],
)

cc_library(
    name = "convert_to_sdf",
    srcs = ["convert_to_sdf.cc"],
    hdrs = ["convert_to_sdf.h"],
    deps = [
        "//intrinsic/geometry/api:geometric_transform",
        "//intrinsic/geometry/compatibility:io",
        "//intrinsic/geometry/internal/legacy/mesh/io:save_mesh_to_stl_file",
        "//intrinsic/util:eigen",
        "//intrinsic/util:full_precision",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_fingerprint",
        "//intrinsic_sdk/intrinsic/geometry/api:io",
        "//intrinsic_sdk/intrinsic/geometry/api:renderable_generation",
        "//intrinsic_sdk/intrinsic/geometry/internal/util:scale_shape",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/scene:user_data_keys",
        "//intrinsic_sdk/intrinsic/scene/sdf:custom_tags",
        "//intrinsic_sdk/intrinsic/scene/sdf:sdf_util",
        "//intrinsic_sdk/intrinsic/scene/sdf:separators",
        "//intrinsic_sdk/intrinsic/scene/sdf:xml_utils",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:material_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_cc_proto",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:path",
    ],
)

cc_library(
    name = "scene_object_to_sdf",
    srcs = ["scene_object_to_sdf.cc"],
    hdrs = ["scene_object_to_sdf.h"],
    deps = [
        ":convert_to_sdf",
        ":sim_spec_to_sdf",
        "//intrinsic/util:full_precision",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_deserializer",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/scene/conversion:user_data",
        "//intrinsic_sdk/intrinsic/scene/sdf:custom_tags",
        "//intrinsic_sdk/intrinsic/scene/sdf:sdf_sensor_pose",
        "//intrinsic_sdk/intrinsic/scene/sdf:sdf_util",
        "//intrinsic_sdk/intrinsic/scene/sdf:xml_utils",
        "//intrinsic_sdk/intrinsic/scene/validate:scene_object_validation",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/scene/proto/v1:entity_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
    ],
)

cc_library(
    name = "sim_spec_to_sdf",
    srcs = ["sim_spec_to_sdf.cc"],
    hdrs = ["sim_spec_to_sdf.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/scene/sdf:xml_utils",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/scene/proto/v1:simulation_spec_cc_proto",
        "@tinyxml2",
    ],
)

cc_library(
    name = "scene_object_from_zipped_sdf",
    srcs = ["scene_object_from_zipped_sdf.cc"],
    hdrs = ["scene_object_from_zipped_sdf.h"],
    visibility = ["//intrinsic/scene/service:__subpackages__"],
    deps = [
        "//intrinsic/util/archive:unzip",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/scene/sdf:scene_object_from_sdf",
        "//intrinsic_sdk/intrinsic/scene/sdf:sdf_path_resolver",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:path",
        "@or_tools//ortools/base:temp_path",
    ],
)

cc_library(
    name = "scene_object_to_zipped_sdf",
    srcs = ["scene_object_to_zipped_sdf.cc"],
    hdrs = ["scene_object_to_zipped_sdf.h"],
    visibility = ["//intrinsic/scene/service:__subpackages__"],
    deps = [
        ":scene_object_to_sdf",
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
        "@libzip",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:path",
    ],
)
