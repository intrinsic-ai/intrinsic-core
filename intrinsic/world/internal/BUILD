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

package(default_visibility = [
    "//intrinsic/world:__subpackages__",
])

cc_library(
    name = "inner_world",
    srcs = ["inner_world.cc"],
    hdrs = ["inner_world.h"],
    deps = [
        ":world_acl",
        "//intrinsic/world:entity",
        "//intrinsic/world:world_acl_spec",
        "//intrinsic/world:world_entity_acl_spec",
        "//intrinsic/world/collision:coal_collision_checker",
        "//intrinsic/world/collision:collision_check_cache",
        "//intrinsic/world/collision:collision_checker",
        "//intrinsic/world/collision:collision_checker_utils",
        "//intrinsic/world/collision:collision_checker_world",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/world/proto:collision_checker_config_cc_proto",
        "@or_tools//ortools/base:strong_int",
        "@ros2_geometry2//:tf2",
    ],
)

cc_library(
    name = "world_acl",
    srcs = ["world_acl.cc"],
    hdrs = ["world_acl.h"],
    deps = [
        "//intrinsic/world:world_acl_spec",
        "//intrinsic/world:world_entity_acl_spec",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/status:statusor",
    ],
)
