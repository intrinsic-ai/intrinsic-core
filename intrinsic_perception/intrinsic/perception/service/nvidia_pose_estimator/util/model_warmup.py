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

"""Model warmup utilities for Nvidia Pose Estimator.

Orchestrates a 4-stage warmup sequence (`run_all_warmup`): stages 1-3 warm up
FoundationPose, FoundationStereo, and SAM 3 in isolation—calling
`_unload_models()` between stages so TensorRT builder workspaces never overlap
in GPU VRAM—followed by a resident stage that loads all models together and runs
a synthetic end-to-end pass so execution contexts remain resident for serving.
"""

from __future__ import annotations

import logging
from pathlib import Path
import time
from typing import Any
from typing import Protocol

import numpy as np
import trimesh

from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.proto import nvidia_service_config_pb2

_WARMUP_CUBE_SIZE_M: float = 0.05
_WARMUP_DUMMY_CAD_PATH = Path("/tmp/dummy_warmup_cube.ply")
_DEFAULT_WARMUP_IMAGE_SIZE: tuple[int, int] = (1280, 800)
_WARMUP_FOCAL_PX: float = 600.0
_WARMUP_DEPTH_M: float = 0.5
_SAM3_WARMUP_BOX: list[float] = [0.25, 0.25, 0.5, 0.5]


class WarmupService(Protocol):
  """Structural protocol for the service methods and attributes used by warmup."""

  _config: nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig
  _dataset_root: Path
  _pose_registry: Any | None
  _pose_renderer: Any | None
  _processor: Any | None
  _fs_engine: Any | None

  def _init_foundation_pose(self) -> None:
    ...

  def _init_perception_pipeline(self) -> None:
    ...

  def _init_foundation_stereo(self) -> None:
    ...

  def _unload_models(self) -> None:
    ...


def get_or_create_warmup_cad_model(cad_model_path: Path | None = None) -> Path:
  """Returns an existing CAD model path or exports a temporary 5cm dummy cube.

  Args:
    cad_model_path: Optional path to a `.ply` CAD mesh.

  Returns:
    `cad_model_path` if it exists on disk, otherwise the path to the generated
    dummy cube PLY file.
  """
  if cad_model_path is not None and cad_model_path.is_file():
    return cad_model_path

  dummy_cube = trimesh.creation.box(
      extents=(
          _WARMUP_CUBE_SIZE_M,
          _WARMUP_CUBE_SIZE_M,
          _WARMUP_CUBE_SIZE_M,
      )
  )
  dummy_cube.export(str(_WARMUP_DUMMY_CAD_PATH), file_type="ply")
  return _WARMUP_DUMMY_CAD_PATH


def _run_sam3_dummy_pass(
    processor: Any,
    config: nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig,
) -> int:
  """Runs a single dummy image + text + box prompt pass on SAM 3."""
  res = config.sam3_detection.resolution

  dummy_rgb = np.zeros((res, res, 3), dtype=np.uint8)
  state = processor.set_image(dummy_rgb)
  state = processor.set_text_prompt("object", state)
  _ = processor.add_geometric_prompt(_SAM3_WARMUP_BOX, True, state)
  return res


def warmup_foundationpose(
    service: WarmupService,
    cad_path: Path | None = None,
) -> dict[str, Any]:
  """Warms up FoundationPose TRT engines (refiner & score) in an isolated stage.

  Initializes the FoundationPose registry, triggers estimator construction to
  build or load the TensorRT plans, and unloads all models before returning.

  Args:
    service: The service instance to warm up.
    cad_path: Optional CAD mesh path; uses a dummy cube if omitted.

  Returns:
    A dictionary with `foundationpose_status` and `foundationpose_warmup_sec`.
  """
  results: dict[str, Any] = {}
  t_fp0 = time.perf_counter()
  logging.info("[Warmup 1/3] Warming up FoundationPose TRT engines...")
  # The stage must hand the GPU back to the next one even when it fails.
  try:
    service._init_foundation_pose()

    pose_reg = service._pose_registry
    if pose_reg is None:
      results["foundationpose_status"] = "skipped"
      return results

    resolved_cad = get_or_create_warmup_cad_model(cad_path)
    pose_reg.register_cad_path(("warmup", 1), resolved_cad)
    pose_rend = service._pose_renderer
    if pose_rend is not None:
      pose_rend.register_cad_path(("warmup", 1), resolved_cad)

    _ = pose_reg.get(
        service._dataset_root, "warmup", 1, _DEFAULT_WARMUP_IMAGE_SIZE
    )
    fp_time = time.perf_counter() - t_fp0
    results["foundationpose_warmup_sec"] = fp_time
    results["foundationpose_status"] = "success"
    logging.info(
        "[Warmup 1/3] FoundationPose warmup successful (%.4fs)!", fp_time
    )
  finally:
    service._unload_models()
  return results


