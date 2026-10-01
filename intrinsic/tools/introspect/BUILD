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

load("//intrinsic/util/proto/build_defs:descriptor_set.bzl", "proto_source_code_info_transitive_descriptor_set")

package(default_visibility = [
    "//visibility:public",
])

proto_source_code_info_transitive_descriptor_set(
    name = "gzmsgs_descriptor_set",
    deps = ["@gz-msgs//:gzmsgs_proto"],
)

proto_source_code_info_transitive_descriptor_set(
    name = "intrinsic_descriptor_set",
    deps = [
        "//intrinsic/executive/proto:executive_events_proto",
        "//intrinsic/logging/proto:log_item_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_proto",
        "@intrinsic_apis//intrinsic/math/proto:tf_message_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_result_proto",
    ] + [],
)

proto_source_code_info_transitive_descriptor_set(
    name = "ros_descriptor_set",
    deps = [
        "//third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg:camera_info_proto",
        "//third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg:image_proto",
        "//third_party/ros2/ros_interfaces/jazzy/visualization_msgs/msg:marker_array_proto",
    ],
)
