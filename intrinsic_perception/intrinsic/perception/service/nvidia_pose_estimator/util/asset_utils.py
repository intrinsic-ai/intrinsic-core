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

"""Target asset resolution utilities for NVIDIA Pose Estimator.

Extracts per-target SAM 3 text prompts, confidence thresholds
(`IocPoseEstimatorParams`), and CAD meshes (`.ply`, `.obj`, `.glb`, `.gltf`,
`.stl`) from installed `PerceptionModel` Data assets in CAS, applying any
`target_t_mesh` transform, exporting normalized `.ply` files to local storage,
and caching results by asset ID.
"""

from __future__ import annotations

import io
import logging
import os
from pathlib import Path

import grpc
import numpy as np
import trimesh

from intrinsic_perception.intrinsic.perception.service.ioc_train_service.proto import ioc_pose_estimator_params_pb2
from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.util import model_asset_utils
from intrinsic.assets import id_utils
from intrinsic.assets.install import installed_assets_client
from intrinsic.assets.proto import id_pb2
from intrinsic.assets.proto import installed_assets_pb2
from intrinsic.assets.proto import view_pb2
from intrinsic.perception.proto.v1 import perception_model_pb2
from intrinsic.storage.content_addressable_storage.proto import cas_service_pb2_grpc
from intrinsic.storage.content_addressable_storage.python import client_helpers

_CAD_EXTENSIONS = (".glb", ".gltf", ".obj", ".ply", ".stl")
_DEFAULT_CAD_OUTPUT_DIR = Path("/tmp/cad_models")


def extract_prompt_from_installed_asset(
    installed_asset: installed_assets_pb2.InstalledAsset,
) -> tuple[str | None, float | None]:
  """Extracts (text_prompt, confidence_threshold) from an InstalledAsset proto.

  Reads IocPoseEstimatorParams from pose_estimation_config.inference_params.
  This is shared across pose estimators whose assets may carry different (or
  no) inference params, so a missing or mismatched payload is not an error.

  Args:
    installed_asset: The installed data asset holding a PerceptionModel.

  Returns:
    The configured text prompt and confidence threshold, each None when the
    asset does not set them or does not carry IocPoseEstimatorParams.
  """
  try:
    perception_model = model_asset_utils.unpack_perception_model(
        installed_asset
    )
  except model_asset_utils.ModelAssetError:
    return None, None
  if not perception_model.HasField("pose_estimation_config"):
    return None, None

  pose_cfg = perception_model.pose_estimation_config
  if not pose_cfg.HasField("inference_params"):
    return None, None

  params = ioc_pose_estimator_params_pb2.IocPoseEstimatorParams()
  if not pose_cfg.inference_params.Unpack(params):
    return None, None

  text_prompt = (
      params.text_prompt if params.HasField("text_prompt") else None
  ) or None
  conf_thresh = (
      round(float(params.confidence_threshold), 6)
      if params.HasField("confidence_threshold")
      else None
  )
  return text_prompt, conf_thresh


def resolve_prompt(  # pylint: disable=dangerous-default-value
    asset_id: id_pb2.Id | None = None,
    assets_client: installed_assets_client.InstalledAssetsClient | None = None,
    prompt_cache: dict[str, tuple[str | None, float | None]] = {},
) -> tuple[str | None, float | None]:
  """Resolves (text_prompt, confidence_threshold) from the installed asset.

  Args:
    asset_id: Id of the pose estimator data asset.
    assets_client: Client used to fetch the installed asset.
    prompt_cache: Results cached by asset id string.

  Returns:
    The text prompt and confidence threshold, each None when unavailable.

  Raises:
    id_utils.IdValidationError: If asset_id is set but malformed.
  """
  if asset_id is None or not asset_id.name:
    return None, None
  cache_key = id_utils.id_from_proto(asset_id)

  if cache_key in prompt_cache:
    return prompt_cache[cache_key]

  # The client is unset outside of a solution (e.g. local runs), in which case
  # the caller falls back to its configured defaults.
  if assets_client is None:
    return None, None

  try:
    installed_asset = assets_client.get_installed_asset(
        asset_id, view=view_pb2.AssetViewType.ASSET_VIEW_TYPE_FULL
    )
  except grpc.RpcError as e:
    logging.debug("Could not retrieve asset '%s': %s", cache_key, e)
    return None, None

  result = extract_prompt_from_installed_asset(installed_asset)
  logging.info(
      "Found text_prompt '%s' and confidence threshold %s from data asset '%s'",
      result[0],
      result[1],
      cache_key,
  )
  prompt_cache[cache_key] = result
  return result


