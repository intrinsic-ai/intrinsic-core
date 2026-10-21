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

"""Nvidia Pose Estimator Service implementing the perception pipeline."""

# ruff: noqa: E402
from __future__ import annotations

from collections.abc import Sequence
import dataclasses
import gc
import logging
import os
import pathlib
import sys
import threading
import time
import types
from typing import Any

import cv2
import grpc

from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.proto import nvidia_service_config_pb2
from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.util import asset_utils
from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.util import config_utils
from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.util import data_classes
from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.util import environment
from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.util import model_asset_utils
from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.util import model_warmup
from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.util import path_resolver

# Preload CUDA/TensorRT shared libraries and set FP_LIBRARY before importing
# FoundationPose and the perception pipeline.
environment.init_environment()

import foundation_pose_nvidia
from foundationpose_perception_pipeline import config as fp_config
from foundationpose_perception_pipeline import dataset as fp_dataset
from foundationpose_perception_pipeline import geometry as fp_geometry
from foundationpose_perception_pipeline import pose as fp_pose
from foundationpose_perception_pipeline.inference import config as fp_inference_config
from foundationpose_perception_pipeline.inference import detect as fp_detect
from foundationpose_perception_pipeline.inference import engine as fp_engine
from foundationpose_perception_pipeline.inference import models as fp_models
from foundationpose_perception_pipeline.inference import pose as fp_inference_pose
from foundationpose_perception_pipeline.inference import refine as fp_refine
from foundationpose_perception_pipeline.inference import select as fp_select
from foundationpose_perception_pipeline.inference.sam3 import trt_processor as sam3_trt_processor
from foundationpose_perception_pipeline.inference.stereo import depth as stereo_depth
from foundationpose_perception_pipeline.inference.stereo import rectify as stereo_rectify
from foundationpose_perception_pipeline.inference.stereo import trt_processor as stereo_trt_processor
from google.protobuf import empty_pb2
from google.protobuf import text_format
import numpy as np

from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.util import debug_utils
from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.util import image_utils
from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.util import logging_utils
from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.util import pose_utils
from intrinsic.assets import id_utils
from intrinsic.assets.dependencies import utils as dependency_utils
from intrinsic.assets.install import installed_assets_client
from intrinsic.assets.proto import id_pb2
from intrinsic.math.python import pose3
from intrinsic.math.python import proto_conversion
from intrinsic.math.python import rotation3
from intrinsic.perception.proto.v1 import pose_estimation_service_pb2 as v1_pose_estimation_service_pb2
from intrinsic.perception.proto.v1 import pose_estimation_service_pb2_grpc as v1_pose_estimation_service_pb2_grpc
from intrinsic.perception.utils import point_cloud_utils
from intrinsic.platform.pubsub.python import pubsub
from intrinsic.storage.content_addressable_storage.proto import cas_service_pb2_grpc

_PACKAGE_DIR = pathlib.Path(__file__).resolve().parent.parent
_INSTALLED_ASSETS_INTERFACE = "grpc://intrinsic_proto.assets.v1.InstalledAssets"
CAS_ADDRESS = os.environ.get(
    "CAS_ADDRESS",
    "content-addressable-storage.app-intrinsic-base.svc.cluster.local:9747",
)
_GRPC_OPTIONS = [
    ("grpc.max_receive_message_length", -1),
    ("grpc.max_send_message_length", -1),
    ("grpc.max_message_length", -1),
]
_DepthSource = nvidia_service_config_pb2.DepthSource

_METERS_UNIT_THRESHOLD: float = 2.0
_METERS_TO_MM: float = 1000.0
# Service manifest requests one GPU, which is always seen as device 0.
_CUDA_DEVICE_ID: int = 0


class _ClientCancelledError(Exception):
  """Raised when the client disconnects or times out before GPU execution."""


def _clear_gpu_cache() -> None:
  """Reclaims residual GPU activations and cache to prevent OOM."""
  gc.collect()
  # torch is not a dependency of this service; only flush its allocator if
  # something else already loaded it.
  if "torch" in sys.modules:
    try:
      import torch

      if torch.cuda.is_available():
        torch.cuda.empty_cache()
    except Exception:
      pass
  # Imported here so that a host without a usable CUDA runtime (e.g. unit
  # tests) degrades to a no-op instead of failing at module import.
  try:
    from cuda.bindings import runtime as cudart

    cudart.cudaDeviceSynchronize()
  except Exception:
    pass


def _fmt(value: Any, spec: str = ".2f") -> str:
  return "N/A" if value is None else format(float(value), spec)


def _format_selection_log(selection_results: Any, target_inference: Any) -> str:
  """Formats a summary of post-pose proposal selection results."""
  results = list(selection_results or [])
  summary = getattr(target_inference, "selection_summary", None)
  summary = summary if isinstance(summary, dict) else {}
  rows = getattr(target_inference, "rerank_rows", None)
  rerank_by_index = {
      int(r["pred_index"]): r.get("rerank_score")
      for r in (rows if isinstance(rows, list) else [])
      if isinstance(r, dict) and "pred_index" in r
  }
  kept = sum(1 for r in results if getattr(r, "kept", False))
  lines = [
      f"Selection: kept {kept} / {len(results)} proposals"
      f" (policy={summary.get('policy', 'N/A')},"
      f" cutoff={_fmt(summary.get('cutoff'), '.3f')})"
  ]
  for idx, res in enumerate(results):
    fp_score = getattr(res, "fp_score", None)
    error = getattr(res, "error", None)
    lines.append(
        f"  [{idx}] {'kept    ' if getattr(res, 'kept', False) else 'rejected'}"
        f" score={_fmt(None if fp_score is None else pose_utils.bounded_score(float(fp_score)))}"
        f" fp={_fmt(fp_score, '.1f')}"
        f" mask_iou={_fmt(getattr(res, 'proposal_render_mask_iou', None))}"
        f" box_iou={_fmt(getattr(res, 'proposal_render_box_iou', None))}"
        f" rerank={_fmt(rerank_by_index.get(idx), '.3f')}"
        + (f" error={error}" if error is not None else "")
    )
  return "\n".join(lines)


