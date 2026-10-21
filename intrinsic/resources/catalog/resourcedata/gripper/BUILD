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

load("//intrinsic/scene/build_defs:sdf_scene_object.bzl", "sdf_scene_object")
load("//intrinsic_sdk/intrinsic/assets/hardware_devices/build_defs:hardware_device.bzl", "intrinsic_hardware_device")
load("//intrinsic_sdk/intrinsic/assets/scene_objects/build_defs:scene_object.bzl", "intrinsic_scene_object")
load("//intrinsic_sdk/intrinsic/assets/services/build_defs:services.bzl", "intrinsic_service")

# Robotiq hand-e gripper.
sdf_scene_object(
    name = "robotiq_gripper",
    src = "robotiq_gripper.sdf",
    sdf_assets = ["//intrinsic_control/intrinsic/models/assets/standard_tooling/robotiq_hande"],
)

intrinsic_service(
    name = "robotiq_pinch_gripper_resource_type_service_node",
    default_config = "robotiq_pinch_gripper_default_config.pbtxt",
    images = [
        "//intrinsic_hardware/intrinsic/hardware/gripper/service:resource-pinch-gripper-server-image.tar",
    ],
    manifest = "robotiq_pinch_gripper_resource_type_service_node_manifest.textproto",
    visibility = ["//visibility:private"],
    deps = [
        "//intrinsic/resources/proto:resource_health_proto",
        "//intrinsic_hardware/intrinsic/hardware/gripper:gripper_equipment_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/service/proto:generic_gripper_proto",
    ],
)

intrinsic_scene_object(
    name = "robotiq_pinch_gripper_resource_type_scene_object_node",
    manifest = "robotiq_pinch_gripper_resource_type_scene_object_node_manifest.textproto",
    scene_object = ":robotiq_gripper",
    visibility = ["//visibility:private"],
)

intrinsic_hardware_device(
    name = "robotiq_pinch_gripper_resource_type",
    assets = [
        ":robotiq_pinch_gripper_resource_type_scene_object_node",
        ":robotiq_pinch_gripper_resource_type_service_node",
    ],
    manifest = ":robotiq_gripper_resource_manifest.textproto",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
)

# Robotiq hand-e grippe without fingers.
sdf_scene_object(
    name = "robotiq_hande_no_fingers",
    src = "robotiq_hande_no_fingers.sdf",
    sdf_assets = ["//intrinsic_control/intrinsic/models/assets/standard_tooling/robotiq_hande"],
)

intrinsic_service(
    name = "robotiq_hande_no_fingers_resource_type_service_node",
    default_config = "robotiq_pinch_gripper_default_config.pbtxt",
    images = [
        "//intrinsic_hardware/intrinsic/hardware/gripper/service:resource-pinch-gripper-server-image.tar",
    ],
    manifest = "robotiq_hande_no_fingers_resource_type_service_node_manifest.textproto",
    visibility = ["//visibility:private"],
    deps = [
        "//intrinsic/resources/proto:resource_health_proto",
        "//intrinsic_hardware/intrinsic/hardware/gripper:gripper_equipment_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/service/proto:generic_gripper_proto",
    ],
)

intrinsic_scene_object(
    name = "robotiq_hande_no_fingers_resource_type_scene_object_node",
    manifest = "robotiq_hande_no_fingers_resource_type_scene_object_node_manifest.textproto",
    scene_object = ":robotiq_hande_no_fingers",
    visibility = ["//visibility:private"],
)

intrinsic_hardware_device(
    name = "robotiq_hande_no_fingers_resource_type",
    assets = [
        ":robotiq_hande_no_fingers_resource_type_scene_object_node",
        ":robotiq_hande_no_fingers_resource_type_service_node",
    ],
    manifest = ":robotiq_hande_no_fingers_resource_manifest.textproto",
    visibility = ["//intrinsic:release_tooling"],
)
