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

""" Intrinsic package geometry/api. """

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//intrinsic:intrinsic_internal"])

cc_library(
    name = "io",
    srcs = ["io.cc"],
    hdrs = ["io.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "//intrinsic_sdk/intrinsic/geometry/api:renderable",
        "//intrinsic_sdk/intrinsic/geometry/api:renderable_generation",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/geometry/internal/point_cloud:point_cloud_riegeli_coder",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "//intrinsic_sdk/intrinsic/geometry/shapes:geometry_shape",
        "//intrinsic_sdk/intrinsic/util/object_store",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@assimp",
        "@com_google_protobuf//:protobuf_lite",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:lazy_exact_geometry_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:point_cloud_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:primitives_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:renderable_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:triangle_mesh_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:material_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_cc_proto",
    ],
)
