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

"""Geometry helpers for depth maps and point clouds.

This library contains utils functions to
- Convert between point clouds, depth maps and 3D images.
- Project points and apply transformations to point clouds.
- Reproject depth maps between cameras.
"""

from typing import Tuple

import numpy as np

from intrinsic.math.python import pose3


def points_to_depthmap(
    point_cloud_camera: np.ndarray,
    intrinsics_matrix: np.ndarray,
    image_size: Tuple[int, int],
    round_pixels: bool = False,
) -> np.ndarray:
  """Convert point cloud to depthmap with zero unnecessary intermediate arrays.

  Args:
    point_cloud_camera: Nx3 array of points in the camera frame.
    intrinsics_matrix: 3x3 camera intrinsics matrix.
    image_size: (cols, rows) of the output depthmap.
    round_pixels: If True, round projected pixel coordinates to the nearest
      integer instead of truncating them. Rounding avoids off-by-one pixel
      shifts when points were unprojected from integer pixel coordinates (e.g.
      via `depthmap_to_image3d`) and floating point error puts them just below
      the pixel boundary.

  Returns:
    (rows, cols) depthmap, NaN where no point projects.
  """
  cols, rows = image_size[:2]
  dtype = point_cloud_camera.dtype

  # Extract coordinates directly
  x = point_cloud_camera[:, 0]
  y = point_cloud_camera[:, 1]
  z = point_cloud_camera[:, 2]

  # Intrinsics
  fx, cx = intrinsics_matrix[0, 0], intrinsics_matrix[0, 2]
  fy, cy = intrinsics_matrix[1, 1], intrinsics_matrix[1, 2]

  # Project directly without full Nx3 matrix multiplications
  u = (x * fx) / z + cx
  v = (y * fy) / z + cy
  if round_pixels:
    u = np.rint(u)
    v = np.rint(v)

  # Boundary filtering: filter FIRST before argsort to minimize sorting cost
  valid_mask = (u >= 0) & (u < cols) & (v >= 0) & (v < rows) & (z > 0)

  valid_u = u[valid_mask].astype(np.int32)
  valid_v = v[valid_mask].astype(np.int32)
  valid_z = z[valid_mask]

  # Sort furthest-to-closest so nearest points overwrite farther points
  order = np.argsort(valid_z)[::-1]

  # Initialize directly with NaN
  depthmap = np.full((rows, cols), np.nan, dtype=dtype)
  depthmap[valid_v[order], valid_u[order]] = valid_z[order]

  return depthmap


def depthmap_to_image3d(
    depthmap: np.ndarray,
    intrinsics_matrix: np.ndarray,
    image_size: Tuple[int, int],
) -> np.ndarray:
  """Compute 3D point image with preallocated (H, W, 3) buffer."""
  cols, rows = image_size[:2]
  fx, cx = intrinsics_matrix[0, 0], intrinsics_matrix[0, 2]
  fy, cy = intrinsics_matrix[1, 1], intrinsics_matrix[1, 2]

  # Preallocate output (H, W, 3) once
  out = np.empty((rows, cols, 3), dtype=depthmap.dtype)

  # Use 1D grid vectors and broadcast across dimensions
  u = np.arange(cols, dtype=depthmap.dtype)  # (W,)
  v = np.arange(rows, dtype=depthmap.dtype)  # (H,)

  # Assign directly into target slices (avoids np.concatenate)
  out[..., 2] = depthmap  # Z
  out[..., 0] = depthmap * (u[None, :] - cx) / fx  # X
  out[..., 1] = depthmap * (v[:, None] - cy) / fy  # Y

  return out


def point_cloud_to_image3d(
    point_cloud: np.ndarray,
    intrinsics_matrix: np.ndarray,
    image_size: Tuple[int, int],
) -> np.ndarray:
  depthmap = points_to_depthmap(point_cloud, intrinsics_matrix, image_size)
  return depthmap_to_image3d(depthmap, intrinsics_matrix, image_size)


def project_points(points: np.ndarray, camera_matrix: np.ndarray) -> np.ndarray:
  """Project 3D points to 2D image coordinates with minimal temporary buffers."""
  # Compute u = fx*X + cx*Z, v = fy*Y + cy*Z, w = Z
  # intrinsics: [[fx, 0, cx], [0, fy, cy], [0, 0, 1]]
  fx, cx = camera_matrix[0, 0], camera_matrix[0, 2]
  fy, cy = camera_matrix[1, 1], camera_matrix[1, 2]

  z = points[:, 2]
  u = (points[:, 0] * fx) / z + cx
  v = (points[:, 1] * fy) / z + cy

  return np.column_stack((u, v))


def _matrix_transpose(matrix: np.ndarray) -> np.ndarray:
  """Transposes last two dimensions of the input."""
  return np.swapaxes(matrix, -1, -2)


