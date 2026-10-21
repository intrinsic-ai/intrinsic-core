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
    name = "footprint_util",
    srcs = ["footprint_util.cc"],
    hdrs = ["footprint_util.h"],
    deps = [
        "//intrinsic/choreographer/footprints:footprint",
        "//intrinsic/world",
        "//intrinsic/world:world_acl_spec",
        "//intrinsic/world:world_entity_acl_spec",
        "//intrinsic/world/util:entity_search_util",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:affine_transform_of_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "//intrinsic_sdk/intrinsic/geometry/api:io",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_deserializer",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/geometry/proto:transformed_geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_geometry_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

alias(
    name = "skillvalidate",
    actual = "//intrinsic_sdk/intrinsic/skills:skillvalidate",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//intrinsic:public_api_users"],
)

alias(
    name = "skillfix",
    actual = "//intrinsic_sdk/intrinsic/skills:skillfix",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//intrinsic:internal_api_users"],
)
