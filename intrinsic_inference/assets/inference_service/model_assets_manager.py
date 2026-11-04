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

"""Classes and helper functions for model assets."""

from concurrent import futures
import os
import shutil
import tempfile
from typing import Any

from absl import logging
import grpc

from intrinsic_inference.assets.inference_service.v1 import ml_model_asset_pb2
from intrinsic_inference.core import model_assets_manager_base
from intrinsic_inference.core import model_assets_managers_factory
from intrinsic_inference.core.v1 import ml_model_pb2
from intrinsic.assets.data.proto.v1 import data_assets_pb2
from intrinsic.assets.data.proto.v1 import data_assets_pb2_grpc
from intrinsic.assets.data.proto.v1 import referenced_data_pb2
from intrinsic.assets.install import installed_assets_client
from intrinsic.assets.proto import asset_type_pb2
from intrinsic.assets.proto import id_pb2
from intrinsic.assets.proto import view_pb2
from intrinsic.storage.content_addressable_storage.proto import cas_service_pb2_grpc
from intrinsic.storage.content_addressable_storage.python import client_helpers


def id_version_as_string(
    proto_with_id: id_pb2.IdVersion | ml_model_pb2.MlModelConfig,
) -> str:
  """Returns a formatted string of the IdVersion or MlModelConfig proto object.

  Returns:
    String format: `package.name.version` (or `name.version` for MlModelConfig).
    Trailing dots are automatically omitted if fields are missing or empty.
  """
  match proto_with_id:
    case id_pb2.IdVersion(id=id_meta, version=version):
      parts = [id_meta.package, id_meta.name, version]
    case ml_model_pb2.MlModelConfig(name=name, version=version):
      parts = [name, version]
    case _:
      raise TypeError(f"Unsupported proto type: {type(proto_with_id)}")

  return ".".join(filter(None, parts))


def _write_in_chunks_from_cas(
    src_path: str,
    dst_path: str,
    cas_stub: cas_service_pb2_grpc.ContentAddressableStorageServiceStub,
) -> None:
  os.makedirs(os.path.dirname(dst_path), exist_ok=True)
  if os.path.exists(dst_path):
    logging.warning(
        "Destination path already exists: %s, overwriting it.", dst_path
    )

  tmp_file_path = None
  try:
    # Download with atomic rename.
    with tempfile.NamedTemporaryFile(
        dir=os.path.dirname(dst_path), delete=False, mode="wb"
    ) as tmp_file:
      tmp_file_path = tmp_file.name
      for chunk in client_helpers.get_iter(cas_stub, src_path):
        tmp_file.write(chunk)
    os.replace(tmp_file.name, dst_path)
  except grpc.RpcError as e:
    # Clean up possible partially written file.
    if tmp_file_path:
      try:
        os.remove(tmp_file_path)
      except OSError:
        pass
    raise ValueError(f"Failed to get model file from CAS for {src_path}") from e


