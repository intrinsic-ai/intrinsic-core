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

""" Intrinsic package geometry/storage. """

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//intrinsic:intrinsic_internal"])

cc_library(
    name = "dummy_storage",
    srcs = ["dummy_storage.cc"],
    hdrs = ["dummy_storage.h"],
    deps = [
        ":geometry_deserializer",
        ":geometry_library",
        ":geometry_serializer",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_fingerprint",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:material_cc_proto",
    ],
)

cc_library(
    name = "geometry_deserializer",
    hdrs = ["geometry_deserializer.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_cc_proto",
    ],
)

cc_library(
    name = "geometry_serializer",
    hdrs = ["geometry_serializer.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_cc_proto",
    ],
)

cc_library(
    name = "gzf_storage",
    srcs = ["gzf_storage.cc"],
    hdrs = ["gzf_storage.h"],
    copts = [
        "-fexceptions",
        "-frounding-math",
    ],
    deps = [
        ":geometry_deserializer",
        ":geometry_library",
        ":geometry_serializer",
        "//intrinsic_runtime/intrinsic/geometry/compatibility:io",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_fingerprint",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "//intrinsic_sdk/intrinsic/geometry/api:io",
        "//intrinsic_sdk/intrinsic/geometry/api:renderable",
        "//intrinsic_sdk/intrinsic/geometry/api:renderable_generation",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/gzfile",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:lazy_exact_geometry_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:material_cc_proto",
    ],
)

cc_library(
    name = "in_memory_storage",
    srcs = ["in_memory_storage.cc"],
    hdrs = ["in_memory_storage.h"],
    deps = [
        ":geometry_deserializer",
        ":geometry_library",
        ":geometry_serializer",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_fingerprint",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "//intrinsic_sdk/intrinsic/geometry/api:renderable",
        "//intrinsic_sdk/intrinsic/geometry/api:renderable_generation",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:material_cc_proto",
    ],
)

cc_library(
    name = "geometry_library",
    hdrs = ["geometry_library.h"],
    deps = [
        ":geometry_deserializer",
        ":geometry_serializer",
    ],
)

cc_library(
    name = "geometry_storage_uri",
    srcs = ["geometry_storage_uri.cc"],
    hdrs = ["geometry_storage_uri.h"],
    visibility = [
        "//intrinsic_runtime/intrinsic/geometry/storage:__pkg__",
        "//intrinsic_sdk/intrinsic/geometry/storage:__pkg__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_cc_proto",
    ],
)

cc_library(
    name = "kvstore_storage",
    srcs = ["kvstore_storage.cc"],
    hdrs = ["kvstore_storage.h"],
    deps = [
        ":geometry_library",
        ":geometry_storage_uri",
        "//intrinsic_sdk/intrinsic/geometry/api:io",
        "//intrinsic_sdk/intrinsic/geometry/api:renderable_generation",
        "//intrinsic_sdk/intrinsic/platform/pubsub:kvstore",
        "//intrinsic_sdk/intrinsic/util:hash",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
    ],
)
