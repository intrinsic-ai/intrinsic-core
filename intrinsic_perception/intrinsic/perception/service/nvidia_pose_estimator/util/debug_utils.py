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

"""Writes intermediate pipeline artifacts to disk for offline inspection.

Enabled by setting DebugConfig.artifact_dir, in the service config or per
request. Unset by default, which disables dumping.

Saves:
- detection_view_rgb.png: The RGB image used for 2D detection and pose estimation.
- sam3_proposals.png & sam3_mask_{i}.png: Raw SAM 3 proposals and masks.
- depth_metric.npy & depth_vis.png: The aligned depth map actually used by
  FoundationPose (from hardware depth or FoundationStereo).
- cad_overlay.png: Visualization of the estimated CAD model silhouette outline,
  semi-transparent mesh overlay, and 3D coordinate frame axes (X=Red, Y=Green,
  Z=Blue) projected onto the detection image.
- sensors/: All raw input sensor images (RGB, IR stereo pair, hardware depth
  in meters as .npy + colormapped preview).
- target_cad.ply: Copy of the target CAD mesh used for estimation.
- summary.json: Complete metadata including camera intrinsics, extrinsics
  (world_t_camera), prompt, confidence threshold, and per-candidate scores
  so the capture can be replayed offline.
"""

from __future__ import annotations

from collections.abc import Sequence
import json
import logging
from pathlib import Path
import shutil
import time
from typing import Any

import cv2
import numpy as np

from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.util import path_resolver
from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.util.pose_utils import bounded_score
from intrinsic.perception.client.v1.python import image_utils as client_image_utils
from intrinsic.perception.proto.v1 import image_buffer_pb2

DETECTION_IMAGE_TOPIC: str = "ioc_pose_estimator/detections"
POSE_IMAGE_TOPIC: str = "ioc_pose_estimator/poses"


def _to_list(value: Any) -> Any:
  """Converts tensors and arrays to plain lists so json can serialise them."""
  if value is None:
    return None
  if hasattr(value, "tolist"):
    return value.tolist()
  return value


def _save_depth_colormap(depth_m: np.ndarray, out_path: Path) -> dict[str, Any]:
  """Writes a colormapped preview of a metric depth map and returns its stats."""
  valid = np.isfinite(depth_m) & (depth_m > 0)
  stats = {
      "shape": list(depth_m.shape),
      "valid_pixels": int(valid.sum()),
      "total_pixels": int(depth_m.size),
      "valid_fraction": (
          float(valid.sum() / depth_m.size) if depth_m.size else 0.0
      ),
  }
  if valid.any():
    d_min = float(depth_m[valid].min())
    d_max = float(depth_m[valid].max())
    stats["min_m"] = d_min
    stats["max_m"] = d_max
    stats["mean_m"] = float(depth_m[valid].mean())

    span = max(d_max - d_min, 1e-6)
    norm = np.zeros(depth_m.shape, dtype=np.uint8)
    norm[valid] = np.clip(
        (depth_m[valid] - d_min) / span * 255.0, 0, 255
    ).astype(np.uint8)
    preview = cv2.applyColorMap(norm, cv2.COLORMAP_TURBO)
    preview[~valid] = 0  # invalid pixels black, not colormapped zero
    cv2.imwrite(str(out_path), preview)
  else:
    logging.warning(
        "[DebugArtifacts] Depth map (%s) has no valid pixels.", out_path.name
    )

  return stats


def _save_depth(
    depth_m: np.ndarray, out_dir: Path, depth_source: str | None = None
) -> dict[str, Any]:
  """Writes the metric depth used by the pipeline and its colormapped preview."""
  np.save(str(out_dir / "depth_metric.npy"), depth_m)
  stats = _save_depth_colormap(depth_m, out_dir / "depth_vis.png")
  if depth_source is not None:
    stats["depth_source"] = depth_source
  return stats


