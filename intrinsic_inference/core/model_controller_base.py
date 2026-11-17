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
import collections
from concurrent import futures
import dataclasses
import enum
import re
import threading
import time

from absl import logging

from intrinsic_inference.core import model_assets_manager_base
from intrinsic_inference.core import telemetry_base
from intrinsic_inference.core.v1 import ml_model_pb2

# Failed operations are retried after 5s, 10s, 20s, ... and at most 5min.
_INITIAL_BACKOFF_SECONDS = 5.0
_MAX_BACKOFF_SECONDS = 300.0


def _next_backoff(backoff: float) -> float:
  """Returns the backoff after another consecutive failure."""
  if backoff:
    return min(2 * backoff, _MAX_BACKOFF_SECONDS)
  return _INITIAL_BACKOFF_SECONDS


def _version_key(model: ml_model_pb2.MlModel) -> list[str | int]:
  """Returns a sort key that compares the numbers in versions numerically.

  For example, 1.9.0 < 1.10.0. Splitting on numbers alternates text and
  numbers, so numbers are only ever compared with numbers.

  In Intrinsic Core, only a single version of an asset is installed at a time.
  In other contexts, e.g. a local repo with several version directories, this
  makes the choice of the served version deterministic.

  Args:
    model: The model whose version to sort by.
  """
  parts = re.split(r"(\d+)", model.model_config.version)
  return [int(part) if i % 2 else part for i, part in enumerate(parts)]


@enum.unique
class _Op(enum.Enum):
  """An operation on a backend model."""

  LOAD = "load"
  UNLOAD = "unload"


@enum.unique
class ModelState(enum.Enum):
  """Lifecycle state of a model.

  LOADING and UNLOADING mean that an operation is pending or running. FAILED
  means that the last operation failed and is retried after a backoff.
  """

  LOADING = "LOADING"
  READY = "READY"
  UNLOADING = "UNLOADING"
  FAILED = "FAILED"


@dataclasses.dataclass(frozen=True)
class ModelAndState:
  """The state of a model.

  Attributes:
    state: The lifecycle state of the model.
    proto: The installed model, or the loaded or loading one if it was
      uninstalled.
    message: Why the last operation failed. Kept until a load succeeds, or
      until the installed model changes and no resources are left to clean up.
  """

  state: ModelState
  proto: ml_model_pb2.MlModel | None = None
  message: str | None = None


@dataclasses.dataclass(frozen=True)
class _Failure:
  """The latest failed operation of a model.

  Cleared by a successful load. If the installed model changes, it is cleared
  right away when nothing is loaded, and otherwise once the cleanup succeeded.

  Attributes:
    op: The operation that failed. Only this kind of operation waits for the
      backoff, so that e.g. the cleanup after a failed load runs right away.
    message: Why the operation failed.
    backoff: The delay before `op` is retried, doubled by each consecutive
      failure.
    retry_at: The time.monotonic() before which `op` isn't retried.
  """

  op: _Op
  message: str
  backoff: float = 0.0
  retry_at: float = 0.0

  @classmethod
  def after(
      cls,
      op: _Op,
      message: str,
      *,
      previous_backoff: float = 0.0,
      back_off: bool,
  ) -> "_Failure":
    """Returns the failure after `op` failed."""
    if not back_off:
      return cls(op, message)
    backoff = _next_backoff(previous_backoff)
    return cls(op, message, backoff, time.monotonic() + backoff)

  def delays(self, op: _Op, now: float) -> bool:
    """Whether `op` has to wait for the backoff at time `now`."""
    return op == self.op and now < self.retry_at

  def without_backoff(self) -> "_Failure":
    """Returns this failure with an immediate retry and a fresh backoff."""
    return dataclasses.replace(self, backoff=0.0, retry_at=0.0)


@dataclasses.dataclass(frozen=True)
class _Task:
  """A pending or running background operation on a model.

  Attributes:
    op: The operation being run.
    proto: The model being loaded or unloaded.
    future: The future of `op`.
  """

  op: _Op
  proto: ml_model_pb2.MlModel
  future: futures.Future[None]


