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

"""Basler hardware device macro for SDF models, scene objects, and devices."""

load("@bazel_skylib//rules:expand_template.bzl", "expand_template")
load("//bazel:xacro.bzl", "xacro_file")
load("//intrinsic_sdk/intrinsic/assets/hardware_devices/build_defs:hardware_device.bzl", "intrinsic_hardware_device")
load("//intrinsic_sdk/intrinsic/assets/scene_objects/build_defs:scene_object.bzl", "intrinsic_scene_object")
load("//intrinsic_sdk/intrinsic/scene/build_defs:sdf_scene_object.bzl", "sdf_scene_object")

def basler_hardware_device(
        name,
        display_name,
        fov = 69.322305,
        mass = 0.1,
        resolution = (1936, 1216),
        working_distance = (0.02, 300.0),
        interface = "gige",
        model = None,
        asset_name = None,
        scene_object_asset_name = None,
        service = None,
        service_asset = None,
        visibility = None):
    """Declares 3D CAD model, scene object, and hardware device for a Basler camera.

    Creates the following public targets:
      - `:{model}_hardware_device`: intrinsic_hardware_device asset
      - `:{model}_hardware_device_manifest_template`: expanded hardware device manifest

    And private intermediate targets:
      - `:{model}_xacro`: xacro generation of `:{model}.sdf`
      - `:{model}_sdf`: sdf_scene_object node
      - `:{model}_scene_object_manifest_template`: expanded scene object manifest
      - `:{model}_scene_object`: intrinsic_scene_object asset
      - `:basler_{series}_gen`: procedural GLB mesh generator genrule
      - `:basler_{series}_body_xacro`: xacro generation of `:basler_{series}_body.sdf`
      - `:basler_{series}_assets`: filegroup containing body SDFs and mesh GLBs

    Args:
        name: Model identifier (e.g. 'basler_ace', 'basler_ace2').
        display_name: Human-readable display name (e.g. 'Basler ace', 'Basler ace 2').
        fov: Horizontal FOV in degrees.
        mass: Camera body mass in kg.
        resolution: (width, height) resolution tuple.
        working_distance: (near, far) working distance tuple in meters.
        interface: Camera interface ('gige' or 'usb').
        model: Optional model series override ('ace' or 'ace2').
        asset_name: Optional hardware device asset ID override.
        scene_object_asset_name: Optional scene object asset ID override.
        service: Optional camera service target override.
        service_asset: Optional camera service asset ID override.
        visibility: Hardware device visibility.
    """
    model_name = name[:-len("_hardware_device")] if name.endswith("_hardware_device") else name

    if not model:
        if "ace2" in model_name:
            model = "ace2"
        elif "ace" in model_name:
            model = "ace"
        else:
            fail("Unknown Basler model series for '%s'. Expected 'ace' or 'ace2'." % model_name)

    if not asset_name:
        if model_name == "basler_ace":
            asset_name = "basler_camera"
        elif model_name == "basler_ace_usb":
            asset_name = "basler_camera_usb"
        else:
            asset_name = model_name

    if not scene_object_asset_name:
        if model_name in ("basler_ace", "basler_ace_usb"):
            scene_object_asset_name = "basler_camera_scene_object"
        else:
            scene_object_asset_name = "%s_scene_object" % model_name

    if not service:
        if interface == "gige":
            service = "//intrinsic_perception/intrinsic/perception/cameras/services:genicam_gige_vision_camera_service"
            service_asset = service_asset or "ai.intrinsic.genicam_gige_vision_camera_service"
        elif interface == "usb":
            service = "//intrinsic_perception/intrinsic/perception/cameras/hardware_devices:basler_usb_service"
            service_asset = service_asset or "ai.intrinsic.basler_camera_usb_service"
        else:
            fail("Unknown interface '%s' for '%s'. Expected 'gige' or 'usb'." % (interface, model_name))

    width, height = resolution
    near, far = working_distance

    # 1. Procedural 3D Mesh Generation
    genrule_name = "basler_%s_gen" % model
    if not native.existing_rule(genrule_name):
        native.genrule(
            name = genrule_name,
            outs = [
                "basler_%s_collision.glb" % model,
                "basler_%s_visual.glb" % model,
            ],
            cmd = "$(location //intrinsic_perception/intrinsic/perception/cameras/hardware_devices:generate_basler_camera) --model %s --output-visual $(location basler_%s_visual.glb) --output-collision $(location basler_%s_collision.glb)" % (model, model, model),
            tools = ["//intrinsic_perception/intrinsic/perception/cameras/hardware_devices:generate_basler_camera"],
        )

    # 2. Body SDF Generation via Xacro
    body_xacro_name = "basler_%s_body_xacro" % model
    if not native.existing_rule(body_xacro_name):
        xacro_file(
            name = body_xacro_name,
            src = "//intrinsic_perception/intrinsic/perception/cameras/hardware_devices:basler_body.sdf.xacro",
            out = "basler_%s_body.sdf" % model,
            arguments = {
                "model": model,
            },
        )

    # 3. Model Assets Filegroup
    assets_name = "basler_%s_assets" % model
    if not native.existing_rule(assets_name):
        native.filegroup(
            name = assets_name,
            srcs = [
                ":%s" % body_xacro_name,
                ":%s" % genrule_name,
            ],
        )

    # 4. Full Camera SDF Model via Xacro
    xacro_file(
        name = "%s_xacro" % model_name,
        src = "//intrinsic_perception/intrinsic/perception/cameras/hardware_devices:basler.sdf.xacro",
        out = "%s.sdf" % model_name,
        arguments = {
            "far": str(far),
            "fov": str(fov),
            "height": str(height),
            "mass": str(mass),
            "model": model,
            "model_name": model_name,
            "near": str(near),
            "width": str(width),
        },
        visibility = ["//visibility:private"],
    )

    sdf_scene_object(
        name = "%s_sdf" % model_name,
        src = "%s.sdf" % model_name,
        sdf_assets = [
            ":%s" % assets_name,
        ],
    )

    # 5. Manifest Templates
    expand_template(
        name = "%s_hardware_device_manifest_template" % model_name,
        out = "%s_hardware_device_manifest.textproto" % model_name,
        substitutions = {
            "${asset_id}": asset_name,
            "${display}": display_name,
            "${scene_object_asset_id}": scene_object_asset_name,
            "${service_asset_id}": service_asset,
        },
        template = "//intrinsic_perception/intrinsic/perception/cameras/hardware_devices:basler_hardware_device_manifest.textproto.tpl",
    )

    expand_template(
        name = "%s_scene_object_manifest_template" % model_name,
        out = "%s_scene_object_manifest.textproto" % model_name,
        substitutions = {
            "${display}": display_name,
            "${scene_object_asset_id}": scene_object_asset_name,
        },
        template = "//intrinsic_perception/intrinsic/perception/cameras/hardware_devices:basler_scene_object_manifest.textproto.tpl",
    )

    # 6. Scene Object Asset
    intrinsic_scene_object(
        name = "%s_scene_object" % model_name,
        manifest = "%s_scene_object_manifest.textproto" % model_name,
        scene_object = ":%s_sdf" % model_name,
        visibility = ["//visibility:private"],
    )

    # 7. Intrinsic Hardware Device
    intrinsic_hardware_device(
        name = "%s_hardware_device" % model_name,
        assets = [
            ":%s_scene_object" % model_name,
            service,
        ],
        manifest = ":%s_hardware_device_manifest.textproto" % model_name,
        visibility = visibility,
    )