def _save_all_sensors(
    views: Sequence[Any], out_dir: Path
) -> dict[str, dict[str, Any]]:
  """Saves all input sensor frames (RGB, IR, hardware depth) and their metadata."""
  sensors_dir = out_dir / "sensors"
  sensors_dir.mkdir(parents=True, exist_ok=True)
  sensors_summary: dict[str, dict[str, Any]] = {}

  for idx, v in enumerate(views):
    sensor_name = str(getattr(v, "sensor_name", idx))
    safe_name = f"sensor_{sensor_name}"
    img = getattr(v, "image", None)
    if img is None:
      continue

    img_arr = np.asarray(img)
    sensor_type_enum = getattr(v, "sensor_type", None)
    if sensor_type_enum in (
        None,
        image_buffer_pb2.PixelType.PIXEL_UNSPECIFIED,
    ):
      is_depth = img_arr.dtype in (np.float32, np.float64)
    else:
      is_depth = sensor_type_enum == image_buffer_pb2.PixelType.PIXEL_DEPTH

    if is_depth:
      npy_rel = f"sensors/{safe_name}_depth.npy"
      vis_rel = f"sensors/{safe_name}_depth_vis.png"
      np.save(str(out_dir / npy_rel), img_arr)
      depth_stats = _save_depth_colormap(img_arr, out_dir / vis_rel)
      file_info = {
          "file": npy_rel,
          "vis_file": vis_rel,
          "depth_stats": depth_stats,
      }
      type_str = "PIXEL_DEPTH"
    else:
      png_rel = f"sensors/{safe_name}_rgb.png"
      if img_arr.ndim == 3 and img_arr.shape[2] == 3:
        cv2.imwrite(
            str(out_dir / png_rel), cv2.cvtColor(img_arr, cv2.COLOR_RGB2BGR)
        )
      else:
        cv2.imwrite(str(out_dir / png_rel), img_arr)
      file_info = {"file": png_rel}
      type_str = "PIXEL_INTENSITY"

    ext_dict = None
    w_t_c = getattr(v, "world_t_camera", None)
    if w_t_c is not None:
      ext_dict = {}
      if hasattr(w_t_c, "translation"):
        ext_dict["translation"] = _to_list(w_t_c.translation)
      if hasattr(w_t_c, "quaternion"):
        q = w_t_c.quaternion
        ext_dict["quaternion_wxyz"] = [
            float(q.w),
            float(q.x),
            float(q.y),
            float(q.z),
        ]
      if hasattr(w_t_c, "matrix4x4"):
        ext_dict["matrix_4x4"] = _to_list(w_t_c.matrix4x4())

    sensors_summary[sensor_name] = {
        "sensor_name": sensor_name,
        "sensor_type": type_str,
        "shape": list(img_arr.shape),
        "dtype": str(img_arr.dtype),
        "camera_matrix": _to_list(getattr(v, "camera_matrix", None)),
        "world_t_camera": ext_dict,
        **file_info,
    }

  return sensors_summary


def _render_and_save_sam3(
    rgb: np.ndarray,
    sam3: dict[str, Any],
    out_dir: Path | None = None,
) -> tuple[np.ndarray, dict[str, Any]]:
  """Renders the SAM3 proposal overlay and optionally writes it and masks to disk."""
  boxes = np.asarray(_to_list(sam3.get("boxes")) or [], dtype=np.float32)
  scores = np.asarray(_to_list(sam3.get("scores")) or [], dtype=np.float32)
  masks = sam3.get("masks")

  overlay = cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR)
  for i, box in enumerate(boxes):
    x0, y0, x1, y1 = (int(v) for v in box[:4])
    cv2.rectangle(overlay, (x0, y0), (x1, y1), (0, 255, 255), 2)
    label = f"#{i} {scores[i]:.3f}" if i < len(scores) else f"#{i}"
    cv2.putText(
        overlay,
        label,
        (x0, max(y0 - 6, 12)),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.5,
        (0, 255, 255),
        1,
    )
  if out_dir is not None:
    cv2.imwrite(str(out_dir / "sam3_proposals.png"), overlay)

    if masks is not None:
      mask_arr = np.asarray(_to_list(masks))
      if mask_arr.ndim == 4 and mask_arr.shape[1] == 1:
        mask_arr = mask_arr[:, 0]
      if mask_arr.ndim == 3:
        for i, mask in enumerate(mask_arr):
          cv2.imwrite(
              str(out_dir / f"sam3_mask_{i}.png"),
              (np.asarray(mask) > 0).astype(np.uint8) * 255,
          )

  vis_rgb = cv2.cvtColor(overlay, cv2.COLOR_BGR2RGB)
  return vis_rgb, {
      "num_proposals": int(len(boxes)),
      "boxes_xyxy": boxes.tolist(),
      "scores": scores.tolist(),
  }