def _load_and_normalize_cad_ply(
    cad_path: pathlib.Path,
) -> tuple[tuple[np.ndarray, np.ndarray] | None, float]:
  """Loads a binary PLY mesh once, normalizing vertices to mm and returning (mesh_mm, mesh_unit_scale).

  Meshes with max axis extent below `_METERS_UNIT_THRESHOLD` are treated as
  authored in meters and scaled to millimeters.

  Args:
    cad_path: Path to the binary little-endian `.ply` file.

  Returns:
    A tuple `((verts_mm, faces), mesh_unit_scale)` where `verts_mm` is in
    millimetres (or `None` if `cad_path` could not be parsed) and
    `mesh_unit_scale` is the scale factor from the file's native coordinates to
    metres (`1.0` for metre-authored files, `1e-3` for millimetre-authored
    files).
  """
  if not cad_path.is_file():
    return None, 1.0 / _METERS_TO_MM
  try:
    with cad_path.open("rb") as f:
      if b"end_header" not in f.read(4096):
        return None, 1.0 / _METERS_TO_MM
    verts, faces = fp_geometry.read_binary_little_endian_ply(cad_path)
  except Exception:
    return None, 1.0 / _METERS_TO_MM
  max_extent = (
      float(np.max(np.max(verts, axis=0) - np.min(verts, axis=0)))
      if verts.size > 0
      else 0.0
  )
  is_meters = verts.size > 0 and max_extent < _METERS_UNIT_THRESHOLD
  if is_meters:
    verts = (verts * _METERS_TO_MM).astype(np.float32)
    logging.info(
        "CAD model '%s' max extent is %.4f (< %.1f): assumed meters and"
        " converted to millimeters (mesh_unit_scale=1.0).",
        cad_path.name,
        max_extent,
        _METERS_UNIT_THRESHOLD,
    )
  else:
    logging.info(
        "CAD model '%s' max extent is %.4f (>= %.1f): assumed millimeters"
        " without conversion (mesh_unit_scale=%.4f).",
        cad_path.name,
        max_extent,
        _METERS_UNIT_THRESHOLD,
        1.0 / _METERS_TO_MM,
    )
  mesh_unit_scale = 1.0 if is_meters else 1.0 / _METERS_TO_MM
  return (verts, faces), mesh_unit_scale


class CustomPoseRenderer(fp_pose.PoseRenderer):
  """PoseRenderer that loads CAD models from registered asset paths and normalizes units to mm.

  Upstream `PoseRenderer` expects a BOP dataset layout
  (`<dataset_root>/<dataset>/models_cad/obj_<obj_id>.ply`) with vertices in
  millimeters. This subclass resolves registered CAS download paths by
  `(dataset, obj_id)` and converts meter-authored meshes to millimeters.
  """

  def __init__(self, dataset_root: pathlib.Path) -> None:
    super().__init__(dataset_root)
    self._lock = threading.RLock()
    self._cad_paths: dict[tuple[str, int], pathlib.Path] = {}
    self.mesh_unit_scales: dict[tuple[str, int], float] = {}

  @property
  def cad_paths(
      self,
  ) -> types.MappingProxyType[tuple[str, int], pathlib.Path]:
    return types.MappingProxyType(self._cad_paths)

  def register_cad_path(
      self,
      key: tuple[str, int],
      cad_path: pathlib.Path,
  ) -> None:
    """Registers a CAD path and normalizes/caches its mesh and unit scale.

    Args:
      key: `(dataset, obj_id)` identifier for the CAD model.
      cad_path: Local path to the `.ply` CAD model file.
    """
    with self._lock:
      if (
          self._cad_paths.get(key) == cad_path
          and key in self.mesh_cache
          and key in self.mesh_unit_scales
      ):
        return
    mesh_mm, mesh_unit_scale = _load_and_normalize_cad_ply(cad_path)
    with self._lock:
      self._cad_paths[key] = cad_path
      if mesh_mm is not None:
        self.mesh_cache[key] = mesh_mm
      else:
        self.mesh_cache.pop(key, None)
      self.mesh_unit_scales[key] = mesh_unit_scale

  def evict_cad(self, key: tuple[str, int]) -> None:
    """Evicts cached mesh, unit scale, and path for `key`.

    Args:
      key: `(dataset, obj_id)` identifier for the CAD model to evict.
    """
    with self._lock:
      self.mesh_cache.pop(key, None)
      self.mesh_unit_scales.pop(key, None)
      self._cad_paths.pop(key, None)

  def reset(self) -> None:
    """Clears all registered CAD paths and cached meshes."""
    with self._lock:
      self.mesh_cache.clear()
      self.mesh_unit_scales.clear()
      self._cad_paths.clear()

  def mesh(self, dataset: str, obj_id: int) -> tuple[np.ndarray, np.ndarray]:
    """Returns the cached `(vertices in mm, faces)` of a CAD model.

    Args:
      dataset: Dataset namespace for the CAD model.
      obj_id: Numeric object identifier within `dataset`.

    Returns:
      Tuple of `(vertices, faces)` arrays with vertices in millimeters.
    """
    key = (dataset, obj_id)
    with self._lock:
      if key in self.mesh_cache:
        return self.mesh_cache[key]
      cad_path = self._cad_paths.get(key)
    if cad_path is not None and cad_path.is_file():
      self.register_cad_path(key, cad_path)
      with self._lock:
        if key in self.mesh_cache:
          return self.mesh_cache[key]
    with self._lock:
      return super().mesh(dataset, obj_id)


