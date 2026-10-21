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

package(default_visibility = ["//visibility:private"])

sdf_scene_object(
    name = "caw_enclosure_scene_object",
    src = "bluebird_enclosure.sdf",
    sdf_assets = ["//intrinsic/models/assets/bluebird"],
)

intrinsic_scene_object(
    name = "caw_enclosure",
    manifest = ":caw_enclosure_manifest.textproto",
    scene_object = ":caw_enclosure_scene_object",
    visibility = [
        "//intrinsic/apps/bluebird_caw:__subpackages__",
        "//intrinsic/apps/bluebird_caw/workcells/bb05:__pkg__",
        "//intrinsic/apps/bluebird_caw/workcells/bb08:__pkg__",
        "//intrinsic/cse/roboception/solutions:__subpackages__",
        "//visibility:public",
    ],
)

### For pose estimators
