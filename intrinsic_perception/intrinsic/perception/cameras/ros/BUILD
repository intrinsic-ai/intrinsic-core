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

load("@rosidl_default_generators//:defs.bzl", "cc_ros_library", "ros_library")
load("@rules_cc//cc:defs.bzl", "cc_test")

package(default_visibility = ["//visibility:public"])

SNAPSHOT_INTERFACES = [
    "//intrinsic_perception/intrinsic/perception/cameras/ros/snapshot_interfaces/msg:DiscoveredCamera",
    "//intrinsic_perception/intrinsic/perception/cameras/ros/snapshot_interfaces/msg:ImageSnapshot",
    "//intrinsic_perception/intrinsic/perception/cameras/ros/snapshot_interfaces/msg:ImuSnapshot",
    "//intrinsic_perception/intrinsic/perception/cameras/ros/snapshot_interfaces/msg:PointCloud2Snapshot",
    "//intrinsic_perception/intrinsic/perception/cameras/ros/snapshot_interfaces/msg:SensorInfo",
    "//intrinsic_perception/intrinsic/perception/cameras/ros/snapshot_interfaces/msg:TemperatureSnapshot",
    "//intrinsic_perception/intrinsic/perception/cameras/ros/snapshot_interfaces/srv:Describe",
    "//intrinsic_perception/intrinsic/perception/cameras/ros/snapshot_interfaces/srv:Discover",
    "//intrinsic_perception/intrinsic/perception/cameras/ros/snapshot_interfaces/srv:LegacyDiscover",
    "//intrinsic_perception/intrinsic/perception/cameras/ros/snapshot_interfaces/srv:Snapshot",
]

ros_library(
    name = "snapshot_interfaces",
    deps = SNAPSHOT_INTERFACES,
)

cc_ros_library(
    name = "snapshot_interfaces_cpp",
    deps = SNAPSHOT_INTERFACES,
)

# Exercise the generated code and statically linked typesupport at runtime.
cc_test(
    name = "snapshot_interfaces_test",
    size = "small",
    srcs = ["snapshot_interfaces_test.cc"],
    deps = [
        ":snapshot_interfaces_cpp",
        "@rclcpp//:rclcpp_library",
        "@rosidl_typesupport_cpp//:rosidl_typesupport_cpp_library",
    ],
)
