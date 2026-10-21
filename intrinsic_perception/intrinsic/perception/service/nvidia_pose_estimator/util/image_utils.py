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

"""Image and camera view extraction utilities for NVIDIA pose estimator.

Decodes sensor images from `RunPoseEstimationRequest` (both inline
`capture_result` and PubSub KV-backed `capture_data_list`) into `CameraFrame`
objects, validates double-precision camera intrinsics, and routes views to the
detection, hardware depth, and stereo stages (`select_pipeline_views`).
"""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass
import logging

import numpy as np

from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.proto import nvidia_service_config_pb2
from intrinsic.math.python import pose3
from intrinsic.perception.client.v1.python.camera import data_classes
from intrinsic.perception.proto.v1 import image_buffer_pb2
from intrinsic.perception.proto.v1 import pose_estimation_service_pb2 as v1_pose_estimation_service_pb2
from intrinsic.perception.skills.multi_view import multi_view_pose_utils
from intrinsic.platform.pubsub.python import pubsub


@dataclass
class CameraFrame:
  """Container for a single sensor's image data and calibration parameters."""

  sensor_name: str
  image: np.ndarray  # (H, W, 3) uint8 RGB, or (H, W) float32 metric depth
  sensor_type: image_buffer_pb2.PixelType.ValueType
  camera_matrix: np.ndarray | None  # (3, 3) float64 K, None if uncalibrated
  world_t_camera: pose3.Pose3 | None = None


@dataclass(frozen=True)
class PipelineViews:
  """Routed camera views for detection, depth, and stereo estimation."""

  detection_view: CameraFrame
  depth_view: CameraFrame | None = None
  stereo_views: tuple[CameraFrame, ...] = ()


def select_pipeline_views(
    views: Sequence[CameraFrame],
    detection_sensor: str | None = "1",
    depth_source: nvidia_service_config_pb2.DepthSource.ValueType = (
        nvidia_service_config_pb2.DepthSource.DEPTH_SOURCE_AUTO
    ),
    hardware_depth_sensor: str | None = "4",
    stereo_sensors: Sequence[str] | None = ("2", "3"),
    base_camera_idx: int = 0,
) -> PipelineViews:
  """Routes the captured views to the detection, depth and stereo stages.

  Sensors are matched by name. If a configured sensor is unset or missing
  from `views`, a view is picked by pixel type instead.

  Args:
    views: All views extracted from the request.
    detection_sensor: Sensor to run detection on. Falls back to the first
      intensity view, then to `views[base_camera_idx]`.
    depth_source: `SensorRoutingConfig.depth_source`. DEPTH_SOURCE_UNSPECIFIED
      is treated as DEPTH_SOURCE_AUTO.
    hardware_depth_sensor: Sensor providing hardware depth. Falls back to the
      first depth view.
    stereo_sensors: Candidate stereo sensors, base camera first. Falls back to
      all intensity views.
    base_camera_idx: Index into `views` used when there is no intensity view.

  Returns:
    The routed views. `depth_view` is None and `stereo_views` is empty when
    `depth_source` excludes them or no matching view exists.

  Raises:
    NotImplementedError: If `depth_source` is unrecognized.
  """
  views_by_name = {v.sensor_name: v for v in views}

  if detection_sensor and detection_sensor in views_by_name:
    detection_view = views_by_name[detection_sensor]
  else:
    intensity_views = [
        v
        for v in views
        if v.sensor_type == image_buffer_pb2.PixelType.PIXEL_INTENSITY
    ]
    detection_view = (
        intensity_views[0] if intensity_views else views[base_camera_idx]
    )

  # DEPTH_SOURCE_AUTO fills both depth_view and stereo_views; the service
  # prefers depth_view and falls back to stereo.
  depth_source_enum = nvidia_service_config_pb2.DepthSource
  if depth_source == depth_source_enum.DEPTH_SOURCE_UNSPECIFIED:
    depth_source = depth_source_enum.DEPTH_SOURCE_AUTO
  elif depth_source not in (
      depth_source_enum.DEPTH_SOURCE_HARDWARE,
      depth_source_enum.DEPTH_SOURCE_STEREO,
      depth_source_enum.DEPTH_SOURCE_AUTO,
  ):
    raise NotImplementedError(f"Unrecognized depth_source: {depth_source}")

  depth_view = None
  if depth_source in (
      depth_source_enum.DEPTH_SOURCE_HARDWARE,
      depth_source_enum.DEPTH_SOURCE_AUTO,
  ):
    if hardware_depth_sensor and hardware_depth_sensor in views_by_name:
      depth_view = views_by_name[hardware_depth_sensor]
    else:
      depth_views = [
          v
          for v in views
          if v.sensor_type == image_buffer_pb2.PixelType.PIXEL_DEPTH
      ]
      if depth_views:
        depth_view = depth_views[0]

  stereo_views: list[CameraFrame] = []
  if depth_source in (
      depth_source_enum.DEPTH_SOURCE_STEREO,
      depth_source_enum.DEPTH_SOURCE_AUTO,
  ):
    if stereo_sensors:
      stereo_views = [
          views_by_name[s] for s in stereo_sensors if s in views_by_name
      ]
    if len(stereo_views) < 2:
      stereo_views = [
          v
          for v in views
          if v.sensor_type == image_buffer_pb2.PixelType.PIXEL_INTENSITY
      ]

  return PipelineViews(
      detection_view=detection_view,
      depth_view=depth_view,
      stereo_views=tuple(stereo_views),
  )


