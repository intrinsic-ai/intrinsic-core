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
load("//intrinsic_sdk/intrinsic/scene/build_defs:sdf_scene_object.bzl", "sdf_scene_object")

package(
    default_visibility = [
        "//visibility:public",
    ],
)

filegroup(
    name = "irb1300_10_115",
    srcs = [
        "irb1300_10_115/irb1300_10_115.sdf.xacro",
        "irb1300_10_115/irb1300_10_115.xacro",
        "irb1300_10_115/irb1300_10_115_base.xacro",
    ],
)

xacro_file(
    name = "irb1300_10_115_xacro",
    src = "irb1300_10_115/irb1300_10_115.sdf.xacro",
    out = "irb1300_10_115_world.sdf",
    data = [":irb1300_10_115"],
    visibility = ["//visibility:private"],
)

filegroup(
    name = "irb1300_10_115_resources",
    srcs = glob(["irb1300_10_115/visual/*"]),
)

sdf_scene_object(
    name = "irb1300_10_115_scene_object",
    src = "irb1300_10_115_world.sdf",
    sdf_assets = [":irb1300_10_115_resources"],
    updates_pbtxts = [
        "irb1300_10_115_default_limits.pbtxt",
    ],
)
