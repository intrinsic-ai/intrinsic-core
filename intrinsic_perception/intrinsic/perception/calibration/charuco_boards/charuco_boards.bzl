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

"""ChArUco board macro for 3D models, scene objects, and estimators."""

load("@bazel_skylib//rules:expand_template.bzl", "expand_template")
load("//intrinsic_sdk/intrinsic/assets/data/build_defs:data.bzl", "intrinsic_data")
load("//intrinsic_sdk/intrinsic/assets/scene_objects/build_defs:scene_object.bzl", "intrinsic_scene_object")
load("//intrinsic_sdk/intrinsic/scene/build_defs:sdf_scene_object.bzl", "sdf_scene_object")

def charuco_board(
        name,
        size_mm,
        grid,
        checker_mm,
        marker_mm,
        dictionary = "DICT_5X5"):
    """Declares 3D CAD model, scene object, and pose estimator assets for a ChArUco board.

    Creates the following public targets:
      - `:{name}`: intrinsic_scene_object asset
      - `:{name}_estimator`: intrinsic_data pose estimator asset
      - `:{name}_model`: genrule generating GLB, SDF, config, and texture files

    And private intermediate targets:
      - `:{name}_resource_manifest`: expanded resource manifest textproto
      - `:{name}_scene_object`: SDF scene object node
      - `:{name}_asset_manifest_gen`: expanded CharucoMarkerPoseEstimationConfigProto

    Args:
        name: Target name (e.g. 'charuco_9x12_15mm_12mm_dict_5x5').
        size_mm: Tuple (width_mm, height_mm) of the physical board backing plate.
        grid: Tuple (rows, cols) of checkerboard squares.
        checker_mm: Outer checkerboard square dimension in millimeters.
        marker_mm: Inner ArUco marker dimension in millimeters.
        dictionary: OpenCV ArUco dictionary prefix ('DICT_5X5' or 'DICT_4X4').
    """
    board_width_mm, board_height_mm = size_mm
    rows, cols = grid

    # Derive dictionary enum suffix matching OpenCV / CharucoMarkerPoseEstimationConfigProto
    num_markers = (rows * cols) // 2
    suffix = [s for s in [50, 100, 250, 1000] if s >= num_markers][0]
    dict_id = "%s_%d" % (dictionary, suffix)

    display_name = "ChArUco %dx%d | %smm | %smm | %s" % (
        rows,
        cols,
        str(checker_mm),
        str(marker_mm),
        dictionary,
    )

    # 1. 3D Model Genrule
    native.genrule(
        name = "%s_model" % name,
        srcs = [
            "//intrinsic_perception/intrinsic/perception/calibration/charuco_boards:model.sdf.template",
            "//intrinsic_perception/intrinsic/perception/calibration/charuco_boards:model.config.template",
        ],
        outs = [
            "%s.glb" % name,
            "%s.sdf" % name,
            "%s.config" % name,
            "%s_texture.png" % name,
        ],
        cmd = (
            "$(location //intrinsic_perception/intrinsic/perception/calibration/charuco_boards:generate_charuco_board) " +
            "--name %s --rows %d --cols %d --checker_mm %s --marker_mm %s --dict_id %s " +
            "--board_width_mm %s --board_height_mm %s " +
            "--sdf_template $(location //intrinsic_perception/intrinsic/perception/calibration/charuco_boards:model.sdf.template) " +
            "--config_template $(location //intrinsic_perception/intrinsic/perception/calibration/charuco_boards:model.config.template) " +
            "--uri_prefix model://intrinsic/perception/calibration/charuco_boards " +
            "--out_dir $(RULEDIR)"
        ) % (
            name,
            rows,
            cols,
            str(checker_mm),
            str(marker_mm),
            dict_id,
            str(board_width_mm),
            str(board_height_mm),
        ),
        tools = [
            "//intrinsic_perception/intrinsic/perception/calibration/charuco_boards:generate_charuco_board",
        ],
        visibility = ["//visibility:public"],
    )

    # 2. Scene Object Manifest & Targets
    expand_template(
        name = "%s_resource_manifest" % name,
        out = "%s_resource_manifest.textproto" % name,
        substitutions = {
            "$DISPLAY_NAME": display_name,
            "$OBJECT_NAME": name,
        },
        template = "//intrinsic_perception/intrinsic/perception/calibration/charuco_boards:scene_object_manifest.textproto.template",
        visibility = ["//visibility:private"],
    )

    sdf_scene_object(
        name = "%s_scene_object" % name,
        src = ":%s.sdf" % name,
        sdf_assets = [":%s_model" % name],
        visibility = ["//visibility:private"],
    )

    intrinsic_scene_object(
        name = name,
        manifest = ":%s_resource_manifest" % name,
        scene_object = ":%s_scene_object" % name,
        visibility = ["//visibility:public"],
    )

    # 3. Estimator Manifest & Targets
    expand_template(
        name = "%s_asset_manifest_gen" % name,
        out = "%s_asset_manifest.textproto" % name,
        substitutions = {
            "$BOARD_ID": name,
            "$DICTIONARY": dict_id,
            "$ESTIMATOR_NAME": "%s_estimator" % name,
            "$MARKER_LENGTH": str(marker_mm / 1000.0),
            "$SQUARES_X": str(cols),
            "$SQUARES_Y": str(rows),
            "$SQUARE_LENGTH": str(checker_mm / 1000.0),
        },
        template = "//intrinsic_perception/intrinsic/perception/calibration/charuco_boards:estimator_manifest.textproto.template",
        visibility = ["//visibility:private"],
    )

    intrinsic_data(
        name = "%s_estimator" % name,
        deps = [
            "@intrinsic_apis//intrinsic/perception/proto/pose_estimators/v1:charuco_marker_pose_estimation_config_proto",
            "@intrinsic_apis//intrinsic/perception/proto/v1:perception_model_proto",
        ],
        manifest = ":%s_asset_manifest_gen" % name,
        visibility = ["//visibility:public"],
    )
