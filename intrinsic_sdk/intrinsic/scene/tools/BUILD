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

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "update_scene_object",
    srcs = ["update_scene_object.cc"],
    deps = [
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/geometry/storage:dummy_storage",
        "//intrinsic_sdk/intrinsic/geometry/storage:gzf_storage",
        "//intrinsic_sdk/intrinsic/scene/util:object_user_data",
        "//intrinsic_sdk/intrinsic/scene/util:scene_object_gzf",
        "//intrinsic_sdk/intrinsic/scene/util:scene_object_updates",
        "//intrinsic_sdk/intrinsic/scene/validate:scene_object_validate_geo",
        "//intrinsic_sdk/intrinsic/scene/validate:scene_object_validation",
        "//intrinsic_sdk/intrinsic/util/proto:descriptors",
        "//intrinsic_sdk/intrinsic/util/proto:dynamic_message_parser",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/gzfile",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_cc_proto",
        "@or_tools//ortools/base:file",
    ],
)
