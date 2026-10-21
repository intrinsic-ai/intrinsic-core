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

cc_library(
    name = "imported_scene_instantiator_h",
    hdrs = ["imported_scene_instantiator.h"],
    visibility = ["//intrinsic/scene/service:__subpackages__"],
    deps = [
        "//intrinsic/kubernetes/acl/cc:client_context",
        "//intrinsic/scene/proto/v1:imported_scene_cc_proto",
        "//intrinsic/scene/proto/v1:scene_object_internal_cc_proto",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "asset_and_resource_instantiator",
    srcs = ["asset_and_resource_instantiator.cc"],
    hdrs = ["asset_and_resource_instantiator.h"],
    visibility = ["//intrinsic/scene/service:__subpackages__"],
    deps = [
        ":imported_scene_instantiator_h",
        "//intrinsic/kubernetes/acl/cc:client_context",
        "//intrinsic/scene/proto/v1:imported_scene_cc_proto",
        "//intrinsic/scene/proto/v1:imported_scene_updates_cc_proto",
        "//intrinsic/scene/proto/v1:scene_object_internal_cc_proto",
        "//intrinsic/stats:scoped_span",
        "//intrinsic_sdk/intrinsic/assets:id_utils",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_cc_proto",
        "//intrinsic_sdk/intrinsic/scene/proto/v1:scene_object_import_cc_proto",
        "//intrinsic_sdk/intrinsic/util/proto:descriptors",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@com_google_googleapis//google/longrunning:longrunning_cc_grpc",
        "@com_google_googleapis//google/longrunning:longrunning_cc_proto",
        "@com_googlesource_code_re2//:re2",
        "@intrinsic_apis//intrinsic/assets/proto:asset_deployment_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_deployment_cc_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_cc_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:entity_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
    ],
)
