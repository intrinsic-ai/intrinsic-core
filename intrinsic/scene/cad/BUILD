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

load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = ["//visibility:private"])

proto_library(
    name = "cad_import_service_proto",
    srcs = ["cad_import_service.proto"],
    deps = [
        "//intrinsic/scene/proto/v1:imported_scene_proto",
    ],
)

cc_proto_library(
    name = "cad_import_service_cc_proto",
    visibility = ["//intrinsic/scene:__subpackages__"],
    deps = [":cad_import_service_proto"],
)

cc_grpc_library(
    name = "cad_import_service_cc_grpc_proto",
    srcs = [":cad_import_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    visibility = ["//intrinsic/scene/service:__subpackages__"],
    deps = [":cad_import_service_cc_proto"],
)

py_proto_library(
    name = "cad_import_service_py_pb2",
    visibility = ["//intrinsic/scene/cad:__subpackages__"],
    deps = [":cad_import_service_proto"],
)

py_grpc_library(
    name = "cad_import_service_py_pb2_grpc",
    srcs = [":cad_import_service_proto"],
    deps = [":cad_import_service_py_pb2"],
)

cc_library(
    name = "cad",
    hdrs = ["cad_importer.h"],
    visibility = ["//intrinsic/scene:__subpackages__"],
    deps = [
        ":cad_import_service_cc_proto",
        "//intrinsic/scene/proto/v1:imported_scene_cc_proto",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "grpc_cad_importer",
    srcs = ["grpc_cad_importer.cc"],
    hdrs = ["grpc_cad_importer.h"],
    visibility = ["//intrinsic/scene/service:__subpackages__"],
    deps = [
        ":cad",
        ":cad_import_service_cc_grpc_proto",
        ":cad_import_service_cc_proto",
        "//intrinsic/world/proto:world_entity_cc_proto",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_cc_proto",
    ],
)
