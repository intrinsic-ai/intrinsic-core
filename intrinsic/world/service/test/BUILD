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
    default_testonly = True,
    default_visibility = [
        "//visibility:public",
    ],
)

cc_library(
    name = "world_service_fake",
    testonly = True,
    srcs = ["world_service_fake.cc"],
    hdrs = ["world_service_fake.h"],
    deps = [
        "//intrinsic/geometry/storage:proxy_storage",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world",
        "//intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic/world/service:object_world_service",
        "//intrinsic/world/service:world_mutex",
        "//intrinsic/world/service:world_service_cc_grpc_proto",
        "//intrinsic/world/service:world_storage",
        "//intrinsic/world/service:worldservice_impl",
        "//intrinsic_sdk/intrinsic/geometry/api:shape_factory",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/geometry/storage:in_memory_storage",
        "//intrinsic_sdk/intrinsic/world/gzfile",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_cc_proto",
    ],
)
