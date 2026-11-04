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

import os

from absl import logging

from intrinsic_inference.core import model_assets_manager_base
from intrinsic_inference.core import model_assets_managers_factory
from intrinsic_inference.core.v1 import ml_model_pb2


@model_assets_managers_factory.register("local")
class ModelAssetsManagerLocalRepo(
    model_assets_manager_base.ModelAssetsManagerBase
):
  """Model assets manager that uses a local directory as the repository.

  This implementation assumes that model assets (files and configurations)
  are already present in the local filesystem under the specified `repo_path`.
  It discovers models by scanning the directory structure:
  `{repo_path}/{model_name}/{model_version}/`.

  Note that create/update/delete functions are in this case no ops, as model
  files are not "duplicated" from a source into a destination working repo.

  Attributes:
    repo_path: Local filesystem path where model assets are stored.
  """

  def __init__(
      self,
      repo_path: str,
  ):
    """Initializes the ModelAssetsManagerRepo.

    Args:
      repo_path: Local filesystem path where model assets will be stored.
    """
    super().__init__(repo_path)
    # The previous listing, for logging new models and as a fallback if a scan
    # fails.
    self._last_listing: set[tuple[str, str]] = set()

  def _list_repo_content(self) -> set[tuple[str, str]]:
    """Scans the repo and returns its (model name, version) directories."""
    if not os.path.exists(self.repo_path):
      logging.warning(
          "Provided local repository %s does not exist.",
          self.repo_path,
      )
      self._last_listing = set()
      return self._last_listing

    new_content = set()
    try:
      with os.scandir(self.repo_path) as repo_path_iterator:
        for model_entry in repo_path_iterator:
          if not model_entry.is_dir():
            continue
          model_name = model_entry.name
          with os.scandir(model_entry.path) as model_path_iterator:
            for version_entry in model_path_iterator:
              if not version_entry.is_dir():
                continue
              model_version = version_entry.name
              new_content.add((model_name, model_version))
    except OSError as e:
      logging.error("Error scanning repository: %s", e)
      return self._last_listing

    # Log new models.
    for model_name, model_version in new_content:
      if (model_name, model_version) not in self._last_listing:
        logging.info(
            "Found new model %s, version %s", model_name, model_version
        )

    self._last_listing = new_content
    return new_content

  def _find_model_in_repo(
      self, model_name: str, model_version: str
  ) -> str | None:
    # Check if model exists in last repository scan.
    if (model_name, model_version) in self._last_listing:
      return f"{model_name}.{model_version}"

    # Fallback to directly checking the disk in case the model wasn't present
    # yet at the time of the last scan.
    model_path = os.path.join(self.repo_path, model_name, model_version)
    if not os.path.isdir(model_path):
      return None
    return f"{model_name}.{model_version}"

  def list_model_assets(
      self,
  ) -> dict[str, ml_model_pb2.MlModel]:
    """Lists available model assets.

    Returns:
      A dictionary mapping model identifier strings (e.g.,
      'name.version') to their corresponding MlModel proto instances.
    """
    model_assets: dict[str, ml_model_pb2.MlModel] = {}
    for model, version in self._list_repo_content():
      model_key = f"{model}.{version}"
      logging.debug("Found model %s, version %s", model, version)
      model_asset = ml_model_pb2.MlModel()
      model_asset.model_config.name = model
      model_asset.model_config.version = version
      model_assets[model_key] = model_asset

    return model_assets

  def _create_model_asset_impl(self, ml_model: ml_model_pb2.MlModel) -> None:
    """Creates model-related files required for inference.

    Args:
      model_asset: The MlModel proto containing configuration and data
        references to download and set up locally.
    """
    model_name = ml_model.model_config.name
    model_version = ml_model.model_config.version
    existing_model_version = self._find_model_in_repo(
        model_name=model_name, model_version=model_version
    )
    if not existing_model_version:
      raise ValueError(f"Model {model_name}.{model_version} does not exist.")
    # No Op.

  def _update_model_asset_impl(
      self, old_model: ml_model_pb2.MlModel, new_model: ml_model_pb2.MlModel
  ) -> None:
    """Updates model-related files for an existing model.

    The files are managed by the user, so this only checks that the new model
    exists. The old one might have been removed already.

    Args:
      old_model: The previously loaded MlModel proto.
      new_model: The new MlModel proto to update to.
    """
    del old_model  # Unused.
    model_name = new_model.model_config.name
    model_version = new_model.model_config.version
    existing_model_version = self._find_model_in_repo(
        model_name=model_name, model_version=model_version
    )
    if not existing_model_version:
      raise ValueError(
          f"New model {model_name}.{model_version} does not exist."
      )
    logging.info(f"Found model {model_name}.{model_version} to update.")
    # No Op.

  def _delete_model_asset_impl(self, ml_model: ml_model_pb2.MlModel) -> None:
    """Deletes model-related files of an existing model.

    No-op, since the files are managed by the user and usually removed already.

    Args:
      model_asset: The MlModel proto of the model to delete.
    """
    del ml_model  # Unused.
