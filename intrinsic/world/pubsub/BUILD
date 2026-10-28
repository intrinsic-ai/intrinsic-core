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

package(default_visibility = ["//intrinsic/world:__subpackages__"])

cc_library(
    name = "tf_echo_guard_interfaces",
    hdrs = ["tf_echo_guard_interfaces.h"],
    deps = [
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@intrinsic_apis//intrinsic/math/proto:tf_message_cc_proto",
    ],
)

cc_library(
    name = "tf_echo_guard",
    srcs = ["tf_echo_guard.cc"],
    hdrs = ["tf_echo_guard.h"],
    deps = [
        ":tf_echo_guard_interfaces",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/proto:repeated_field_util",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/synchronization",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/math/proto:tf_message_cc_proto",
    ],
)

cc_library(
    name = "world_tf_publisher",
    srcs = ["world_tf_publisher.cc"],
    hdrs = ["world_tf_publisher.h"],
    visibility = [
        "//intrinsic/logging:__subpackages__",
        "//intrinsic/world:__subpackages__",
    ],
    deps = [
        ":tf_echo_guard_interfaces",
        "//intrinsic/world",
        "//intrinsic/world/proto:tf_associations_cc_proto",
        "//intrinsic/world/service:world_storage",
        "//intrinsic/world/util:tf_frame_util",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/util/proto:repeated_field_util",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)
