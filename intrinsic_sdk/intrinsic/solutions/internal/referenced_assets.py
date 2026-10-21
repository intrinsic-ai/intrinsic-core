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

"""Utilities for extracting and updating referenced assets in Process assets."""

from collections.abc import Iterable
from typing import cast

import grpc

from intrinsic.assets import id_utils
from intrinsic.assets.install import installed_assets_client
from intrinsic.assets.processes.proto import process_asset_pb2
from intrinsic.assets.proto import view_pb2
from intrinsic.assets.proto.v1 import reference_pb2
from intrinsic.executive.proto import behavior_tree_pb2
from intrinsic.executive.py import behavior_tree_visitor


def _get_referenced_asset_ids(
    process: process_asset_pb2.ProcessAsset,
) -> set[str]:
  referenced_ids: set[str] = set()

  def _visit_node(
      _: behavior_tree_pb2.BehaviorTree,  # unused
      node: behavior_tree_pb2.BehaviorTree.Node,
  ) -> None:
    skill_id = node.task.call_behavior.skill_id
    if skill_id:
      referenced_ids.add(skill_id)

  behavior_tree_visitor.walk(
      process.behavior_tree,
      node_visitor=_visit_node,
      # We must NOT visit called tree state since this contains contents of
      # another Process. This Process would need to define its own referenced
      # Assets. The Assets referenced in called tree state are beyond the scope
      # of the Process being checked.
      visit_called_tree_state=False,
  )
  return referenced_ids


def _get_catalog_asset_map(
    asset_ids: Iterable[str],
    installed_assets_client: installed_assets_client.InstalledAssetsClient,
) -> dict[str, reference_pb2.CatalogAsset]:
  asset_id_list = list(asset_ids)
  if not asset_id_list:
    return {}

  try:
    installed_asset_list = installed_assets_client.batch_get_installed_assets(
        asset_id_list, view=view_pb2.AssetViewType.ASSET_VIEW_TYPE_BASIC
    )
  except grpc.RpcError as e:
    if cast(grpc.Call, e).code() == grpc.StatusCode.NOT_FOUND:
      return {}
    raise e

  catalog_map: dict[str, reference_pb2.CatalogAsset] = {}
  for installed_asset in installed_asset_list:
    if not installed_asset.asset.HasField("catalog"):
      continue
    catalog_asset = installed_asset.asset.catalog
    try:
      asset_id = id_utils.id_from_proto(catalog_asset.id_version.id)
      if asset_id:
        catalog_map[asset_id] = catalog_asset
    except Exception:  # pylint: disable=broad-exception-caught
      pass

  return catalog_map


def update_referenced_assets(
    process: process_asset_pb2.ProcessAsset,
    installed_assets_client: installed_assets_client.InstalledAssetsClient,
) -> None:
  """Updates the referenced Assets map on the `ProcessAsset` in place.

  Adds any Assets that are referenced in the Process to its referenced Assets,
  iff a catalog Asset by the same ID is installed or the Asset is already
  present in the referenced Assets map. Removes any Assets not referenced in the
  Process from the referenced Assets map.

  Args:
    process: The ProcessAsset proto whose `assets` map to update in place.
    installed_assets_client: The InstalledAssetsClient to fetch catalog Assets
      from.
  """
  referenced_ids = _get_referenced_asset_ids(process)
  if not referenced_ids:
    process.assets.clear()
    return

  catalog_map = _get_catalog_asset_map(referenced_ids, installed_assets_client)
  updated_map: dict[str, process_asset_pb2.ProcessAsset.ProcessedAsset] = {}

  for asset_id in referenced_ids:
    installed_catalog = catalog_map.get(asset_id)
    if installed_catalog is not None:
      processed_asset = process_asset_pb2.ProcessAsset.ProcessedAsset(
          catalog=installed_catalog
      )
      updated_map[asset_id] = processed_asset
    else:
      existing = process.assets.get(asset_id)
      if existing is not None:
        updated_map[asset_id] = existing

  process.assets.clear()
  for asset_id, processed_asset in updated_map.items():
    process.assets[asset_id].CopyFrom(processed_asset)
