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

exports_files(["check_objects_test.sh"])

cc_binary(
    name = "update_world",
    srcs = ["update_world.cc"],
    deps = [
        "//intrinsic/world",
        "//intrinsic/world:entity",
        "//intrinsic/world:print_world_lib",
        "//intrinsic/world:update_world",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/proto:world_updates_cc_proto",
        "//intrinsic/world/service/objects:object_world_updates_utils",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:labels",
        "//intrinsic_sdk/intrinsic/world/gzfile",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:strong_int",
    ],
)

cc_binary(
    name = "check_objects",
    srcs = ["check_objects.cc"],
    deps = [
        "//intrinsic/world",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:print_object_world",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/gzfile",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

cc_binary(
    name = "compose_world_check",
    srcs = ["compose_world_check.cc"],
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        "//intrinsic/config/proto:resource_set_cc_proto",
        "//intrinsic/resources/proto:geometric_resource_data_cc_proto",
        "//intrinsic/resources/proto:resource_instance_cc_proto",
        "//intrinsic/simulation/world:world_to_sdf",
        "//intrinsic/world/util:compose_world_util",
        "//intrinsic_control/intrinsic/icon/release:file_helpers",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/geometry/storage:dummy_storage",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)
