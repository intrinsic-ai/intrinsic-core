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

"""Base class definition for model assets management."""

import abc
from collections.abc import Generator
import contextlib
import threading

from intrinsic_inference.core.v1 import ml_model_pb2


class ConcurrentModelOperationError(RuntimeError):
  """Raised if two operations for the same model overlap."""


class _ModelOperationGuard:
  """Detects overlapping operations for the same model.

  Doesn't serialize them: the caller decides the order of the operations, so
  an overlap is a bug in the caller and raises instead of waiting.
  """

  def __init__(self) -> None:
    self._lock = threading.Lock()
    self._in_progress: set[str] = set()

  @contextlib.contextmanager
  def exclusive(
      self, model: ml_model_pb2.MlModel
  ) -> Generator[None, None, None]:
    name = model.model_config.name
    with self._lock:
      if name in self._in_progress:
        raise ConcurrentModelOperationError(
            f"Another operation for model {name} is in progress."
        )
      self._in_progress.add(name)
    try:
      yield
    finally:
      with self._lock:
        self._in_progress.discard(name)


class ModelAssetsManagerBase(abc.ABC):
  """Base class for managing model assets.

  Handles the lifecycle of model files on disk, including listing, creating,
  updating, and deleting model assets. Subclasses holding long-lived background
  resources (e.g., thread pools, network channels, or file descriptors) should
  override `close()` to ensure proper resource cleanup upon shutdown.

  Supports the Python context manager protocol (`with manager:`), which
  automatically triggers `close()` upon context block exit.

  Thread safety: methods may be called concurrently for different models, but
  calls for the same model (model_config.name) must not overlap, and
  list_model_assets() must not be called concurrently with itself. The model
  controller should guarantee both. Overlapping calls for the same model raise
  a ConcurrentModelOperationError.

  Subclasses implement list_model_assets() and the _*_impl() methods.

  Attributes:
    repo_path: The root directory where model assets are stored.
  """

  def __init__(self, repo_path: str):
    """Initializes the ModelAssetsManagerBase.

    Args:
      repo_path: The root directory where model assets are stored.
    """
    self.repo_path = repo_path
    # Calls for the same model must not overlap, see the class docstring.
    self._guard = _ModelOperationGuard()

  @abc.abstractmethod
  def list_model_assets(
      self,
  ) -> dict[str, ml_model_pb2.MlModel]:
    """Lists available model assets.

    Returns:
      A dictionary mapping model identifier strings (e.g.,
      'package.name.version') to their corresponding MlModel proto instances.
    """

  def create_model_asset(self, ml_model: ml_model_pb2.MlModel) -> None:
    """Creates model-related files required for inference.

    Args:
      ml_model: The MlModel proto containing configuration and data
        references to download and set up locally.
    """
    with self._guard.exclusive(ml_model):
      self._create_model_asset_impl(ml_model)

  def update_model_asset(
      self, old_model: ml_model_pb2.MlModel, new_model: ml_model_pb2.MlModel
  ) -> None:
    """Updates model-related files for an existing model.

    This method should perform differential updates, deleting files that are
    no longer referenced and downloading new or changed files.

    Args:
      old_model: The previously loaded MlModel proto.
      new_model: The new MlModel proto to update to.
    """
    # Both are versions of the same model, i.e. share model_config.name.
    with self._guard.exclusive(new_model):
      self._update_model_asset_impl(old_model, new_model)

  def delete_model_asset(self, ml_model: ml_model_pb2.MlModel) -> None:
    """Deletes model-related files of an existing model.

    Args:
      ml_model: The MlModel proto of the model to delete.
    """
    with self._guard.exclusive(ml_model):
      self._delete_model_asset_impl(ml_model)

  @abc.abstractmethod
  def _create_model_asset_impl(self, ml_model: ml_model_pb2.MlModel) -> None:
    """Implements create_model_asset(). Must be overridden."""

  @abc.abstractmethod
  def _update_model_asset_impl(
      self, old_model: ml_model_pb2.MlModel, new_model: ml_model_pb2.MlModel
  ) -> None:
    """Implements update_model_asset(). Must be overridden."""

  @abc.abstractmethod
  def _delete_model_asset_impl(self, ml_model: ml_model_pb2.MlModel) -> None:
    """Implements delete_model_asset(). Must be overridden."""

  def close(self) -> None:
    """Frees resources associated with the manager (e.g., thread pools, gRPC channels).

    Subclasses maintaining background threads, persistent thread pools, or network
    clients should override this method to perform cleanup operations. The
    default implementation is a no-op.
    """
    pass

  def __enter__(self):
    """Enters the context manager block, returning the manager instance."""
    return self

  def __exit__(self, exc_type, exc_val, exc_tb):
    """Exits the context manager block, automatically invoking close()."""
    self.close()
