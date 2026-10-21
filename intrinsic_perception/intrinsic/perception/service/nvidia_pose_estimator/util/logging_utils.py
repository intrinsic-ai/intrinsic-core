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

"""Logging helpers for the pose estimation pipeline.

Provides `log_depth_statistics` for logging depth map summary metrics (mean,
median, and percentage of invalid/zero/NaN pixels) before and after camera
reprojection.
"""

from __future__ import annotations

import logging

import numpy as np


def log_depth_statistics(depth: np.ndarray, stage: str) -> None:
  """Logs mean, median, and invalid depth percentage for a depth map."""
  invalid_mask = np.isnan(depth) | (depth == 0) | (~np.isfinite(depth))
  total_pixels = depth.size
  invalid_pct = (
      float(np.count_nonzero(invalid_mask) / total_pixels * 100.0)
      if total_pixels > 0
      else 0.0
  )
  valid_depths = depth[~invalid_mask]
  if valid_depths.size > 0:
    mean_val = float(np.mean(valid_depths))
    median_val = float(np.median(valid_depths))
    logging.info(
        f"Depth statistics ({stage}): mean={mean_val:.4f},"
        f" median={median_val:.4f}, invalid={invalid_pct:.2f}%"
    )
  else:
    logging.info(
        f"Depth statistics ({stage}): mean=nan, median=nan,"
        f" invalid={invalid_pct:.2f}%"
    )