def _extract_target_transform(
    perception_model: perception_model_pb2.PerceptionModel,
) -> np.ndarray | None:
  """Extracts optional 4x4 target_t_mesh transform from PoseEstimationConfig."""
  if not perception_model.HasField("pose_estimation_config"):
    return None
  for t in perception_model.pose_estimation_config.targets:
    if t.HasField("mesh") and t.mesh.HasField("target_t_mesh"):
      ttm = t.mesh.target_t_mesh
      mat = np.eye(4, dtype=np.float64)
      if len(ttm.linear.values) == 9:
        mat[:3, :3] = np.array(ttm.linear.values, dtype=np.float64).reshape(
            (3, 3), order="F"
        )
      mat[0, 3] = ttm.translation.x
      mat[1, 3] = ttm.translation.y
      mat[2, 3] = ttm.translation.z
      return mat
  return None


def get_local_cad_path_from_asset(  # pylint: disable=dangerous-default-value
    asset_id: id_pb2.Id | None,
    assets_client: installed_assets_client.InstalledAssetsClient | None,
    cas_stub: cas_service_pb2_grpc.ContentAddressableStorageServiceStub | None,
    cad_cache: dict[str, Path] = {},
    output_dir: Path | None = None,
) -> Path | None:
  """Returns a local path to the asset's CAD model, downloading it if needed.

  Args:
    asset_id: Id of the pose estimator data asset holding the CAD model.
    assets_client: Client used to fetch the installed asset.
    cas_stub: CAS stub used to download the CAD file.
    cad_cache: Local CAD paths cached by asset id string.
    output_dir: Directory the converted .ply is written to.

  Returns:
    Path to a local CAD model, or None if none could be found.

  Raises:
    id_utils.IdValidationError: If asset_id is set but malformed.
  """
  if asset_id is None or not asset_id.name:
    return None
  cache_key = id_utils.id_from_proto(asset_id)

  out_dir = output_dir or _DEFAULT_CAD_OUTPUT_DIR
  out_dir.mkdir(parents=True, exist_ok=True)
  if cache_key in cad_cache and cad_cache[cache_key].is_file():
    return cad_cache[cache_key]

  if assets_client is not None and cas_stub is not None:
    try:
      installed_asset = assets_client.get_installed_asset(
          asset_id, view=view_pb2.AssetViewType.ASSET_VIEW_TYPE_FULL
      )
      perception_model = model_asset_utils.unpack_perception_model(
          installed_asset
      )

      cad_uri, cad_path_str = None, None
      for path, ref_data in perception_model.data_references.items():
        if path.lower().endswith(_CAD_EXTENSIONS):
          cad_uri, cad_path_str = ref_data.reference, path
          break

      if cad_uri:
        logging.info(
            "Downloading CAD model from CAS (%s) for asset '%s'",
            cad_uri,
            cache_key,
        )
        raw_cad_bytes = client_helpers.get(cas_stub, cad_uri)
        ext = (
            os.path.splitext(cad_path_str)[1].lower().lstrip(".")
            if cad_path_str
            else "ply"
        )
        loaded_mesh = trimesh.load(
            io.BytesIO(raw_cad_bytes), file_type=ext, force="mesh"
        )
        if isinstance(loaded_mesh, trimesh.Scene):
          loaded_mesh = loaded_mesh.dump(concatenate=True)
        target_transform = _extract_target_transform(perception_model)
        if target_transform is not None:
          loaded_mesh.apply_transform(target_transform)

        local_cad_path = out_dir / f"{cache_key}.ply"
        staging_path = local_cad_path.with_name(
            f"{local_cad_path.name}.partial"
        )
        try:
          loaded_mesh.export(str(staging_path), file_type="ply")
          staging_path.rename(local_cad_path)
        except Exception:
          staging_path.unlink(missing_ok=True)
          raise
        cad_cache[cache_key] = local_cad_path
        return local_cad_path
    except Exception as e:  # pylint: disable=broad-except
      logging.debug("Could not load CAD model from asset %s: %s", cache_key, e)

  local_candidates = [
      out_dir / f"{cache_key}.ply",
      out_dir / f"{cache_key}.obj",
  ]
  for cand_path in local_candidates:
    if cand_path.is_file():
      cad_cache[cache_key] = cand_path.resolve()
      return cad_cache[cache_key]

  return None
