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
        "simulation_plugins.xacro",
    ],
)

filegroup(
    name = "lr_10ia_10_data",
    srcs = [
        "lr_10ia_10/lr_10ia_10.sdf.xacro",
        "lr_10ia_10/lr_10ia_10.xacro",
        ":plugins",
        "//third_party/fanuc/lr_10ia_10:lr_10ia_10_base.xacro",
    ],
)

filegroup(
    name = "crx_10ia_data",
    srcs = [
        "crx_10ia/crx_10ia.sdf.xacro",
        "crx_10ia/crx_10ia.xacro",
        ":plugins",
        "//third_party/fanuc/crx_10ia:crx_10ia_base.xacro",
    ],
)

filegroup(
    name = "crx_10ia_l_data",
    srcs = [
        "crx_10ia_l/crx_10ia_l.sdf.xacro",
        "crx_10ia_l/crx_10ia_l.xacro",
        ":plugins",
        "//third_party/fanuc/crx_10ia_l:crx_10ia_l_base.xacro",
    ],
)

filegroup(
    name = "crx_20ia_l_data",
    srcs = [
        "crx_20ia_l/crx_20ia_l.sdf.xacro",
        "crx_20ia_l/crx_20ia_l.xacro",
        ":plugins",
        "//third_party/fanuc/crx_20ia_l:crx_20ia_l_base.xacro",
    ],
)

filegroup(
    name = "crx_30ia_data",
    srcs = [
        "crx_30ia/crx_30ia.sdf.xacro",
        "crx_30ia/crx_30ia.xacro",
        ":plugins",
        "//third_party/fanuc/crx_30ia:crx_30ia_base.xacro",
    ],
)

filegroup(
    name = "lr_mate_200id_7l_data",
    srcs = [
        "lr_mate_200id_7l/lr_mate_200id_7l.sdf.xacro",
        "lr_mate_200id_7l/lr_mate_200id_7l.xacro",
        ":plugins",
        "//third_party/fanuc/lr_mate_200id_7l:lr_mate_200id_7l_base.xacro",
    ],
)

filegroup(
    name = "lr_mate_200id_4s_data",
    srcs = [
        "lr_mate_200id_4s/lr_mate_200id_4s.sdf.xacro",
        "lr_mate_200id_4s/lr_mate_200id_4s.xacro",
        ":plugins",
        "//third_party/fanuc/lr_mate_200id_4s:lr_mate_200id_4s_base.xacro",
    ],
)

filegroup(
    name = "r_2000ic_210l_data",
    srcs = [
        "r_2000ic_210l/r_2000ic_210l.sdf.xacro",
        "r_2000ic_210l/r_2000ic_210l.xacro",
        ":plugins",
        "//third_party/fanuc/r_2000ic_210l:r_2000ic_210l_base.xacro",
    ],
)

filegroup(
    name = "m_10id_12",
    srcs = [
        "m_10id_12/m_10id_12.sdf.xacro",
        "m_10id_12/m_10id_12.xacro",
        ":plugins",
        "//third_party/fanuc/m_10id_12:m_10id_12_base.xacro",
    ],
)

filegroup(
    name = "m_20id",
    srcs = [
        "m_20id/m_20id.sdf.xacro",
        "m_20id/m_20id.xacro",
        ":plugins",
        "//third_party/fanuc/m_20id:m_20id_base.xacro",
    ],
)

filegroup(
    name = "m_710id_50m",
    srcs = [
        "m_710id_50m/m_710id_50m.sdf.xacro",
        "m_710id_50m/m_710id_50m.xacro",
        ":plugins",
        "//third_party/fanuc/m_710id_50m:m_710id_50m_base.xacro",
    ],
)