class CustomFoundationPoseRegistry(fp_pose.FoundationPoseRegistry):
  """FoundationPoseRegistry with support for explicit CAD file paths."""

  def __init__(
      self,
      engine_cache_dir: pathlib.Path,
      refine_model_path: pathlib.Path,
      score_model_path: pathlib.Path,
      device_id: int = 0,
      prepare_batch: int = 42,
      pose_renderer: CustomPoseRenderer | None = None,
  ) -> None:
    super().__init__(
        engine_cache_dir=engine_cache_dir,
        refine_model_path=refine_model_path,
        score_model_path=score_model_path,
        device_id=device_id,
        prepare_batch=prepare_batch,
    )
    self._lock = threading.RLock()
    self._pose_renderer = pose_renderer
    self._cad_paths: dict[tuple[str, int], pathlib.Path] = {}
    self.mesh_unit_scales: dict[tuple[str, int], float] = {}

  @property
  def cad_paths(
      self,
  ) -> types.MappingProxyType[tuple[str, int], pathlib.Path]:
    return types.MappingProxyType(self._cad_paths)

  def register_cad_path(
      self,
      key: tuple[str, int],
      cad_path: pathlib.Path,
  ) -> None:
    """Registers an explicit CAD model path and caches its unit scale.

    Args:
      key: `(dataset, obj_id)` identifier for the CAD model.
      cad_path: Local path to the `.ply` CAD model file.
    """
    with self._lock:
      if self._cad_paths.get(key) == cad_path and key in self.mesh_unit_scales:
        return
      pose_renderer = self._pose_renderer
    mesh_unit_scale: float | None = None
    if pose_renderer is not None:
      pose_renderer.register_cad_path(key, cad_path)
      mesh_unit_scale = pose_renderer.mesh_unit_scales.get(key)
    if mesh_unit_scale is None:
      _, mesh_unit_scale = _load_and_normalize_cad_ply(cad_path)
    with self._lock:
      self._cad_paths[key] = cad_path
      self.mesh_unit_scales[key] = mesh_unit_scale

  def evict_cad(self, key: tuple[str, int]) -> None:
    """Evicts and closes estimators and cached CAD state for `key`.

    Args:
      key: `(dataset, obj_id)` identifier for the CAD model to evict.
    """
    with self._lock:
      self._cad_paths.pop(key, None)
      self.mesh_unit_scales.pop(key, None)
      stale = [
          self.estimators.pop(k) for k in list(self.estimators) if k[:2] == key
      ]
    for estimator in stale:
      estimator.close()

  def close(self) -> None:
    """Closes all cached FoundationPose estimators while keeping registered CAD paths."""
    with self._lock:
      stale = list(self.estimators.values())
      self.estimators.clear()
    for estimator in stale:
      estimator.close()

  def reset(self) -> None:
    """Closes all cached estimators and clears registered CAD paths."""
    self.close()
    with self._lock:
      self._cad_paths.clear()
      self.mesh_unit_scales.clear()

  def get(
      self,
      dataset_root: pathlib.Path,
      dataset: str,
      obj_id: int,
      image_size: tuple[int, int],
  ) -> foundation_pose_nvidia.Estimator:
    """Returns a cached or newly created FoundationPose `Estimator` for the target.

    Args:
      dataset_root: Unused; retained for upstream interface compatibility.
      dataset: Dataset namespace for the target object.
      obj_id: Numeric object identifier within `dataset`.
      image_size: Input `(width, height)` in pixels.

    Returns:
      The `Estimator` instance configured for `(dataset, obj_id, width, height)`.

    Raises:
      ValueError: If no CAD model is registered for `(dataset, obj_id)`.
    """
    del dataset_root
    width, height = image_size
    key = (dataset, obj_id, width, height)
    mesh_key = (dataset, obj_id)
    with self._lock:
      if key not in self.estimators:
        if mesh_key in self._cad_paths and self._cad_paths[mesh_key].is_file():
          cad_path = self._cad_paths[mesh_key]
          if mesh_key not in self.mesh_unit_scales:
            self.register_cad_path(mesh_key, cad_path)
          mesh_unit_scale = self.mesh_unit_scales.get(
              mesh_key, 1.0 / _METERS_TO_MM
          )
        else:
          raise ValueError(f"No CAD model registered for {mesh_key}.")

        options = foundation_pose_nvidia.EstimatorOptions.from_env(
            cad_path,
            refine_model_path=self.refine_model_path,
            score_model_path=self.score_model_path,
            engine_cache_dir=self.engine_cache_dir,
            device_id=self.device_id,
            mesh_unit_scale=mesh_unit_scale,
        )
        config = foundation_pose_nvidia.RuntimeConfig(
            max_image_width=width,
            max_image_height=height,
            batch_size=self.prepare_batch,
        )
        self.estimators[key] = foundation_pose_nvidia.Estimator(
            options, config, prepare_batch=self.prepare_batch
        )
      return self.estimators[key]


