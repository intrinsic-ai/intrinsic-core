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

"""Configuration resolution and conversion utilities for NVIDIA pose estimator.

Loads the default `NvidiaPoseEstimatorServiceConfig` textproto, merges runtime
or per-request overrides, validates stereo and FoundationPose parameters, and
translates the resolved proto config into sensor routing settings and the
upstream pipeline's `InferenceConfig`.
"""

from __future__ import annotations

import os
from pathlib import Path

from foundationpose_perception_pipeline import config as pipeline_config
from foundationpose_perception_pipeline.inference.config import DetectionConfig
from foundationpose_perception_pipeline.inference.config import InferenceConfig
from foundationpose_perception_pipeline.inference.config import PoseConfig
from foundationpose_perception_pipeline.inference.config import RefinementConfig
from foundationpose_perception_pipeline.inference.config import SelectionConfig
from google.protobuf import any_pb2
from google.protobuf import text_format

from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.proto import nvidia_service_config_pb2
from incode.util.runfiles import runfiles

_DepthSource = nvidia_service_config_pb2.DepthSource
_Sam3RefinementPolicy = nvidia_service_config_pb2.Sam3RefinementPolicy
_ProposalSelectionPolicy = nvidia_service_config_pb2.ProposalSelectionPolicy
_SAM3_REFINEMENT_POLICIES: dict[int, str] = {
    _Sam3RefinementPolicy.SAM3_REFINEMENT_POLICY_UNSPECIFIED: (
        pipeline_config.DEFAULT_SAM3_REFINEMENT_POLICY
    ),
    _Sam3RefinementPolicy.SAM3_REFINEMENT_POLICY_NONE: (
        pipeline_config.NO_REFINEMENT_POLICY
    ),
    _Sam3RefinementPolicy.SAM3_REFINEMENT_POLICY_REPLACE_MID_NMS06: (
        pipeline_config.REPLACE_MID_NMS06_REFINEMENT_POLICY
    ),
}
_PROPOSAL_SELECTION_POLICIES: dict[int, str] = {
    _ProposalSelectionPolicy.PROPOSAL_SELECTION_POLICY_UNSPECIFIED: (
        pipeline_config.DEFAULT_RERANK_POLICY
    ),
    _ProposalSelectionPolicy.PROPOSAL_SELECTION_POLICY_KEEP_ALL: (
        pipeline_config.KEEP_ALL_RERANK_POLICY
    ),
    _ProposalSelectionPolicy.PROPOSAL_SELECTION_POLICY_SOFT_GLOBAL_V1: (
        pipeline_config.SOFT_GLOBAL_RERANK_POLICY
    ),
}

_DEFAULT_PACKAGE_DIR = Path(__file__).resolve().parent.parent
_DEFAULT_CONFIG_RELPATH = Path("configs") / "default_config.textproto"
_DEFAULT_CONFIG_RLOCATION = (
    f"{__package__.rpartition('.')[0].replace('.', '/')}/"
    f"{_DEFAULT_CONFIG_RELPATH.as_posix()}"
)


def resolve_service_config(
    user_config: (
        nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig | None
    ) = None,
    package_dir: Path | None = None,
) -> nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig:
  """Loads default_config.textproto and merges any user-provided config overrides.

  Args:
    user_config: Optional user-provided service configuration protobuf to merge
      on top of the default configuration.
    package_dir: Optional package root directory override used when resolving
      `configs/default_config.textproto` outside Bazel runfiles.

  Returns:
    A resolved `NvidiaPoseEstimatorServiceConfig` protobuf message.

  Raises:
    ValueError: If `stereo_sensors` is overridden with fewer than 2 sensors or
      contains an empty sensor name, or if `DEPTH_SOURCE_STEREO` is selected
      without at least 2 stereo sensors.
  """
  pkg_dir = package_dir or _DEFAULT_PACKAGE_DIR
  try:
    default_config_path = runfiles.RlocationCurrentRepository(
        _DEFAULT_CONFIG_RLOCATION
    )
  except (AssertionError, OSError):
    default_config_path = None
  if not default_config_path or not os.path.exists(default_config_path):
    candidate = pkg_dir / _DEFAULT_CONFIG_RELPATH
    if candidate.is_file():
      default_config_path = str(candidate)

  config = nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig()
  if default_config_path and os.path.exists(default_config_path):
    with open(default_config_path, encoding="utf-8") as f:
      content = f.read().strip()
      # The asset toolchain may store the default config either as the bare
      # message or wrapped in an Any, depending on how it was packaged.
      try:
        text_format.Parse(content, config)
      except text_format.ParseError:
        any_msg = any_pb2.Any()
        text_format.Parse(content, any_msg)
        any_msg.Unpack(config)

  if user_config is not None:
    # Proto3 repeated fields do not track presence, so an empty stereo_sensors
    # list means "unset (inherit defaults)"; disabling stereo depth is done via
    # depth_source = DEPTH_SOURCE_HARDWARE. When non-empty, MergeFrom would
    # append to the default pair rather than replacing it, so validate and clear
    # the default list first.
    user_stereo = user_config.sensor_routing.stereo_sensors
    if user_stereo:
      if len(user_stereo) < 2 or any(not s.strip() for s in user_stereo):
        raise ValueError(
            "stereo_sensors must contain at least 2 non-empty sensor names,"
            f" got {list(user_stereo)!r}"
        )
      del config.sensor_routing.stereo_sensors[:]
    config.MergeFrom(user_config)

  resolved_stereo = config.sensor_routing.stereo_sensors
  if resolved_stereo and (
      len(resolved_stereo) < 2 or any(not s.strip() for s in resolved_stereo)
  ):
    raise ValueError(
        "stereo_sensors must contain at least 2 non-empty sensor names,"
        f" got {list(resolved_stereo)!r}"
    )
  if (
      config.sensor_routing.depth_source == _DepthSource.DEPTH_SOURCE_STEREO
      and len(resolved_stereo) < 2
  ):
    raise ValueError(
        "DEPTH_SOURCE_STEREO requires at least 2 stereo_sensors,"
        f" got {list(resolved_stereo)!r}"
    )

  return config


