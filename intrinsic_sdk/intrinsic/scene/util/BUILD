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
    name = "scene_object_gzf",
    srcs = ["scene_object_gzf.cc"],
    hdrs = ["scene_object_gzf.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/world/gzfile",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//src/google/protobuf/io",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
    ],
)

cc_library(
    name = "geometry_update_util",
    srcs = ["geometry_update_util.cc"],
    hdrs = ["geometry_update_util.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/geometry/api:apply_material_properties",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_cc_proto",
    ],
)

cc_library(
    name = "scene_object_updates",
    srcs = [
        "scene_object_updates.cc",
        "scene_object_updates_internal.cc",
    ],
    hdrs = [
        "scene_object_updates.h",
        "scene_object_updates_internal.h",
    ],
    deps = [
        ":geometry_update_util",
        ":object_user_data",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/scene/conversion:scene_object_model_utils",
        "//intrinsic_sdk/intrinsic/scene/validate:scene_object_validation",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:collision_rules_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:entity_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:object_properties_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_cc_proto",
    ],
)

cc_library(
    name = "object_user_data",
    srcs = ["object_user_data.cc"],
    hdrs = ["object_user_data.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/scene/conversion:user_data",
        "//intrinsic_sdk/intrinsic/util/proto:descriptors",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_cc_proto",
    ],
)