@dataclasses.dataclass
class _ModelSlot:
  """The place of one backend model, which installed versions are loaded into.

  The backend and the model files identify a model by its model_config.name,
  so all versions of an asset share one slot. A slot holds the installed
  version (`desired`) and the one in the backend (`loaded`), and
  reconcile_models() moves the latter towards the former: a new version
  replaces the loaded one in place, and an uninstalled model empties the slot.
  Empty slots are removed.

  Only accessed while holding ModelControllerBase._lock.

  Attributes:
    desired: The installed model, or None if it was uninstalled.
    loaded: The model whose resources may exist in the backend and on disk.
    failure: The latest failed operation, if any. `loaded` is unloaded before
      anything else while it is set. Kept until a load succeeds, or until the
      installed model changed and nothing is loaded anymore.
    task: The pending or running operation, if any.
  """

  desired: ml_model_pb2.MlModel | None = None
  loaded: ml_model_pb2.MlModel | None = None
  failure: _Failure | None = None
  task: _Task | None = None

  @property
  def busy(self) -> bool:
    """Whether an operation is pending or running."""
    # A cancelled future never runs, so it can't reset `task`.
    return self.task is not None and not self.task.future.done()

  @property
  def serving(self) -> bool:
    """Whether `loaded` is ready to serve, including while an update loads."""
    return (
        self.loaded is not None
        and self.failure is None
        and self.desired is not None
        # E.g. a model that is reinstalled while it is being unloaded.
        and not (self.busy and self.task.op == _Op.UNLOAD)
    )

  @property
  def empty(self) -> bool:
    """Whether the slot is idle and has no installed and no loaded model."""
    return not self.busy and self.desired is None and self.loaded is None

  def next_op(self) -> _Op | None:
    """Returns the operation that moves the model towards `desired`, if any."""
    if self.loaded is not None and (self.failure or self.desired is None):
      # Clean up after a failure, or remove an uninstalled model.
      return _Op.UNLOAD
    if self.desired is not None and self.desired != self.loaded:
      # Load a new model, or update a loaded one in place.
      return _Op.LOAD
    return None

  def set_desired(self, desired: ml_model_pb2.MlModel | None) -> None:
    """Updates `desired`, resets backoff on change, and cancels moot ops."""
    if desired != self.desired:
      # A changed model gets a fresh attempt. A running operation keeps its own
      # copy of the old model and finishes first. The failure itself is kept
      # while loaded resources still need to be cleaned up.
      self.desired = desired
      if self.failure:
        self.failure = (
            self.failure.without_backoff() if self.loaded is not None else None
        )
    # Skip a pending operation that is no longer the next step, e.g. a load of a
    # version that was replaced or uninstalled. The next step starts on a later
    # reconciliation. Running operations can't be cancelled, and cancelled ones
    # never call finish_op().
    if self.task is not None and (
        self.task.op != self.next_op()
        or (self.task.op == _Op.LOAD and self.task.proto != self.desired)
    ):
      self.task.future.cancel()

  def ready_op(self, now: float) -> _Op | None:
    """Returns the next operation if idle and not backing off, else None."""
    if self.busy:
      return None
    op = self.next_op()
    # Only a retry of the failed operation waits for the backoff, e.g. the
    # cleanup after a failed load runs right away.
    if op is None or (self.failure and self.failure.delays(op, now)):
      return None
    return op

  def finish_op(
      self,
      op: _Op,
      desired: ml_model_pb2.MlModel | None,
      error: str | None,
  ) -> None:
    """Records the outcome of `op`."""
    self.task = None
    if error is not None:
      if op == _Op.LOAD:
        # A failed load may leave resources behind, which are unloaded next.
        self.loaded = desired
      # Consecutive failures, also of the cleanup, back off further unless the
      # model changed meanwhile.
      self.failure = _Failure.after(
          op,
          error,
          previous_backoff=self.failure.backoff if self.failure else 0.0,
          back_off=(self.desired == desired),
      )
    elif op == _Op.LOAD:
      self.loaded, self.failure = desired, None
    else:
      # Keeps the failure of a failed load, which is retried after its backoff,
      # unless the model changed meanwhile and the failure is moot.
      self.loaded = None
      if self.failure and self.failure.retry_at == 0.0:
        self.failure = None

  def to_model_and_state(self) -> ModelAndState:
    """Returns the public ModelAndState snapshot of this slot."""
    if self.failure and self.task is None:
      state = ModelState.FAILED
    else:
      # When idle (e.g. between steps, or after stop() cancelled a pending op),
      # the next operation determines the state.
      op = self.task.op if self.busy else self.next_op()
      state = {
          _Op.LOAD: ModelState.LOADING,
          _Op.UNLOAD: ModelState.UNLOADING,
          None: ModelState.READY,
      }[op]
    if self.desired is not None:
      proto = self.desired
    elif self.loaded is not None:
      proto = self.loaded
    else:
      proto = self.task.proto if self.task is not None else None
    return ModelAndState(
        state=state,
        proto=proto,
        message=self.failure.message if self.failure else None,
    )


