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
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "pubsub_world_updates_proto",
    srcs = ["pubsub_world_updates.proto"],
    deps = [
        "//third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg:joint_state_proto",
        "@intrinsic_apis//intrinsic/math/proto:tf_message_proto",
    ],
)

cc_proto_library(
    name = "pubsub_world_updates_cc_proto",
    deps = [":pubsub_world_updates_proto"],
)

go_proto_library(
    name = "pubsub_world_updates_go_proto",
    importpath = "intrinsic/world/proto/v1/pubsub_world_updates_go_proto",
    importpath_aliases = ["intrinsic/world/public/proto/v1/pubsub_world_updates_go_proto"],
    protos = [":pubsub_world_updates_proto"],
    deps = [
        "//third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg:joint_state_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:tf_message_go_proto",
    ],
)