xacro_file(
    name = "m_710id_50m_xacro",
    src = "m_710id_50m/m_710id_50m.sdf.xacro",
    out = "m_710id_50m_world.sdf",
    data = [":m_710id_50m"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "m_710id_50m_scene_object",
    src = "m_710id_50m_world.sdf",
    sdf_assets = ["//third_party/fanuc/m_710id_50m:m_710id_50m_resources"],
    updates_pbtxts = [
        "m_710id_50m_default_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

xacro_file(
    name = "m_10id_12_xacro",
    src = "m_10id_12/m_10id_12.sdf.xacro",
    out = "m_10id_12_world.sdf",
    data = [":m_10id_12"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "m_10id_12_scene_object",
    src = "m_10id_12_world.sdf",
    sdf_assets = ["//third_party/fanuc/m_10id_12:m_10id_12_resources"],
    updates_pbtxts = [
        "m_10id_12_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

xacro_file(
    name = "m_20id_xacro",
    src = "m_20id/m_20id.sdf.xacro",
    out = "m_20id_world.sdf",
    data = [":m_20id"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "m_20id_25_scene_object",
    src = "m_20id_world.sdf",
    sdf_assets = ["//third_party/fanuc/m_20id:m_20id_resources"],
    updates_pbtxts = [
        "m_20id_25_default_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

sdf_scene_object(
    name = "m_20id_35_scene_object",
    src = "m_20id_world.sdf",
    sdf_assets = ["//third_party/fanuc/m_20id:m_20id_resources"],
    updates_pbtxts = [
        "m_20id_35_default_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

xacro_file(
    name = "lr_10ia_10_xacro",
    src = "lr_10ia_10/lr_10ia_10.sdf.xacro",
    out = "lr_10ia_10_world.sdf",
    data = [":lr_10ia_10_data"],
    # TODO(b/441705658): Remove visibility once Jacobian is exposed by motion
    # planner client.
    # LINT.IfChange(lr_10ia_10_xacro_visibility)
    visibility = ["//visibility:public"],
    # LINT.ThenChange()
)

sdf_scene_object(
    name = "lr_10ia_10",
    src = "lr_10ia_10_world.sdf",
    sdf_assets = ["//third_party/fanuc/lr_10ia_10:lr_10ia_10_resources"],
    updates_pbtxts = [
        "lr_10ia_10_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

xacro_file(
    name = "lr_mate_200id_7l_xacro",
    src = "lr_mate_200id_7l/lr_mate_200id_7l.sdf.xacro",
    out = "lr_mate_200id_7l_world.sdf",
    data = [":lr_mate_200id_7l_data"],
    visibility = ["//visibility:public"],
)

sdf_scene_object(
    name = "lr_mate_200id_7l",
    src = "lr_mate_200id_7l_world.sdf",
    sdf_assets = ["//third_party/fanuc/lr_mate_200id_7l:lr_mate_200id_7l_resources"],
    updates_pbtxts = [
        "lr_mate_200id_7l_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

xacro_file(
    name = "r_2000ic_210l_xacro",
    src = "r_2000ic_210l/r_2000ic_210l.sdf.xacro",
    out = "r_2000ic_210l_world.sdf",
    data = [":r_2000ic_210l_data"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "r_2000ic_210l",
    src = "r_2000ic_210l_world.sdf",
    sdf_assets = ["//third_party/fanuc/r_2000ic_210l:r_2000ic_210l_resources"],
    updates_pbtxts = [
        "r_2000ic_210l_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

filegroup(
    name = "lr_mate_200id",
    srcs = [
        "lr_mate_200id/lr_mate_200id.sdf.xacro",
        "lr_mate_200id/lr_mate_200id.xacro",
        ":plugins",
        "//third_party/fanuc/lr_mate_200id:lr_mate_200id_base.xacro",
    ],
)

xacro_file(
    name = "lr_mate_200id_xacro",
    src = "lr_mate_200id/lr_mate_200id.sdf.xacro",
    out = "lr_mate_200id_world.sdf",
    data = [":lr_mate_200id"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "lr_mate_200id_scene_object",
    src = "lr_mate_200id_world.sdf",
    sdf_assets = ["//third_party/fanuc/lr_mate_200id:lr_mate_200id_resources"],
    updates_pbtxts = [
        "lr_mate_200id_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

xacro_file(
    name = "lr_mate_200id_4s_xacro",
    src = "lr_mate_200id_4s/lr_mate_200id_4s.sdf.xacro",
    out = "lr_mate_200id_4s_world.sdf",
    data = [":lr_mate_200id_4s_data"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "lr_mate_200id_4s_scene_object",
    src = "lr_mate_200id_4s_world.sdf",
    sdf_assets = ["//third_party/fanuc/lr_mate_200id_4s:lr_mate_200id_4s_resources"],
    updates_pbtxts = [
        "lr_mate_200id_4s_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

xacro_file(
    name = "crx_10ia_xacro",
    src = "crx_10ia/crx_10ia.sdf.xacro",
    out = "crx_10ia_world.sdf",
    data = [":crx_10ia_data"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "crx_10ia_scene_object",
    src = "crx_10ia_world.sdf",
    sdf_assets = ["//third_party/fanuc/crx_10ia:crx_10ia_resources"],
    updates_pbtxts = [
        "crx_10ia_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

xacro_file(
    name = "crx_10ia_l_xacro",
    src = "crx_10ia_l/crx_10ia_l.sdf.xacro",
    out = "crx_10ia_l_world.sdf",
    data = [":crx_10ia_l_data"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "crx_10ia_l_scene_object",
    src = "crx_10ia_l_world.sdf",
    sdf_assets = ["//third_party/fanuc/crx_10ia_l:crx_10ia_l_resources"],
    updates_pbtxts = [
        "crx_10ia_l_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

xacro_file(
    name = "crx_20ia_l_xacro",
    src = "crx_20ia_l/crx_20ia_l.sdf.xacro",
    out = "crx_20ia_l_world.sdf",
    data = [":crx_20ia_l_data"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "crx_20ia_l_scene_object",
    src = "crx_20ia_l_world.sdf",
    sdf_assets = ["//third_party/fanuc/crx_20ia_l:crx_20ia_l_resources"],
    updates_pbtxts = [
        "crx_20ia_l_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

xacro_file(
    name = "crx_30ia_xacro",
    src = "crx_30ia/crx_30ia.sdf.xacro",
    out = "crx_30ia_world.sdf",
    data = [":crx_30ia_data"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "crx_30ia_scene_object",
    src = "crx_30ia_world.sdf",
    sdf_assets = ["//third_party/fanuc/crx_30ia:crx_30ia_resources"],
    updates_pbtxts = [
        "crx_30ia_limits.pbtxt",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)
