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

load("//intrinsic_control/intrinsic/icon/hal/bzl:resources.bzl", "hardware_module_manifest")
load("//intrinsic_sdk/intrinsic/assets/hardware_devices/build_defs:hardware_device.bzl", "intrinsic_hardware_device")
load("//intrinsic_sdk/intrinsic/assets/scene_objects/build_defs:scene_object.bzl", "intrinsic_scene_object")
load("//intrinsic_sdk/intrinsic/assets/services/build_defs:services.bzl", "intrinsic_service")

package(default_visibility = ["//visibility:public"])

hardware_module_manifest(
    name = "abb_irb_1300_10_115_hardware_module_manifest",
    image = "//third_party/abb_hardware_module/abb_hwm:abb_hardware_module_image.tar",
    image_sim = "//intrinsic_control/intrinsic/icon/hardware_modules/gazebo_hwm_stub:gazebo_hwm_stub_image.tar",
    manifest = "abb_irb_1300_10_115_hardware_module_service_node_manifest.textproto",
)

intrinsic_service(
    name = "abb_irb_1300_10_115_hardware_module_service_node",
    default_config = ":abb_irb_1300_10_115_hardware_module_config.pbtxt",
    images = [
        "//third_party/abb_hardware_module/abb_hwm:abb_hardware_module_image.tar",
        "//intrinsic_control/intrinsic/icon/hardware_modules/gazebo_hwm_stub:gazebo_hwm_stub_image.tar",
    ],
    manifest = ":abb_irb_1300_10_115_hardware_module_manifest",
    visibility = ["//visibility:private"],
    deps = [
        "//third_party/abb_hardware_module/abb_hwm:abb_config_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
        "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_config_proto",
        "@intrinsic_apis//intrinsic/icon/hardware_modules/sim_bus:sim_bus_hardware_module_proto",
    ],
)

intrinsic_scene_object(
    name = "abb_irb_1300_10_115_hardware_module_scene_object_node",
    manifest = "abb_irb_1300_10_115_hardware_module_scene_object_manifest.textproto",
    scene_object = "//intrinsic_control/intrinsic/models/robot_definitions/abb:irb1300_10_115_scene_object",
    visibility = ["//visibility:private"],
)

intrinsic_hardware_device(
    name = "abb_irb_1300_10_115_hardware_module",
    assets = [
        ":abb_irb_1300_10_115_hardware_module_scene_object_node",
        ":abb_irb_1300_10_115_hardware_module_service_node",
    ],
    manifest = "abb_irb_1300_10_115_hardware_module_manifest.textproto",
)

filegroup(
    name = "abb_robots_core",
    srcs = [
        "README.md",
        ":abb_irb_1300_10_115_hardware_module",
    ],
)
