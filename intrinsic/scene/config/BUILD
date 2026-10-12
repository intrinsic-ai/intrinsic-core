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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "scene_object_config",
    srcs = ["scene_object_config.cc"],
    hdrs = ["scene_object_config.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/scene/util:scene_object_updates",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/scene/proto/v1:collision_rules_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_cc_proto",
    ],
)

cc_library(
    name = "scene_object_config_updater",
    srcs = ["scene_object_config_updater.cc"],
    hdrs = ["scene_object_config_updater.h"],
    deps = [
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_cc_proto",
    ],
)