def get_sensor_routing(
    config: nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig,
) -> tuple[
    str | None,
    nvidia_service_config_pb2.DepthSource.ValueType,
    str | None,
    tuple[str, ...],
]:
  """Extracts sensor routing selectors as (detection, depth_source, hw_depth, stereo).

  Unset sensor selectors come back as None so that select_pipeline_views()
  falls back to choosing by pixel type instead of matching a sensor name. An
  unset or DEPTH_SOURCE_UNSPECIFIED depth source comes back as
  DEPTH_SOURCE_AUTO.

  Args:
    config: The resolved service config.

  Returns:
    The detection sensor, depth source, hardware depth sensor and the ordered
    stereo sensor candidates.
  """
  routing = config.sensor_routing
  detection_sensor = (
      routing.detection_sensor if routing.HasField("detection_sensor") else None
  )
  depth_source = routing.depth_source or _DepthSource.DEPTH_SOURCE_AUTO
  hardware_depth_sensor = (
      routing.hardware_depth_sensor
      if routing.HasField("hardware_depth_sensor")
      else None
  )
  stereo_sensors = tuple(routing.stereo_sensors)
  return detection_sensor, depth_source, hardware_depth_sensor, stereo_sensors


def build_inference_config(
    config: nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig,
    sam3_confidence_threshold_override: float | None = None,
) -> InferenceConfig:
  """Converts a resolved service protobuf config into a pipeline InferenceConfig.

  Args:
    config: Resolved service configuration protobuf (from
      `resolve_service_config`).
    sam3_confidence_threshold_override: Optional per-target or per-request SAM3
      confidence threshold override (e.g. from `IocPoseEstimatorParams`).

  Returns:
    A populated `InferenceConfig` dataclass for the inference pipeline.

  Raises:
    ValueError: If a policy enum holds a value this service does not know, or if
      any numeric parameter is out of range.
  """
  refinement_policy = config.sam3_refinement.policy
  if refinement_policy not in _SAM3_REFINEMENT_POLICIES:
    raise ValueError(f"Unknown SAM3 refinement policy: {refinement_policy}")
  selection_policy = config.selection.policy
  if selection_policy not in _PROPOSAL_SELECTION_POLICIES:
    raise ValueError(f"Unknown proposal selection policy: {selection_policy}")

  confidence_threshold = (
      float(sam3_confidence_threshold_override)
      if sam3_confidence_threshold_override is not None
      else float(config.sam3_detection.confidence_threshold)
  )
  n_refine = int(config.foundation_pose.n_refine)
  n_hypotheses = int(config.foundation_pose.n_hypotheses)
  fp_score_divisor = float(config.selection.fp_score_divisor)

  if not 0.0 <= confidence_threshold <= 1.0:
    raise ValueError(
        "confidence_threshold must be in [0.0, 1.0], got"
        f" {confidence_threshold}"
    )
  if n_refine < 0:
    raise ValueError(f"n_refine must be >= 0, got {n_refine}")
  if n_hypotheses <= 0:
    raise ValueError(f"n_hypotheses must be > 0, got {n_hypotheses}")
  if fp_score_divisor <= 0:
    raise ValueError(f"fp_score_divisor must be > 0, got {fp_score_divisor}")

  return InferenceConfig(
      detection=DetectionConfig(
          confidence_threshold=confidence_threshold,
          resolution=int(config.sam3_detection.resolution),
      ),
      pose=PoseConfig(
          n_refine=n_refine,
          n_hypotheses=n_hypotheses,
      ),
      refinement=RefinementConfig(
          policy=_SAM3_REFINEMENT_POLICIES[refinement_policy],
          low_miou=float(config.sam3_refinement.low_miou),
          high_miou=float(config.sam3_refinement.high_miou),
          nms_threshold=float(config.sam3_refinement.nms_threshold),
      ),
      selection=SelectionConfig(
          policy=_PROPOSAL_SELECTION_POLICIES[selection_policy],
          cutoff=float(config.selection.cutoff),
          weight_sam_score=float(config.selection.weight_sam_score),
          weight_render_mask_iou=float(config.selection.weight_render_mask_iou),
          weight_render_box_iou=float(config.selection.weight_render_box_iou),
          fp_score_divisor=fp_score_divisor,
      ),
  )
