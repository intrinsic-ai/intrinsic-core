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

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "image",
    srcs = ["image.cc"],
    hdrs = ["image.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/core:encoding",
        "//intrinsic_perception/intrinsic/perception/core:image",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/core:single_thread_executor",
        "//intrinsic_sdk/intrinsic/logging:data_logger_client",
        "//intrinsic_sdk/intrinsic/logging/proto:log_item_cc_proto",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/logging/proto:blob_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
    ],
)

cc_library(
    name = "pattern_detection",
    srcs = ["pattern_detection.cc"],
    hdrs = ["pattern_detection.h"],
    deps = [
        ":image",
        "//intrinsic_perception/intrinsic/perception/cameras:capture_result_helper",
        "//intrinsic_perception/intrinsic/perception/cameras:sensor_image",
        "//intrinsic_perception/intrinsic/perception/core:drawing",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/core:single_thread_executor",
        "//intrinsic_perception/intrinsic/perception/proto_conversion:vector",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pattern_detection_result_cc_proto",
    ],
)

cc_library(
    name = "sensor_image",
    srcs = ["sensor_image.cc"],
    hdrs = ["sensor_image.h"],
    deps = [
        ":image",
        "//intrinsic_perception/intrinsic/perception/cameras:sensor_image",
        "//intrinsic_perception/intrinsic/perception/core:single_thread_executor",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
    ],
)
