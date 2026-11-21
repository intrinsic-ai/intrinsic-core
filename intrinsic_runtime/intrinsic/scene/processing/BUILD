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
    name = "transform_imported_scene",
    srcs = ["transform_imported_scene.cc"],
    hdrs = ["transform_imported_scene.h"],
    visibility = ["//intrinsic_runtime/intrinsic/scene/service:__pkg__"],
    deps = [
        "//intrinsic_runtime/intrinsic/scene/proto/v1:imported_scene_cc_proto",
        "//intrinsic_runtime/intrinsic/scene/proto/v1:imported_scene_updates_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/scene/processing:transform_scene_object",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_cc_proto",
    ],
)

cc_library(
    name = "scale_imported_scene",
    srcs = ["scale_imported_scene.cc"],
    hdrs = ["scale_imported_scene.h"],
    visibility = ["//intrinsic_runtime/intrinsic/scene/service:__pkg__"],
    deps = [
        "//intrinsic_runtime/intrinsic/scene/proto/v1:imported_scene_cc_proto",
        "//intrinsic_runtime/intrinsic/scene/proto/v1:imported_scene_updates_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/scene/processing:scale_scene_object",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/math/proto:point_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_cc_proto",
    ],
)
