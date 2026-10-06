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

load("@rosidl_default_generators//:defs.bzl", "ros_service")

package(default_visibility = ["//visibility:public"])

ros_service(
    name = "Describe",
    src = "Describe.srv",
    package = "snapshot_interfaces",
    deps = [
        "//intrinsic_perception/intrinsic/perception/cameras/ros/snapshot_interfaces/msg:SensorInfo",
    ],
)

ros_service(
    name = "Discover",
    src = "Discover.srv",
    package = "snapshot_interfaces",
    deps = [
        "//intrinsic_perception/intrinsic/perception/cameras/ros/snapshot_interfaces/msg:DiscoveredCamera",
    ],
)

ros_service(
    name = "LegacyDiscover",
    src = "LegacyDiscover.srv",
    package = "snapshot_interfaces",
)

ros_service(
    name = "Snapshot",
    src = "Snapshot.srv",
    package = "snapshot_interfaces",
    deps = [
        "//intrinsic_perception/intrinsic/perception/cameras/ros/snapshot_interfaces/msg:ImageSnapshot",
        "//intrinsic_perception/intrinsic/perception/cameras/ros/snapshot_interfaces/msg:ImuSnapshot",
        "//intrinsic_perception/intrinsic/perception/cameras/ros/snapshot_interfaces/msg:PointCloud2Snapshot",
        "//intrinsic_perception/intrinsic/perception/cameras/ros/snapshot_interfaces/msg:TemperatureSnapshot",
        "@builtin_interfaces//msg:Duration",
        "@builtin_interfaces//msg:Time",
    ],
)