def validate_camera_matrix(
    camera_matrix: np.ndarray | None, sensor_name: str
) -> np.ndarray:
  """Validates a sensor's intrinsic matrix and returns it as float64.

  Converts the matrix to float64 because downstream stereo rectification
  (`cv2.stereoRectify`) and depth reprojection require double-precision (`CV_64F`)
  intrinsics.

  Args:
    camera_matrix: The sensor's intrinsic matrix K, or None if uncalibrated.
    sensor_name: Sensor name, used in the error message.

  Returns:
    The validated (3, 3) intrinsic matrix cast to float64.

  Raises:
    ValueError: If `camera_matrix` is None, not (3, 3), contains non-finite
      entries, or has a non-positive focal length fx or fy.
  """
  if (
      camera_matrix is None
      or camera_matrix.shape != (3, 3)
      or not np.all(np.isfinite(camera_matrix))
      or camera_matrix[0, 0] <= 0
      or camera_matrix[1, 1] <= 0
  ):
    raise ValueError(
        f"Sensor {sensor_name!r} has no valid intrinsic matrix "
        f"(got {camera_matrix!r}). Pose estimation requires calibrated "
        "intrinsics; check that the capture result carries camera_params."
    )
  return camera_matrix.astype(np.float64)


def _is_depth_sensor(s_img: data_classes.SensorImage) -> bool:
  """Returns True if the sensor image represents a depth map."""
  return s_img.proto.buffer.pixel_type == image_buffer_pb2.PixelType.PIXEL_DEPTH


def _to_3channel_rgb(arr: np.ndarray) -> np.ndarray:
  """Ensures an intensity image array has 3 channels (H, W, 3)."""
  if arr.ndim == 2:
    return np.repeat(arr[:, :, None], 3, axis=-1)
  if arr.ndim == 3 and arr.shape[2] == 1:
    return np.repeat(arr, 3, axis=-1)
  if arr.ndim == 3 and arr.shape[2] == 4:
    return arr[:, :, :3]
  return arr


def extract_views_from_request(
    request: v1_pose_estimation_service_pb2.RunPoseEstimationRequest,
    pubsub_client: pubsub.PubSub | None = None,
) -> list[CameraFrame]:
  """Extracts one CameraFrame per sensor image from a RunPoseEstimationRequest.

  Supports both `capture_data_list` (loaded via `pubsub_client`) and inline
  `capture_result` inputs. Depth sensors become `(H, W)` float32 depth maps in
  meters (`uint16` input is treated as millimeters); all other sensors become
  `(H, W, 3)` RGB images.

  Args:
    request: The pose estimation request.
    pubsub_client: Used to load captures referenced by `capture_data_list`
      from the KV store.

  Returns:
    One CameraFrame per sensor image in capture order, or an empty list if the
    request has neither field.
  """
  raw_capture_results: list[data_classes.CaptureResult] = []
  if request.HasField("capture_data_list"):
    raw_capture_results = multi_view_pose_utils.load_capture_results(
        request.capture_data_list.capture_data, pubsub_client
    )
  elif request.HasField("capture_result"):
    raw_capture_results.append(
        data_classes.CaptureResult(request.capture_result)
    )
  else:
    return []

  views: list[CameraFrame] = []
  for cap_obj in raw_capture_results:
    if not cap_obj.sensor_images:
      continue

    for s_img in cap_obj.sensor_images.values():
      sensor_name_str = s_img.sensor_name
      is_depth = _is_depth_sensor(s_img)
      w_t_c = s_img.world_t_sensor or s_img.camera_t_sensor
      camera_matrix = s_img.intrinsic_matrix

      if is_depth:
        d_arr = s_img.array
        if d_arr.ndim == 3 and d_arr.shape[2] == 1:
          d_arr = d_arr.squeeze(2)
        depth = (
            d_arr.astype(np.float32) / 1000.0
            if d_arr.dtype == np.uint16
            else d_arr.astype(np.float32)
        )
        logging.info("Extracted depth map from sensor '%s'.", sensor_name_str)
        views.append(
            CameraFrame(
                sensor_name=sensor_name_str,
                image=depth,
                sensor_type=image_buffer_pb2.PixelType.PIXEL_DEPTH,
                camera_matrix=camera_matrix,
                world_t_camera=w_t_c,
            )
        )
      else:
        logging.info(
            "Extracted intensity image and matching intrinsics from sensor"
            " '%s'.",
            sensor_name_str,
        )
        views.append(
            CameraFrame(
                sensor_name=sensor_name_str,
                image=_to_3channel_rgb(s_img.array),
                sensor_type=image_buffer_pb2.PixelType.PIXEL_INTENSITY,
                camera_matrix=camera_matrix,
                world_t_camera=w_t_c,
            )
        )

  return views