class ModelControllerBase(abc.ABC):
  """Base class for controlling models.

  reconcile_models() converges the backend towards the installed models. It
  keeps one slot per backend model, i.e. per model_config.name, so versions of
  an asset are the same model and a new version updates the model in place.
  For each slot that is idle and not backing off, it starts at most one
  operation in a background thread pool:

  1. unload: if resources may exist and the last operation failed or the model
     was uninstalled.
  2. load: if the installed model differs from the loaded one.

  Failed operations are retried with exponential backoff. A failed load is
  cleaned up by an unload before it is retried, so a stale model never serves.

  There are two core methods that have to be implemented by subclasses:
    - **_load_model_impl**: Loads a model into the inference backend, or updates
      it in place if it is loaded already.
    - **_unload_model_impl**: Unloads all versions of a model from the
      inference backend. Must succeed if the model isn't loaded.

  Attributes:
    models: A mapping of model names to their serving MlModel proto instances,
      which are shared and must not be modified.
    model_states: A mapping of model names to their current ModelAndState,
      whose protos are shared and must not be modified.
  """

  def __init__(
      self,
      model_assets_manager: model_assets_manager_base.ModelAssetsManagerBase,
      max_concurrent_tasks: int = 4,
  ) -> None:
    self._model_assets_manager = model_assets_manager
    self._max_concurrent_tasks = max_concurrent_tasks
    # Serializes reconciliations, so that an older listing of the installed
    # models never overwrites a newer one. Also guards _ignored_versions.
    self._reconcile_lock = threading.Lock()
    # The versions that _latest_versions() ignored last time, by model name.
    self._ignored_versions: dict[str, list[str]] = {}
    # Guards the attributes below. Never held during I/O, so that reading
    # models and model_states never waits for the backend.
    self._lock = threading.Lock()
    self._slots: dict[str, _ModelSlot] = {}
    self._executor: futures.ThreadPoolExecutor | None = None
    self.start()

  def start(self) -> None:
    """Starts or restarts the background thread pool executor if stopped."""
    with self._lock:
      if self._executor is None:
        self._executor = futures.ThreadPoolExecutor(
            max_workers=self._max_concurrent_tasks
        )

  def stop(self, wait: bool = False) -> None:
    """Shuts down the background thread pool executor.

    Running operations finish. Pending operations are cancelled, and their
    models stay as they were until the next reconciliation.

    Args:
      wait: Whether to block until all running operations have finished.
    """
    with self._lock:
      executor, self._executor = self._executor, None
    if executor is not None:
      # Not under the lock: operations need it to record their outcome.
      executor.shutdown(wait=wait, cancel_futures=True)
    if wait:
      self.wait_for_idle()

  def wait_for_idle(self) -> None:
    """Blocks until no operation is pending or running."""
    while True:
      with self._lock:
        pending = [s.task.future for s in self._slots.values() if s.busy]
      if not pending:
        return
      futures.wait(pending)

  @property
  def models(self) -> dict[str, ml_model_pb2.MlModel]:
    """Returns a thread-safe copy of the serving models."""
    with self._lock:
      return {n: s.loaded for n, s in self._slots.items() if s.serving}

  def get_model(self, model_name: str) -> ml_model_pb2.MlModel | None:
    """Retrieves a serving model by its model_config.name."""
    with self._lock:
      slot = self._slots.get(model_name)
      return slot.loaded if slot is not None and slot.serving else None

  @property
  def model_states(self) -> dict[str, ModelAndState]:
    """Returns a thread-safe copy of all model states."""
    with self._lock:
      return {n: s.to_model_and_state() for n, s in self._slots.items()}

  @telemetry_base.trace_span("ModelController.load_model")
  def _load_model(
      self,
      loaded: ml_model_pb2.MlModel | None,
      desired: ml_model_pb2.MlModel,
  ) -> None:
    """Loads `desired` into the inference backend, replacing `loaded` if set.

    Called without holding a lock, since it does I/O.

    Args:
      loaded: The model whose resources may exist already, if any.
      desired: The model to load.
    """
    if loaded is None:
      self._model_assets_manager.create_model_asset(desired)
    elif (
        loaded.model_data != desired.model_data
        or loaded.model_config.version != desired.model_config.version
    ):
      # Only changed files are downloaded, and config-only changes skip this.
      self._model_assets_manager.update_model_asset(loaded, desired)
    self._load_model_impl(model_proto=desired)

  @abc.abstractmethod
  def _load_model_impl(self, model_proto: ml_model_pb2.MlModel) -> None:
    """Loads or updates a model in the inference backend. Must be overridden."""

  @telemetry_base.trace_span("ModelController.unload_model")
  def _unload_model(self, model_proto: ml_model_pb2.MlModel) -> None:
    """Unloads a model from the inference backend and deletes its files.

    Called without holding a lock, since it does I/O.

    Args:
      model_proto: The model to unload.
    """
    try:
      self._unload_model_impl(model_proto=model_proto)
    finally:
      # Files are deleted even if the backend failed, which is retried.
      self._model_assets_manager.delete_model_asset(model_proto)

  @abc.abstractmethod
  def _unload_model_impl(self, model_proto: ml_model_pb2.MlModel) -> None:
    """Unloads a model from the inference backend. Must be overridden."""

  @telemetry_base.trace_span("ModelController.reconcile_models")
  def reconcile_models(self) -> None:
    """Starts the operations that converge the models to the installed ones.

    The operations run in the background, see model_states for their progress.
    Does nothing while stopped.
    """
    # One reconciliation at a time, so that an older listing never overwrites a
    # newer one. The listing does I/O, so it happens outside of self._lock.
    with self._reconcile_lock:
      with self._lock:
        # Prevents gRPC calls and disk ops while being stopped.
        if self._executor is None:
          return
        # Snapshot the busy models before listing to prevent race conditions.
        was_busy = {n for n, s in self._slots.items() if s.busy}
      # The installed models, one version per model name.
      installed = self._model_assets_manager.list_model_assets()
      desired = self._latest_versions(installed)
      # A single timestamp, so that all slots are evaluated at the same time.
      now = time.monotonic()
      with self._lock:
        # Submitting under the lock ensures that stop() can't race with it.
        if self._executor is None:
          return
        # Known models, which might still be loaded, and installed ones.
        for name in sorted(self._slots.keys() | desired.keys()):
          slot = self._slots.setdefault(name, _ModelSlot())
          slot.set_desired(desired.get(name))
          if slot.empty:
            del self._slots[name]
            continue
          # Wait for a fresh listing after an operation finishes, so that e.g.
          # an asset manager cache isn't cleared after listing.
          op = None if name in was_busy else slot.ready_op(now)
          if op is None:
            continue
          # Runs in the background, _run_op() records the outcome. The next
          # step of this model happens on a later call.
          logging.info("Starting to %s model %s", op.value, name)
          future = self._executor.submit(
              self._run_op, name, op, slot.loaded, slot.desired
          )
          proto = slot.desired if op == _Op.LOAD else slot.loaded
          slot.task = _Task(op, proto, future)

  def _latest_versions(
      self, installed: dict[str, ml_model_pb2.MlModel]
  ) -> dict[str, ml_model_pb2.MlModel]:
    """Returns the latest installed version by model name.

    Versions of an asset share the backend model, so only one of them can be
    served, e.g. if a local repo has more than one version directory. The
    ignored versions are logged whenever they change. Requires
    self._reconcile_lock.

    Args:
      installed: The installed models by asset key.
    """
    versions_by_name = collections.defaultdict(list)
    for model in sorted(installed.values(), key=_version_key):
      versions_by_name[model.model_config.name].append(model)
    ignored = {
        name: [m.model_config.version for m in models[:-1]]
        for name, models in versions_by_name.items()
        if len(models) > 1
    }
    if ignored != self._ignored_versions:
      for name, versions in ignored.items():
        logging.warning(
            "Serving the latest version of model %s, ignoring versions %s",
            name,
            versions,
        )
      self._ignored_versions = ignored
    return {name: models[-1] for name, models in versions_by_name.items()}

  def _run_op(
      self,
      name: str,
      op: _Op,
      loaded: ml_model_pb2.MlModel | None,
      desired: ml_model_pb2.MlModel | None,
  ) -> None:
    """Runs `op` for model `name` and records its outcome.

    Runs in the thread pool without holding a lock, and only takes self._lock
    to record the outcome.

    Args:
      name: The model_config.name of the model.
      op: The operation to run.
      loaded: The model whose resources may exist, if any.
      desired: The installed model when `op` was submitted, if any.
    """
    try:
      if op == _Op.LOAD:
        self._load_model(loaded, desired)
      else:
        self._unload_model(loaded)
      error = None
      logging.info("Successfully %sed model %s", op.value, name)
    except Exception as e:  # pylint: disable=broad-except
      error = f"Failed to {op.value} model {name}: {e}"
      logging.exception("Failed to %s model %s", op.value, name)
    with self._lock:
      slot = self._slots[name]
      slot.finish_op(op, desired, error)
      if slot.empty:
        del self._slots[name]
