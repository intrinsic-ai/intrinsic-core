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

load("@rosidl_default_generators//:defs.bzl", "ros_message")

package(default_visibility = ["//visibility:public"])

ros_message(
    name = "DiscoveredCamera",
    src = "DiscoveredCamera.msg",
    package = "snapshot_interfaces",
)

ros_message(
    name = "ImageSnapshot",
    src = "ImageSnapshot.msg",
    package = "snapshot_interfaces",
    deps = [
        "@sensor_msgs//msg:CameraInfo",
        "@sensor_msgs//msg:Image",
    ],
)

ros_message(
    name = "ImuSnapshot",
    src = "ImuSnapshot.msg",
    package = "snapshot_interfaces",
    deps = [
        "@sensor_msgs//msg:Imu",
    ],
)

ros_message(
    name = "PointCloud2Snapshot",
    src = "PointCloud2Snapshot.msg",
    package = "snapshot_interfaces",
    deps = [
        "@sensor_msgs//msg:PointCloud2",
    ],
)

ros_message(
    name = "SensorInfo",
    src = "SensorInfo.msg",
    package = "snapshot_interfaces",
    deps = [
        "@geometry_msgs//msg:TransformStamped",
        "@sensor_msgs//msg:CameraInfo",
    ],
)

ros_message(
    name = "TemperatureSnapshot",
    src = "TemperatureSnapshot.msg",
    package = "snapshot_interfaces",
    deps = [
        "@sensor_msgs//msg:Temperature",
    ],
)
