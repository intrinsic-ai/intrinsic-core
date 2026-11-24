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
    name = "kvstore_factory",
    srcs = ["kvstore_factory.cc"],
    hdrs = ["kvstore_factory.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:kvstore",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "kvstore_options",
    hdrs = ["kvstore_options.h"],
    deps = [
        ":kvstore_factory",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/platform/pubsub:storage_location_cc_proto",
    ],
)

cc_library(
    name = "kvstore_utils",
    srcs = ["kvstore_utils.cc"],
    hdrs = ["kvstore_utils.h"],
    visibility = [
        "//intrinsic_perception/intrinsic/perception/cameras/services/v1:__pkg__",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:__pkg__",
        "//intrinsic_perception/intrinsic/perception/storage:__pkg__",
    ],
    deps = [
        ":kvstore_factory",
        ":kvstore_options",
        "//intrinsic_sdk/intrinsic/middleware/buffers:immutable_buffer",
        "//intrinsic_sdk/intrinsic/middleware/buffers:mutable_buffer",
        "//intrinsic_sdk/intrinsic/platform/pubsub:kvstore",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/time:deadline_timeout",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/platform/pubsub:storage_location_cc_proto",
    ],
)

cc_library(
    name = "capture_result_storage",
    srcs = ["capture_result_storage.cc"],
    hdrs = ["capture_result_storage.h"],
    deps = [
        ":kvstore_factory",
        "//intrinsic_perception/intrinsic/perception/cameras:capture_result",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:capture_result",
        "//intrinsic_sdk/intrinsic/platform/pubsub:kvstore",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/time:deadline_timeout",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_result_cc_proto",
        "@intrinsic_apis//intrinsic/platform/pubsub:storage_location_cc_proto",
    ],
)
