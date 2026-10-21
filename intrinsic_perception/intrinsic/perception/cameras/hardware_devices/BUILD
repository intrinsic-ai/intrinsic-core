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

load("//bazel:python.bzl", "py_binary")
load("//intrinsic_sdk/intrinsic/assets/data/build_defs:data.bzl", "intrinsic_data")
load("//intrinsic_sdk/intrinsic/assets/hardware_devices/build_defs:hardware_device.bzl", "intrinsic_hardware_device")
load("//intrinsic_sdk/intrinsic/assets/scene_objects/build_defs:scene_object.bzl", "intrinsic_scene_object")
load("//intrinsic_sdk/intrinsic/scene/build_defs:sdf_scene_object.bzl", "sdf_scene_object")
load(":basler.bzl", "basler_hardware_device")

package(default_visibility = [
    "//visibility:public",
])

##############################################################################
# Basler
py_binary(
    name = "generate_basler_camera",
    srcs = ["generate_basler_camera.py"],
    deps = [
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@ai_intrinsic_sdks_pip_deps//scipy",
        "@ai_intrinsic_sdks_pip_deps//trimesh",
    ],
)

basler_hardware_device(
    name = "basler_ace",
    display_name = "Basler ace",
)

##############################################################################
# Photoneo

##############################################################################
# IPS 2

##############################################################################
# Orbbec Gemini 335 LE

sdf_scene_object(
    name = "orbbec_gemini_335le_sdf",
    src = "orbbec_gemini_335le.sdf",
    sdf_assets = ["//third_party/orbbec:assets"],
)

intrinsic_scene_object(
    name = "orbbec_gemini_335le_scene_object",
    manifest = "orbbec_gemini_335le_scene_object_manifest.textproto",
    scene_object = ":orbbec_gemini_335le_sdf",
    visibility = ["//visibility:private"],
)

intrinsic_data(
    name = "orbbec_gemini_335le_data",
    manifest = "orbbec_gemini_335le_data_manifest.textproto",
    deps = ["@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_proto"],
)

intrinsic_hardware_device(
    name = "orbbec_gemini_335le_hardware_device",
    assets = [
        ":orbbec_gemini_335le_data",
        ":orbbec_gemini_335le_scene_object",
        "//intrinsic_perception/intrinsic/perception/cameras/services:ros_camera_service",
    ],
    manifest = ":orbbec_gemini_335le_hardware_device_manifest.textproto",
)

##############################################################################
# Zivid

##############################################################################
# Roboception
