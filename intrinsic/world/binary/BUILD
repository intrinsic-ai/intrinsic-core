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

load("//bazel:cc_macros.bzl", "cc_binary")
load("//bazel:cc_oci_image.bzl", "cc_oci_image")

package(default_visibility = [
    "//visibility:private",
])

cc_binary(
    name = "world_binary",
    srcs = ["world_binary.cc"],
    linkopts = ["-lresolv"],
    deps = [
        "//intrinsic/conductor",
        "//intrinsic/conductor:resource_world_wrapper_cc",
        "//intrinsic/geometry/storage:cas_client",
        "//intrinsic/geometry/storage:cas_storage",
        "//intrinsic/geometry/storage:geo_cache",
        "//intrinsic/resources/client:resource_registry_client_cc",
        "//intrinsic/stats:opencensus",
        "//intrinsic/util:macros",
        "//intrinsic/util/grpc",
        "//intrinsic/util/status:log_if_error",
        "//intrinsic/world",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic/world/pubsub:tf_echo_guard",
        "//intrinsic/world/pubsub:world_tf_publisher",
        "//intrinsic/world/service:object_world_service",
        "//intrinsic/world/service:world_service_cc_grpc_proto",
        "//intrinsic/world/service:world_storage",
        "//intrinsic/world/service:worldservice_impl",
        "//intrinsic/world/service/updater:world_updater",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/logging:data_logger_client",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_cc_grpc_proto",
    ],
)

filegroup(
    name = "world-template",
    srcs = [
        "world.yaml",
    ],
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
)

cc_oci_image(
    name = "world-image",
    binary = ":world_binary",
    data_path = "/",
    files = [
        "//intrinsic/kubernetes/waitforservices",
    ],
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)