def _to_homogeneous(coords: np.ndarray) -> np.ndarray:
  """Converts the coordinates to homogeneous format (i.e. XYZ to XYZ1).

  Args:
    coords: np.array or torch.Tensor: [N1, ..., Nm, 3] or [N1, ..., Nm, 2] array
      of points to be converted.

  Returns:
      np.array or torch.Tensor: [N1, ..., Nm, 4] or [N1, ..., Nm, 3] array of
      points in homogeneous format.
  """
  return np.c_[coords, np.ones(coords.shape[:-1] + (1,), dtype=coords.dtype)]


def _from_homogeneous(coords: np.ndarray) -> np.ndarray:
  """Converts the coordinates from homogeneous format (i.e. XYZ1 to XYZ).

  Args:
    coords: np.array or torch.Tensor: Nx4 or Nx3 array of points to be
      converted.

  Returns:
      np.array or torch.Tensor: Nx3 or Nx2 array of points in homogeneous
      format.
  """
  return coords[..., :-1] / coords[..., -1:]


def transform_points_in_place(
    point_cloud: np.ndarray, transformation_matrix: np.ndarray
) -> None:
  """Apply transformation to a point cloud in-place.

  Args:
    point_cloud: Nx3 numpy array (modified in place).
    transformation_matrix: 4x4 matrix transformation.

  Returns:
    The modified Nx3 point_cloud array.
  """
  rotation = np.ascontiguousarray(
      transformation_matrix[:3, :3].T, dtype=point_cloud.dtype
  )
  translation = transformation_matrix[:3, 3].astype(
      point_cloud.dtype, copy=False
  )
  np.matmul(point_cloud, rotation, out=point_cloud)
  point_cloud += translation


def transform_points(
    point_cloud: np.ndarray, transformation_matrix: np.ndarray
) -> np.ndarray:
  """Apply transformation to a point cloud represented by a Nx3 array.

  Args:
    point_cloud: Nx3 numpy array.
    transformation_matrix: 4x4 matrix transformation to transform point cloud.

  Returns:
    Nx3 transformed array.
  """
  points3d_transformed = _from_homogeneous(
      _to_homogeneous(point_cloud) @ _matrix_transpose(transformation_matrix)
  )

  return points3d_transformed


def reproject_depthmap_to_target_camera(
    source_depth: np.ndarray,
    source_intrinsics: np.ndarray,
    source_cam_t_world: pose3.Pose3,
    target_intrinsics: np.ndarray,
    target_cam_t_world: pose3.Pose3,
    target_image_size: Tuple[int, int],
) -> np.ndarray:
  """Reprojects a depthmap from a source camera into a target camera.

  The source depthmap is unprojected to a point cloud, transformed into the
  target camera frame, and z-buffered into the target image (nearest point
  wins). Projected pixel coordinates are rounded to avoid off-by-one shifts.

  Args:
    source_depth: (H, W) source depthmap. Non-finite or non-positive values are
      treated as invalid.
    source_intrinsics: 3x3 source camera intrinsics matrix.
    source_cam_t_world: Pose of the world in the source camera frame.
    target_intrinsics: 3x3 target camera intrinsics matrix.
    target_cam_t_world: Pose of the world in the target camera frame.
    target_image_size: (cols, rows) of the target depthmap.

  Returns:
    float32 (rows, cols) depthmap in the target camera, 0 where no point
    projects. May return `source_depth` unchanged when source and target
    cameras are identical.

  Raises:
    ValueError: If any of the intrinsics or poses is None.
  """
  missing = [
      name
      for name, value in (
          ("source_intrinsics", source_intrinsics),
          ("source_cam_t_world", source_cam_t_world),
          ("target_intrinsics", target_intrinsics),
          ("target_cam_t_world", target_cam_t_world),
      )
      if value is None
  ]
  if missing:
    raise ValueError(
        "reproject_depthmap_to_target_camera requires non-None"
        f" {', '.join(missing)}."
    )

  target_w, target_h = target_image_size
  target_t_source = target_cam_t_world.matrix4x4() @ np.linalg.inv(
      source_cam_t_world.matrix4x4()
  )

  # Fast path: source and target cameras are (approximately) identical.
  if (
      source_depth.shape[:2] == (target_h, target_w)
      and np.allclose(source_intrinsics, target_intrinsics)
      and np.allclose(target_t_source, np.eye(4))
  ):
    return source_depth

  src_h, src_w = source_depth.shape[:2]
  image3d = depthmap_to_image3d(source_depth, source_intrinsics, (src_w, src_h))
  valid = np.isfinite(image3d[..., 2]) & (image3d[..., 2] > 0)
  if not np.any(valid):
    return np.zeros((target_h, target_w), dtype=np.float32)

  points_t = transform_points(image3d[valid], target_t_source)
  depthmap = points_to_depthmap(
      points_t, target_intrinsics, (target_w, target_h), round_pixels=True
  )
  return np.nan_to_num(depthmap, nan=0.0).astype(np.float32)
