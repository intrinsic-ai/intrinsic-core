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

""" Intrinsic package geometry/service. """

load("//bazel:cc_macros.bzl", "cc_binary", "cc_library")
load("//bazel:container.bzl", "container_image")

package(default_visibility = ["//intrinsic:intrinsic_internal"])

cc_library(
    name = "geometry_client",
    srcs = ["geometry_client.cc"],
    hdrs = ["geometry_client.h"],
    deps = [
        "//intrinsic/geometry/processing:pipeline_config_cc_proto",
        "//intrinsic/longrunning/cc:operations_proxy",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/geometry/proto:geometry_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/geometry/proto:geometry_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googleapis//google/longrunning:longrunning_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_service_types_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_cc_proto",
    ],
)

cc_library(
    name = "geometry_service_impl",
    srcs = ["geometry_service_impl.cc"],
    hdrs = ["geometry_service_impl.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        "//intrinsic/geometry/api:fuse_geometries",
        "//intrinsic/geometry/compatibility:io",
        "//intrinsic/geometry/processing:pipeline",
        "//intrinsic/geometry/processing:pipeline_builder",
        "//intrinsic/geometry/processing:pipeline_config_cc_proto",
        "//intrinsic/geometry/service/util:geometry_data",
        "//intrinsic/geometry/storage:geo_cache",
        "//intrinsic/longrunning/cc:operation_context",
        "//intrinsic/longrunning/cc:operation_scheduler_interface",
        "//intrinsic/stats:tracing_utils",
        "//intrinsic_sdk/intrinsic/geometry/api:apply_transform",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "//intrinsic_sdk/intrinsic/geometry/api:io",
        "//intrinsic_sdk/intrinsic/geometry/api:renderable_generation",
        "//intrinsic_sdk/intrinsic/geometry/proto:geometry_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/geometry/proto:geometry_service_cc_proto",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_builder_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googleapis//google/longrunning:longrunning_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/config/proto:config_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_service_types_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:renderable_cc_proto",
    ],
)

cc_library(
    name = "geometry_server",
    srcs = ["geometry_server.cc"],
    hdrs = ["geometry_server.h"],
    deps = [
        ":geometry_service_impl",
        "//intrinsic/geometry/storage:cas_client",
        "//intrinsic/geometry/storage:cas_storage",
        "//intrinsic/geometry/storage:geo_cache",
        "//intrinsic/longrunning/cc:operation_scheduler",
        "//intrinsic/longrunning/cc:operation_scheduler_interface",
        "//intrinsic/longrunning/cc:operations_service_impl",
        "//intrinsic/longrunning/cc:wrappers",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_cc_grpc_proto",
    ],
)

cc_binary(
    name = "geometry_service",
    srcs = ["geometry_service.cc"],
    deps = [
        ":geometry_server",
        "//intrinsic/stats:opencensus",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:flags",
    ],
)

cc_library(
    name = "in_memory_geometry_service",
    srcs = ["in_memory_geometry_service.cc"],
    hdrs = ["in_memory_geometry_service.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        ":geometry_service_impl",
        "//intrinsic/geometry/storage:geo_cache",
        "//intrinsic/geometry/storage:proxy_storage",
        "//intrinsic/longrunning/cc:operation_scheduler",
        "//intrinsic/longrunning/cc:operations_proxy",
        "//intrinsic_sdk/intrinsic/geometry/storage:in_memory_storage",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

filegroup(
    name = "geomservice_template",
    srcs = ["geomservice.yaml"],
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)

container_image(
    name = "geometry-image",
    base = "//intrinsic/kubernetes:base-image-oci",
    data_path = "/",
    files = [":geometry_service"],
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)
