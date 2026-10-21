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

"""Data classes shared across the NVIDIA pose estimator service.

Defines `PipelinePrediction`, which holds a single object's estimated 4x4 pose
in the camera frame, its normalized confidence score, raw FoundationPose score,
visibility score, and 2D bounding box prior to gRPC response serialization.
"""

from __future__ import annotations

import dataclasses

import numpy as np


@dataclasses.dataclass(frozen=True, eq=False)
class PipelinePrediction:
  """Structured prediction for downstream gRPC serialization."""

  pose_matrix_4x4: np.ndarray
  score: float  # Bounded (0, 1), higher is better (published as PoseEstimate.score).
  fp_score: float  # Raw FoundationPose score, lower is better.
  visibility_score: float = 1.0
  unique_id: int = 0
  bbox_xyxy: np.ndarray = dataclasses.field(
      default_factory=lambda: np.zeros((4,), dtype=np.float32)
  )
