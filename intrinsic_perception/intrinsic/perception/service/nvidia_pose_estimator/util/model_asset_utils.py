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

"""Downloads model weights from Data assets and manages versioned TensorRT engine caches.

Model weights (FoundationPose, FoundationStereo, and SAM 3) are installed as
Intrinsic Data assets carrying `intrinsic_proto.perception.v1.PerceptionModel`
payloads whose `data_references` map destination paths to blobs in CAS.

Two persistent host directories are managed, both namespaced by the same
16-character digest of the installed model assets:
  * `<models_root>/<namespace>/`: holds downloaded model weights so restarts
    only fetch missing files and weight updates get a clean directory.
  * `<engine_cache_root>/<namespace>/`: holds compiled TensorRT `.plan` files.
    `ModelPaths.preferred()` matches plans by filename prefix without
    re-checking the ONNX fingerprint, so per-version namespacing prevents stale
    plans from being reused when weights change while keeping rollbacks cached.

`<models_root>/<namespace>/engine_cache` is symlinked to the active engine cache
namespace because `ModelPaths` derives the cache path from `models_root`.
"""

from __future__ import annotations

import concurrent.futures
import dataclasses
import hashlib
import logging
import os
from pathlib import Path
import shutil

import grpc

from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.proto import nvidia_service_config_pb2
from intrinsic.assets import id_utils
from intrinsic.assets.data.proto.v1 import referenced_data_pb2
from intrinsic.assets.install import installed_assets_client
from intrinsic.assets.proto import id_pb2
from intrinsic.assets.proto import view_pb2
from intrinsic.assets.proto.v1 import resolved_dependency_pb2
from intrinsic.perception.proto.v1 import perception_model_pb2
from intrinsic.storage.content_addressable_storage.proto import cas_service_pb2_grpc
from intrinsic.storage.content_addressable_storage.python import client_helpers

_PERCEPTION_MODEL_URI = "data://intrinsic_proto.perception.v1.PerceptionModel"

_ENGINE_CACHE_DIRNAME = "engine_cache"

_MAX_PARALLEL_DOWNLOADS = 2

# Attempts per file before its download is reported as failed.
_DOWNLOAD_ATTEMPTS = 3

# Number of recent version namespaces retained for weights and plans.
_NAMESPACES_KEPT = 2


class ModelAssetError(RuntimeError):
  """Raised when required model weights cannot be made available."""


@dataclasses.dataclass(frozen=True)
class _ModelAssets:
  """Where the service should look for weights and compiled engines."""

  models_root: Path
  engine_cache_dir: Path


def _extract_asset_id(
    dependency: resolved_dependency_pb2.ResolvedDependency,
    field_name: str,
) -> id_pb2.Id:
  """Returns the asset id the platform resolved for a dependency field."""
  interface = dependency.interfaces.get(_PERCEPTION_MODEL_URI)
  if interface is None or not interface.HasField("data"):
    raise ModelAssetError(
        f"Config field '{field_name}' was not resolved to a"
        f" {_PERCEPTION_MODEL_URI} data asset. Interfaces present:"
        f" {sorted(dependency.interfaces)}"
    )
  return interface.data.id


def unpack_perception_model(
    installed_asset,
) -> perception_model_pb2.PerceptionModel:
  """Unpacks the PerceptionModel payload carried by an installed data asset."""
  perception_model = perception_model_pb2.PerceptionModel()
  if installed_asset.deployment_data.HasField("data"):
    if installed_asset.deployment_data.data.data.data.Unpack(perception_model):
      return perception_model
  raise ModelAssetError(
      "Installed asset carries no intrinsic_proto.perception.v1.PerceptionModel"
      " payload."
  )


def _dependency_fields(
    config: nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig,
) -> list[tuple[str, resolved_dependency_pb2.ResolvedDependency]]:
  """Returns the model dependency fields that the config actually sets."""
  names = ("foundationpose_model", "foundationstereo_model", "sam3_model")
  return [
      (name, getattr(config, name)) for name in names if config.HasField(name)
  ]


def _namespace_key(
    models: list[tuple[str, perception_model_pb2.PerceptionModel]],
) -> str:
  """Derives a 16-character hex digest for a set of installed model assets.

  Hashes both per-file CAS digests and `one_shot_base_model_config.version` (since
  small inlined blobs carry no CAS digest).
  """
  hasher = hashlib.sha256()
  for asset_id, model in sorted(models, key=lambda item: item[0]):
    hasher.update(asset_id.encode())
    hasher.update(model.one_shot_base_model_config.version.encode())
    for key in sorted(model.data_references):
      hasher.update(key.encode())
      hasher.update(model.data_references[key].digest.encode())
  return hasher.hexdigest()[:16]


def _prune_namespaces(root: Path, keep: str) -> None:
  """Drops all but the most recently used namespaces under `root`."""
  namespaces = [p for p in root.iterdir() if p.is_dir()]
  if len(namespaces) <= _NAMESPACES_KEPT:
    return
  namespaces.sort(key=lambda p: p.stat().st_mtime, reverse=True)
  for stale in namespaces[_NAMESPACES_KEPT:]:
    if stale.name == keep:
      continue
    logging.info("Pruning stale namespace %s", stale)
    shutil.rmtree(stale, ignore_errors=True)


