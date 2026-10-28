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
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "collision_check_cache",
    srcs = ["collision_check_cache.cc"],
    hdrs = ["collision_check_cache.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "//third_party/simple_lru_cache",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "collision_checker",
    srcs = ["collision_checker.cc"],
    hdrs = ["collision_checker.h"],
    deps = [
        ":collision_checker_world",
        ":collision_context_cc_proto",
        ":collision_types",
        "//intrinsic/geometry/api:distance_stats",
        "//intrinsic/world:entity",
        "//intrinsic/world/component:attachment_component",
        "//intrinsic/world/component:collections_member_component",
        "//intrinsic/world/component:ppr_component",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@gloop//gloop/util/gtl:flat_set",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_error_cc_proto",
    ],
)

cc_library(
    name = "collision_checker_utils",
    srcs = ["collision_checker_utils.cc"],
    hdrs = ["collision_checker_utils.h"],
    deps = [
        ":collision_checker_world",
        ":collision_context_cc_proto",
        ":collision_types",
        ":rule_matching",
        "//intrinsic/geometry/api:distance_stats",
        "//intrinsic/util/proto:pb_hash",
        "//intrinsic/world:entity",
        "//intrinsic/world/collision/util:rule_set_util",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:affine_transform_of_geometry",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:aggregate_type",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/world/proto:collision_action_cc_proto",
    ],
)

cc_library(
    name = "collision_checker_world",
    hdrs = ["collision_checker_world.h"],
    deps = [
        "//intrinsic/world:cc_proto",
        "//intrinsic/world:entity",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "collision_types",
    hdrs = ["collision_types.h"],
)

proto_library(
    name = "collision_context_proto",
    srcs = ["collision_context.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/world/proto:collision_action_proto",
    ],
)

py_proto_library(
    name = "collision_context_py_pb2",
    deps = [":collision_context_proto"],
)

go_proto_library(
    name = "collision_context_go_proto",
    importpath = "intrinsic/world/collision/collision_context_go_proto",
    protos = [":collision_context_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/world/proto:collision_action_go_proto",
    ],
)

cc_proto_library(
    name = "collision_context_cc_proto",
    deps = [":collision_context_proto"],
)

cc_library(
    name = "rule_matching",
    srcs = ["rule_matching.cc"],
    hdrs = ["rule_matching.h"],
    deps = [
        ":collision_context_cc_proto",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@intrinsic_apis//intrinsic/world/proto:collision_action_cc_proto",
    ],
)

cc_library(
    name = "coal_collision_checker",
    srcs = ["coal_collision_checker.cc"],
    hdrs = ["coal_collision_checker.h"],
    deps = [
        ":collision_check_cache",
        ":collision_checker",
        ":collision_checker_utils",
        ":collision_checker_world",
        "//intrinsic/eigenmath:random_number_utils",
        "//intrinsic/geometry/api:file_io",
        "//intrinsic/world:cc_proto",
        "//intrinsic/world:entity",
        "//intrinsic/world/collision/util:rule_set_util",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:affine_transform_of_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:shape_factory",
        "//intrinsic_sdk/intrinsic/geometry/internal/util:scale_shape",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "//intrinsic_sdk/intrinsic/util/path_resolver",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@coal",
        "@intrinsic_apis//intrinsic/world/proto:collision_action_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_checker_config_cc_proto",
    ],
)
