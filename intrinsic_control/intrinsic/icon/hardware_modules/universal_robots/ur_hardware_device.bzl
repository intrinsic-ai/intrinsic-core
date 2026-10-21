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

"""Starlark macro for defining Universal Robots (UR) hardware devices.

This module defines the `ur_hardware_device` macro, which encapsulates manifest template
expansion (internal and intrinsic-core variants) and registers standard asset
and execution targets (HardwareDevice, ServiceNode, SceneObject).
"""

load("@bazel_skylib//rules:expand_template.bzl", "expand_template")
load("//intrinsic_control/intrinsic/icon/hal/bzl:resources.bzl", "hardware_module_manifest")
load("//intrinsic_sdk/intrinsic/assets/hardware_devices/build_defs:hardware_device.bzl", "intrinsic_hardware_device")
load("//intrinsic_sdk/intrinsic/assets/scene_objects/build_defs:scene_object.bzl", "intrinsic_scene_object")
load("//intrinsic_sdk/intrinsic/assets/services/build_defs:services.bzl", "intrinsic_service")

def ur_hardware_device(name, display_name, is_core_variant = False):
    """Creates service, scene object and hardware devices including manifests for a UR robot.

    Args:
        name: The target name of the hardware device (e.g., "ur3e_hardware_module", "ur5e_hardware_module_core").
        display_name: The display name of the robot (e.g., "UR3e", "UR20").
        is_core_variant: Set to True to compile for intrinsic-core variant.
    """

    # Set core suffixes
    core_suffix = "_core" if is_core_variant else ""
    core_display_suffix = " Core" if is_core_variant else ""
    core_description = "\\nintrinsic-core variant." if is_core_variant else ""

    # Set OCI images
    image_tar = "//intrinsic_control/intrinsic/icon/hardware_modules/universal_robots:ur_module_image.tar"

    # Target and File Names
    manifest_name = name + "_manifest.textproto"
    service_manifest_name = name + "_service_node_manifest.textproto"
    scene_object_manifest_name = name + "_scene_object_node_manifest.textproto"
    scene_object_target_name = name + "_scene_object_node"

    # --- 1. Template Expansions ---

    # Hardware Device Manifest
    expand_template(
        name = "gen_" + name + "_manifest",
        template = "//intrinsic_control/intrinsic/icon/hardware_modules/universal_robots:hardware_module_manifest.textproto.tpl",
        out = manifest_name,
        substitutions = {
            "@CORE_DESCRIPTION@": core_description,
            "@CORE_DISPLAY_SUFFIX@": core_display_suffix,
            "@CORE_SUFFIX@": core_suffix,
            "@ROBOT_DISPLAY_NAME@": display_name,
            "@ROBOT_ID@": name,
        },
    )

    # Service Node Manifest
    expand_template(
        name = "gen_" + name + "_service_node_manifest",
        template = "//intrinsic_control/intrinsic/icon/hardware_modules/universal_robots:hardware_module_service_node_manifest.textproto.tpl",
        out = service_manifest_name,
        substitutions = {
            "@CORE_DESCRIPTION@": core_description,
            "@CORE_DISPLAY_SUFFIX@": core_display_suffix,
            "@CORE_SUFFIX@": core_suffix,
            "@ROBOT_DISPLAY_NAME@": display_name,
            "@ROBOT_ID@": name,
        },
    )

    # Generate Scene Object Manifest
    expand_template(
        name = "gen_" + scene_object_target_name + "_manifest",
        template = "//intrinsic_control/intrinsic/icon/hardware_modules/universal_robots:hardware_module_scene_object_node_manifest.textproto.tpl",
        out = scene_object_manifest_name,
        substitutions = {
            "@CORE_DESCRIPTION@": core_description,
            "@ROBOT_DISPLAY_NAME@": display_name,
            "@ROBOT_ID@": name,
        },
    )

    # --- 2. Rule Target Registrations ---

    # Shared scene object node target
    intrinsic_scene_object(
        name = scene_object_target_name,
        manifest = ":" + scene_object_manifest_name,
        scene_object = "//intrinsic_control/intrinsic/models/robot_definitions/ur:" + display_name.lower() + "_scene_object",
        visibility = ["//visibility:private"],
    )

    # Compiled service manifest target
    hardware_module_manifest(
        name = name + "_service_manifest_compiled",
        image = image_tar,
        image_sim = "//intrinsic_control/intrinsic/icon/hardware_modules/gazebo_hwm_stub:gazebo_hwm_stub_image.tar",
        manifest = ":" + service_manifest_name,
        provides_service_inspection = True,
        service_proto_prefixes = ["/intrinsic_proto.world.RobotCalibrationDataService/"],
    )

    # Service node execution target
    intrinsic_service(
        name = name + "_service_node",
        default_config = "//intrinsic_control/intrinsic/icon/hardware_modules/universal_robots:default_config_with_scene_object",
        images = [
            image_tar,
            "//intrinsic_control/intrinsic/icon/hardware_modules/gazebo_hwm_stub:gazebo_hwm_stub_image.tar",
        ],
        manifest = ":" + name + "_service_manifest_compiled",
        visibility = ["//visibility:private"],
        deps = [
            "//intrinsic_control/intrinsic/icon/hardware_modules/universal_robots:config_proto",
            "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
            "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_config_proto",
            "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_inspection_proto",
            "@intrinsic_apis//intrinsic/icon/hardware_modules/sim_bus:sim_bus_hardware_module_proto",
            "//intrinsic/world/service/robot_calibration:robot_calibration_data_service_proto",
        ],
    )

    # Main Hardware Device Target
    intrinsic_hardware_device(
        name = name,
        assets = [
            ":" + scene_object_target_name,
            ":" + name + "_service_node",
        ],
        manifest = ":" + manifest_name,
    )
