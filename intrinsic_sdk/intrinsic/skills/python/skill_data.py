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

"""Thread-safe, in-memory caching for Skill data across lifecycle methods."""

from __future__ import annotations

from collections import OrderedDict
import functools
import logging
import threading
from typing import Any
from typing import Callable
from typing import Optional
from typing import TypeVar

_T = TypeVar("_T")

# Sentinel to missing values in the internal LRU cache.
_MISSING = object()


@functools.cache
def get_skill_data() -> SkillData:
  """Returns the process-wide default SkillData instance."""
  return SkillData()


class SkillData:
  """Provides thread-safe in-memory caching across Skill lifecycle methods.

  SkillData caches values keyed by (context_id, key) across Skill lifecycle
  methods with automatic LRU eviction bounded by the maximum number of active
  context_ids.

  Args:
    max_contexts: Maximum number of concurrent action execution contexts
      expected to be kept in memory (i.e., the maximum number of expected
      parallel calls of the Skill by the Executive). Defaults to 32.
  """

  def __init__(self, max_contexts: int = 32) -> None:
    if max_contexts < 0:
      raise ValueError(
          f"max_contexts must be non-negative, got {max_contexts}."
      )
    self._cache = _LRUCache(max_contexts)

  def get(
      self,
      context_id: str,
      key: str,
      validate_fn: Optional[Callable[[_T], bool]] = None,
  ) -> Optional[_T]:
    """Returns the cached value for (context_id, key), or None if not found.

    If `context_id` is empty, logs a warning and returns None without querying
    the cache.

    If `validate_fn` is provided and a cached value exists:
    - Returns cached value if `validate_fn` returns True.
    - Evicts the cached key and returns None if `validate_fn` returns False.
    - Evicts the cached key and propagates any exception immediately if
      `validate_fn` raises one.

    Args:
      context_id: Identifier for the action execution context.
      key: Identifier for the cached item within the context.
      validate_fn: Optional callback to validate the cached value.

    Returns:
      The cached value, or None if missing, empty context_id, or invalidated.
    """
    if not context_id:
      logging.warning(
          "SkillData: context_id is empty for key '%s'; cache lookup skipped.",
          key,
      )
      return None

    cached = self._get_and_validate(context_id, key, validate_fn)
    return None if cached is _MISSING else cached

  def get_or_compute(
      self,
      context_id: str,
      key: str,
      compute_fn: Callable[[], _T],
      validate_fn: Optional[Callable[[_T], bool]] = None,
  ) -> _T:
    """Returns the cached value for (context_id, key) or computes and stores it.

    If `context_id` is empty, logs a warning and computes via `compute_fn`
    returning the result without caching.

    If `validate_fn` is provided and a cached value exists:
    - Returns cached value if `validate_fn` returns True.
    - Evicts the cached key, recomputes via `compute_fn`, updates cache, and
      returns fresh value if `validate_fn` returns False.
    - Evicts the cached key and propagates any exception immediately if
      `validate_fn` raises an exception.

    Automatically evicts the least recently used context_id (and all its
    associated keys) if the context capacity is reached.

    Args:
      context_id: Identifier for the action execution context.
      key: Identifier for the cached item within the context.
      compute_fn: Callback to compute the value if missing or invalidated.
      validate_fn: Optional callback to validate the cached value.

    Returns:
      The cached or computed value.
    """
    if compute_fn is None:
      raise ValueError("compute_fn must not be None.")

    if not context_id:
      logging.warning(
          "SkillData: context_id is empty for key '%s'; computing without"
          " caching.",
          key,
      )
      return compute_fn()

    cached = self._get_and_validate(context_id, key, validate_fn)
    if cached is not _MISSING:
      return cached

    fresh_value = compute_fn()
    self._cache.put(context_id, key, fresh_value)

    return fresh_value

  def delete(self, context_id: str) -> bool:
    """Deletes all entries for a specific context_id.

    If `context_id` is empty, logs a warning and returns False.

    Args:
      context_id: Identifier for the action execution context to delete.

    Returns:
      True if the context was deleted, False if not found or empty context_id.
    """
    if not context_id:
      logging.warning("SkillData: context_id is empty; delete skipped.")
      return False
    return self._cache.erase(context_id)

  def _get_and_validate(
      self,
      context_id: str,
      key: str,
      validate_fn: Optional[Callable[[_T], bool]] = None,
  ) -> Any:
    """Returns the cached value, or _MISSING if not found or invalidated."""
    cached = self._cache.get(context_id, key)
    if cached is _MISSING or validate_fn is None:
      return cached

    is_valid = False
    try:
      is_valid = bool(validate_fn(cached))
    finally:
      if not is_valid:
        self._cache.erase_key(context_id, key)

    return cached if is_valid else _MISSING


class _LRUCache:
  """Thread-safe raw LRU cache storing data by context_id and key."""

  def __init__(self, max_contexts: int) -> None:
    self._max_contexts = max_contexts
    self._lock = threading.Lock()
    self._contexts: OrderedDict[str, dict[str, Any]] = OrderedDict()

  def get(self, context_id: str, key: str) -> Any:
    """Returns the cached value or _MISSING if not found."""
    if not context_id:
      return _MISSING

    with self._lock:
      context = self._contexts.get(context_id)
      if context is None:
        return _MISSING
      self._contexts.move_to_end(context_id)
      return context.get(key, _MISSING)

  def put(self, context_id: str, key: str, value: Any) -> None:
    """Stores a value for (context_id, key), updating LRU order."""
    if self._max_contexts == 0 or not context_id:
      return

    with self._lock:
      context = self._contexts.get(context_id)
      if context is not None:
        context[key] = value
        self._contexts.move_to_end(context_id)
        return

      if len(self._contexts) >= self._max_contexts:
        self._contexts.popitem(last=False)

      self._contexts[context_id] = {key: value}

  def erase(self, context_id: str) -> bool:
    """Erases a context_id and all its keys. Returns True if found."""
    if not context_id:
      return False

    with self._lock:
      return self._contexts.pop(context_id, None) is not None

  def erase_key(self, context_id: str, key: str) -> bool:
    """Erases a single key under context_id. Returns True if found."""
    if not context_id:
      return False

    with self._lock:
      context = self._contexts.get(context_id)
      if context is None or key not in context:
        return False
      del context[key]
      if not context:
        del self._contexts[context_id]
      return True