def warmup_foundationstereo(service: WarmupService) -> dict[str, Any]:
  """Warms up the FoundationStereo TRT engine in an isolated stage.

  Initializes the stereo disparity engine, runs a single dummy inference at the
  configured stereo resolution, and unloads all models before returning.

  Args:
    service: The service instance to warm up.

  Returns:
    A dictionary with `foundationstereo_status` and
    `foundationstereo_warmup_sec`.
  """
  results: dict[str, Any] = {}
  t0 = time.perf_counter()
  logging.info("[Warmup 2/3] Warming up FoundationStereo TRT engine...")
  try:
    service._init_foundation_stereo()

    fs_engine = service._fs_engine
    if fs_engine is None:
      results["foundationstereo_status"] = "skipped"
      return results

    stereo_cfg = service._config.stereo
    w = stereo_cfg.width
    h = stereo_cfg.height
    dummy_left = np.zeros((h, w, 3), dtype=np.uint8)
    dummy_right = np.zeros((h, w, 3), dtype=np.uint8)
    _ = fs_engine.infer_disparity(dummy_left, dummy_right)

    fs_time = time.perf_counter() - t0
    results["foundationstereo_warmup_sec"] = fs_time
    results["foundationstereo_status"] = "success"
    logging.info(
        "[Warmup 2/3] FoundationStereo warmup successful (%.4fs)!", fs_time
    )
  finally:
    service._unload_models()
  return results


def warmup_sam3(service: WarmupService) -> dict[str, Any]:
  """Warms up SAM 3 TRT engines in an isolated stage.

  Initializes the perception pipeline, loads the lazy box decoder engine, runs a
  dummy image/text/box prompt pass, and unloads all models before returning.

  Args:
    service: The service instance to warm up.

  Returns:
    A dictionary with `sam3_status` and `sam3_warmup` details.
  """
  results: dict[str, Any] = {}
  t_sam0 = time.perf_counter()
  logging.info("[Warmup 3/3] Warming up SAM 3 TRT engines...")
  try:
    service._init_perception_pipeline()

    processor = service._processor
    if processor is None:
      results["sam3_status"] = "skipped"
      return results

    # b_engine is loaded lazily by the SAM 3 processor on the first box prompt.
    if getattr(processor, "b_engine", None) is None:
      processor.b_engine = processor._load_engine("box_decoder")
    res = _run_sam3_dummy_pass(processor, service._config)
    results["sam3_warmup"] = {"status": "success", "warmup_resolution": res}
    results["sam3_status"] = "success"
    logging.info(
        "[Warmup 3/3] SAM 3 warmup successful (%.4fs)!",
        time.perf_counter() - t_sam0,
    )
  finally:
    service._unload_models()
  return results