def _download_reference(
    cas_stub: cas_service_pb2_grpc.ContentAddressableStorageServiceStub,
    reference: referenced_data_pb2.ReferencedData,
    destination: Path,
) -> None:
  """Materialises one data reference at `destination`, retrying on failure."""
  destination.parent.mkdir(parents=True, exist_ok=True)

  # Write to a .partial file and rename atomically so an interrupted download
  # cannot leave a truncated file that looks complete on the next boot.
  staging = destination.with_name(destination.name + ".partial")
  if reference.HasField("inlined"):
    staging.write_bytes(reference.inlined)
  else:
    for attempt in range(1, _DOWNLOAD_ATTEMPTS + 1):
      try:
        # Each attempt starts the file over.
        with (
            staging.open("wb") as sink,
            client_helpers.get_resumable_reader(
                reference.reference, cas_stub=cas_stub
            ) as reader,
        ):
          while chunk := reader.read1():
            sink.write(chunk)
        break
      except (grpc.RpcError, client_helpers.IncompleteDownloadError) as e:
        if attempt == _DOWNLOAD_ATTEMPTS:
          staging.unlink(missing_ok=True)
          raise
        logging.warning(
            "Downloading %s failed (attempt %d/%d), retrying: %s",
            destination.name,
            attempt,
            _DOWNLOAD_ATTEMPTS,
            e,
        )
  staging.rename(destination)


def ensure_models_downloaded(
    config: nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig,
    assets_client: installed_assets_client.InstalledAssetsClient,
    cas_stub: cas_service_pb2_grpc.ContentAddressableStorageServiceStub,
    models_root: Path,
    engine_cache_root: Path,
) -> _ModelAssets:
  """Downloads every configured model asset and prepares the engine cache.

  Args:
    config: The resolved service config, carrying the dependency fields.
    assets_client: Client for the installed asset registry.
    cas_stub: Stub for the cluster's content-addressable storage.
    models_root: Persistent directory holding per-weight-version namespaces
      of downloaded weights.
    engine_cache_root: Persistent directory holding per-weight-version
      namespaces of compiled TensorRT plans.

  Returns:
    The resolved model and engine cache locations.

  Raises:
    ModelAssetError: If a dependency is unresolved, an asset carries no
      PerceptionModel, or a blob cannot be fetched.
  """
  dependencies = _dependency_fields(config)
  if not dependencies:
    raise ModelAssetError(
        "No model data assets are configured. Set foundationpose_model,"
        " foundationstereo_model and sam3_model in the service config."
    )

  models: list[tuple[str, perception_model_pb2.PerceptionModel]] = []
  for field_name, dependency in dependencies:
    asset_id = _extract_asset_id(dependency, field_name)
    try:
      installed_asset = assets_client.get_installed_asset(
          asset_id, view=view_pb2.AssetViewType.ASSET_VIEW_TYPE_FULL
      )
    except grpc.RpcError as e:
      raise ModelAssetError(
          f"Could not read model asset '{id_utils.id_from_proto(asset_id)}'"
          f" for config field '{field_name}': {e}"
      ) from e
    models.append((
        id_utils.id_from_proto(asset_id),
        unpack_perception_model(installed_asset),
    ))

  namespace = _namespace_key(models)
  engine_cache_dir = engine_cache_root / namespace
  weights_dir = models_root / namespace
  for root, directory in (
      (engine_cache_root, engine_cache_dir),
      (models_root, weights_dir),
  ):
    directory.mkdir(parents=True, exist_ok=True)
    # Touch mtime so a rolled-back namespace is not pruned as stale.
    os.utime(directory, None)
    _prune_namespaces(root, keep=namespace)

  # ModelPaths derives its engine cache as `<root>/engine_cache`, so the
  # namespace has to be reached through a link rather than a path argument.
  cache_link = weights_dir / _ENGINE_CACHE_DIRNAME
  if cache_link.is_symlink() or cache_link.exists():
    if cache_link.is_symlink():
      cache_link.unlink()
    else:
      shutil.rmtree(cache_link)
  cache_link.symlink_to(engine_cache_dir.resolve(), target_is_directory=True)

  work: list[tuple[str, str, referenced_data_pb2.ReferencedData, Path]] = []
  for asset_id, model in models:
    for key, reference in model.data_references.items():
      destination = weights_dir / key
      # Plans resolve through the symlink into the namespace, so a plan shipped
      # in the asset is only unpacked for the weights it was built from.
      if not destination.exists():
        work.append((asset_id, key, reference, destination))

  if not work:
    logging.info("All model files already present in %s", weights_dir)
  else:
    logging.info(
        "Downloading %d model file(s) into %s (engine cache namespace %s)",
        len(work),
        weights_dir,
        namespace,
    )
    pool = concurrent.futures.ThreadPoolExecutor(
        max_workers=_MAX_PARALLEL_DOWNLOADS
    )
    try:
      futures = {
          pool.submit(_download_reference, cas_stub, reference, destination): (
              asset_id,
              key,
          )
          for asset_id, key, reference, destination in work
      }
      for future in concurrent.futures.as_completed(futures):
        asset_id, key = futures[future]
        try:
          future.result()
        except Exception as e:
          raise ModelAssetError(
              f"Failed to download '{key}' from model asset '{asset_id}': {e}"
          ) from e
    finally:
      # Drops the queued downloads on failure; one already running finishes.
      pool.shutdown(wait=False, cancel_futures=True)

  return _ModelAssets(
      models_root=weights_dir, engine_cache_dir=engine_cache_dir
  )


def default_models_root() -> Path:
  """Returns the persistent root holding per-version weight namespaces."""
  return Path(os.environ.get("NVIDIA_POSE_MODELS_ROOT", "/models/weights"))


def default_engine_cache_root() -> Path:
  """Returns the persistent root holding per-version engine cache namespaces."""
  return Path(
      os.environ.get("NVIDIA_POSE_ENGINE_CACHE_ROOT", "/models/engine_cache")
  )
