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

# Component to log structured data with context.
package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "log_item_builder",
    srcs = ["log_item_builder.cc"],
    hdrs = ["log_item_builder.h"],
    deps = [
        "//intrinsic/world:cc_proto",
        "//intrinsic/world/service:world_compatibility_service_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_proto",
        "//intrinsic_sdk/intrinsic/logging/proto:log_item_cc_proto",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_rpc",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_googleapis//google/longrunning:longrunning_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/executive/proto:clips_snapshot_cc_proto",
        "@intrinsic_apis//intrinsic/executive/proto:log_items_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
        "@intrinsic_apis//intrinsic/logging/errors/proto:error_report_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:blob_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:flowstate_event_cc_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_cc_proto",
    ],
)
