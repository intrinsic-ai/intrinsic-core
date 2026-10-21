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

"""Provides behavior trees from a solution."""

import dataclasses
from typing import cast
from typing import Iterable
from typing import Iterator
import warnings

from google.protobuf import duration_pb2
import grpc

from intrinsic.assets import id_utils
from intrinsic.assets.install import installed_assets_client
from intrinsic.assets.processes.proto import process_asset_pb2
from intrinsic.assets.proto import asset_type_pb2
from intrinsic.assets.proto import installed_assets_pb2
from intrinsic.assets.proto import view_pb2
from intrinsic.executive.proto import behavior_tree_pb2
from intrinsic.frontend.solution_service.proto import solution_service_pb2
from intrinsic.frontend.solution_service.proto import solution_service_pb2_grpc
from intrinsic.solutions import behavior_tree
from intrinsic.solutions import providers
from intrinsic.solutions.internal import referenced_assets
from intrinsic.util.grpc import error_handling

_SOLUTION_SERVICE_MAX_PAGE_SIZE = 50
_WAIT_OPERATION_TIMEOUT = duration_pb2.Duration(seconds=10)


@dataclasses.dataclass
class _Process:
  asset_proto: process_asset_pb2.ProcessAsset

  def create_behavior_tree(self) -> behavior_tree.BehaviorTree:
    if not self.asset_proto.HasField('metadata'):
      return behavior_tree.BehaviorTree.create_from_proto(
          self.asset_proto.behavior_tree
      )
    else:
      return behavior_tree.BehaviorTree.create_from_proto(self.asset_proto)


