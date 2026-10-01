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

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic/skills/build_defs:skill.bzl", "cc_skill", "skill_manifest")

package(default_visibility = [
    "//visibility:public",
])

# ----------------------------------------------------------------------------
# Skill definition for 'capture_images'

proto_library(
    name = "capture_images_proto",
    srcs = ["capture_images.proto"],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "@com_google_protobuf//:duration_proto",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_settings_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_data_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:post_processing_proto",
        "@intrinsic_apis//intrinsic/platform/pubsub:storage_location",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ],
)

cc_proto_library(
    name = "capture_images_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [":capture_images_proto"],
)

py_proto_library(
    name = "capture_images_py_pb2",
    visibility = [
        "//visibility:public",
    ],
    deps = [":capture_images_proto"],
)

cc_library(
    name = "capture_images",
    srcs = ["capture_images.cc"],
    hdrs = ["capture_images.h"],
    deps = [
        ":capture_images_cc_proto",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/grpc:connection_params",
        "//intrinsic/util/proto:repeated_field_util",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_perception/intrinsic/perception/cameras:camera",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_identifier",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_identifier",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/skills/cc:execute_context",
        "//intrinsic_sdk/intrinsic/skills/cc:execute_request",
        "//intrinsic_sdk/intrinsic/skills/cc:get_footprint_context",
        "//intrinsic_sdk/intrinsic/skills/cc:get_footprint_request",
        "//intrinsic_sdk/intrinsic/skills/cc:preview_context",
        "//intrinsic_sdk/intrinsic/skills/cc:preview_request",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/util/thread:thread_pool",
        "//intrinsic_sdk/intrinsic/util/time:deadline_timeout",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/objects:transform_node",
        "//intrinsic_sdk/intrinsic/world/objects:world_object",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:differencer",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_settings_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_data_cc_proto",
        "@intrinsic_apis//intrinsic/platform/pubsub:storage_location_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

skill_manifest(
    name = "capture_images_manifest",
    src = "capture_images_manifest.textproto",
    visibility = [
        "//visibility:public",
    ],
    deps = [":capture_images_proto"],
)

cc_skill(
    name = "capture_images_skill",
    manifest = ":capture_images_manifest",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        ":capture_images",
        ":capture_images_cc_proto",
    ],
)

go_proto_library(
    name = "capture_images_go_proto",
    importpath = "intrinsic/perception/skills/capture_images_go_proto",
    proto = ":capture_images_proto",
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_settings_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_data_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:post_processing_go_proto",
        "@intrinsic_apis//intrinsic/platform/pubsub:storage_location_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
    ],
)
