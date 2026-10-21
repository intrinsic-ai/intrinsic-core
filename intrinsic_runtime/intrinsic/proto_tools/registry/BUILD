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
    "//intrinsic/util/proto/build_defs:descriptor_set.bzl",
    "proto_source_code_info_transitive_descriptor_set",
)
load("//intrinsic/util/proto/build_defs:proto_data.bzl", "proto_data")
load(
    "//intrinsic_runtime/intrinsic/proto_tools/builder:well_known_proto_targets.bzl",
    "WELL_KNOWN_PROTO_TARGETS",
)

proto_source_code_info_transitive_descriptor_set(
    name = "common_types",
    deps = WELL_KNOWN_PROTO_TARGETS + [
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_proto",
    ],
)

proto_data(
    name = "status_specs_message",
    src = "//intrinsic_runtime/intrinsic/proto_tools/registry:proto_registry.status_specs.pbtxt",
    proto_deps = [
        "@intrinsic_apis//intrinsic/assets/proto:status_spec_proto",
    ],
    proto_name = "intrinsic_proto.assets.StatusSpecs",
)

cc_library(
    name = "extended_status_codes",
    hdrs = ["extended_status_codes.h"],
)

cc_library(
    name = "file_descriptor_set_name",
    srcs = ["file_descriptor_set_name.cc"],
    hdrs = ["file_descriptor_set_name.h"],
    deps = [
        "//intrinsic/util:string_type",
        "//intrinsic_runtime/intrinsic/proto_tools/proto:proto_registry_internal_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "resolver_common",
    srcs = ["resolver_common.cc"],
    hdrs = ["resolver_common.h"],
    data = [
        ":common_types",
    ],
    deps = [
        "//intrinsic/util/proto:parsed_type_url",
        "//intrinsic/util/proto:type_url",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_specs",
        "//intrinsic_runtime/intrinsic/proto_tools/registry:resolver",
        "//intrinsic_runtime/intrinsic/proto_tools/util:descriptor_pool_loader",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "proto_registry_service",
    srcs = ["proto_registry_service.cc"],
    hdrs = ["proto_registry_service.h"],
    deps = [
        "//intrinsic/util/proto:parsed_type_url",
        "//intrinsic/util/proto:type_url",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_macros_grpc",
        "//intrinsic/util/status:status_specs",
        "//intrinsic_runtime/intrinsic/proto_tools/registry:file_descriptor_set_name",
        "//intrinsic_runtime/intrinsic/proto_tools/registry:resolver",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/proto_tools/proto:proto_registry_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/proto_tools/proto:proto_registry_cc_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_cc_proto",
    ],
)

cc_library(
    name = "resolver",
    srcs = [
        "resolver.cc",
        "resolver_skill.cc",
    ],
    hdrs = [
        "resolver.h",
        "resolver_skill.h",
    ],
    deps = [
        "//intrinsic/skills/internal:skill_registry_client_interface",
        "//intrinsic/util/proto:parsed_type_url",
        "//intrinsic/util/proto:type_url",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_specs",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
    ],
)

cc_library(
    name = "resolver_asset",
    srcs = ["resolver_asset.cc"],
    hdrs = ["resolver_asset.h"],
    deps = [
        "//intrinsic/util/grpc",
        "//intrinsic/util/proto:parsed_type_url",
        "//intrinsic/util/proto:type_url",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_specs",
        "//intrinsic_runtime/intrinsic/proto_tools/registry:resolver",
        "//intrinsic_sdk/intrinsic/assets:id_utils",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/proto:view_cc_proto",
    ],
)

cc_library(
    name = "registry_pubsub",
    srcs = ["registry_pubsub.cc"],
    hdrs = ["registry_pubsub.h"],
    deps = [
        "//intrinsic/platform/pubsub",
        "//intrinsic/platform/pubsub:queryable",
        "//intrinsic/util/proto:parsed_type_url",
        "//intrinsic/util/proto:type_url",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_specs",
        "//intrinsic_runtime/intrinsic/proto_tools/proto:proto_registry_pubsub_cc_proto",
        "//intrinsic_runtime/intrinsic/proto_tools/registry:resolver",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@boringssl//:crypto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_binary(
    name = "proto-registry",
    srcs = ["proto_registry_main.cc"],
    data = [
        ":status_specs_message",
    ],
    deps = [
        "//intrinsic/skills/internal:skill_registry_client",
        "//intrinsic/stats:opencensus",
        "//intrinsic/util/path_resolver",
        "//intrinsic/util/status:return",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_specs",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_runtime/intrinsic/proto_tools/registry:extended_status_codes",
        "//intrinsic_runtime/intrinsic/proto_tools/registry:proto_registry_service",
        "//intrinsic_runtime/intrinsic/proto_tools/registry:registry_pubsub",
        "//intrinsic_runtime/intrinsic/proto_tools/registry:resolver",
        "//intrinsic_runtime/intrinsic/proto_tools/registry:resolver_asset",
        "//intrinsic_runtime/intrinsic/proto_tools/registry:resolver_common",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/thread",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
    ],
)

# Proto Registry service
cc_oci_image(
    name = "proto-registry-image",
    binary = ":proto-registry",
    data_path = "/",
    visibility = ["//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__"],
)