def _render_cad_mask_fallback(
    pose_renderer: Any,
    obj_id: int,
    pose_mat: np.ndarray,
    camera_matrix: np.ndarray,
    image_size: tuple[int, int],
) -> np.ndarray | None:
  """Renders a binary CAD silhouette mask using pose_renderer or software projection."""
  w, h = image_size
  if pose_renderer is not None:
    try:
      mask = pose_renderer.render_mask_from_pose(
          *path_resolver.pipeline_mesh_key(obj_id),
          pose_mat,
          camera_matrix,
          (w, h),
      )
      if mask is not None:
        return np.asarray(mask) > 0
    except Exception:
      pass

    # Software rasterization fallback using mesh triangles
    try:
      verts_mm, faces = pose_renderer.mesh(
          *path_resolver.pipeline_mesh_key(obj_id)
      )
      verts_m = np.asarray(verts_mm, dtype=np.float64) * 0.001
      rot = pose_mat[:3, :3].astype(np.float64)
      trans = pose_mat[:3, 3].astype(np.float64)
      verts_cam = (rot @ verts_m.T).T + trans
      valid_z = verts_cam[:, 2] > 1e-3
      if not np.any(valid_z):
        return None
      proj = (camera_matrix @ verts_cam.T).T
      uv = np.round(proj[:, :2] / np.maximum(proj[:, 2:3], 1e-6)).astype(
          np.int32
      )
      mask_img = np.zeros((h, w), dtype=np.uint8)
      faces_arr = np.asarray(faces, dtype=np.int32)
      for tri in faces_arr:
        if valid_z[tri[0]] and valid_z[tri[1]] and valid_z[tri[2]]:
          pts = uv[tri].reshape((-1, 1, 2))
          cv2.fillConvexPoly(mask_img, pts, 255)
      return mask_img > 0
    except Exception:
      pass
  return None


def _draw_pose_axes(
    canvas: np.ndarray,
    pose_mat: np.ndarray,
    camera_matrix: np.ndarray,
    axis_length_m: float = 0.05,
) -> tuple[int, int] | None:
  """Draws 3D coordinate frame axes (X=Red, Y=Green, Z=Blue) on BGR canvas."""
  try:
    pts_obj = np.array(
        [
            [0.0, 0.0, 0.0],
            [axis_length_m, 0.0, 0.0],
            [0.0, axis_length_m, 0.0],
            [0.0, 0.0, axis_length_m],
        ],
        dtype=np.float64,
    )
    rot = pose_mat[:3, :3].astype(np.float64)
    trans = pose_mat[:3, 3].astype(np.float64)
    pts_cam = (rot @ pts_obj.T).T + trans
    if np.any(pts_cam[:, 2] <= 1e-3):
      return None
    proj = (camera_matrix @ pts_cam.T).T
    uv = np.round(proj[:, :2] / proj[:, 2:3]).astype(np.int32)
    origin = (int(uv[0, 0]), int(uv[0, 1]))
    # BGR colors: X=Red (0,0,255), Y=Green (0,255,0), Z=Blue (255,0,0)
    colors = [(0, 0, 255), (0, 255, 0), (255, 0, 0)]
    for axis_idx in range(3):
      end_pt = (int(uv[axis_idx + 1, 0]), int(uv[axis_idx + 1, 1]))
      cv2.arrowedLine(
          canvas, origin, end_pt, colors[axis_idx], 2, tipLength=0.2
      )
    return origin
  except Exception:
    return None