class Processes(providers.ProcessProvider):
  """Provides the processes (= behavior trees) from a solution."""

  _solution: solution_service_pb2_grpc.SolutionServiceStub
  _installed_assets: installed_assets_client.InstalledAssetsClient

  def __init__(
      self,
      solution: solution_service_pb2_grpc.SolutionServiceStub,
      installed_assets: installed_assets_client.InstalledAssetsClient,
  ):
    self._solution = solution
    self._installed_assets = installed_assets

  def keys(self) -> Iterable[str]:
    return self._list_all_processes(keys_only=True).keys()

  def items(self) -> Iterable[tuple[str, behavior_tree.BehaviorTree]]:
    for id, process in self._list_all_processes(keys_only=False).items():  # pylint: disable=redefined-builtin
      if process is not None:
        yield id, process.create_behavior_tree()

  def values(self) -> Iterable[behavior_tree.BehaviorTree]:
    for process in self._list_all_processes(keys_only=False).values():
      if process is not None:
        yield process.create_behavior_tree()

  def __iter__(self) -> Iterator[str]:
    return self._list_all_processes(keys_only=True).keys().__iter__()

  def __contains__(self, identifier: str) -> bool:
    return self._get_process(identifier) is not None

  def __getitem__(self, identifier: str) -> behavior_tree.BehaviorTree:
    process = self._get_process(identifier)
    if process is None:
      raise KeyError(f'Process "{identifier}" not found')
    return process.create_behavior_tree()

  def __setitem__(self, identifier: str, value: behavior_tree.BehaviorTree):
    warnings.warn(
        '__setitem__ is deprecated. Please use Solution.processes.save()'
        ' instead.',
        DeprecationWarning,
        stacklevel=2,
    )
    if not isinstance(value, behavior_tree.BehaviorTree):
      raise TypeError(f'Expected a BehaviorTree, got {type(value)}.')
    if value.asset_metadata_proto is not None:
      raise ValueError(
          'BehaviorTree represents a Process asset and must be saved using'
          ' Solution.processes.save().'
      )

    # Update the legacy process
    value.name = identifier
    self._save_legacy_process(value)

  def save(self, bt: behavior_tree.BehaviorTree):
    if bt.asset_metadata_proto is None:
      self._save_legacy_process(bt)
    else:
      self._save_process_asset(bt)

  def __delitem__(self, identifier: str):
    try:
      self._delete_process(identifier)
    except Exception as e:
      raise KeyError(f"Failed to delete behavior tree '{identifier}'") from e

  def _get_process(self, identifier: str) -> _Process | None:
    if id_utils.is_id(identifier):
      process_asset = self._get_process_asset(identifier)
      if process_asset is not None:
        return process_asset
    # Fallback: Always try the legacy lookup. Even if it looks like an asset id
    # it can be a behavior tree name like "my_tree.bt.pb".
    return self._get_legacy_process(identifier)

  def _get_process_asset(self, identifier: str) -> _Process | None:
    try:
      asset = self._installed_assets.get_installed_asset(identifier)
      pa = asset.deployment_data.process.process
      # Use metadata from the `InstalledAsset` as metadata in the deployment
      # data may be less complete (e.g., omits the version).
      pa.metadata.CopyFrom(asset.metadata)
      return _Process(asset_proto=pa)
    except grpc.RpcError as e:
      if hasattr(e, 'code') and e.code() == grpc.StatusCode.NOT_FOUND:
        return None
      raise e

  @error_handling.retry_on_grpc_unavailable
  def _get_legacy_process(self, identifier: str) -> _Process | None:
    try:
      bt_proto = self._solution.GetBehaviorTree(
          solution_service_pb2.GetBehaviorTreeRequest(name=identifier)
      )
      return _Process(
          asset_proto=process_asset_pb2.ProcessAsset(behavior_tree=bt_proto),
      )
    except grpc.RpcError as e:
      if cast(grpc.Call, e).code() == grpc.StatusCode.NOT_FOUND:
        return None
      raise e

  # Returns a dict with None values if keys_only is True. This is faster because
  # we need to request less data from the backends.
  def _list_all_processes(
      self,
      *,
      keys_only: bool,
  ) -> dict[str, _Process | None]:
    process_assets = self._list_all_process_assets(keys_only=keys_only)
    legacy_processes = self._list_all_legacy_processes(keys_only=keys_only)

    # "Concatenate" process assets (first) and legacy processes (second). In
    # case of a collision between asset id and legacy behavior tree name the
    # asset takes precedence. Note that we have to do that manually since
    # neither "a | b" nor "b | a" is what we want (one gets the order wrong, the
    # other gets the precedence wrong).
    combined = process_assets
    for k, v in legacy_processes.items():
      if k not in combined:
        combined[k] = v
    return combined

  def _list_all_process_assets(
      self,
      *,
      keys_only: bool,
  ) -> dict[str, _Process | None]:
    view = (
        view_pb2.AssetViewType.ASSET_VIEW_TYPE_BASIC
        if keys_only
        else view_pb2.AssetViewType.ASSET_VIEW_TYPE_FULL
    )

    result: dict[str, _Process | None] = {}
    for installed_asset in self._installed_assets.list_all_installed_assets(
        asset_types=[asset_type_pb2.AssetType.ASSET_TYPE_PROCESS],
        view=view,
    ):
      id_str = id_utils.id_from_proto(installed_asset.metadata.id_version.id)
      if keys_only:
        result[id_str] = None
      else:
        pa = installed_asset.deployment_data.process.process
        # Use metadata from the `InstalledAsset` as metadata in the deployment
        # data may be less complete (e.g., omits the version).
        pa.metadata.CopyFrom(installed_asset.metadata)
        result[id_str] = _Process(asset_proto=pa)

    return result

  @error_handling.retry_on_grpc_unavailable
  def _list_all_legacy_processes(
      self,
      *,
      keys_only: bool,
  ) -> dict[str, _Process | None]:
    view = (
        solution_service_pb2.BehaviorTreeView.BEHAVIOR_TREE_VIEW_BASIC
        if keys_only
        else solution_service_pb2.BehaviorTreeView.BEHAVIOR_TREE_VIEW_FULL
    )
    next_page_token = None

    result: dict[str, _Process | None] = {}
    while True:
      response = self._solution.ListBehaviorTrees(
          solution_service_pb2.ListBehaviorTreesRequest(
              page_size=_SOLUTION_SERVICE_MAX_PAGE_SIZE,
              page_token=next_page_token,
              view=view,
          )
      )
      for bt in response.behavior_trees:
        result[bt.name] = (
            None
            if keys_only
            else _Process(
                asset_proto=process_asset_pb2.ProcessAsset(behavior_tree=bt),
            )
        )
      if not response.next_page_token:
        break
      next_page_token = response.next_page_token

    return result

  def _save_legacy_process(self, bt: behavior_tree.BehaviorTree):
    # Warn outside of the retried RPC so that retries don't repeat the warning.
    # The stack level points at the caller of `save()` or `__setitem__`.
    warnings.warn(
        f'Saving Process "{bt.name}" as a legacy process because it has no'
        ' Asset metadata. Saving legacy processes is deprecated and will not'
        ' be supported in a following release. Set Asset metadata with'
        ' BehaviorTree.set_asset_metadata() before saving.',
        DeprecationWarning,
        stacklevel=3,
    )
    self._update_legacy_process(bt)

  @error_handling.retry_on_grpc_unavailable
  def _update_legacy_process(self, bt: behavior_tree.BehaviorTree):
    self._solution.UpdateBehaviorTree(
        solution_service_pb2.UpdateBehaviorTreeRequest(
            behavior_tree=bt.proto,
            allow_missing=True,
        )
    )

  def _save_process_asset(self, bt: behavior_tree.BehaviorTree):
    # The installed assets service requires the version and output-only fields
    # to be unset in an installation request.

    # `asset_metadata_proto` returns a copy which we can safely mutate.
    metadata_for_saving = bt.asset_metadata_proto
    assert metadata_for_saving is not None
    metadata_for_saving.id_version.ClearField('version')
    metadata_for_saving.ClearField('file_descriptor_set')
    bt_for_saving = bt.proto
    bt_for_saving.description.ClearField('id_version')

    process_for_saving = process_asset_pb2.ProcessAsset(
        metadata=metadata_for_saving,
        behavior_tree=bt_for_saving,
        assets=bt.referenced_assets,
    )

    referenced_assets.update_referenced_assets(
        process_for_saving,
        self._installed_assets,
    )

    # Propagate the updated list back to the BehaviorTree instance.
    bt.referenced_assets = dict(process_for_saving.assets)

    try:
      saved_asset = self._installed_assets.create_installed_asset(
          asset=installed_assets_pb2.CreateInstalledAssetRequest.Asset(
              process=process_for_saving,
          ),
      )
    except installed_assets_client.OperationError as e:
      if e.code() == grpc.StatusCode.ALREADY_EXISTS:
        return
      raise e

    # Update the BehaviorTree metadata. Effectively, this only changes the asset
    # version which gets generated by the installed assets service upon
    # installation.
    bt.asset_metadata_proto = saved_asset.metadata

  def _delete_process(self, identifier: str):
    if id_utils.is_id(identifier):
      try:
        # Try to delete a Process asset with the given identifier.
        self._installed_assets.delete_installed_asset(identifier)
        return
      except installed_assets_client.OperationError as e:
        # If the Process asset is not found, try the legacy deletion below.
        # Even if it looks like an asset id the identifier can be a behavior
        # tree name like "my_tree.bt.pb".
        if e.code() != grpc.StatusCode.NOT_FOUND:
          raise e

    self._delete_legacy_process(identifier)

  @error_handling.retry_on_grpc_unavailable
  def _delete_legacy_process(self, identifier: str):
    self._solution.DeleteBehaviorTree(
        solution_service_pb2.DeleteBehaviorTreeRequest(name=identifier)
    )
