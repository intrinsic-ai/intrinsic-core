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
load("//intrinsic_sdk/intrinsic/assets/scene_objects/build_defs:scene_object.bzl", "intrinsic_scene_object")

package(default_visibility = [
    "//visibility:public",
])

sdf_scene_object(
    name = "building_block_scene_object",
    src = "building_block.sdf",
    sdf_assets = ["//intrinsic/models/assets/building_block"],
)

intrinsic_scene_object(
    name = "building_block",
    manifest = "building_block_manifest.textproto",
    scene_object = ":building_block_scene_object",
)

sdf_scene_object(
    name = "building_block_tray_scene_object",
    src = "building_block_tray.sdf",
    sdf_assets = ["//intrinsic/models/assets/building_block"],
)

intrinsic_scene_object(
    name = "building_block_tray",
    manifest = "building_block_tray_manifest.textproto",
    scene_object = ":building_block_tray_scene_object",
)