def _render_and_save_cad_overlay(
    rgb: np.ndarray,
    camera_matrix: np.ndarray | None,
    selection_results: Sequence[Any] | None,
    pose_renderer: Any,
    obj_id: int,
    out_dir: Path | None = None,
) -> np.ndarray:
  """Renders the CAD silhouette and 3D pose axes overlay and optionally writes cad_overlay.png."""
  overlay = cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR)
  h, w = rgb.shape[:2]

  if camera_matrix is not None and selection_results:
    K = np.asarray(camera_matrix, dtype=np.float64)
    for idx, res in enumerate(selection_results):
      if getattr(res, "pose_row_major", None) is None:
        continue
      pose_mat = np.asarray(res.pose_row_major, dtype=np.float64).reshape(4, 4)
      kept = bool(getattr(res, "kept", False))
      color = (
          (0, 255, 0) if kept else (0, 140, 255)
      )  # Green if kept, Orange if rejected

      mask_bool = _render_cad_mask_fallback(
          pose_renderer, obj_id, pose_mat, K, (w, h)
      )
      if (
          mask_bool is not None
          and mask_bool.shape == (h, w)
          and np.any(mask_bool)
      ):
        # Alpha blend silhouette fill
        tint = np.zeros_like(overlay)
        tint[:, :] = color
        alpha = 0.25 if kept else 0.15
        overlay[mask_bool] = cv2.addWeighted(
            overlay[mask_bool], 1.0 - alpha, tint[mask_bool], alpha, 0
        )
        # Draw crisp CAD contour outline
        contours, _ = cv2.findContours(
            mask_bool.astype(np.uint8),
            cv2.RETR_EXTERNAL,
            cv2.CHAIN_APPROX_SIMPLE,
        )
        thickness = 2 if kept else 1
        cv2.drawContours(overlay, contours, -1, color, thickness)

      origin = _draw_pose_axes(overlay, pose_mat, K)
      fp_score = getattr(res, "fp_score", None)
      mask_iou = getattr(res, "proposal_render_mask_iou", None)
      score_str = (
          f"{bounded_score(float(fp_score)):.2f} (fp={float(fp_score):.1f})"
          if fp_score is not None
          else "N/A"
      )
      iou_str = f"{float(mask_iou):.2f}" if mask_iou is not None else "N/A"
      status_str = "KEPT" if kept else "REJ"
      label = f"#{idx} [{status_str}] score={score_str} iou={iou_str}"

      label_pos = origin if origin is not None else (20, 30 + idx * 25)
      cv2.putText(
          overlay,
          label,
          (max(label_pos[0] - 40, 5), max(label_pos[1] - 12, 18)),
          cv2.FONT_HERSHEY_SIMPLEX,
          0.5,
          color,
          2,
      )

  if out_dir is not None:
    cv2.imwrite(str(out_dir / "cad_overlay.png"), overlay)
  return cv2.cvtColor(overlay, cv2.COLOR_BGR2RGB)


def _selection_summary(selection_results: Any) -> list[dict[str, Any]]:
  """Flattens the selector's per-candidate outcome into json-friendly dicts."""
  summary = []
  for idx, res in enumerate(selection_results):
    pose = _to_list(res.pose_row_major)
    summary.append({
        "index": idx,
        "kept": bool(res.kept),
        "error": str(res.error) if res.error is not None else None,
        "fp_score": _to_list(res.fp_score),
        "score": (
            bounded_score(float(res.fp_score))
            if res.fp_score is not None
            else None
        ),
        "proposal_render_mask_iou": _to_list(res.proposal_render_mask_iou),
        "proposal_render_box_iou": _to_list(res.proposal_render_box_iou),
        "render_box_xyxy": _to_list(res.render_box_xyxy),
        "pose_row_major": pose,
    })
  return summary


