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
    default_visibility = [
        "//visibility:public",
    ],
)

cc_library(
    name = "world_service",
    hdrs = ["world_service.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
    ],
)

cc_library(
    name = "assembly_from_world_service",
    srcs = ["assembly_from_world_service.cc"],
    hdrs = ["assembly_from_world_service.h"],
    deps = [
        ":assembly_service",
        ":service",
        ":service_registration",
        ":world_service",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:world_utils",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
    ],
    alwayslink = True,
)

cc_library(
    name = "kinematics_from_world_service",
    srcs = ["kinematics_from_world_service.cc"],
    hdrs = ["kinematics_from_world_service.h"],
    deps = [
        ":kinematics_service",
        ":service",
        ":service_registration",
        ":world_service",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:world_utils",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:model_interface",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:inverse_kinematics_factory",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:inverse_kinematics_interface",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
    ],
    alwayslink = True,
)

cc_library(
    name = "dynamics_from_kinematics_service",
    srcs = ["dynamics_from_kinematics_service.cc"],
    hdrs = ["dynamics_from_kinematics_service.h"],
    deps = [
        ":dynamics_service",
        ":kinematics_service",
        ":service",
        ":service_registration",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_control/intrinsic/icon/dynamics:dynamics_factory",
        "//intrinsic_control/intrinsic/icon/dynamics:rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/icon/server/config:services_config_cc_proto",
    ],
    alwayslink = True,
)

cc_library(
    name = "world_from_grpc_service",
    srcs = ["world_from_grpc_service.cc"],
    hdrs = ["world_from_grpc_service.h"],
    deps = [
        ":service",
        ":service_registration",
        ":world_service",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
    alwayslink = True,
)

cc_library(
    name = "geometry_library_service",
    hdrs = ["geometry_library_service.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "@abseil-cpp//absl/base:nullability",
    ],
)

cc_library(
    name = "geometry_from_geometry_library",
    srcs = ["geometry_from_geometry_library.cc"],
    hdrs = ["geometry_from_geometry_library.h"],
    deps = [
        ":geometry_library_service",
        ":service",
        "//intrinsic/geometry/proto:geometry_service_cc_grpc_proto",
        "//intrinsic/geometry/storage:geometry_service_storage",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "dynamics_service",
    hdrs = ["dynamics_service.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/dynamics:rigid_body_interface",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "kinematics_service",
    hdrs = ["kinematics_service.h"],
    deps = [
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:inverse_kinematics_interface",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "assembly_service",
    hdrs = ["assembly_service.h"],
    deps = ["//intrinsic_kinematics/intrinsic/kinematics:skeleton"],
)

cc_library(
    name = "service",
    hdrs = ["service.h"],
    deps = [
        "//intrinsic/util:demangle",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/utility",
    ],
)

cc_library(
    name = "service_collection",
    srcs = ["service_collection.cc"],
    hdrs = ["service_collection.h"],
    deps = [
        ":service",
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "service_registration",
    srcs = ["service_registration.cc"],
    hdrs = ["service_registration.h"],
    deps = [
        ":service",
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "@abseil-cpp//absl/hash",
        "@cppregpattern//:registry",
    ],
)
