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

"""Model file locations and upstream CAD model cache keys for NVIDIA Pose Estimator.

This module provides:
  * Local filesystem fallbacks (`resolve_models_root`, `resolve_engine_cache_dir`)
    used when running outside a cluster (where `model_asset_utils` is skipped).
  * Canonical `(dataset, obj_id)` cache keys (`PIPELINE_DATASET_ROOT`,
    `PIPELINE_DATASET_KEY`, `pipeline_mesh_key`). The upstream perception
    pipeline keys its estimator and mesh caches by BOP `(dataset, obj_id)`
    identifiers; the service registers explicit CAD asset paths under these keys.
"""

from __future__ import annotations

import os
from pathlib import Path

_ENGINE_CACHE_DIRNAME = "engine_cache"

REFINE_MODEL_FILENAME = "refiner_net.onnx"
SCORE_MODEL_FILENAME = "score_net.onnx"


def resolve_models_root() -> Path:
  """Resolves the fallback local directory holding model weights.

  Returns:
    The directory given by `MODELS_DIR`, or `/tmp/models` when unset.
  """
  return Path(os.environ.get("MODELS_DIR", "/tmp/models"))


def resolve_engine_cache_dir(models_root: Path | None = None) -> Path:
  """Resolves the fallback local directory for compiled TensorRT plans.

  Args:
    models_root: Optional models root directory; defaults to
      `resolve_models_root()`.

  Returns:
    `<models_root>/engine_cache`, created if absent.
  """
  root = models_root if models_root is not None else resolve_models_root()
  cache_dir = root / _ENGINE_CACHE_DIRNAME
  cache_dir.mkdir(parents=True, exist_ok=True)
  return cache_dir


# Dataset root required by upstream pipeline constructors (unused when explicit
# CAD paths are registered).
PIPELINE_DATASET_ROOT = Path("/tmp")
# Dataset name under which the service registers every CAD model upstream.
PIPELINE_DATASET_KEY = "nvidia_pose_estimator"


def pipeline_mesh_key(obj_id: int) -> tuple[str, int]:
  """Returns the `(dataset, obj_id)` cache key used by the upstream estimator and renderer."""
  return (PIPELINE_DATASET_KEY, obj_id)