def save_debug_artifacts(
    artifact_dir: Path | str | None,
    routing_info: dict[str, Any],
    target_name: str,
    prompt: str,
    obj_id: int,
    estimates: list[dict[str, Any]],
    views: Sequence[Any] | None = None,
    pose_renderer: Any = None,
    cad_model_path: Path | str | None = None,
    confidence_threshold: float | None = None,
    depth_source: str | None = None,
    detection_image_pub: Any = None,
    pose_image_pub: Any = None,
) -> Path | None:
  """Dumps one request's intermediates to disk and/or publishes visualization overlays.

  Returns the directory written, or None if disk dumping was disabled or failed
  (errors are logged and swallowed).
  """
  try:
    out_dir: Path | None = None
    if artifact_dir:
      out_dir = Path(artifact_dir) / time.strftime("%Y%m%d-%H%M%S")
      # Handle sub-second collisions if multiple requests arrive within the same second
      suffix = 1
      base_out_dir = out_dir
      while out_dir.exists():
        out_dir = Path(f"{base_out_dir}_{suffix}")
        suffix += 1
      out_dir.mkdir(parents=True, exist_ok=True)

    summary: dict[str, Any] = {
        "target_name": target_name,
        "prompt": prompt,
        "confidence_threshold": confidence_threshold,
        "depth_source": depth_source,
        "num_estimates": len(estimates),
    }

    if (
        out_dir is not None
        and cad_model_path is not None
        and Path(cad_model_path).is_file()
    ):
      try:
        shutil.copy2(str(cad_model_path), str(out_dir / "target_cad.ply"))
        summary["cad_model_file"] = "target_cad.ply"
        summary["cad_model_source_path"] = str(cad_model_path)
      except Exception as e:
        logging.warning("[DebugArtifacts] Could not copy CAD model: %s", e)

    if out_dir is not None and views:
      summary["sensors"] = _save_all_sensors(views, out_dir)

    detection_view = routing_info.get("detection_view")
    camera_matrix = routing_info.get("camera_matrix")
    selection_results = routing_info.get("selection_results")

    vis_img: np.ndarray | None = None
    pose_vis_img: np.ndarray | None = None
    if detection_view is not None:
      rgb = detection_view.image
      if out_dir is not None:
        cv2.imwrite(
            str(out_dir / "detection_view_rgb.png"),
            cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR),
        )
      summary["detection_sensor"] = detection_view.sensor_name

      sam3 = routing_info.get("sam3")
      if sam3 is not None:
        vis_img, summary["sam3"] = _render_and_save_sam3(rgb, sam3, out_dir)

      pose_vis_img = _render_and_save_cad_overlay(
          rgb=rgb,
          camera_matrix=camera_matrix,
          selection_results=selection_results,
          pose_renderer=pose_renderer,
          obj_id=obj_id,
          out_dir=out_dir,
      )

    if out_dir is not None:
      if camera_matrix is not None:
        summary["camera_matrix"] = _to_list(camera_matrix)

      depth_m = routing_info.get("aligned_depth")
      if depth_m is not None:
        summary["depth"] = _save_depth(
            np.asarray(depth_m), out_dir, depth_source=depth_source
        )

      if selection_results is not None:
        summary["selection_results"] = _selection_summary(selection_results)

      if "error" in routing_info:
        summary["error"] = routing_info["error"]

      summary["estimates"] = [
          {
              "id": e["id"],
              "score": e["score"],
              "fp_score": e["fp_score"],
              "visibility_score": e["visibility_score"],
          }
          for e in estimates
      ]

      (out_dir / "summary.json").write_text(
          json.dumps(summary, indent=2, default=str)
      )
      logging.info("[DebugArtifacts] Wrote request artifacts to %s", out_dir)

    # publish visualizations
    try:
      if detection_image_pub is not None and vis_img is not None:
        vis_img_proto = client_image_utils.serialize_image_buffer(
            vis_img, pixel_type=image_buffer_pb2.PixelType.PIXEL_INTENSITY
        )
        detection_image_pub.Publish(vis_img_proto)
        logging.info(
            "Published detection visualization to topic %s",
            DETECTION_IMAGE_TOPIC,
        )

      if pose_image_pub is not None and pose_vis_img is not None:
        pose_vis_img_proto = client_image_utils.serialize_image_buffer(
            pose_vis_img, pixel_type=image_buffer_pb2.PixelType.PIXEL_INTENSITY
        )
        pose_image_pub.Publish(pose_vis_img_proto)
        logging.info(
            "Published pose estimation visualization to topic %s",
            POSE_IMAGE_TOPIC,
        )
    except Exception as e:
      logging.warning("Failed to publish visualization images: %s", e)

    return out_dir
  except Exception as e:
    logging.warning("[DebugArtifacts] Failed to write artifacts: %s", e)
    return None
