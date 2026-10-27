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

load("//bazel:cc_macros.bzl", "cc_binary", "cc_library")
load("//bazel:cc_oci_image.bzl", "cc_oci_image")
load(
    "//intrinsic_sdk/intrinsic/util/proto/build_defs:descriptor_set.bzl",
    "proto_source_code_info_transitive_descriptor_set",
)
load(":well_known_proto_targets.bzl", "WELL_KNOWN_PROTO_TARGETS")

proto_source_code_info_transitive_descriptor_set(
    name = "well_known_types",
    deps = WELL_KNOWN_PROTO_TARGETS,
)

cc_library(
    name = "proto_builder_service",
    srcs = ["proto_builder_service.cc"],
    hdrs = ["proto_builder_service.h"],
    data = [":well_known_types"],
    deps = [
        "//intrinsic/util/proto:compile_utils",
        "//intrinsic_runtime/intrinsic/proto_tools/util:descriptor_pool_loader",
        "//intrinsic_sdk/intrinsic/util:log_lines",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:differencer",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/executive/proto:proto_builder_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/executive/proto:proto_builder_cc_proto",
    ],
)

cc_binary(
    name = "proto_builder",
    srcs = ["proto_builder_main.cc"],
    deps = [
        ":proto_builder_service",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/strings:str_format",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
    ],
)

cc_oci_image(
    name = "proto-builder-image",
    binary = ":proto_builder",
    data_path = "/",
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)
