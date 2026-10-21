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
    name = "plugins",
    srcs = [
        "timeslicer_plugins.xacro",
    ],
)

filegroup(
    name = "kr16_r2010_2",
    srcs = [
        "kr16_r2010_2/kr16_r2010_2.sdf.xacro",
        "kr16_r2010_2/kr16_r2010_2.xacro",
        "kr16_r2010_2/kr16_r2010_2_base.xacro",
        ":plugins",
    ],
)

filegroup(
    name = "kr10_r1100_2",
    srcs = [
        "kr10_r1100_2/kr10_r1100_2.xacro",
        "kr10_r1100_2/kr10_r1100_2_base.xacro",
        "kr10_r1100_2/kr10_r1100_2_limits.yaml",
        ":plugins",
    ],
)

filegroup(
    name = "kr20_r1810_2",
    srcs = [
        "kr20_r1810_2/kr20_r1810_2.xacro",
        "kr20_r1810_2/kr20_r1810_2_base.xacro",
        ":plugins",
    ],
)

filegroup(
    name = "kr50_r2500",
    srcs = [
        "kr50_r2500/kr50_r2500.xacro",
        "kr50_r2500/kr50_r2500_base.xacro",
        ":plugins",
    ],
)

filegroup(
    name = "kr6_r900_2",
    srcs = [
        "kr6_r900_2/kr6_r900_2.xacro",
        "kr6_r900_2/kr6_r900_2_base.xacro",
        "kr6_r900_2/kr6_r900_2_limits.yaml",
        ":plugins",
    ],
)

xacro_file(
    name = "kr6_r900_2_xacro",
    src = "kr6_r900_2/kr6_r900_2.sdf.xacro",
    out = "kr6_r900_2.sdf",
    data = [":kr6_r900_2"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "kr6_r900_2_scene_object",
    src = "kr6_r900_2.sdf",
    sdf_assets = ["//third_party/kuka/kr6_r900_2:kr6_r900_2_resources"],
    updates_pbtxts = [
        "kr6_r900_2_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

xacro_file(
    name = "kr10_r1100_2_xacro",
    src = "kr10_r1100_2/kr10_r1100_2.sdf.xacro",
    out = "kr10_r1100_2.sdf",
    data = [":kr10_r1100_2"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "kr10_r1100_2_scene_object",
    src = "kr10_r1100_2.sdf",
    sdf_assets = ["//third_party/kuka/kr10_r1100_2:kr10_r1100_2_resources"],
    updates_pbtxts = [
        "kr10_r1100_2_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

xacro_file(
    name = "kr16_r2010_2_xacro",
    src = "kr16_r2010_2/kr16_r2010_2.sdf.xacro",
    out = "kr16_r2010_2.sdf",
    data = [":kr16_r2010_2"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "kr16_r2010_2_scene_object",
    src = "kr16_r2010_2.sdf",
    sdf_assets = ["//third_party/kuka/kr16_r2010_2:kr16_r2010_2_resources"],
    updates_pbtxts = [
        "kr16_r2010_2_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

xacro_file(
    name = "kr20_r1810_2_xacro",
    src = "kr20_r1810_2/kr20_r1810_2.sdf.xacro",
    out = "kr20_r1810_2.sdf",
    data = [":kr20_r1810_2"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "kr20_r1810_2_scene_object",
    src = "kr20_r1810_2.sdf",
    sdf_assets = ["//third_party/kuka/kr20_r1810_2:kr20_r1810_2_resources"],
    updates_pbtxts = [
        "kr20_r1810_2_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

xacro_file(
    name = "kr50_r2500_xacro",
    src = "kr50_r2500/kr50_r2500.sdf.xacro",
    out = "kr50_r2500.sdf",
    data = [":kr50_r2500"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "kr50_r2500_scene_object",
    src = "kr50_r2500.sdf",
    sdf_assets = ["//third_party/kuka/kr50_r2500:kr50_r2500_resources"],
    updates_pbtxts = [
        "kr50_r2500_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)