class NvidiaPoseEstimatorService(
    v1_pose_estimation_service_pb2_grpc.PoseEstimationServiceServicer,
):
  """Implementation of the Nvidia PoseEstimatorService."""

  def __init__(
      self,
      config: (
          nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig | None
      ) = None,
  ):
    self._config = config_utils.resolve_service_config(config, _PACKAGE_DIR)
    self._asset_prompt_cache: dict[str, tuple[str | None, float | None]] = {}
    self._cad_model_cache: dict[str, pathlib.Path] = {}
    # Upstream obj_id allocated to each CAD file, so that distinct or updated
    # meshes never share an upstream cache entry. Guarded by _cache_lock.
    self._cad_obj_ids: dict[pathlib.Path, int] = {}
    self._cad_file_stats: dict[pathlib.Path, tuple[int, int]] = {}
    self._next_cad_obj_id: int = 1
    self._cache_lock = threading.RLock()
    self._gpu_lock = threading.Lock()

    # Outside a cluster there is no asset registry or CAS to talk to; the
    # pipeline then reads weights from MODELS_DIR.
    self._in_cluster = bool(os.environ.get("KUBERNETES_SERVICE_HOST"))

    self._assets_client = None
    # The default config always names intrinsic_runtime, but only the platform
    # fills in its interfaces, so an unresolved one means a local run.
    if self._config.intrinsic_runtime.interfaces:
      channel = dependency_utils.connect(
          self._config.intrinsic_runtime,
          _INSTALLED_ASSETS_INTERFACE,
          grpc_options=_GRPC_OPTIONS,
      )
      self._assets_client = (
          installed_assets_client.InstalledAssetsClient.from_channel(channel)
      )

    self._cas_stub = None
    if self._in_cluster or "CAS_ADDRESS" in os.environ:
      cas_channel = grpc.insecure_channel(CAS_ADDRESS, options=_GRPC_OPTIONS)
      self._cas_stub = (
          cas_service_pb2_grpc.ContentAddressableStorageServiceStub(cas_channel)
      )

    self._pubsub = None
    self._detection_image_pub = None
    self._pose_image_pub = None
    try:
      self._pubsub = pubsub.PubSub()
      self._detection_image_pub = self._pubsub.CreatePublisher(
          debug_utils.DETECTION_IMAGE_TOPIC
      )
      self._pose_image_pub = self._pubsub.CreatePublisher(
          debug_utils.POSE_IMAGE_TOPIC
      )
    except Exception as e:
      logging.warning("PubSub client init skipped: %s", e)

    if self._assets_client is not None and self._cas_stub is not None:
      assets = model_asset_utils.ensure_models_downloaded(
          self._config,
          self._assets_client,
          self._cas_stub,
          models_root=model_asset_utils.default_models_root(),
          engine_cache_root=model_asset_utils.default_engine_cache_root(),
      )
      self._models_root = assets.models_root
      self._fp_engine_cache_dir = assets.engine_cache_dir
    else:
      logging.warning(
          "Skipping model asset download (%s); models must already exist"
          " locally.",
          "intrinsic_runtime dependency is not resolved"
          if self._assets_client is None
          else "no CAS connection",
      )
      self._models_root = path_resolver.resolve_models_root()
      self._fp_engine_cache_dir = path_resolver.resolve_engine_cache_dir(
          self._models_root
      )

    self._fp_refine_model_path = (
        self._models_root / path_resolver.REFINE_MODEL_FILENAME
    )
    self._fp_score_model_path = (
        self._models_root / path_resolver.SCORE_MODEL_FILENAME
    )
    self._dataset_root = path_resolver.PIPELINE_DATASET_ROOT

    (
        self._detection_sensor,
        self._depth_source,
        self._hardware_depth_sensor,
        self._stereo_sensors,
    ) = config_utils.get_sensor_routing(self._config)

    self._processor: sam3_trt_processor.Sam3TrtProcessor | None = None
    self._pose_registry: CustomFoundationPoseRegistry | None = None
    self._pose_renderer: CustomPoseRenderer | None = None
    self._pose_estimator: fp_engine.PoseEstimator | None = None
    self._fs_engine: (
        stereo_trt_processor.FoundationStereoTrtProcessor | None
    ) = None

    logging.info(
        "NvidiaPoseEstimatorService initialized with config: %s",
        text_format.MessageToString(self._config),
    )

  def _init_foundation_pose(self) -> None:
    """Initializes the FoundationPose renderer and registry."""
    prepare_batch = int(self._config.foundation_pose.batch_size)
    self._pose_renderer = CustomPoseRenderer(self._dataset_root)
    self._pose_registry = CustomFoundationPoseRegistry(
        engine_cache_dir=self._fp_engine_cache_dir,
        refine_model_path=self._fp_refine_model_path,
        score_model_path=self._fp_score_model_path,
        device_id=_CUDA_DEVICE_ID,
        prepare_batch=prepare_batch,
        pose_renderer=self._pose_renderer,
    )

  def _init_perception_pipeline(self) -> None:
    """Initializes the perception pipeline combining SAM 3 and FoundationPose."""
    self._init_foundation_pose()
    conf_thresh = float(self._config.sam3_detection.confidence_threshold)
    sam3_res = int(self._config.sam3_detection.resolution)
    self._processor = sam3_trt_processor.Sam3TrtProcessor(
        models_dir=self._models_root,
        resolution=sam3_res,
        confidence_threshold=conf_thresh,
    )
    self._pose_estimator = fp_engine.PoseEstimator(
        processor=self._processor,
        pose_registry=self._pose_registry,
        pose_renderer=self._pose_renderer,
        dataset_root=self._dataset_root,
        run_foundationpose_for_proposals=(
            fp_inference_pose.run_foundationpose_for_proposals
        ),
        apply_sam3_refinement=fp_refine.apply_sam3_refinement,
        select_proposals=fp_select.select_proposals,
        mark_selected_filter_results=fp_select.mark_selected_filter_results,
        base_text_state_from_prompt_state=(
            fp_detect.base_text_state_from_prompt_state
        ),
        no_refinement_policy=fp_config.NO_REFINEMENT_POLICY,
    )
    logging.info(
        "Initialized perception pipeline (SAM 3 + FoundationPose) successfully."
    )

  def _init_foundation_stereo(self) -> None:
    """Initializes the FoundationStereo disparity engine unless depth is hardware-sourced."""
    if self._depth_source == _DepthSource.DEPTH_SOURCE_HARDWARE:
      return

    # ModelPaths prefers a compiled plan from the engine cache and falls back to
    # the ONNX, which TRTEngine then compiles.
    os.environ.setdefault("MODELS_DIR", str(self._models_root))
    engine_file = fp_models.ModelPaths.configured(self._models_root).preferred(
        fp_models.STEREO_MODEL
    )
    stereo_w = int(self._config.stereo.width)
    stereo_h = int(self._config.stereo.height)
    self._fs_engine = stereo_trt_processor.FoundationStereoTrtProcessor(
        model_path=engine_file,
        max_width=stereo_w,
        fixed_height=stereo_h,
    )
    logging.info(
        "Initialized FoundationStereoTrtProcessor from %s at %dx%d",
        engine_file,
        stereo_w,
        stereo_h,
    )
    # A compiled plan with a fixed input shape takes precedence over the
    # configured width and height.
    engine_hw = self._fs_engine.fixed_hw
    if engine_hw is not None:
      engine_h, engine_w = (int(dim) for dim in engine_hw)
      if (engine_h, engine_w) != (stereo_h, stereo_w):
        logging.warning(
            "FoundationStereo engine %s has a fixed %dx%d input, which"
            " overrides the configured stereo width/height of %dx%d.",
            engine_file,
            engine_w,
            engine_h,
            stereo_w,
            stereo_h,
        )

  def _unload_models(self) -> None:
    """Releases all models and per-asset caches from GPU VRAM.

    The handles are reset to None, so the next request reloads the models.
    """
    if self._pose_registry is not None:
      self._pose_registry.reset()
    if self._pose_renderer is not None:
      self._pose_renderer.reset()
    if self._processor is not None:
      try:
        self._processor.release()
      except Exception:
        pass
    if self._fs_engine is not None:
      try:
        self._fs_engine.release()
      except Exception:
        pass
    self._pose_estimator = None
    self._pose_registry = None
    self._pose_renderer = None
    self._processor = None
    self._fs_engine = None
    with self._cache_lock:
      self._cad_model_cache.clear()
      self._asset_prompt_cache.clear()
      self._cad_obj_ids.clear()
      self._cad_file_stats.clear()
    _clear_gpu_cache()

  def warmup(
      self, cad_model_path: pathlib.Path | None = None
  ) -> dict[str, Any]:
    """Loads and warms up all models once before the server starts serving.

    Args:
      cad_model_path: Mesh to warm FoundationPose with; a dummy cube if None.

    Returns:
      Per-stage warmup status and timings.
    """
    with self._gpu_lock:
      return model_warmup.run_all_warmup(self, cad_model_path=cad_model_path)

  def _unpack_request_config(
      self, request: Any
  ) -> nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig | None:
    """Unpacks a per-request service config override if present."""
    inference_parameters = (
        request.pose_estimation_run_config.inference_parameters
    )
    if not inference_parameters.Is(
        nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig.DESCRIPTOR
    ):
      return None

    override = nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig()
    inference_parameters.Unpack(override)
    return override

  def _resolve_prompt(
      self,
      asset_id: id_pb2.Id | None = None,
      request_config: (
          nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig | None
      ) = None,
  ) -> tuple[str, float]:
    """Resolves the SAM3 prompt and confidence threshold for one request."""
    prompt, threshold = None, None
    if asset_id is not None and asset_id.name:
      asset_key = id_utils.id_from_proto(asset_id)
      with self._cache_lock:
        cached = self._asset_prompt_cache.get(asset_key)
      if cached is not None:
        prompt, threshold = cached
      else:
        local_cache: dict[str, tuple[str | None, float | None]] = {}
        prompt, threshold = asset_utils.resolve_prompt(
            asset_id=asset_id,
            assets_client=self._assets_client,
            prompt_cache=local_cache,
        )
        if asset_key in local_cache:
          with self._cache_lock:
            self._asset_prompt_cache[asset_key] = local_cache[asset_key]

    if request_config is not None:
      request_sam3 = request_config.sam3_detection
      if request_sam3.HasField("prompt"):
        prompt = request_sam3.prompt
      if request_sam3.HasField("confidence_threshold"):
        threshold = request_sam3.confidence_threshold

    sam3_cfg = self._config.sam3_detection
    if prompt is None:
      prompt = sam3_cfg.prompt

    if threshold is None:
      threshold = float(sam3_cfg.confidence_threshold)

    return prompt, threshold

  def _select_stereo_partner(
      self, views: Sequence[image_utils.CameraFrame]
  ) -> image_utils.CameraFrame | None:
    """Picks the best horizontal stereo partner for `views[0]`.

    Views without a pose are skipped, since partners are ranked by their pose
    relative to the base.

    Args:
      views: Three or more stereo views; the first is the base.

    Returns:
      The partner, or None if the base has no pose or no view forms a
      horizontal pair with it.
    """
    base_view = views[0]
    if base_view.world_t_camera is None:
      logging.error(
          "Stereo base sensor '%s' has no pose; no stereo depth.",
          base_view.sensor_name,
      )
      return None
    candidates = [v for v in views if v.world_t_camera is not None]
    skipped = [v.sensor_name for v in views if v.world_t_camera is None]
    if skipped:
      logging.warning("Skipping stereo sensors without a pose: %s", skipped)

    h, w = base_view.image.shape[:2]
    intrinsics_np = np.stack([
        image_utils.validate_camera_matrix(v.camera_matrix, v.sensor_name)
        for v in candidates
    ])
    extrinsics_np = np.stack(
        [v.world_t_camera.inverse().matrix4x4()[:3, :4] for v in candidates]
    )

    if self._fs_engine is not None and self._fs_engine.fixed_hw is not None:
      stereo_w = int(self._fs_engine.fixed_hw[1])
    else:
      stereo_w = int(self._config.stereo.width)
    min_dist = float(self._config.stereo.min_depth_m)
    partner_res = stereo_rectify.select_partner_camera(
        base_camera=0,
        intrinsics=intrinsics_np,
        extrinsics=extrinsics_np,
        image_width=w,
        image_height=h,
        min_working_distance_m=min_dist,
        model_width=stereo_w,
    )
    if partner_res is None:
      logging.error(
          "No stereo sensor forms a horizontal pair with '%s' (candidates:"
          " %s); no stereo depth.",
          base_view.sensor_name,
          [v.sensor_name for v in candidates[1:]],
      )
      return None
    return candidates[partner_res[0]]

  def _compute_stereo_depth(
      self, stereo_views: Sequence[image_utils.CameraFrame]
  ) -> tuple[np.ndarray, np.ndarray, pose3.Pose3 | None] | None:
    """Computes metric depth in the base stereo camera frame via FoundationStereo."""
    if self._fs_engine is None:
      return None
    left_view = stereo_views[0]
    right_view = (
        stereo_views[1]
        if len(stereo_views) == 2
        else self._select_stereo_partner(stereo_views)
    )
    if right_view is None:
      return None
    logging.info(
        "Running FoundationStereo on '%s' (base) and '%s' (partner, %s).",
        left_view.sensor_name,
        right_view.sensor_name,
        "configured" if len(stereo_views) == 2 else "selected by search",
    )

    left_h, left_w = left_view.image.shape[:2]
    left_k = image_utils.validate_camera_matrix(
        left_view.camera_matrix, left_view.sensor_name
    )
    right_k = image_utils.validate_camera_matrix(
        right_view.camera_matrix, right_view.sensor_name
    )
    if left_view.world_t_camera is None or right_view.world_t_camera is None:
      missing = [
          v.sensor_name
          for v in (left_view, right_view)
          if v.world_t_camera is None
      ]
      raise ValueError(
          f"Stereo sensor(s) {missing} have no camera pose (world_t_camera)."
      )
    left_extrinsics = left_view.world_t_camera.inverse().matrix4x4()[:3, :4]
    right_extrinsics = right_view.world_t_camera.inverse().matrix4x4()[:3, :4]
    rect = stereo_rectify.rectify_pair(
        left_view.image,
        right_view.image,
        left_k,
        right_k,
        left_extrinsics,
        right_extrinsics,
    )
    rect_left, rect_right, k_rect = rect.left, rect.right, rect.k_rect
    mirrored = bool(rect.baseline_x_signed_m > 0)
    if mirrored:
      rect_left = np.ascontiguousarray(rect_left[:, ::-1])
      rect_right = np.ascontiguousarray(rect_right[:, ::-1])
    fitted = stereo_depth.fit_to_model(self._fs_engine, rect_left, rect_right)

    # Apply CLAHE after rectification so remapping does not blur equalized images.
    stereo_config = self._config.stereo
    if stereo_config.clahe_clip_limit:
      fitted = dataclasses.replace(
          fitted,
          left=stereo_depth.apply_clahe(
              fitted.left,
              stereo_config.clahe_clip_limit,
              detail_boost=stereo_config.clahe_detail_boost,
          ),
          right=stereo_depth.apply_clahe(
              fitted.right,
              stereo_config.clahe_clip_limit,
              detail_boost=stereo_config.clahe_detail_boost,
          ),
      )

    # Scale focal length to model input width for disparity_pre_shift.
    focal_px_model = float(k_rect[0, 0]) * fitted.width_scale
    baseline_m = float(abs(rect.baseline_x_signed_m))
    pre_shift = stereo_depth.disparity_pre_shift(
        focal_px_model,
        baseline_m,
        stereo_config.min_depth_m,
        stereo_config.max_depth_m,
    )
    if pre_shift:
      logging.info(
          "Disparity pre-shift %d px (working volume [%.2f, %.2f] m, range"
          " %.0f px).",
          pre_shift,
          stereo_config.min_depth_m,
          stereo_config.max_depth_m,
          stereo_depth.MAX_MODEL_DISPARITY,
      )
    disparity = self._fs_engine.infer_disparity(
        fitted.left, fitted.right, pre_shift_px=pre_shift
    )
    if fitted.width_scale != 1.0 or disparity.shape[:2] != rect_left.shape[:2]:
      disparity = disparity[: fitted.content_height, : fitted.content_width]
      disparity = cv2.resize(
          disparity,
          (rect_left.shape[1], rect_left.shape[0]),
          interpolation=cv2.INTER_LINEAR,
      )

    # Measure saturation in the model's unshifted disparity space.
    saturation = stereo_depth.saturated_fraction(disparity - pre_shift)
    if saturation > 0.001:
      logging.warning(
          "%.2f%% of valid disparity is at or above %.0f px, the model's"
          " search range. Those pixels are clamped, not matched, and their"
          " depth is biased too far. Widen the working-volume bounds or"
          " force a shorter-baseline partner.",
          100 * saturation,
          stereo_depth.MAX_MODEL_DISPARITY
          * stereo_depth.DISPARITY_SATURATION_FRACTION,
      )

    if mirrored:
      disparity = disparity[:, ::-1]
    if fitted.width_scale != 1.0:
      disparity = disparity / fitted.width_scale
    disparity = np.ascontiguousarray(disparity)

    depth_rect_m = stereo_depth.disparity_to_depth_m(
        disparity, focal_px=float(k_rect[0, 0]), baseline_m=baseline_m
    )
    depth_left_m = stereo_depth.depth_rectified_to_base(
        depth_rect_m, k_rect, rect.r1, left_k, (left_h, left_w)
    )
    return depth_left_m, left_k, left_view.world_t_camera

  def _resolve_depth(
      self,
      pipeline_views: image_utils.PipelineViews,
      camera_matrix: np.ndarray,
      target_shape: tuple[int, int],
      depth_source: _DepthSource.ValueType = _DepthSource.DEPTH_SOURCE_AUTO,
  ) -> np.ndarray | None:
    """Resolves depth and reprojects it into the detection view's frame."""
    h, w = target_shape
    detection_view = pipeline_views.detection_view
    source: tuple[np.ndarray, np.ndarray, pose3.Pose3 | None] | None = None

    if (
        depth_source
        in (_DepthSource.DEPTH_SOURCE_HARDWARE, _DepthSource.DEPTH_SOURCE_AUTO)
        and pipeline_views.depth_view is not None
    ):
      logging.info(
          "Using hardware depth from sensor '%s'.",
          pipeline_views.depth_view.sensor_name,
      )
      raw_depth = pipeline_views.depth_view.image.astype(np.float32, copy=False)
      source = (
          raw_depth,
          image_utils.validate_camera_matrix(
              pipeline_views.depth_view.camera_matrix,
              pipeline_views.depth_view.sensor_name,
          ),
          pipeline_views.depth_view.world_t_camera,
      )
    elif (
        depth_source
        in (_DepthSource.DEPTH_SOURCE_STEREO, _DepthSource.DEPTH_SOURCE_AUTO)
        and len(pipeline_views.stereo_views) >= 2
        and self._fs_engine is not None
    ):
      source = self._compute_stereo_depth(pipeline_views.stereo_views)

    if source is None:
      return None

    source_depth, source_k, world_t_source = source
    world_t_target = detection_view.world_t_camera
    if world_t_source is None or world_t_target is None:
      logging.warning(
          "Missing camera extrinsics (world_t_camera) for depth or detection"
          " sensor '%s'; cannot reproject depth.",
          detection_view.sensor_name,
      )
      return None
    source_t_world = world_t_source.inverse()
    target_t_world = world_t_target.inverse()
    logging_utils.log_depth_statistics(source_depth, "before reprojection")
    reprojected_depth = point_cloud_utils.reproject_depthmap_to_target_camera(
        source_depth,
        source_k,
        source_t_world,
        camera_matrix,
        target_t_world,
        (w, h),
    )
    if reprojected_depth is not source_depth:
      logging_utils.log_depth_statistics(
          reprojected_depth, "after reprojection"
      )
    return reprojected_depth

  def _obj_id_for_cad(self, cad_model_path: pathlib.Path | None) -> int:
    """Returns the upstream obj_id for a CAD file, allocating one if new or modified."""
    if cad_model_path is None or not cad_model_path.is_file():
      raise ValueError(f"CAD model file does not exist: {cad_model_path}")
    key = cad_model_path.resolve()
    st = key.stat()
    file_sig = (st.st_mtime_ns, st.st_size)
    old_obj_id = None
    with self._cache_lock:
      if key not in self._cad_obj_ids:
        obj_id = self._next_cad_obj_id
        self._next_cad_obj_id += 1
        self._cad_obj_ids[key] = obj_id
        self._cad_file_stats[key] = file_sig
      elif self._cad_file_stats.get(key) != file_sig:
        old_obj_id = self._cad_obj_ids[key]
        obj_id = self._next_cad_obj_id
        self._next_cad_obj_id += 1
        self._cad_obj_ids[key] = obj_id
        self._cad_file_stats[key] = file_sig
      current_obj_id = self._cad_obj_ids[key]
    # Evict CAD outside _cache_lock in case file is modified on disk.
    if old_obj_id is not None:
      old_mesh_key = path_resolver.pipeline_mesh_key(old_obj_id)
      if self._pose_renderer is not None:
        self._pose_renderer.evict_cad(old_mesh_key)
      if self._pose_registry is not None:
        self._pose_registry.evict_cad(old_mesh_key)
    return current_obj_id

  def _estimate_poses(
      self,
      views: Sequence[image_utils.CameraFrame] | image_utils.CameraFrame,
      base_camera_idx: int = 0,
      prompt: str = "object",
      cad_model_path: pathlib.Path | None = None,
      confidence_threshold: float | None = None,
      inference_config: fp_inference_config.InferenceConfig | None = None,
  ) -> tuple[list[data_classes.PipelinePrediction], dict[str, Any]]:
    """Runs full in-memory estimation pipeline for the target prompt and views.

    Args:
      views: Camera frames of the capture; routed to detection, hardware depth
        and stereo depth by `select_pipeline_views`.
      base_camera_idx: Index of the base camera among `views`.
      prompt: SAM 3 text prompt for proposal detection.
      cad_model_path: Local `.ply` CAD model of the target.
      confidence_threshold: SAM 3 confidence threshold override; only consulted
        when `inference_config` is None.
      inference_config: Validated pipeline config (service defaults merged with
        any per-request override). Built from the service config when None.

    Returns:
      The kept predictions and a routing/debug info dictionary.

    Raises:
      ValueError: If no views or CAD model are given, or no depth is available.
      RuntimeError: If the perception pipeline is not initialized.
    """
    view_list = (
        [views] if isinstance(views, image_utils.CameraFrame) else list(views)
    )
    if not view_list:
      raise ValueError("No camera views provided for pose estimation.")
    if cad_model_path is None or not cad_model_path.is_file():
      raise ValueError(f"CAD model file does not exist: {cad_model_path}")

    pipeline_views = image_utils.select_pipeline_views(
        view_list,
        detection_sensor=self._detection_sensor,
        depth_source=self._depth_source,
        hardware_depth_sensor=self._hardware_depth_sensor,
        stereo_sensors=self._stereo_sensors,
        base_camera_idx=base_camera_idx,
    )
    detection_view = pipeline_views.detection_view
    h, w = detection_view.image.shape[:2]
    camera_matrix = image_utils.validate_camera_matrix(
        detection_view.camera_matrix, detection_view.sensor_name
    )

    t_depth = time.perf_counter()
    depth_m = self._resolve_depth(
        pipeline_views=pipeline_views,
        camera_matrix=camera_matrix,
        target_shape=(h, w),
        depth_source=self._depth_source,
    )
    if depth_m is None:
      raise ValueError("No depth available for pose estimation.")
    logging.info("Resolved depth in %.3fs.", time.perf_counter() - t_depth)

    if self._pose_estimator is None or self._processor is None:
      raise RuntimeError("Perception pipeline is not initialized.")

    mesh_key = path_resolver.pipeline_mesh_key(
        self._obj_id_for_cad(cad_model_path)
    )
    dataset, obj_id = mesh_key
    logging.info(
        "Pose estimation with CAD model %s (obj_id=%d)",
        cad_model_path,
        obj_id,
    )
    target = fp_dataset.Target(
        dataset=dataset, scene_id=0, im_id=0, obj_id=obj_id, inst_count=1
    )
    # Upstream PoseEstimator.run_target() looks up CAD meshes by (dataset, obj_id),
    # so register the CAS-downloaded .ply path under mesh_key beforehand.
    if self._pose_renderer is not None:
      self._pose_renderer.register_cad_path(mesh_key, cad_model_path)
    if self._pose_registry is not None:
      self._pose_registry.register_cad_path(mesh_key, cad_model_path)

    if inference_config is None:
      inference_config = config_utils.build_inference_config(
          self._config,
          sam3_confidence_threshold_override=confidence_threshold,
      )

    depth_image_size = (w, h)
    depth_result = {"depth_m": depth_m, "camera_matrix": camera_matrix}
    routing_info = {
        "detection_view": detection_view,
        "pipeline_views": pipeline_views,
        "aligned_depth": depth_m,
        "camera_matrix": camera_matrix,
        "obj_id": obj_id,
    }

    previous_confidence_threshold = self._processor.confidence_threshold
    try:
      self._processor.confidence_threshold = (
          inference_config.detection.confidence_threshold
      )
      logging.info("Processing RGB image through segmentation...")
      t_seg = time.perf_counter()
      image_state = self._pose_estimator.begin_scene(
          detection_view.image, depth_image_size
      )
      raw_boxes, raw_scores, raw_masks, base_text_state = (
          self._pose_estimator.propose(prompt, image_state)
      )
      logging.info(
          "Found %d objects from Segmentor in %.3fs!",
          len(raw_boxes),
          time.perf_counter() - t_seg,
      )
      routing_info["sam3"] = {
          "boxes": raw_boxes,
          "scores": raw_scores,
          "masks": raw_masks,
      }

      if not raw_boxes.size:
        logging.info("No objects detected in scene")
        return [], routing_info

      logging.info(
          "Running batched FoundationPose on %d detected masks...",
          len(raw_boxes),
      )
      t_pose = time.perf_counter()
      target_inference = self._pose_estimator.run_target(
          config=inference_config,
          target=target,
          image=detection_view.image,
          camera_matrix=camera_matrix,
          depth_result=depth_result,
          pose_image_np=detection_view.image,
          depth_image_size=depth_image_size,
          raw_boxes=raw_boxes,
          raw_scores=raw_scores,
          raw_masks=raw_masks,
          base_text_state=base_text_state,
      )
      logging.info(
          "Completed FoundationPose in %.3fs.\n%s",
          time.perf_counter() - t_pose,
          _format_selection_log(
              target_inference.selection_results, target_inference
          ),
      )

      predictions: list[data_classes.PipelinePrediction] = []
      routing_info["selection_results"] = target_inference.selection_results
      for idx, res in enumerate(target_inference.selection_results):
        if not res.kept or res.pose_row_major is None or res.error is not None:
          continue
        pose_mat = np.array(res.pose_row_major, dtype=np.float64).reshape(4, 4)
        fp_score = float(res.fp_score) if res.fp_score is not None else 1.0
        score = pose_utils.bounded_score(fp_score)
        vis_score = (
            float(res.proposal_render_mask_iou)
            if res.proposal_render_mask_iou is not None
            else 1.0
        )
        box = (
            np.array(res.render_box_xyxy, dtype=np.float32)
            if res.render_box_xyxy is not None
            else np.zeros((4,), dtype=np.float32)
        )
        logging.info(
            "Estimated pose for object %d with fp_score %.3f (lower is better)"
            " -> score %.4f (higher is better, as published)",
            idx,
            fp_score,
            score,
        )
        predictions.append(
            data_classes.PipelinePrediction(
                pose_matrix_4x4=pose_mat,
                score=score,
                fp_score=fp_score,
                visibility_score=vis_score,
                unique_id=int(res.pred_index),
                bbox_xyxy=box,
            )
        )

      return predictions, routing_info
    finally:
      self._processor.confidence_threshold = previous_confidence_threshold
      try:
        self._pose_estimator.end_scene()
      except Exception:
        pass

  def RunPoseEstimation(
      self,
      request: v1_pose_estimation_service_pb2.RunPoseEstimationRequest,
      context: grpc.ServicerContext | None = None,
  ) -> v1_pose_estimation_service_pb2.RunPoseEstimationResponse:
    """Runs 6D object pose estimation for the requested target asset and views.

    Args:
      request: Pose estimation request containing `asset_id`, camera captures,
        and optional `pose_estimation_run_config` overrides.
      context: Optional gRPC servicer context used to report status codes.

    Returns:
      A `RunPoseEstimationResponse` with estimated poses ordered best-first.

    Raises:
      ValueError: If request validation fails (missing `asset_id`, camera views,
        CAD model, or invalid `debug.artifact_dir`).
      RuntimeError: If an internal pipeline or inference error occurs.
    """
    t0 = time.perf_counter()
    logging.info("RunPoseEstimation called")
    try:
      if not request.HasField("asset_id") or not request.asset_id.name:
        raise ValueError(
            "RunPoseEstimationRequest must include a populated asset_id."
        )
      asset_id = request.asset_id
      display_name = asset_id.name

      views = image_utils.extract_views_from_request(request, self._pubsub)
      if not views:
        raise ValueError("No camera views provided for pose estimation.")

      request_config = self._unpack_request_config(request)
      if request_config is not None and request_config.debug.HasField(
          "artifact_dir"
      ):
        raw_artifact_dir = pathlib.Path(request_config.debug.artifact_dir)
        if (
            ".." in raw_artifact_dir.parts
            or not raw_artifact_dir.resolve().is_relative_to(
                pathlib.Path("/tmp").resolve()
            )
        ):
          raise ValueError(
              "request_config.debug.artifact_dir must be under /tmp without"
              f" '..' traversal: {request_config.debug.artifact_dir!r}"
          )

      prompt, custom_conf_thresh = self._resolve_prompt(
          asset_id=asset_id, request_config=request_config
      )
      # Per-request overrides are client input: merge and validate them here
      # so a bad override is rejected as INVALID_ARGUMENT before taking the
      # GPU lock. build_inference_config raises ValueError on bad values.
      effective_config = (
          nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig()
      )
      effective_config.CopyFrom(self._config)
      if request_config is not None:
        effective_config.MergeFrom(request_config)
      inference_config = config_utils.build_inference_config(
          effective_config,
          sam3_confidence_threshold_override=custom_conf_thresh,
      )
      asset_key = id_utils.id_from_proto(asset_id)
      with self._cache_lock:
        cad_model_path = self._cad_model_cache.get(asset_key)
        if cad_model_path is not None and not cad_model_path.is_file():
          self._cad_model_cache.pop(asset_key, None)
          cad_model_path = None
      if cad_model_path is None:
        cad_model_path = asset_utils.get_local_cad_path_from_asset(
            asset_id=asset_id,
            assets_client=self._assets_client,
            cas_stub=self._cas_stub,
            cad_cache={},
        )
        if cad_model_path is None or not cad_model_path.is_file():
          raise ValueError(f"No CAD model found for asset '{display_name}'.")
        with self._cache_lock:
          self._cad_model_cache[asset_key] = cad_model_path
    except ValueError as e:
      logging.error(
          "Invalid argument in RunPoseEstimation after %.3fs: %s",
          time.perf_counter() - t0,
          e,
          exc_info=True,
      )
      if context is not None:
        context.abort(grpc.StatusCode.INVALID_ARGUMENT, str(e))
      raise

    try:
      with self._gpu_lock:
        if context is not None and not context.is_active():
          # Raise a dedicated exception rather than calling context.abort()
          # inside the try block: grpcio's ServicerContext.abort() raises
          # Exception(), which would otherwise be caught by `except Exception`
          # below and overwritten with StatusCode.INTERNAL.
          raise _ClientCancelledError(
              "Client cancelled or timed out while waiting for GPU lock."
          )
        if self._pose_estimator is None or self._processor is None:
          self._init_perception_pipeline()
        if self._fs_engine is None:
          self._init_foundation_stereo()

        predictions, routing_info = self._estimate_poses(
            views=views,
            base_camera_idx=0,
            prompt=prompt,
            cad_model_path=cad_model_path,
            inference_config=inference_config,
        )

        all_estimates = sorted(predictions, key=lambda p: p.score, reverse=True)

        # effective_config already has the per-request debug override merged.
        debug_cfg = effective_config.debug
        debug_dir = (
            debug_cfg.artifact_dir
            if debug_cfg.HasField("artifact_dir")
            else None
        )
        publish_vis = debug_cfg.publish_visualization

        if debug_dir or publish_vis:
          resolved_obj_id = routing_info.get("obj_id")
          if resolved_obj_id is None:
            resolved_obj_id = self._obj_id_for_cad(cad_model_path)
          debug_utils.save_debug_artifacts(
              artifact_dir=debug_dir,
              routing_info=routing_info,
              target_name=display_name,
              prompt=prompt,
              obj_id=resolved_obj_id,
              estimates=[
                  {
                      "id": display_name,
                      "score": p.score,
                      "fp_score": p.fp_score,
                      "visibility_score": p.visibility_score,
                  }
                  for p in all_estimates
              ],
              views=views,
              pose_renderer=self._pose_renderer,
              cad_model_path=cad_model_path,
              confidence_threshold=custom_conf_thresh,
              depth_source=_DepthSource.Name(self._depth_source),
              detection_image_pub=(
                  self._detection_image_pub if publish_vis else None
              ),
              pose_image_pub=self._pose_image_pub if publish_vis else None,
          )

      response = v1_pose_estimation_service_pb2.RunPoseEstimationResponse()
      for pred in all_estimates:
        pe = response.pose_estimates.add()
        pe.id = display_name
        pe.score = pred.score
        pose_mat = pred.pose_matrix_4x4
        rotation = rotation3.Rotation3.from_matrix(
            pose_mat[:3, :3], rtol=1e-05, atol=1e-05
        )
        pe.camera_t_target.CopyFrom(
            proto_conversion.pose_to_proto(
                pose3.Pose3(
                    rotation=rotation3.Rotation3(
                        quat=rotation.quaternion, normalize=True
                    ),
                    translation=pose_mat[:3, 3],
                )
            )
        )
      logging.info(
          "RunPoseEstimation completed in %.3fs (%d pose estimates).",
          time.perf_counter() - t0,
          len(response.pose_estimates),
      )
      return response
    except _ClientCancelledError as e:
      logging.warning("%s (after %.3fs)", e, time.perf_counter() - t0)
      if context is not None:
        context.abort(grpc.StatusCode.CANCELLED, str(e))
      raise
    except Exception as e:
      logging.error(
          "Error in RunPoseEstimation after %.3fs: %s",
          time.perf_counter() - t0,
          e,
          exc_info=True,
      )
      if context is not None:
        context.abort(grpc.StatusCode.INTERNAL, str(e))
      raise RuntimeError(f"Pose estimation failed: {e}") from e

  def DeletePoseEstimation(
      self,
      request: v1_pose_estimation_service_pb2.DeletePoseEstimationRequest,
      context: grpc.ServicerContext | None = None,
  ) -> empty_pb2.Empty:
    """Drops one asset's cached prompt, CAD model, mesh and estimators.

    The models are shared by every asset and stay loaded. The asset's obj_id is
    also evicted so a re-downloaded CAD model at the same path gets a fresh id.
    """
    if not request.HasField("asset_id") or not request.asset_id.name:
      return empty_pb2.Empty()
    try:
      asset_key = id_utils.id_from_proto(request.asset_id)
    except id_utils.IdValidationError as e:
      logging.error("Invalid asset_id in DeletePoseEstimation: %s", e)
      if context is not None:
        context.abort(grpc.StatusCode.INVALID_ARGUMENT, str(e))
      raise
    logging.info("Dropping cached state for asset '%s'.", asset_key)
    with self._gpu_lock:
      with self._cache_lock:
        self._asset_prompt_cache.pop(asset_key, None)
        cad_path = self._cad_model_cache.pop(asset_key, None)
        obj_id = None
        if cad_path:
          resolved_cad = cad_path.resolve()
          obj_id = self._cad_obj_ids.pop(resolved_cad, None)
          self._cad_file_stats.pop(resolved_cad, None)
      if cad_path:
        try:
          cad_path.unlink(missing_ok=True)
        except OSError as e:
          logging.warning(
              "Failed to remove cached CAD file '%s': %s", cad_path, e
          )
      if obj_id is None:
        return empty_pb2.Empty()
      mesh_key = path_resolver.pipeline_mesh_key(obj_id)
      if self._pose_renderer is not None:
        self._pose_renderer.evict_cad(mesh_key)
      if self._pose_registry is not None:
        self._pose_registry.evict_cad(mesh_key)
    return empty_pb2.Empty()
