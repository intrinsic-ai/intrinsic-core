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
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

# keep
proto_library(
    name = "time_interval_proto",
    srcs = ["time_interval.proto"],
    strip_import_prefix = "/intrinsic_motion_planning",
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
    deps = [
        "@com_google_protobuf//:struct_proto",
    ],
)

go_proto_library(
    name = "time_interval_go_proto",
    importpath = "intrinsic_motion_planning/intrinsic/motion_planning/service/debug/time_interval_go_proto",
    proto = ":time_interval_proto",
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
)

cc_proto_library(
    name = "time_interval_cc_proto",
    deps = [":time_interval_proto"],
)

cc_library(
    name = "timing_debug_logger",
    srcs = ["timing_debug_logger.cc"],
    hdrs = ["timing_debug_logger.h"],
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
    deps = [
        ":time_interval_cc_proto",
        "//intrinsic_sdk/intrinsic/logging:data_logger_client",
        "//intrinsic_sdk/intrinsic/logging/proto:log_item_cc_proto",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:json_util",
        "@com_google_protobuf//:protobuf",
    ],
)
