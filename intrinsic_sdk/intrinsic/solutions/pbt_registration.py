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

"""Lightweight Python wrapper around the BehaviorTreeRegistry."""

from __future__ import annotations

import uuid
import warnings

import grpc

from intrinsic.executive.proto import behavior_tree_pb2
from intrinsic.skills.proto import behavior_tree_registry_pb2
from intrinsic.skills.proto import behavior_tree_registry_pb2_grpc
from intrinsic.skills.proto import skill_registry_config_pb2
from intrinsic.solutions import behavior_tree
from intrinsic.util.grpc import error_handling


class BehaviorTreeRegistry:
  """Wrapper for the proto builder gRPC service."""

  def __init__(
      self, stub: behavior_tree_registry_pb2_grpc.BehaviorTreeRegistryStub
  ):
    """Constructs a new BehaviorTreeRegistry object.

    Args:
      stub: The gRPC stub to be used for communication with the service.
    """
    self._stub: behavior_tree_registry_pb2_grpc.BehaviorTreeRegistryStub = stub

  @classmethod
  def connect(cls, grpc_channel: grpc.Channel) -> BehaviorTreeRegistry:
    """Connect to a running proto builder.

    Args:
      grpc_channel: Channel to the gRPC service.

    Returns:
      A newly created instance of the wrapper class.

    Raises:
      grpc.RpcError: When gRPC call to service fails.
    """
    stub = behavior_tree_registry_pb2_grpc.BehaviorTreeRegistryStub(
        grpc_channel
    )
    return cls(stub)

  @error_handling.retry_on_grpc_unavailable
  def _register_or_update_behavior_tree(
      self, tree_proto: behavior_tree_pb2.BehaviorTree
  ):
    """Register a PBT.

    Args:
      tree_proto: The PBT to register.

    Raises:
      grpc.RpcError: When gRPC call fails.
    """
    reg = skill_registry_config_pb2.BehaviorTreeRegistration()
    reg.behavior_tree.CopyFrom(tree_proto)
    request = behavior_tree_registry_pb2.RegisterOrUpdateBehaviorTreeRequest()
    request.registration.CopyFrom(reg)

    self._stub.RegisterOrUpdateBehaviorTree(request)

  def sideload_behavior_tree(
      self,
      tree: behavior_tree_pb2.BehaviorTree | behavior_tree.BehaviorTree,
  ):
    """Sideload a PBT.

    This will automatically generate a random version for the PBT's id version,
    so that the sideloaded PBT is registered as a new version in the registry.

    Args:
      tree: The PBT to sideload.

    Raises:
      grpc.RpcError: When gRPC call fails.
    """
    if isinstance(tree, behavior_tree.BehaviorTree):
      if tree.asset_metadata_proto is not None:
        raise ValueError(
            'BehaviorTree represents a Process asset and does not have to be'
            ' sideloaded. Simply save the tree using Solution.processes.save()'
            ' and, if the tree should be listed as a runnable "Skill" in the'
            ' Process Explorer in the Frontend, set the SUBPROCESS tag (see'
            ' BehaviorTree.set_metadata()).'
        )
      tree_proto = tree.proto
    else:
      tree_proto = tree
    assert tree_proto.description
    skill_id = tree_proto.description.id
    warnings.warn(
        f'Sideloading behavior tree "{skill_id}" as a legacy process.'
        ' Sideloading is deprecated and will not be supported in a following'
        ' release. Set Asset metadata with'
        ' BehaviorTree.set_asset_metadata(..., subprocess=True) and save the'
        ' tree using Solution.processes.save() instead.',
        DeprecationWarning,
        stacklevel=2,
    )
    version = str(uuid.uuid4()).replace('-', '')
    tree_proto.description.id_version = skill_id + '.0.0.1+sideload' + version
    self._register_or_update_behavior_tree(tree_proto)