def _warm_foundationpose_end_to_end(
    service: WarmupService,
    *,
    pose_reg: Any | None,
    pose_rend: Any | None,
    dataset_root: Path,
    resolved_cad: Path,
) -> dict[str, Any]:
  """Runs FoundationPose once on synthetic RGBD to pre-allocate resident buffers."""
  results: dict[str, Any] = {}
  if pose_reg is None:
    results["resident_foundationpose_status"] = "skipped"
    return results

  pose_reg.register_cad_path(("warmup", 1), resolved_cad)
  if pose_rend is not None:
    pose_rend.register_cad_path(("warmup", 1), resolved_cad)

  width, height = _DEFAULT_WARMUP_IMAGE_SIZE
  try:
    estimator = pose_reg.get(dataset_root, "warmup", 1, (width, height))

    try:
      from foundation_pose_nvidia import RgbdFrame  # pylint: disable=g-import-not-at-top
    except ImportError:
      results["resident_foundationpose_status"] = "unavailable"
      return results

    camera_matrix = np.array(
        [
            [_WARMUP_FOCAL_PX, 0.0, width / 2.0],
            [0.0, _WARMUP_FOCAL_PX, height / 2.0],
            [0.0, 0.0, 1.0],
        ],
        dtype=np.float32,
    )
    dummy_rgb = np.zeros((height, width, 3), dtype=np.uint8)
    dummy_depth = np.full((height, width), _WARMUP_DEPTH_M, dtype=np.float32)

    half_extent_px = max(
        8, int(_WARMUP_FOCAL_PX * _WARMUP_CUBE_SIZE_M / _WARMUP_DEPTH_M / 2)
    )
    center_x, center_y = width // 2, height // 2
    dummy_mask = np.zeros((height, width), dtype=np.uint8)
    dummy_mask[
        center_y - half_extent_px : center_y + half_extent_px,
        center_x - half_extent_px : center_x + half_extent_px,
    ] = 255

    foundation_pose = service._config.foundation_pose
    n_refine = foundation_pose.n_refine
    n_hypotheses = foundation_pose.n_hypotheses

    t_register = time.perf_counter()
    result = estimator.register(
        RgbdFrame(
            rgb=dummy_rgb,
            depth_m=dummy_depth,
            intrinsics=camera_matrix,
            mask=dummy_mask,
        ),
        n_refine=n_refine,
        n_hypotheses=n_hypotheses,
    )
    results["resident_register_sec"] = time.perf_counter() - t_register

    if pose_rend is not None and getattr(result, "pose", None) is not None:
      t_render = time.perf_counter()
      _ = pose_rend.render_mask_from_pose(
          "warmup",
          1,
          np.asarray(result.pose, dtype=np.float32),
          camera_matrix.astype(np.float64),
          (width, height),
      )
      results["resident_render_sec"] = time.perf_counter() - t_render
    results["resident_foundationpose_status"] = "success"
    return results
  finally:
    pose_reg.evict_cad(("warmup", 1))
    if pose_rend is not None:
      pose_rend.evict_cad(("warmup", 1))


def warmup_resident(
    service: WarmupService,
    cad_path: Path | None = None,
) -> dict[str, Any]:
  """Initializes all models and runs a dummy pass to leave contexts resident.

  Unlike the earlier stages, models remain loaded in GPU memory on return so
  the service is ready to serve requests immediately.

  Args:
    service: The service instance to warm up.
    cad_path: Optional CAD mesh path; uses a dummy cube if omitted.

  Returns:
    A dictionary with `resident_status` and FoundationPose registration/render
    timing metrics.
  """
  logging.info("[Warmup Resident] Pre-allocating resident model contexts...")
  service._init_perception_pipeline()
  service._init_foundation_stereo()

  results = _warm_foundationpose_end_to_end(
      service,
      pose_reg=service._pose_registry,
      pose_rend=service._pose_renderer,
      dataset_root=service._dataset_root,
      resolved_cad=get_or_create_warmup_cad_model(cad_path),
  )

  processor = service._processor
  if processor is not None:
    _run_sam3_dummy_pass(processor, service._config)
  results["resident_status"] = "success"
  return results


def run_all_warmup(
    service: WarmupService,
    cad_model_path: Path | None = None,
) -> dict[str, Any]:
  """Runs the three isolated engine warmup stages followed by resident warmup.

  Args:
    service: The service instance to warm up.
    cad_model_path: Optional CAD mesh path; uses a dummy cube if omitted.

  Returns:
    A combined dictionary of per-stage warmup statuses and timing metrics.
  """
  results: dict[str, Any] = {}
  t0 = time.perf_counter()
  cad_path = get_or_create_warmup_cad_model(cad_model_path)

  service._unload_models()
  results.update(warmup_foundationpose(service, cad_path=cad_path))
  results.update(warmup_foundationstereo(service))
  results.update(warmup_sam3(service))
  results.update(warmup_resident(service, cad_path=cad_path))

  total_sec = time.perf_counter() - t0
  results["status"] = "success"
  results["total_warmup_sec"] = total_sec
  logging.info(
      "[Warmup Resident] Staged resident warmup completed in %.4fs.",
      total_sec,
  )
  return results
