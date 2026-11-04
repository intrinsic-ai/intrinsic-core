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

"""Base model controller handling backend-agnostic model loading."""

import abc
from concurrent import futures
import dataclasses
import enum
import threading

from absl import logging

from intrinsic_inference.core import model_assets_manager_base
from intrinsic_inference.core import telemetry_base
from intrinsic_inference.core.v1 import ml_model_pb2


@enum.unique
class ModelState(enum.Enum):
  LOADING = "LOADING"
  READY = "READY"
  RELOADING = "RELOADING"
  UNLOADING = "UNLOADING"
  FAILED = "FAILED"


@dataclasses.dataclass(frozen=True)
class ModelAndState:
  state: ModelState
  proto: ml_model_pb2.MlModel | None = None
  message: str | None = None


@dataclasses.dataclass(frozen=True)
class ReconciliationDiff:
  """Container for the difference between current and desired model states."""

  to_load: set[str]
  to_unload: set[str]
  to_reload: set[str]


class ModelControllerBase(abc.ABC):
  """Base class for controlling models.

  Handles state, thread safety, and backend-agnostic asset loading.
  There are two core methods that have to be implemented by subclasses:
    - **_load_model_impl**: Loads a model into the inference backend, or updates
      it in place if it is loaded already.
    - **_unload_model_impl**: Unloads all versions of a model from the
      inference backend. Must succeed if the model isn't loaded.

  Attributes:
    models: A mapping of model names to their loaded MlModel proto instances.
    model_states: A mapping of model names to their current ModelStatus.
  """

  def __init__(
      self,
      model_assets_manager: model_assets_manager_base.ModelAssetsManagerBase,
      max_concurrent_tasks: int = 4,
  ) -> None:
    self._model_assets_manager = model_assets_manager
    self._max_concurrent_tasks = max_concurrent_tasks
    self._models: dict[str, ml_model_pb2.MlModel] = {}
    self._model_states: dict[str, ModelAndState] = {}
    self._lock = threading.Lock()
    self._reconcile_lock = threading.Lock()
    self._executor: futures.ThreadPoolExecutor | None = None
    self._in_flight_futures: set[futures.Future[None]] = set()
    self.start()

  def start(self) -> None:
    """Starts or restarts the background thread pool executor if stopped."""
    with self._lock:
      if self._executor is None:
        self._executor = futures.ThreadPoolExecutor(
            max_workers=self._max_concurrent_tasks
        )

  def _submit_task(self, fn, *args) -> None:
    with self._lock:
      if self._executor is None:
        self._executor = futures.ThreadPoolExecutor(
            max_workers=self._max_concurrent_tasks
        )
      future = self._executor.submit(fn, *args)
      self._in_flight_futures.add(future)
    future.add_done_callback(self._on_future_done)

  def _on_future_done(self, future: futures.Future[None]) -> None:
    with self._lock:
      self._in_flight_futures.discard(future)

  def stop(self, wait: bool = False) -> None:
    """Shuts down the background thread pool executor."""
    with self._lock:
      executor_to_stop = self._executor
      self._executor = None

    if executor_to_stop is not None:
      executor_to_stop.shutdown(wait=wait)

  def wait_for_idle(self) -> None:
    """Blocks until all background reconciliation tasks have finished."""
    while True:
      with self._lock:
        pending = list(self._in_flight_futures)
      if not pending:
        break
      futures.wait(pending)

  @property
  def models(self) -> dict[str, ml_model_pb2.MlModel]:
    """Returns a thread-safe copy of the currently loaded models."""
    with self._lock:
      return dict(self._models)

  def get_model(self, model_name: str) -> ml_model_pb2.MlModel | None:
    """Retrieves a loaded model by key or model name without copying the models dict."""
    with self._lock:
      if model_name in self._models:
        return self._models[model_name]
      candidates = []
      for k, model_proto in self._models.items():
        if (
            model_proto.model_config.name == model_name
            or k.startswith(f"{model_name}.")
            or k.endswith(f".{model_name}")
        ):
          candidates.append((k, model_proto))
      if candidates:
        if len(candidates) > 1:
          logging.warning(
              "Model name did match more than one model, using %s",
              candidates[0][0],
          )
        return candidates[0][1]
      return None

  @property
  def model_states(self) -> dict[str, ModelAndState]:
    """Returns a thread-safe copy of all model statuses."""
    with self._lock:
      states = {
          k: ModelAndState(state=ModelState.READY, proto=v)
          for k, v in self._models.items()
      }
      states.update(self._model_states)
      return states

  @telemetry_base.trace_span("ModelController.load_model")
  def _load_model(
      self,
      loaded: ml_model_pb2.MlModel | None,
      desired: ml_model_pb2.MlModel,
  ) -> None:
    """Loads `desired` into the inference backend, replacing `loaded` if set."""
    if loaded is None:
      self._model_assets_manager.create_model_asset(desired)
    elif loaded.model_data != desired.model_data:
      # Only changed files are downloaded, and config-only changes skip this.
      self._model_assets_manager.update_model_asset(loaded, desired)
    self._load_model_impl(model_proto=desired)

  @abc.abstractmethod
  def _load_model_impl(self, model_proto: ml_model_pb2.MlModel) -> None:
    """Loads or updates a model in the inference backend. Must be overridden."""

  @telemetry_base.trace_span("ModelController.unload_model")
  def _unload_model(self, model_proto: ml_model_pb2.MlModel) -> None:
    """Unloads a model from the inference backend.."""
    try:
      self._unload_model_impl(model_proto=model_proto)
    except Exception as e:  # pylint: disable=broad-except
      logging.warning(
          "Failed to unload model %s from backend: %s. Proceeding with file"
          " cleanup.",
          model_proto.model_config.name,
          e,
      )
    # Always try to delete model files even if backend unload failed.
    try:
      self._model_assets_manager.delete_model_asset(model_proto)
    except Exception as e:  # pylint: disable=broad-except
      logging.warning(
          "Failed to delete model asset %s: %s",
          model_proto.model_config.name,
          e,
      )

  @abc.abstractmethod
  def _unload_model_impl(self, model_proto: ml_model_pb2.MlModel) -> None:
    """Unloads a model from the inference backend. Must be overridden."""

  @telemetry_base.trace_span("ModelController.reload_model")
  def _reload_model(self, model_proto: ml_model_pb2.MlModel) -> None:
    """Updates a loaded model in place."""
    model_name = model_proto.model_config.name

    with self._lock:
      # Start with key lookup by name or name.version before falling
      # back to prefix matching.
      full_key = (
          f"{model_proto.model_config.name}.{model_proto.model_config.version}"
          if model_proto.model_config.version
          else model_proto.model_config.name
      )
      old_model = self._models.get(model_name) or self._models.get(full_key)
      if not old_model:
        for key, model in self._models.items():
          if key.startswith(model_name + "."):
            old_model = model
            break

    if not old_model:
      logging.warning("No model found to reload. Loading as new model.")
    self._load_model(old_model, model_proto)

  def _get_reconciliation_diff(
      self,
      current_models: dict[str, ml_model_pb2.MlModel],
      new_models: dict[str, ml_model_pb2.MlModel],
      current_states: dict[str, ModelAndState] | None = None,
  ) -> ReconciliationDiff:
    """Calculates the difference between current and desired model states."""
    current_keys = set(current_models.keys())
    new_keys = set(new_models.keys())

    models_to_unload = current_keys - new_keys
    raw_models_to_load = new_keys - current_keys

    # Do not re-attempt loading models that previously failed loading until
    # state is reset or asset configuration changes.
    models_to_load = set()
    for candidate_model_key in raw_models_to_load:
      status = (
          current_states.get(candidate_model_key) if current_states else None
      )
      if status is None or status.state != ModelState.FAILED:
        models_to_load.add(candidate_model_key)
      elif (
          status.state == ModelState.FAILED
          and status.proto != new_models[candidate_model_key]
      ):
        logging.info(
            "Retrying previously failed model '%s' due to updated"
            " configuration.",
            candidate_model_key,
        )
        models_to_load.add(candidate_model_key)

    models_to_reload = set()
    common_keys = current_keys & new_keys
    for key in common_keys:
      if current_models[key] != new_models[key]:
        models_to_reload.add(key)

    return ReconciliationDiff(
        to_load=models_to_load,
        to_unload=models_to_unload,
        to_reload=models_to_reload,
    )

  def _async_unload_task(
      self, model_name: str, model_proto: ml_model_pb2.MlModel
  ) -> None:
    try:
      self._unload_model(model_proto)
      with self._lock:
        self._models.pop(model_name, None)
        self._model_states.pop(model_name, None)
      logging.info("Successfully unloaded model %s", model_name)
    except Exception as e:  # pylint: disable=broad-except
      message = f"Failed to unload model {model_name}: {e}"
      logging.exception("Failed to unload model %s", model_name)
      with self._lock:
        self._model_states[model_name] = ModelAndState(
            state=ModelState.FAILED,
            proto=model_proto,
            message=message,
        )

  def _async_load_task(
      self, model_name: str, model_proto: ml_model_pb2.MlModel
  ) -> None:
    try:
      self._load_model(None, model_proto)
      with self._lock:
        self._models[model_name] = model_proto
        self._model_states[model_name] = ModelAndState(
            state=ModelState.READY, proto=model_proto
        )
      logging.info("Successfully loaded model %s", model_name)
    except Exception as e:  # pylint: disable=broad-except
      message = f"Failed to load model {model_name}: {e}"
      logging.exception("Failed to load model %s", model_name)
      with self._lock:
        self._model_states[model_name] = ModelAndState(
            state=ModelState.FAILED,
            proto=model_proto,
            message=message,
        )

  def _async_reload_task(
      self, model_name: str, model_proto: ml_model_pb2.MlModel
  ) -> None:
    try:
      self._reload_model(model_proto)
      with self._lock:
        self._models[model_name] = model_proto
        self._model_states[model_name] = ModelAndState(
            state=ModelState.READY, proto=model_proto
        )
      logging.info("Successfully reloaded model %s", model_name)
    except Exception as e:  # pylint: disable=broad-except
      message = f"Failed to reload model {model_name}: {e}"
      logging.exception("Failed to reload model %s", model_name)
      with self._lock:
        self._model_states[model_name] = ModelAndState(
            state=ModelState.FAILED,
            proto=model_proto,
            message=message,
        )

  def _execute_reconciliation(
      self,
      diff: ReconciliationDiff,
      current_models: dict[str, ml_model_pb2.MlModel],
      new_models: dict[str, ml_model_pb2.MlModel],
  ) -> None:
    """Executes the loading, unloading, and reloading of models asynchronously."""
    with self._lock:
      unloads_to_submit = []
      for m in diff.to_unload:
        status = self._model_states.get(m)
        current_state = (
            status.state
            if status
            else (ModelState.READY if m in current_models else None)
        )
        if current_state == ModelState.READY:
          unloads_to_submit.append(m)
          self._model_states[m] = ModelAndState(
              state=ModelState.UNLOADING, proto=current_models.get(m)
          )

      loads_to_submit = []
      for m in diff.to_load:
        status = self._model_states.get(m)
        current_state = (
            status.state
            if status
            else (ModelState.READY if m in current_models else None)
        )
        if current_state is None or current_state == ModelState.FAILED:
          loads_to_submit.append(m)
          self._model_states[m] = ModelAndState(
              state=ModelState.LOADING, proto=new_models[m]
          )

      reloads_to_submit = []
      for m in diff.to_reload:
        status = self._model_states.get(m)
        current_state = (
            status.state
            if status
            else (ModelState.READY if m in current_models else None)
        )
        if current_state == ModelState.READY:
          reloads_to_submit.append(m)
          self._model_states[m] = ModelAndState(
              state=ModelState.RELOADING, proto=new_models[m]
          )

    for model_name in unloads_to_submit:
      self._submit_task(
          self._async_unload_task, model_name, current_models[model_name]
      )

    for model_name in loads_to_submit:
      self._submit_task(
          self._async_load_task, model_name, new_models[model_name]
      )

    for model_name in reloads_to_submit:
      self._submit_task(
          self._async_reload_task, model_name, new_models[model_name]
      )

  @telemetry_base.trace_span("ModelController.reconcile_models")
  def reconcile_models(self) -> None:
    """Reconciles the difference in current and desired loaded model state.

    Tries to efficiently load, unload and reload models to achieve the desired
    state of models loaded in the inference server backend.
    """
    with self._reconcile_lock:
      new_models: dict[str, ml_model_pb2.MlModel] = (
          self._model_assets_manager.list_model_assets()
      )
      with self._lock:
        current_models = dict(self._models)
        current_states = dict(self._model_states)

      diff = self._get_reconciliation_diff(
          current_models, new_models, current_states
      )

      if not diff.to_load and not diff.to_unload and not diff.to_reload:
        return

      logging.info("Change in installed model assets detected.")

      logging.info(
          "Reconciliation results:\n\tLoading: %s\n\tUnloading:"
          " %s\n\tReloading: %s",
          diff.to_load,
          diff.to_unload,
          diff.to_reload,
      )
      try:
        self._execute_reconciliation(
            diff=diff,
            current_models=current_models,
            new_models=new_models,
        )
      except Exception as e:
        # TODO(lhecht): Add rollback logic.
        logging.exception("Error while updating model configuration: %s", e)
        raise
