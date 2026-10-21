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

load("//bazel:xacro.bzl", "xacro_file")
load("//intrinsic/scene/build_defs:sdf_scene_object.bzl", "sdf_scene_object")
load("//intrinsic_sdk/intrinsic/assets/scene_objects/build_defs:scene_object.bzl", "intrinsic_scene_object")

package(default_visibility = [
    "//visibility:public",
])

# ATI Axia80_m20 Sensor only with updated axes #####################################################
xacro_file(
    name = "ati_axia80_m20_updated_xacro",
    src = "ati_axia80_m20_updated.sdf.xacro",
    out = "ati_axia80_m20_updated.sdf",
    data = ["//intrinsic_control/intrinsic/models/assets/standard_tooling/ati/axia80_m20_updated"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "ati_axia80_m20_updated_scene_object",
    src = "ati_axia80_m20_updated.sdf",
    sdf_assets = ["//intrinsic_control/intrinsic/models/assets/standard_tooling/ati/axia80_m20_updated"],
)

intrinsic_scene_object(
    name = "ati_axia80_m20_resource_type",
    manifest = ":ati_axia80_m20_updated_manifest.textproto",
    scene_object = ":ati_axia80_m20_updated_scene_object",
)

####################################################################################################
