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

"""Pose score conversion utilities for NVIDIA Pose Estimator.

Maps raw FoundationPose hypothesis scores (`fp_score`, unbounded and
lower-is-better) onto the `(0, 1)` higher-is-better range required by
`intrinsic_proto.perception.v1.PoseEstimate.score` via a decreasing logistic
function.
"""

from __future__ import annotations

import math

import numpy as np

# Temperature for squashing the unbounded fp_score into (0, 1); chosen so typical
# fp_score values spread across ~0.09..0.98 without saturating float64.
FP_SCORE_TEMPERATURE: float = 10.0
FP_MAX_SCORE: float = 1000.0


def bounded_score(fp_score: float) -> float:
  """Maps FoundationPose's lower-is-better score onto (0, 1) where higher is better.

  Uses a strictly decreasing logistic so the published score order always agrees
  with ascending `fp_score`. The output is a bounded rank score rather than a
  calibrated probability.

  Args:
    fp_score: Raw FoundationPose score, where lower is better.

  Returns:
    A value in (0, 1), where higher is better.
  """
  fp_score = float(np.clip(fp_score, -FP_MAX_SCORE, FP_MAX_SCORE))
  return 1.0 / (1.0 + math.exp(fp_score / FP_SCORE_TEMPERATURE))