@model_assets_managers_factory.register("data_assets")
class ModelAssetsManager(model_assets_manager_base.ModelAssetsManagerBase):

  def __init__(
      self,
      repo_path: str,
      data_assets_stub: data_assets_pb2_grpc.DataAssetsStub,
      cas_stub: cas_service_pb2_grpc.ContentAddressableStorageServiceStub,
      installed_assets_client: installed_assets_client.InstalledAssetsClient,
      max_workers: int = 4,
  ) -> None:
    """Initializes the ModelAssetsManager.

    Args:
      repo_path: Local filesystem path where model assets will be stored.
      data_assets_stub: gRPC stub for the DataAssets service, used to stream
        inlined asset data.
      cas_stub: gRPC stub for the ContentAddressableStorage service, used to
        stream CAS-referenced asset data.
      installed_assets_client: Client for the InstalledAssets service, used to
        list installed assets.
      max_workers: Maximum number of worker threads for downloading model data
        references concurrently.
    """
    super().__init__(repo_path)
    self._data_assets_stub = data_assets_stub
    self._installed_assets_client = installed_assets_client
    self._cas_stub = cas_stub
    self._executor = futures.ThreadPoolExecutor(
        max_workers=max_workers, thread_name_prefix="model_assets_dl"
    )
    # Internal cache to store the ReferencedData map for each model name,
    # enabling metadata preservation without exposing MlModelAsset externally.
    # The format is {pkg.name.version : {path_strings: referenced_data_objs}}
    # No lock: dict ops are atomic and only one op per model uses its keys.
    self._referenced_data_cache: dict[
        str, dict[str, referenced_data_pb2.ReferencedData]
    ] = {}

  def close(self) -> None:
    """Shuts down the internal ThreadPoolExecutor."""
    self._executor.shutdown(wait=True)

  def _get_referenced_data_entry(self, model_proto: ml_model_pb2.MlModel):
    cache_key = id_version_as_string(model_proto.model_config)
    return self._referenced_data_cache.get(cache_key, {})

  def _get_ml_model_from_data_asset(
      self,
      asset_id_version: id_pb2.IdVersion,
  ) -> ml_model_pb2.MlModel:
    asset_id = asset_id_version.id
    version = asset_id_version.version
    asset_obj = self._data_assets_stub.GetDataAsset(
        data_assets_pb2.GetDataAssetRequest(id=asset_id)
    )
    logging.debug(
        "Getting data asset '%s.%s'...",
        asset_id.package,
        asset_id.name,
    )
    model_name = f"{asset_id.package}.{asset_id.name}"
    ml_model_asset = ml_model_asset_pb2.MlModelAsset()
    if not asset_obj.data.Unpack(ml_model_asset):
      error_msg = (
          "Data asset '%s.%s' does not contain a MlModelAsset proto and cannot "
          "be unpacked!"
      )
      logging.error(error_msg, asset_id.package, asset_id.name)
      raise RuntimeError(error_msg % (asset_id.package, asset_id.name))

    if not ml_model_asset.HasField("ml_model"):
      raise RuntimeError("MlModelAsset does not contain an ml_model field.")
    ml_model = ml_model_asset.ml_model
    # Enforce consistency between asset ID/version and model config.
    ml_model.model_config.name = model_name
    ml_model.model_config.version = version

    # Store the referenced_data map in our cache, keyed by model_name_and_version
    # to avoid collisions when multiple versions of the same model are listed.
    cache_key = id_version_as_string(asset_id_version)
    self._referenced_data_cache[cache_key] = dict(
        ml_model_asset.referenced_data
    )

    return ml_model

  def _load_single_data_reference(
      self,
      model_name: str,
      key: str,
      ref_data: referenced_data_pb2.ReferencedData,
  ) -> None:
    # TODO(lhecht): Sanitize output_file_path to prevent path traversal attacks.
    output_file_path = os.path.join(self.repo_path, model_name, key)
    os.makedirs(os.path.dirname(output_file_path), exist_ok=True)

    if ref_data.HasField("inlined"):
      logging.info("Writing inlined data for key: '%s'...", key)
      with open(output_file_path, "wb") as out_file:
        out_file.write(ref_data.inlined)
      return

    if ref_data.HasField("reference"):
      if ref_data.reference.startswith("intcas://"):
        # CAS references.
        logging.info("Using CAS to stream data for key: '%s'...", key)
        _write_in_chunks_from_cas(
            ref_data.reference, output_file_path, self._cas_stub
        )
      else:
        # Other references, stream via DataAssets.
        logging.info("Using DataAssets to stream data for key: '%s'...", key)

        try:
          stream_request = data_assets_pb2.StreamReferencedDataRequest(
              data=ref_data
          )
          response_stream = self._data_assets_stub.StreamReferencedData(
              stream_request
          )

          with open(output_file_path, "wb") as out_file:
            for stream_response in response_stream:
              # Write each chunk of data from the response to the file
              out_file.write(stream_response.chunk)

          logging.info("Successfully saved to: %s", output_file_path)
        except grpc.RpcError as e:
          logging.error(
              "Failed to stream and save data from DataAssets for '%s': %s",
              key,
              e,
          )
          raise

  def list_model_assets(
      self,
  ) -> dict[str, ml_model_pb2.MlModel]:
    """Queries the InstalledAssets service for installed MlModel data assets.

    Returns:
      A dictionary mapping `package.name.version` strings to their corresponding
      unpacked `MlModel` proto instances.
    """
    installed_models = self._installed_assets_client.list_all_installed_assets(
        asset_types=[asset_type_pb2.AssetType.ASSET_TYPE_DATA],
        provides=[ml_model_asset_pb2.MlModelAsset.DESCRIPTOR.full_name],
        view=view_pb2.AssetViewType.ASSET_VIEW_TYPE_VERSIONS,
    )
    asset_id_versions = {
        id_version_as_string(m.metadata.id_version): m.metadata.id_version
        for m in installed_models
    }

    ml_models: dict[str, ml_model_pb2.MlModel] = {}
    for model_name_and_version, asset_id_version in asset_id_versions.items():
      ml_model = self._get_ml_model_from_data_asset(asset_id_version)
      ml_models[model_name_and_version] = ml_model

    return ml_models

  def _download_data_items(
      self,
      ml_model: ml_model_pb2.MlModel,
      items: list[tuple[str, Any]],
  ) -> None:
    """Downloads a list of data items for a model concurrently."""
    if not items:
      return

    model_name = ml_model.model_config.name
    referenced_data_map = self._get_referenced_data_entry(ml_model)

    def _download_item(item: tuple[str, Any]) -> None:
      key, model_data_val = item
      ref_data = referenced_data_map.get(key)
      if not ref_data:
        logging.warning(
            "Could not find matching ReferencedData in cache for key"
            " '%s' in model '%s'. Constructing a skeleton one.",
            key,
            model_name,
        )
        ref_data = referenced_data_pb2.ReferencedData()
        if model_data_val.HasField("reference"):
          ref_data.reference = model_data_val.reference

      self._load_single_data_reference(model_name, key, ref_data)

    fs = [self._executor.submit(_download_item, item) for item in items]
    for f in futures.as_completed(fs):
      f.result()

  def _create_model_asset_impl(self, ml_model: ml_model_pb2.MlModel) -> None:
    """Creates the model asset on the local filesystem.

    Downloads and streams all referenced data files for the model from either
    CAS or the DataAssets service, saving them to the local repository path.

    Args:
      ml_model: The `MlModel` proto instance defining the model and its data
        references.
    """
    items_to_download = list(ml_model.model_data.items())
    self._download_data_items(ml_model, items_to_download)

  def _remove_empty_parents(self, path: str, stop_dir: str) -> None:
    """Removes empty parent directories of a deleted file up to `stop_dir`.

    This is a helper function intended to remove empty directories left behind
    after deleting stale file references during a model update.
    """
    path = os.path.normpath(path)
    stop_dir = os.path.normpath(stop_dir)
    while path != stop_dir and path.startswith(stop_dir + os.sep):
      try:
        os.rmdir(path)
      except OSError:
        break
      path = os.path.dirname(path)

  def _update_model_asset_impl(
      self, old_model: ml_model_pb2.MlModel, new_model: ml_model_pb2.MlModel
  ) -> None:
    """Differentially updates the model asset on the local filesystem.

    Compares the data references of the old and new model versions. It deletes
    local files that are no longer referenced in the new version, and downloads
    new or modified files. Unchanged files are kept as is.

    Args:
      old_model: The currently installed `MlModel` proto instance.
      new_model: The desired new `MlModel` proto instance.
    """
    old_map = old_model.model_data
    new_map = new_model.model_data
    model_name = new_model.model_config.name
    model_dir = os.path.join(self.repo_path, model_name)

    # Delete files that are no longer referenced.
    for key in old_map:
      if key in new_map:
        continue
      output_file_path = os.path.join(model_dir, key)
      logging.info("Deleting unreferenced file: %s", output_file_path)
      try:
        os.remove(output_file_path)
      except FileNotFoundError:
        logging.warning(
            "File '%s', which is marked for deletion was not found,"
            " skipping...",
            output_file_path,
        )
      except (IsADirectoryError, PermissionError) as e:
        raise ValueError(f"{output_file_path} is not a file.") from e
      self._remove_empty_parents(os.path.dirname(output_file_path), model_dir)

    # Identify new or changed files. The old referenced data is read before its
    # cache entry is dropped.
    old_ref_map = self._get_referenced_data_entry(old_model)
    new_ref_map = self._get_referenced_data_entry(new_model)
    old_cache_key = id_version_as_string(old_model.model_config)
    new_cache_key = id_version_as_string(new_model.model_config)
    if old_cache_key != new_cache_key:
      self._referenced_data_cache.pop(old_cache_key, None)
    items_to_download = []
    for key, model_data_val in new_map.items():
      # The MlModelData reference is usually the same file:// path for all
      # versions, so the ReferencedData is compared as well. Its CAS reference,
      # inlined bytes or digest change with the content.
      if (
          key not in old_map
          or old_map[key] != model_data_val
          or old_ref_map.get(key) != new_ref_map.get(key)
          # Without referenced data, only a CAS reference proves that the
          # content is unchanged.
          or (
              key not in new_ref_map
              and not model_data_val.reference.startswith("intcas://")
          )
      ):
        items_to_download.append((key, model_data_val))
      else:
        logging.info("File for key '%s' is unchanged, skipping download.", key)

    self._download_data_items(new_model, items_to_download)

  def _delete_model_asset_impl(self, ml_model: ml_model_pb2.MlModel) -> None:
    """Deletes the model asset from the local filesystem.

    Removes the entire directory associated with the model from the local
    repository path.

    Args:
      ml_model: The `MlModel` proto instance to delete.
    """
    model_name = ml_model.model_config.name
    cache_key = id_version_as_string(ml_model.model_config)
    self._referenced_data_cache.pop(cache_key, None)
    # Delete model files after successful unload.
    model_dir = os.path.join(self.repo_path, model_name)
    if os.path.exists(model_dir):
      logging.info("Deleting model files at %s", model_dir)
      shutil.rmtree(model_dir)
