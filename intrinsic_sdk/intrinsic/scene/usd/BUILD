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
    default_visibility = [
        "//intrinsic_runtime/intrinsic/scene:__subpackages__",
        "//intrinsic_sdk/intrinsic/scene:__subpackages__",
    ],
)

cc_library(
    name = "utils",
    srcs = ["utils.cc"],
    hdrs = ["utils.h"],
    visibility = [
        "//intrinsic_runtime/intrinsic/scene:__subpackages__",
        "//intrinsic_sdk/intrinsic/scene:__subpackages__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
        "@bazel_tools//tools/cpp/runfiles",
        "@openusd",
    ],
)

cc_library(
    name = "scene_object_from_usd",
    srcs = ["scene_object_from_usd.cc"],
    hdrs = ["scene_object_from_usd.h"],
    visibility = [
        "//intrinsic_runtime/intrinsic/scene:__subpackages__",
        "//intrinsic_sdk/intrinsic/scene:__subpackages__",
    ],
    deps = [
        ":connection_graph",
        ":entity_from_usd",
        ":utils",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/scene/proto/v1:entity_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
        "@openusd",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:temp_path",
    ],
)

cc_library(
    name = "entity_from_usd",
    srcs = ["entity_from_usd.cc"],
    hdrs = ["entity_from_usd.h"],
    deps = [
        ":utils",
        "//intrinsic_sdk/intrinsic/geometry/api:io",
        "//intrinsic_sdk/intrinsic/geometry/api:renderable_generation",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/component:physics_component",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:material_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:entity_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_cc_proto",
        "@openusd",
    ],
)

cc_library(
    name = "connection_graph",
    srcs = ["connection_graph.cc"],
    hdrs = ["connection_graph.h"],
    deps = [
        ":entity_from_usd",
        ":utils",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@openusd",
    ],
)

cc_binary(
    name = "usd_file_converter",
    srcs = ["usd_file_converter.cc"],
    data = [
        # Required for initializing the USD library.
        "@openusd//:plugin_files",
    ],
    output_licenses = ["unencumbered"],
    visibility = [
        "//intrinsic_sdk/intrinsic/scene:__subpackages__",
    ],
    deps = [
        ":scene_object_from_usd",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/geometry/storage:gzf_storage",
        "//intrinsic_sdk/intrinsic/scene/util:object_user_data",
        "//intrinsic_sdk/intrinsic/scene/util:scene_object_gzf",
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
        "@or_tools//ortools/base:file",
    ],
)
