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

package(default_visibility = ["//intrinsic:intrinsic_internal"])

cc_library(
    name = "cas_and_kvstore_storage",
    srcs = ["cas_and_kvstore_storage.cc"],
    hdrs = ["cas_and_kvstore_storage.h"],
    deps = [
        "//intrinsic/geometry/storage:cas_storage",
        "//intrinsic/geometry/storage:geo_cache",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_deserializer",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_storage_uri",
        "//intrinsic_sdk/intrinsic/geometry/storage:kvstore_storage",
        "//intrinsic_sdk/intrinsic/platform/pubsub:kvstore",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:material_cc_proto",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_cc_grpc_proto",
    ],
)
