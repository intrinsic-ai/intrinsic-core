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

"""Python API for behavior trees.

Includes a BehaviorTree class, which inherits from Plan,
a nested Node class, which represents the nodes of the tree,
and various children classes of Node for each type of supported
BT node.
The BehaviorTree includes a method to generate a Graphviz dot representation
for it, a method to generate a proto of type behavior_tree_pb2,
and a class method to initialize a new BehaviorTree from a BT proto object.

To execute the behavior tree, simply pass an instance of BehaviorTree to the
executive.run() method.
"""

from __future__ import annotations

import abc
import collections
import dataclasses
import enum
import textwrap
from typing import Any as AnyType
from typing import Callable
from typing import cast
from typing import Iterable
from typing import List
from typing import Mapping
from typing import Optional
from typing import Sequence as SequenceType
from typing import Tuple
from typing import Union
import uuid
import warnings

from cel.expr import syntax_pb2
from google.protobuf import any_pb2
from google.protobuf import message as protobuf_message
from google.protobuf import wrappers_pb2
import graphviz

from intrinsic.assets import id_utils
from intrinsic.assets.processes.proto import process_asset_pb2
from intrinsic.assets.proto import asset_tag_pb2
from intrinsic.assets.proto import asset_type_pb2
from intrinsic.assets.proto import documentation_pb2
from intrinsic.assets.proto import id_pb2
from intrinsic.assets.proto import metadata_pb2
from intrinsic.assets.proto import vendor_pb2
from intrinsic.executive.proto import any_list_pb2
from intrinsic.executive.proto import behavior_call_pb2
from intrinsic.executive.proto import behavior_tree_pb2
from intrinsic.executive.proto import code_execution_pb2
from intrinsic.executive.proto import world_query_pb2
from intrinsic.skills.proto import skills_pb2
from intrinsic.solutions import blackboard_value
from intrinsic.solutions import cel
from intrinsic.solutions import errors as solutions_errors
from intrinsic.solutions import ipython
from intrinsic.solutions import proto_building
from intrinsic.solutions import utils
from intrinsic.solutions.internal import actions as _actions_internal
from intrinsic.solutions.internal import code_execution as _code_execution_internal
from intrinsic.solutions.internal import skill_utils
from intrinsic.solutions.internal.bt import bt_util
from intrinsic.solutions.internal.bt import world_query as _world_query_internal
from intrinsic.util.proto import descriptors
from intrinsic.util.status import extended_status_pb2
from intrinsic.world.python import object_world_resources

_SUBTREE_DOT_ATTRIBUTES = {'labeljust': 'l', 'labelloc': 't'}
_NODE_TYPES_TO_DOT_SHAPES = {
    'task': 'box',
    'sub_tree': 'point',
    'fail': 'box',
    'sequence': 'cds',
    'parallel': 'trapezium',
    'selector': 'octagon',
    'fallback': 'octagon',
    'loop': 'hexagon',
    'retry': 'hexagon',
    'branch': 'diamond',
    'data': 'box',
    'debug': 'box',
}


@dataclasses.dataclass(frozen=True, kw_only=True)
class NodeIdentifier:
  """A class identifying a specific node in a process.

  Currently only nodes in the main process tree can be identified. Nodes within
  reusable processes are not yet supported.
  """

  tree_id: str
  node_id: int

  def __post_init__(self):
    # Check for valid identifier: tree id and node id are set and not "" or 0.
    assert self.tree_id and self.node_id

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.NodeIdentifier:
    return behavior_tree_pb2.BehaviorTree.NodeIdentifier(
        tree_id=self.tree_id, node_id=self.node_id
    )


class NodeName(str):
  """Strong type for a node name"""


class NodeId(int):
  """Strong type for a node id.

  This only refers to the node's id field. To fully identify a node in a tree,
  use a NodeIdentifier (i.e., include the tree id)
  """


# A generic type interface that allows API calls to refer to a specific node in
# a tree. Node objects, names or ids are only valid in relation to a tree.
NodeInTreeType = Union[
    NodeIdentifier,
    NodeName,  # refers to the name of a node, must be unique in the process
    NodeId,  # refers to a node id, must be unique in the process
]

generate_unique_node_id = bt_util.generate_unique_node_id

ActionBase = _actions_internal.ActionBase
CodeExecution = _code_execution_internal.CodeExecution
PythonScript = _code_execution_internal.PythonScript
get_function_body_as_str = _code_execution_internal.get_function_body_as_str
WorldQueryObject = _world_query_internal.WorldQueryObject
WorldQuery = _world_query_internal.WorldQuery


def _transform_to_node(node: Node | ActionBase | CodeExecution) -> Node:
  if isinstance(node, (ActionBase, CodeExecution)):
    return Task(node)
  return node


def _transform_to_optional_node(
    node: Node | ActionBase | CodeExecution | None,
) -> Optional[Node]:
  if node is None:
    return None
  return _transform_to_node(node)


def _dot_wrap_in_box(
    child_graph: graphviz.Digraph, name: str, label: str
) -> graphviz.Digraph:
  box_dot_graph = graphviz.Digraph()
  box_dot_graph.name = 'cluster_' + name
  box_dot_graph.graph_attr = {'label': label}
  box_dot_graph.graph_attr.update(_SUBTREE_DOT_ATTRIBUTES)
  box_dot_graph.subgraph(child_graph)
  return box_dot_graph


def _dot_append_child(
    dot_graph: graphviz.Digraph,
    parent_node_name: str,
    child_node: Node,
    child_node_id_suffix: str,
    edge_label: str = '',
):
  """Inserts in place a subgraph of the given child into the given graph.

  This function has side effects!
  It changes the `dot_graph` and returns nothing.

  Args:
    dot_graph: The dot graph instance, which should be updated.
    parent_node_name: The name of the node in the dot graph, which should get an
      edge connecting it to the child node.
    child_node: A behavior tree Node, which should get converted to dot and its
      graph should be appended to the `dot_graph`.
    child_node_id_suffix: A little string to make the child node name unique
      within the dot graph.
    edge_label: Typically, the edge from the parent to the child is not
      annotated with a label. If a custom edge annotation is needed, this
      argument value can be used for that.
  """
  child_dot_graph, child_node_name = child_node.dot_graph(child_node_id_suffix)
  dot_graph.subgraph(child_dot_graph)
  dot_graph.edge(parent_node_name, child_node_name, label=edge_label)


def _dot_append_children(
    dot_graph: graphviz.Digraph,
    parent_node_name: str,
    child_nodes: Iterable[Node],
    parent_node_id_suffix: str,
    node_id_suffix_offset: int,
):
  """Inserts in place subgraphs of the given children into the given graph.

  This function has side effects!
  It changes the `dot_graph` and returns nothing.

  Args:
    dot_graph: The dot graph instance, which should be updated.
    parent_node_name: The name of the node in the dot graph, which should get
      edges connecting it to the child nodes.
    child_nodes: A list of behavior tree Nodes, which should get converted to a
      dot representation and be added to the `dot_graph` as subgraphs.
    parent_node_id_suffix: The suffix that was used to make the parent node
      unique in the dot graph.
    node_id_suffix_offset: A number that is unique among the children of the
      given parent node, which is appended as a suffix to the child node names
      to make them unique in the dot graph.
  """
  for i, child_node in enumerate(child_nodes):
    _dot_append_child(
        dot_graph,
        parent_node_name,
        child_node,
        parent_node_id_suffix + '_' + str(i + node_id_suffix_offset),
    )


@utils.protoenum(
    proto_enum_type=behavior_tree_pb2.BehaviorTree.Breakpoint.Type,
    unspecified_proto_enum_map_to_none=(
        behavior_tree_pb2.BehaviorTree.Breakpoint.TYPE_UNSPECIFIED
    ),
)
class BreakpointType(enum.Enum):
  """Specifies when to apply a breakpoint."""


@utils.protoenum(
    proto_enum_type=behavior_tree_pb2.BehaviorTree.Node.ExecutionSettings.Mode,
    unspecified_proto_enum_map_to_none=(
        behavior_tree_pb2.BehaviorTree.Node.ExecutionSettings.UNSPECIFIED
    ),
)
class NodeExecutionMode(enum.Enum):
  """Specifies the execution mode for a node."""


@utils.protoenum(
    proto_enum_type=(
        # pylint: disable=line-too-long
        behavior_tree_pb2.BehaviorTree.Node.ExecutionSettings.DisabledResultState
    ),
    unspecified_proto_enum_map_to_none=(
        # pylint: disable=line-too-long
        behavior_tree_pb2.BehaviorTree.Node.ExecutionSettings.DISABLED_RESULT_STATE_UNSPECIFIED
    ),
)
class DisabledResultState(enum.Enum):
  """Specifies the forced resulting state for a disabled node."""


class Decorators:
  """Collection of properties assigned to a node.

  Currently, we support a single condition decorator.

  Attributes:
    condition: A condition to decide if a node can be executed or should fail
      immediately.
    breakpoint_type: Optional breakpoint type for the node, see BreakpointType.
    execution_mode: Optional NodeExecutionMode that allows to disable a node.
    disabled_result_state: Optional DisabledResultState forcing a resulting
      state for disabled nodes. Ignored unless execution_mode is DISABLED.
    proto: The proto representation of the decorators objects.
  """

  condition: Optional[Condition]
  breakpoint_type: Optional[BreakpointType]
  execution_mode: Optional[NodeExecutionMode]
  disabled_result_state: Optional[DisabledResultState]

  def __init__(
      self,
      condition: Optional[Condition] = None,
      breakpoint_type: Optional[BreakpointType] = None,
      execution_mode: Optional[NodeExecutionMode] = None,
      disabled_result_state: Optional[DisabledResultState] = None,
  ):
    self.condition = condition
    self.breakpoint_type = breakpoint_type
    self.execution_mode = execution_mode
    self.disabled_result_state = disabled_result_state

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Node.Decorators:
    """Converts the Decorators object to a Decorators proto.

    Returns:
      A proto representation of this object.
    """
    proto_message = behavior_tree_pb2.BehaviorTree.Node.Decorators()
    if self.condition is not None:
      proto_message.condition.CopyFrom(self.condition.proto)
    if self.breakpoint_type is not None:
      proto_message.breakpoint = self.breakpoint_type.value
    if self.execution_mode is not None:
      proto_message.execution_settings.mode = self.execution_mode.value
      if self.disabled_result_state is not None:
        proto_message.execution_settings.disabled_result_state = (
            self.disabled_result_state.value
        )

    return proto_message

  @classmethod
  def create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree.Node.Decorators
  ) -> Decorators:
    """Creates an instance from a Decorators proto.

    Args:
      proto_object: Proto to read data from.

    Returns:
      Instance of Decorators wrapper with data from proto.
    """
    decorator = cls()
    if proto_object.HasField('condition'):
      decorator.condition = Condition.create_from_proto(proto_object.condition)
    if proto_object.HasField('breakpoint'):
      decorator.breakpoint_type = BreakpointType.from_proto(
          proto_object.breakpoint
      )
    if proto_object.HasField('execution_settings'):
      execution_settings_proto = proto_object.execution_settings
      decorator.execution_mode = NodeExecutionMode.from_proto(
          execution_settings_proto.mode
      )
      if execution_settings_proto.HasField('disabled_result_state'):
        decorator.disabled_result_state = DisabledResultState.from_proto(
            execution_settings_proto.disabled_result_state
        )
    return decorator


@utils.protoenum(
    proto_enum_type=behavior_tree_pb2.BehaviorTree.Node.State,
    unspecified_proto_enum_map_to_none=(
        behavior_tree_pb2.BehaviorTree.Node.State.UNSPECIFIED
    ),
)
class NodeState(enum.Enum):
  """Specifies the node state."""


@utils.protoenum(
    proto_enum_type=behavior_tree_pb2.BehaviorTree.TaskNode.State,
    unspecified_proto_enum_map_to_none=(
        behavior_tree_pb2.BehaviorTree.TaskNode.State.ACCEPTED
    ),
)
class TaskNodeState(enum.Enum):
  """Specifies the task state of a task node."""


class Node(abc.ABC):
  """Parent abstract base class for all the supported behavior tree nodes.

  Attributes:
    proto: The proto representation of the node.
    name: Optional name of the node.
    node_type: A string label of the node type.
    state: Optional (execution) state of the node (read-only).
    decorators: A list of decorators for the current node.
    breakpoint: Optional type of breakpoint configured for this node.
    execution_mode: Optional execution mode for this node.
    node_id: A unique id for this node.
    user_data_protos: user data protos as dict from key to Any protos.
    user_data_bytes: bytes user data as dict from key to bytes.
  """

  _name: str | None
  _node_id: int | None
  _decorators: Decorators | None
  _state: NodeState | None
  _user_data_protos: dict[str, any_pb2.Any]
  _user_data_bytes: dict[str, bytes]
  _failure_settings: Node.FailureSettings | None

  def __init__(self, name: str | None = None, node_id: int | None = None):
    self._name = name
    self._node_id = node_id

    self._decorators = None
    self._state = None
    self._user_data_protos = {}
    self._user_data_bytes = {}
    self._failure_settings = None

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    return f'{type(self).__name__}({self._name_repr()})'

  def _name_repr(self) -> str:
    """Returns a snippet for the name attribute to be used in __repr__.

    The snippet is a keyword argument of the form 'name="example_name", '. It
    will be empty, if name is not set. It can be inserted in the output of a
    constructor call in __repr__ without any logic (e.g., adding commas or not,
    handling the name not being set).
    """
    name_snippet = ''
    if self.name is not None:
      name_snippet = f'name="{self.name}", '
    return name_snippet

  class FailureSettings:
    """Wrapper to configure failure settings for a node.

    If any are set, will end up in the decorators of a node proto.
    """

    _settings: behavior_tree_pb2.BehaviorTree.Node.Decorators.FailureSettings
    _parent_node: Node

    def __init__(self, parent_node: Node):
      self._settings = (
          behavior_tree_pb2.BehaviorTree.Node.Decorators.FailureSettings()
      )
      self._parent_node = parent_node

    @property
    def proto(
        self,
    ) -> behavior_tree_pb2.BehaviorTree.Node.Decorators.FailureSettings:
      """Retrieves proto if any settings have been made.

      Returns:
        proto, if any settings set, None otherwise.
      """
      return self._settings

    def emit_extended_status_proto(
        self,
        extended_status: extended_status_pb2.ExtendedStatus,
        to_blackboard_key: str = '',
    ) -> Node:
      """Causes an extended status to be emitted on node failure.

      Args:
        extended_status: the extended status to emit
        to_blackboard_key: the blackboard key to emit the extended status to. If
          empty, will not write extended status to a blackboard key (cannot be
          used for status matches), but status will still be propagated.

      Returns:
        Node that this instance blongs to.
      """
      if extended_status.HasField('status_code'):
        if (
            extended_status.status_code.code < 0
            or extended_status.status_code.code > 0xFFFFFFFF
        ):
          raise ValueError(
              f'Status code number {extended_status.status_code.code} out of'
              f' range, must be in range [0..{int(0xffffffff)}]'
          )

      if self._settings is None:
        self._settings = (
            behavior_tree_pb2.BehaviorTree.Node.Decorators.FailureSettings()
        )
      self._settings.emit_extended_status.extended_status.CopyFrom(
          extended_status
      )
      self._settings.emit_extended_status.to_blackboard_key = to_blackboard_key
      return self._parent_node

    def emit_extended_status(
        self,
        component: str,
        code: int,
        *,
        title: str = '',
        user_message: str = '',
        debug_message: str = '',
        to_blackboard_key: str = '',
    ) -> Node:
      """Causes an extended status to be emitted on node failure.

      Args:
        component: Component for StatusCode where error originated.
        code: Numeric code specific to component for StatusCode.
        title: brief title of the error, make this meaningful and keep to a
          length of 75 characters if possible.
        user_message: if non-empty, set extended status external report message
          to this string.
        debug_message: if non-empty, set extended status internal report message
          to this string. Only set this in an environment where the data may be
          shared.
        to_blackboard_key: the blackboard key to also write the extended status
          to. If empty, will not write extended status to a blackboard key
          (cannot be used for status matches), but status will still be
          propagated.

      Returns:
        Node that this instance blongs to.
      """
      es = extended_status_pb2.ExtendedStatus(
          status_code=extended_status_pb2.StatusCode(
              component=component, code=code
          )
      )
      if title:
        es.title = title
      if user_message:
        es.user_report.message = user_message
      if debug_message:
        es.debug_report.message = debug_message

      self.emit_extended_status_proto(es, to_blackboard_key)
      return self._parent_node

    def emit_extended_status_to(self, blackboard_key: str) -> Node:
      """Causes an extended status to be written to the blackboard.

      This applies to extended status that may be produced by the node, e.g., by
      Task nodes calling a skill, or due to extended status propagation.

      This will not configure a specific extended status to be emitted, but only
      to store one that was encountered.

      Args:
        blackboard_key: The blackboard key to write the extended status to.

      Returns:
        Node that this instance blongs to.
      """
      if self._settings is None:
        self._settings = (
            behavior_tree_pb2.BehaviorTree.Node.Decorators.FailureSettings()
        )
      self._settings.emit_extended_status.to_blackboard_key = blackboard_key
      return self._parent_node

    @classmethod
    def create_from_proto(
        cls,
        parent_node: Node,
        # pylint: disable=line-too-long
        proto_object: behavior_tree_pb2.BehaviorTree.Node.Decorators.FailureSettings,
    ) -> Node.FailureSettings:
      instance = cls(parent_node)
      instance._settings = proto_object
      return instance

  @classmethod
  def create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree.Node
  ) -> Node:
    """Instantiates a Node instance from a proto."""
    if cls != Node:
      raise TypeError('create_from_proto can only be called on the Node class')
    node_type = proto_object.WhichOneof('node_type')
    # pylint:disable=protected-access
    # Intentionally using knowledge of subclasses in this parent class, so that
    # it is possible to provide a generic function to create the appropriate
    # subclass from a Node proto.
    if node_type == 'task':
      created_node = Task._create_from_proto(proto_object.task)
    elif node_type == 'sub_tree':
      created_node = SubTree._create_from_proto(proto_object.sub_tree)
    elif node_type == 'fail':
      created_node = Fail._create_from_proto(proto_object.fail)
    elif node_type == 'sequence':
      created_node = Sequence._create_from_proto(proto_object.sequence)
    elif node_type == 'parallel':
      created_node = Parallel._create_from_proto(proto_object.parallel)
    elif node_type == 'selector':
      created_node = Selector._create_from_proto(proto_object.selector)
    elif node_type == 'retry':
      created_node = Retry._create_from_proto(proto_object.retry)
    elif node_type == 'fallback':
      created_node = Fallback._create_from_proto(proto_object.fallback)
    elif node_type == 'loop':
      created_node = Loop._create_from_proto(proto_object.loop)
    elif node_type == 'branch':
      created_node = Branch._create_from_proto(proto_object.branch)
    elif node_type == 'data':
      created_node = Data._create_from_proto(proto_object.data)
    elif node_type == 'debug':
      created_node = Debug._create_from_proto(proto_object.debug)
    else:
      raise TypeError('Unsupported proto node type', node_type)

    if proto_object.HasField('decorators'):
      created_node.set_decorators(
          Decorators.create_from_proto(proto_object.decorators)
      )
      if proto_object.decorators.HasField('on_failure'):
        created_node._failure_settings = Node.FailureSettings.create_from_proto(
            created_node, proto_object.decorators.on_failure
        )

    if proto_object.HasField('user_data'):
      for k, m in proto_object.user_data.data_any.items():
        created_node.set_user_data_proto_from_any(k, m)
      for k, b in proto_object.user_data.data_bytes.items():
        created_node.set_user_data_bytes(k, b)
    if proto_object.HasField('name'):
      created_node.name = proto_object.name
    if proto_object.HasField('id') and proto_object.id != 0:
      created_node.node_id = proto_object.id
    if proto_object.HasField('state'):
      # (1) The following requires the _state attribute to exist in
      #     the subclass that created_node is an instance of (i.e., all
      #     subclasses of Node).
      # (2) Intentionally writing protected attribute here
      created_node._state = NodeState.from_proto(proto_object.state)

    # pylint:enable=protected-access
    return created_node

  @property
  @abc.abstractmethod
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Node:
    """Return proto representation of a Node object."""
    proto_message = behavior_tree_pb2.BehaviorTree.Node()
    if self.name is not None:
      proto_message.name = self.name
    if self.node_id is not None:
      proto_message.id = self.node_id
    if self.state is not None:
      proto_message.state = self.state.value
    if self.decorators is not None:
      proto_message.decorators.CopyFrom(self.decorators.proto)
    if self._user_data_protos:
      for k, m in self._user_data_protos.items():
        proto_message.user_data.data_any[k].CopyFrom(m)
    if self._user_data_bytes:
      for k, b in self._user_data_bytes.items():
        proto_message.user_data.data_bytes[k] = b
    if self._failure_settings is not None:
      proto_message.decorators.on_failure.CopyFrom(self._failure_settings.proto)

    return proto_message

  def generate_and_set_unique_id(self) -> int:
    """Generates a new random id and sets it for this node."""
    if self.node_id is not None:
      print(
          'Warning: Creating a new unique id, but this node already had an id'
          f' ({self.node_id})'
      )
    self.node_id = bt_util.generate_unique_node_id()
    return self.node_id

  @property
  def on_failure(self) -> Node.FailureSettings:
    """Sets extra settings for behavior on failure."""
    if self._failure_settings is None:
      self._failure_settings = Node.FailureSettings(self)
    return self._failure_settings

  @utils.classproperty
  @abc.abstractmethod
  def node_type(cls) -> str:  # pylint:disable=no-self-argument
    ...

  @abc.abstractmethod
  def dot_graph(
      self,
      node_id_suffix: str = '',
      node_label: Optional[str] = None,
      name: Optional[str] = None,
  ) -> Tuple[graphviz.Digraph, str]:
    """Generates a graphviz subgraph with a single node for `self`.

    Args:
      node_id_suffix: A little string of form `_1_2`, which is just a suffix to
        make a unique node name in the graph. If the node names clash within the
        graph, they are merged into one, and we do not want to merge unrelated
        nodes.
      node_label: The label is typically just the type of the node. To use a
        different value, this argument can be used.
      name: name of the node as set by the user.

    Returns:
      A tuple of the generated graphviz dot graph and
      the name of the graph's root node.
    """
    dot_graph = graphviz.Digraph()
    node_name = self.node_type.lower() + node_id_suffix
    dot_graph.node(
        node_name,
        label=node_label if node_label is not None else self.node_type.lower(),
        shape=_NODE_TYPES_TO_DOT_SHAPES[self.node_type.lower()],
    )
    if name:
      dot_graph.name = name
      dot_graph.graph_attr = {'label': name}

    return dot_graph, node_name

  def show(self) -> None:
    return ipython.display_if_ipython(self.dot_graph()[0])

  @property
  def name(self) -> str | None:
    return self._name

  @name.setter
  def name(self, value: str | None):
    self._name = value

  @property
  def node_id(self) -> int | None:
    return self._node_id

  @node_id.setter
  def node_id(self, value: int | None):
    self._node_id = value

  @property
  def failure_settings(self) -> Node.FailureSettings | None:
    """Retrieves failure settings if any have been configured."""
    return self._failure_settings

  @property
  def state(self) -> NodeState | None:
    return self._state

  @property
  def decorators(self) -> Decorators | None:
    return self._decorators

  def set_decorators(self, decorators: Decorators | None) -> Node:
    self._decorators = decorators
    return self

  @abc.abstractmethod
  def has_child(self, node_id: int) -> bool:
    """Checks if the node has a direct child with the given ID.

    Args:
      node_id: ID of child to check for. Which specific children are checked
        depends on the specific node implementation. This shall not recurse and
        not check children with other tree IDs (i.e., not in sub-trees).

    Returns:
      True if node has a direct child with the given ID, False otherwise.
    """

  @abc.abstractmethod
  def remove_child(self, node_id: int) -> None:
    """Removes a direct child with the given ID.

    Args:
      node_id: ID of child to remove. Which specific children are considered
        depends on the specific node implementation. This shall not recurse and
        not check children with other tree IDs (i.e., not in sub-trees).

    Raises:
      ValueError if node cannot remove a child or the child is not found.
    """

  def set_user_data_proto(
      self, key: str, proto: protobuf_message.Message
  ) -> Node:
    """Sets a user data proto for a specified key.

    This will modify the user_data field of the node proto to contain the
    specified proto encoded as Any proto in the data_any field.

    Args:
      key: key to store value at
      proto: proto to store

    Returns:
      self
    """
    packed = any_pb2.Any()
    packed.Pack(proto)
    self._user_data_protos[key] = packed
    return self

  def delete_user_data_proto(self, key: str) -> None:
    if key in self._user_data_protos:
      del self._user_data_protos[key]

  def set_user_data_proto_from_any(
      self, key: str, any_proto: any_pb2.Any
  ) -> Node:
    """Sets a user data proto for a specified key.

    This will take the Any proto as-is.

    Args:
      key: key to store value at
      any_proto: proto to store encoded as Any proto

    Returns:
      self
    """
    self._user_data_protos[key] = any_proto
    return self

  @property
  def user_data_protos(self) -> dict[str, any_pb2.Any]:
    return self._user_data_protos

  def delete_user_data_bytes(self, key: str) -> None:
    if key in self._user_data_bytes:
      del self._user_data_bytes[key]

  def set_user_data_bytes(self, key: str, value: bytes) -> Node:
    """Sets a user data byte for a specified key.

    Args:
      key: key to store value at
      value: byte sequence to store

    Returns:
      self
    """
    self._user_data_bytes[key] = value
    return self

  @property
  def user_data_bytes(self) -> dict[str, bytes]:
    return self._user_data_bytes

  @property
  def breakpoint(self) -> BreakpointType:
    if self.decorators is not None:
      return self.decorators.breakpoint_type
    return None

  def set_breakpoint(self, breakpoint_type: Optional[BreakpointType]) -> Node:
    """Sets the breakpoint type on the decorator.

    Args:
      breakpoint_type: desired breakpoint type, None to remove type.

    Returns:
      Builder pattern, returns self.
    """
    decorators = self.decorators or Decorators()
    decorators.breakpoint_type = breakpoint_type
    self.set_decorators(decorators)
    return self

  @property
  def execution_mode(self) -> NodeExecutionMode:
    if (
        self.decorators is not None
        and self.decorators.execution_mode is not None
    ):
      return self.decorators.execution_mode
    return NodeExecutionMode.NORMAL

  def disable_execution(
      self,
      result_state: Optional[DisabledResultState] = None,
  ) -> Node:
    """Disables a node, so that it is not executed and appears to be skipped.

    Args:
      result_state: Optionally force the result of the execution to this state.
        If not set, the resulting state is automatically determined, so that the
        node is skipped.

    Returns:
      Builder pattern, returns self.
    """
    decorators = self.decorators or Decorators()
    decorators.execution_mode = NodeExecutionMode.DISABLED
    if result_state is not None:
      decorators.disabled_result_state = result_state
    self.set_decorators(decorators)
    return self

  def enable_execution(self) -> Node:
    """Enables a node, so that it will be executed.

    Returns:
      Builder pattern, returns self.
    """
    decorators = self.decorators or Decorators()
    decorators.execution_mode = None
    decorators.disabled_result_state = None
    self.set_decorators(decorators)
    return self


class Condition(abc.ABC):
  """Parent abstract base class for supported behavior tree conditions.

  Attributes:
    proto: The proto representation of the node.
    condition_type: A string label of the condition type.
  """

  @classmethod
  def create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree.Condition
  ) -> Condition:
    """Instantiates a Condition instance from a proto."""
    if cls != Condition:
      raise TypeError(
          'create_from_proto can only be called on the Condition class'
      )
    condition_type = proto_object.WhichOneof('condition_type')
    # pylint:disable=protected-access
    # Intentionally using knowledge of subclasses in this parent class, so that
    # it is possible to provide a generic function to create the appropriate
    # subclass from a Condition proto.
    if condition_type == 'behavior_tree':
      return SubTreeCondition._create_from_proto(proto_object.behavior_tree)
    elif condition_type == 'blackboard':
      return Blackboard._create_from_proto(proto_object.blackboard)
    elif condition_type == 'all_of':
      return AllOf._create_from_proto(proto_object.all_of)
    elif condition_type == 'any_of':
      return AnyOf._create_from_proto(proto_object.any_of)
    elif condition_type == 'not':
      return Not._create_from_proto(getattr(proto_object, 'not'))
    elif condition_type == 'status_match':
      return ExtendedStatusMatch._create_from_proto(
          getattr(proto_object, 'status_match')
      )
    else:
      raise TypeError('Unsupported proto condition type', condition_type)
    # pylint:enable=protected-access

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    return f'{type(self).__name__}()'

  @property
  @abc.abstractmethod
  def condition_type(self) -> str:
    ...

  @property
  @abc.abstractmethod
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Condition:
    ...


class SubTreeCondition(Condition):
  """A BT condition of type SubTree.

  The outcome of the subtree determines the result of the condition. If the
  tree succeeds, the condition evaluates to true, if the tree fails, it
  evaluates to false.

  Attributes:
    proto: The proto representation of the node.
    condition_type: A string label of the condition type.
    tree: The subtree deciding the outcome of the condition.
  """

  tree: BehaviorTree

  def __init__(self, tree: BehaviorTree | Node | ActionBase | CodeExecution):
    if tree is None:
      raise ValueError(
          'SubTreeCondition requires `tree` to be set to either a BehaviorTree,'
          ' Node, ActionBase or CodeExecution.'
      )
    if not isinstance(tree, BehaviorTree):
      node = _transform_to_optional_node(tree)
      tree = BehaviorTree(root=node)
    self.tree = tree

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    return f'{type(self).__name__}({str(self.tree)})'

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Condition:
    proto_object = behavior_tree_pb2.BehaviorTree.Condition()
    # Do not validate sub-tree. Only top-level calls to BehaviorTree.proto
    # should trigger validation once.
    # pylint: disable-next=protected-access
    proto_object.behavior_tree.CopyFrom(self.tree._to_proto(validate=False))
    return proto_object

  @property
  def condition_type(self) -> str:
    return 'sub_tree'

  @classmethod
  def _create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree
  ) -> SubTreeCondition:
    return cls(BehaviorTree.create_from_proto(proto_object))


class Blackboard(Condition):
  """A BT condition of type Blackboard.

  Evaluates a boolean CEL expression with respect to a reference to a proto.

  Attributes:
    proto: The proto representation of the node.
    condition_type: A string label of the condition type.
    cel_expression: string containing a CEL expression for evaluation.
    cel_expression_proto: Proto for the CEL expression.
  """

  _cel_expression: Union[str, syntax_pb2.Expr]

  def __init__(
      self, cel_expression: Union[str, cel.CelExpression, syntax_pb2.Expr]
  ):
    if isinstance(cel_expression, syntax_pb2.Expr):
      self._cel_expression = cel_expression
    else:
      self._cel_expression: str = str(cel_expression)

  # Returns the expression that sets the value of the blackboard key.
  #
  # Deprecated: Replace references to blackboard_key in the behavior tree
  # by inlining this cel_expression.
  #
  # For example, given a data node:
  # bt.Data(blackboard_key="foo", cel_expression="skill_return.bar")
  #
  # is used in a skill call as:
  # my_skill = ai.intrinsic.some_skill(
  #     my_param=CelExpression("foo"))
  #
  # then it should be replaced by:
  # my_skill = ai.intrinsic.some_skill(
  #     my_param=CelExpression("skill_return.bar"))
  #
  # and eliminate the data node entirely.
  @property
  def cel_expression(self) -> Optional[str]:
    if isinstance(self._cel_expression, syntax_pb2.Expr):
      return None
    return self._cel_expression

  # Assigns the expression that sets the value of the blackboard key.
  #
  # Deprecated: Replace references to blackboard_key in the behavior tree
  # by inlining this cel_expression.
  #
  # For example, given a data node:
  # bt.Data(blackboard_key="foo", cel_expression="skill_return.bar")
  #
  # is used in a skill call as:
  # my_skill = ai.intrinsic.some_skill(
  #     my_param=CelExpression("foo"))
  #
  # then it should be replaced by:
  # my_skill = ai.intrinsic.some_skill(
  #     my_param=CelExpression("skill_return.bar"))
  #
  # and eliminate the data node entirely.
  @cel_expression.setter
  def cel_expression(self, expression: str):
    self._cel_expression = expression

  @property
  def cel_expression_proto(self) -> Optional[syntax_pb2.Expr]:
    if isinstance(self._cel_expression, str):
      return None
    return self._cel_expression

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    expr = str(self._cel_expression)
    expr = textwrap.shorten(expr, width=80)
    return f'{type(self).__name__}({expr})'

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Condition:
    proto_object = behavior_tree_pb2.BehaviorTree.Condition()
    if isinstance(self._cel_expression, syntax_pb2.Expr):
      proto_object.blackboard.cel_expression_proto.CopyFrom(
          self._cel_expression
      )
    else:
      proto_object.blackboard.cel_expression = self._cel_expression
    return proto_object

  @property
  def condition_type(self) -> str:
    return 'blackboard'

  @classmethod
  def _create_from_proto(
      cls,
      # pylint: disable=line-too-long
      proto_object: behavior_tree_pb2.BehaviorTree.Condition.BlackboardExpression,
  ) -> Blackboard:
    if proto_object.HasField('cel_expression'):
      return cls(proto_object.cel_expression)
    else:
      return cls(proto_object.cel_expression_proto)


class CompoundCondition(Condition):
  """A base implementation for conditions composed of a number of conditions.

  Does not impose specific semantics on the children (these are to be defined
  by the sub-classes).

  Attributes:
    conditions: The list of conditions of the given condition.
    proto: The proto representation of the node.
  """

  def __init__(self, conditions: Optional[List[Condition]] = None):
    self.conditions: List[Condition] = conditions or []

  def set_conditions(self, conditions: List[Condition]) -> Condition:
    self.conditions = conditions
    return self

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    representation = f'{type(self).__name__}([ '
    for condition in self.conditions:
      representation += f'{str(condition)} '
    representation += '])'
    return representation


class AllOf(CompoundCondition):
  """A BehaviorTree condition encoding a boolean “and”.

  Compound of conditions, all of the sub-conditions need to be true.

  Attributes:
    proto: The proto representation of the node.
    condition_type: A string label of the condition type.
  """

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Condition:
    proto_object = behavior_tree_pb2.BehaviorTree.Condition()
    if self.conditions:
      for condition in self.conditions:
        proto_object.all_of.conditions.append(condition.proto)
    else:
      proto_object.all_of.CopyFrom(
          behavior_tree_pb2.BehaviorTree.Condition.LogicalCompound()
      )
    return proto_object

  @property
  def condition_type(self) -> str:
    return 'AllOf'

  @classmethod
  def _create_from_proto(
      cls,
      proto_object: behavior_tree_pb2.BehaviorTree.Condition.LogicalCompound,
  ) -> AllOf:
    condition = cls()
    for condition_proto in proto_object.conditions:
      condition.conditions.append(Condition.create_from_proto(condition_proto))
    return condition


class AnyOf(CompoundCondition):
  """A BehaviorTree condition encoding a boolean “or”.

  Compound of conditions, any of the sub-conditions needs to be true.

  Attributes:
    proto: The proto representation of the node.
    condition_type: A string label of the condition type.
  """

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Condition:
    proto_object = behavior_tree_pb2.BehaviorTree.Condition()
    if self.conditions:
      for condition in self.conditions:
        proto_object.any_of.conditions.append(condition.proto)
    else:
      proto_object.any_of.CopyFrom(
          behavior_tree_pb2.BehaviorTree.Condition.LogicalCompound()
      )
    return proto_object

  @property
  def condition_type(self) -> str:
    return 'AnyOf'

  @classmethod
  def _create_from_proto(
      cls,
      proto_object: behavior_tree_pb2.BehaviorTree.Condition.LogicalCompound,
  ) -> AnyOf:
    condition = cls()
    for condition_proto in proto_object.conditions:
      condition.conditions.append(Condition.create_from_proto(condition_proto))
    return condition


class Not(Condition):
  """A BT condition of type Not.

  Negates a condition.

  Attributes:
    proto: The proto representation of the node.
    condition_type: A string label of the condition type.
    condition: The condition to negate.
  """

  def __init__(self, condition: Condition):
    self.condition: Condition = condition

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    return f'{type(self).__name__}({self.condition})'

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Condition:
    proto_object = behavior_tree_pb2.BehaviorTree.Condition()
    not_proto = getattr(proto_object, 'not')
    not_proto.CopyFrom(self.condition.proto)
    return proto_object

  @property
  def condition_type(self) -> str:
    return 'not'

  @classmethod
  def _create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree.Condition
  ) -> Not:
    return cls(Condition.create_from_proto(proto_object))


class ExtendedStatusMatch(Condition):
  """A BT condition of type StatusMatch.

  Implements matching an extended status stored in the blackboard.

  Attributes:
    proto: The proto representation of the condition.
    condition_type: A string label of the condition type.
  """

  class StatusMatcherDeclaration(abc.ABC):
    """Abstract base class to formulate expectations for status matchers.

    A status matcher declaration enables to read and write status match types
    from the FailureSettings proto. One implementation per supported match_type
    is required.
    """

    @abc.abstractmethod
    def update_proto(
        self,
        proto: behavior_tree_pb2.BehaviorTree.Condition.ExtendedStatusMatch,
    ) -> None:
      """Updates an ExtendedStatusMatch from the matcher's configuration."""

    @classmethod
    @abc.abstractmethod
    def create_from_proto(
        cls,
        proto: behavior_tree_pb2.BehaviorTree.Condition.ExtendedStatusMatch,
    ) -> ExtendedStatusMatch.StatusMatcherDeclaration:
      """Reads a matcher's configuration from an ExtendedStatusMatch proto.

      Args:
        proto: Proto to configure the matcher from.

      Returns:
        New matcher instance for given configuration.
      """

  class MatchStatusCode(StatusMatcherDeclaration):
    """Status matcher to match component and code."""

    _proto: extended_status_pb2.StatusCode

    def __init__(self, component: str, code: int):
      if code < 0 or code > 0xFFFFFFFF:
        raise ValueError(
            f'Status code number {code} out of range, must be in range'
            f' [0..{int(0xffffffff)}]'
        )
      self._proto = extended_status_pb2.StatusCode(
          component=component, code=code
      )

    @classmethod
    def create_from_proto(
        cls,
        proto: behavior_tree_pb2.BehaviorTree.Condition.ExtendedStatusMatch,
    ) -> ExtendedStatusMatch.StatusMatcherDeclaration:
      if not proto.HasField('status_code'):
        # Should not happen, this would indicate an error in the caller
        raise TypeError('No status code field in ExtendedStatusMatch')
      return cls(proto.status_code.component, proto.status_code.code)

    def update_proto(
        self,
        proto: behavior_tree_pb2.BehaviorTree.Condition.ExtendedStatusMatch,
    ) -> None:
      proto.status_code.CopyFrom(self._proto)

    @property
    def proto(self) -> extended_status_pb2.StatusCode:
      """Returns the underlying StatusCode proto."""
      return self._proto

    def __repr__(self) -> str:
      return (
          f'{type(self).__name__}("{self._proto.component}",'
          f' {self._proto.code})'
      )

  _blackboard_key: str
  _matcher: StatusMatcherDeclaration

  def __init__(self, blackboard_key: str, matcher: MatchStatusCode):
    self._blackboard_key = blackboard_key
    self._matcher = matcher

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    return (
        f'{type(self).__name__}("{self._blackboard_key}",'
        f' {type(self).__name__}.{self._matcher!r})'
    )

  @property
  def blackboard_key(self) -> str:
    """Returns the blackboard key to match status against."""
    return self._blackboard_key

  @property
  def matcher(self) -> StatusMatcherDeclaration:
    """Returns the status matcher declaration."""
    return self._matcher

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Condition:
    proto_object = behavior_tree_pb2.BehaviorTree.Condition()
    status_match_proto = getattr(proto_object, 'status_match')
    status_match_proto.blackboard_key = self._blackboard_key
    self._matcher.update_proto(status_match_proto)
    return proto_object

  @property
  def condition_type(self) -> str:
    return 'status_match'

  @classmethod
  def _create_from_proto(
      cls,
      proto: behavior_tree_pb2.BehaviorTree.Condition.ExtendedStatusMatch,
  ) -> ExtendedStatusMatch:
    """Creates an instance from a proto.

    Args:
      proto: ExtendedStatusMatch proto to read configuration from.

    Returns:
      New instance based on extended status match configuration.

    Raises:
      TypeError: Unsupported match_type encountered.
    """
    blackboard_key = proto.blackboard_key
    match_type = proto.WhichOneof('match_type')
    if match_type == 'status_code':
      return cls(
          blackboard_key,
          ExtendedStatusMatch.MatchStatusCode.create_from_proto(proto),
      )

    raise TypeError(f'Cannot handle match type {match_type}')


class Task(Node):
  """A BT node of type Task for behavior_tree_pb2.TaskNode.

  This node type is a thin wrapper around a plan action, which is a thin
  wrapper around a skill. Ultimately, a plan represented as a behavior tree
  is a set of task nodes, which are combined together using the other node
  types that guide the control flow of the plan.

  Attributes:
    proto: The proto representation of the node.
    node_type: A string label of the node type.
    result: A reference to the result value on the blackboard, if available.

  Raises:
    solutions_errors.InvalidArgumentError: Unknown action specification.
  """

  _action: ActionBase | None
  _behavior_call_proto: behavior_call_pb2.BehaviorCall | None
  _code_execution: CodeExecution | None
  _task_state: behavior_tree_pb2.BehaviorTree.TaskNode.State | None

  def __init__(
      self,
      action: (
          ActionBase
          | CodeExecution
          | behavior_call_pb2.BehaviorCall
          | code_execution_pb2.CodeExecution
      ),
      name: str | None = None,
      node_id: int | None = None,
      task_state: behavior_tree_pb2.BehaviorTree.TaskNode.state | None = None,
  ):
    super().__init__(name, node_id)
    self._action = None
    self._code_execution = None
    self._behavior_call_proto = None
    if isinstance(action, ActionBase):
      self._behavior_call_proto = action.proto
      self._action = action
    elif isinstance(action, CodeExecution):
      self._code_execution = action
    elif isinstance(action, behavior_call_pb2.BehaviorCall):
      self._behavior_call_proto = action
    elif isinstance(action, code_execution_pb2.CodeExecution):
      self._code_execution = _code_execution_internal.create_from_proto(action)
    else:
      raise solutions_errors.InvalidArgumentError(
          f'Unknown action specification: {action}'
      )
    self._task_state = task_state

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    args = self._name_repr()
    if self._behavior_call_proto is not None:
      args += (
          'action=behavior_call.Action('
          f'skill_id="{self._behavior_call_proto.skill_id}")'
      )
    elif self._code_execution is not None:
      args += 'action=code_execution.PythonScript(...)'
    return f'{type(self).__name__}({args})'

  @property
  def task_state(self) -> TaskNodeState | None:
    return self._task_state

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Node:
    proto_object = super().proto
    if self._behavior_call_proto is not None:
      proto_object.task.call_behavior.CopyFrom(self._behavior_call_proto)
    if self._code_execution is not None:
      proto_object.task.execute_code.CopyFrom(self._code_execution.proto)
    if self._task_state is not None:
      proto_object.task.state = self._task_state
    return proto_object

  @property
  def behavior_call_proto(self) -> behavior_call_pb2.BehaviorCall | None:
    """BehaviorCall proto (if this task node was initialized for a skill)."""
    return self._behavior_call_proto

  @property
  def code_execution_proto(self) -> code_execution_pb2.CodeExecution | None:
    """CodeExecution proto (if this node was initialized for code execution)."""
    return (
        self._code_execution.proto if self._code_execution is not None else None
    )

  @property
  def code_execution(self) -> CodeExecution | None:
    """CodeExecution object (if initialized for code execution)."""
    return self._code_execution

  def update_behavior_call(
      self, behavior_call_proto: behavior_call_pb2.BehaviorCall
  ) -> None:
    """Updates the behavior call.

    Args:
      behavior_call_proto: New behavior call proto to use.

    Raises:
      TypeError: This task node was not initialized from a behavior call.
    """
    if self._behavior_call_proto is None:
      raise TypeError(
          f'Task node {self.node_id} was not initialized from a behavior call.'
      )
    self._action = None
    self._behavior_call_proto = behavior_call_proto

  def update_code_execution(
      self, code_execution_proto: code_execution_pb2.CodeExecution
  ) -> None:
    """Updates the code execution configuration.

    Args:
      code_execution_proto: New code execution proto to use.

    Raises:
      TypeError: This task node was not initialized for code execution.
    """
    if self._code_execution is None:
      raise TypeError(
          f'Task node {self.node_id} was not initialized for code execution.'
      )
    self._code_execution = _code_execution_internal.create_from_proto(
        code_execution_proto
    )

  @utils.classproperty
  def node_type(cls) -> str:  # pylint:disable=no-self-argument
    return 'task'

  @property
  def result(self) -> blackboard_value.BlackboardValue | None:
    if self._action is not None:
      return self._action.result
    if self._code_execution is not None:
      return self._code_execution.result
    return None

  def has_child(self, node_id: int) -> bool:
    return False

  def remove_child(self, node_id: int) -> None:
    raise ValueError('Task node does not have children to remove')

  @classmethod
  def _create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree.TaskNode
  ) -> Task:
    task_state = proto_object.state if proto_object.HasField('state') else None
    if proto_object.HasField('execute_code'):
      return cls(proto_object.execute_code, task_state=task_state)
    return cls(proto_object.call_behavior, task_state=task_state)

  def dot_graph(
      self,
      node_id_suffix: str = '',
      node_label: Optional[str] = None,
      name: Optional[str] = None,
  ) -> Tuple[graphviz.Digraph, str]:
    if node_label is None:
      node_label = self._name or ''
      if self._behavior_call_proto:
        if node_label:
          node_label += f' ({self._behavior_call_proto.skill_id})'
        else:
          node_label += f'Skill {self._behavior_call_proto.skill_id}'
      if self._code_execution is not None:
        if node_label:
          node_label += ' (CodeExecution)'
        else:
          node_label += 'CodeExecution'
    return super().dot_graph(node_id_suffix, node_label, name)


class SubTree(Node):
  """A BT node of type SubTree for behavior_tree_pb2.SubTreeNode.

  This node is usually used to group components into a subtree.

  Attributes:
    behavior_tree: The subtree, a BehaviorTree object.
    proto: The proto representation of the node.
    node_type: A string label of the node type.
  """

  def __init__(
      self,
      behavior_tree: Optional[Union[Node, BehaviorTree]] = None,
      name: Optional[str] = None,
      node_id: int | None = None,
  ):
    """Creates a SubTree node.

    Args:
      behavior_tree: behavior tree or root node of a tree for this subtree. If
        passing a root node you must also provide the name argument.
      name: name of the behavior tree, if behavior_tree is a node, i.e., a root
        node of a tree; otherwise, the name of this node.
      node_id: Pre-determined node ID, must be unique in the tree.
    """
    super().__init__(name, node_id)
    self.behavior_tree: Optional[BehaviorTree] = None
    if behavior_tree is not None:
      self.set_behavior_tree(behavior_tree, name)

  def set_behavior_tree(
      self,
      behavior_tree: Union[Node, BehaviorTree],
      name: Optional[str] = None,
  ) -> SubTree:
    """Sets the subtree's behavior tree.

    Args:
      behavior_tree: behavior tree or root node of a tree for this subtree. If
        passing a root node you must also provide the name argument.
      name: name of the behavior tree, if behavior_tree is a node, i.e., a root
        node of a tree; otherwise, the name of this node.

    Returns:
      self for chaining.
    """
    self.name = name
    if isinstance(behavior_tree, BehaviorTree):
      self.behavior_tree = behavior_tree
    elif isinstance(behavior_tree, Node):
      if name is None:
        raise ValueError(
            'You must give a name when passing a root node for a tree.'
        )
      self.behavior_tree = BehaviorTree(name=name, root=behavior_tree)
    else:
      raise TypeError('Given behavior_tree is not a BehaviorTree.')
    return self

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    if not self.behavior_tree:
      return f'{type(self).__name__}({self._name_repr()})'
    else:
      return (
          f'{type(self).__name__}({self._name_repr()}'
          + f'behavior_tree={repr(self.behavior_tree)})'
      )

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Node:
    proto_object = super().proto
    if self.behavior_tree is None:
      raise ValueError(
          'A SubTree node has not been set. Please call '
          'sub_tree_node_instance.set_behavior_tree(tree_instance).'
      )
    # Do not validate sub-tree. Only top-level calls to BehaviorTree.proto
    # should trigger validation once.
    # pylint: disable=protected-access
    proto_object.sub_tree.tree.CopyFrom(
        self.behavior_tree._to_proto(validate=False)
    )
    return proto_object

  @utils.classproperty
  def node_type(cls) -> str:  # pylint:disable=no-self-argument
    return 'sub_tree'

  def has_child(self, node_id: int) -> bool:
    return False

  def remove_child(self, node_id: int) -> None:
    raise ValueError(
        'Subtree node does not support child removal, call on immediate parent'
        ' of node to remove.'
    )

  @classmethod
  def _create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree.SubtreeNode
  ) -> SubTree:
    return cls(behavior_tree=BehaviorTree.create_from_proto(proto_object.tree))

  def dot_graph(
      self,
      node_id_suffix: str = '',
      node_label: Optional[str] = None,
      name: Optional[str] = None,
  ) -> Tuple[graphviz.Digraph, str]:
    """Converts the given subtree into a graphviz dot graph.

    The edge goes from parent to the root node of the subtree.

    Args:
      node_id_suffix: A little string with the suffix to make the given node
        unique in the dot graph.
      node_label: The label is typically just the type of the node. To use a
        different value, this argument can be used.
      name: name of the node as set by the user.

    Returns:
      A tuple of graphviz dot graph representation of the full subtree and
      the name of the subtree's root node.
    """
    child_dot_graph = None
    child_node_name = ''
    if self.behavior_tree is not None and self.behavior_tree.root is not None:
      child_dot_graph, child_node_name = self.behavior_tree.root.dot_graph(
          node_id_suffix + '_0'
      )
    else:
      return super().dot_graph(node_id_suffix, node_label, name)

    box_dot_graph = _dot_wrap_in_box(
        child_graph=child_dot_graph,
        name=self.behavior_tree.name,
        label=self.behavior_tree.name,
    )
    return box_dot_graph, child_node_name


class Fail(Node):
  """A BT node of type Fail for behavior_tree_pb2.BehaviorTree.FailNode.

  A node that can be used to signal a failure. Most used to direct the control
  flow of execution, in combination with a failure handling strategy.

  Attributes:
    failure_message: A string that gives more information about the failure,
      mostly for the user's convenience. This will be set on the fail node's
      extended status on failure decorator. Prefer to set the decorator directly
      using node.on_failure.emit_extended_status(...). It is an error set both,
      the title of an extended status decorator and a failure message.
    proto: The proto representation of the node.
    node_type: A string label of the node type.
  """

  failure_message: str

  def __init__(
      self,
      failure_message: str = '',
      name: Optional[str] = None,
      node_id: int | None = None,
  ):
    super().__init__(name, node_id)
    self.failure_message = failure_message

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    rep = f'{type(self).__name__}({self._name_repr()}'
    if self.failure_message:
      rep += f'failure_message="{self.failure_message}"'
    rep += ')'
    return rep

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Node:
    proto_object = super().proto
    if self.failure_message:
      # pylint: disable=line-too-long
      if (
          proto_object.decorators.on_failure.emit_extended_status.extended_status.title
      ):
        raise ValueError(
            f'Fail node has failure_message ("{self.failure_message}") and'
            ' decorator to emit extended status with title'
            f' ("{proto_object.decorators.on_failure.emit_extended_status.extended_status.title}")'
            ' set. Only one can be set at a time. Prefer to use the decorator.'
        )
      proto_object.decorators.on_failure.emit_extended_status.extended_status.title = (
          self.failure_message
      )
    proto_object.fail.CopyFrom(behavior_tree_pb2.BehaviorTree.FailNode())
    return proto_object

  @utils.classproperty
  def node_type(cls) -> str:  # pylint:disable=no-self-argument
    return 'fail'

  def has_child(self, node_id: int) -> bool:
    return False

  def remove_child(self, node_id: int) -> None:
    raise ValueError('Fail node does not have children to remove')

  @classmethod
  def _create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree.FailNode
  ) -> Fail:
    """Creates a Fail node instance from a proto.

    Args:
      proto_object: Proto to read data from.

    Returns:
      Instance of Fail node with data from proto.
    """
    del proto_object
    return cls()

  def dot_graph(
      self,
      node_id_suffix: str = '',
      node_label: Optional[str] = None,
      name: Optional[str] = None,
  ) -> Tuple[graphviz.Digraph, str]:
    return super().dot_graph(node_id_suffix, node_label, name or self._name)


class Debug(Node):
  """A BT node of type Debug for behavior_tree_pb2.BehaviorTree.DebugNode.

  A node that can be used to suspend the tree. Using the optional suspend
  behavior the outcome of the debug node can be defined (success vs failure).

  Attributes:
    fail_on_resume: Describes whether the node should succeed or fail after
      resuming. Defaults to succeed.
    proto: The proto representation of the node.
    node_type: A string label of the node type.
  """

  def __init__(
      self,
      fail_on_resume: Optional[bool] = False,
      name: Optional[str] = None,
      node_id: int | None = None,
  ):
    super().__init__(name, node_id)
    self.fail_on_resume: Optional[bool] = fail_on_resume

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    rep = f'{type(self).__name__}({self._name_repr()}'
    if self.fail_on_resume:
      rep += f'fail_on_resume={self.fail_on_resume}'
    rep += ')'
    return rep

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Node:
    proto_object = super().proto
    proto_object.debug.suspend.fail_on_resume = self.fail_on_resume
    return proto_object

  @utils.classproperty
  def node_type(cls) -> str:  # pylint:disable=no-self-argument
    return 'debug'

  def has_child(self, node_id: int) -> bool:
    return False

  def remove_child(self, node_id: int) -> None:
    raise ValueError('Debug node does not have children to remove')

  @classmethod
  def _create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree.DebugNode
  ) -> Debug:
    return cls(proto_object.suspend.fail_on_resume)

  def dot_graph(
      self,
      node_id_suffix: str = '',
      node_label: Optional[str] = None,
      name: Optional[str] = None,
  ) -> Tuple[graphviz.Digraph, str]:
    return super().dot_graph(node_id_suffix, node_label, name or self._name)


class NodeWithChildren(Node):
  """A parent class for any behavior tree node that has self.children.

  Attributes:
    children: The list of child nodes of the given node.
    proto: The proto representation of the node.
  """

  children: list[Node]

  def __init__(
      self,
      children: SequenceType[Node | ActionBase | CodeExecution] | None,
      name: str | None,
      node_id: int | None,
  ):
    super().__init__(name, node_id)
    if not children:
      self.children = []
    else:
      # pytype: disable=annotation-type-mismatch,always-use-return-annotations
      self.children = [_transform_to_optional_node(x) for x in children]

  def set_children(self, *children: Node) -> Node:
    if isinstance(children[0], list):
      # pytype: disable=annotation-type-mismatch,always-use-return-annotations
      self.children = [_transform_to_optional_node(x) for x in children[0]]
    else:
      # pytype: disable=annotation-type-mismatch,always-use-return-annotations
      self.children = [_transform_to_optional_node(x) for x in children]

    return self

  def has_child(self, node_id: int) -> bool:
    return any(child.node_id == node_id for child in self.children)

  def remove_child(self, node_id: int) -> None:
    for i, child in enumerate(self.children):
      if child.node_id == node_id:
        del self.children[i]
        break

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    representation = f'{type(self).__name__}({self._name_repr()}children=['
    representation += ', '.join(map(str, self.children))
    representation += '])'
    return representation

  def dot_graph(
      self,
      node_id_suffix: str = '',
      node_label: Optional[str] = None,
      name: Optional[str] = None,
  ) -> Tuple[graphviz.Digraph, str]:
    dot_graph, node_name = super().dot_graph(
        node_id_suffix, node_label, name or self.name
    )
    _dot_append_children(dot_graph, node_name, self.children, node_id_suffix, 0)
    label_to_use = name or self.name or ''
    box_dot_graph = _dot_wrap_in_box(
        child_graph=dot_graph,
        name=label_to_use + node_id_suffix,
        label=label_to_use,
    )
    return box_dot_graph, node_name


class Sequence(NodeWithChildren):
  """A BT node of type Sequence.

  Represented in the proto as behavior_tree_pb2.BehaviorTree.SequenceNode.

  The child nodes are executed sequentially. If any of the children fail,
  the node fails. If all the children succeed, the node succeeds.

  Attributes:
    children: The list of child nodes of the given node, inherited from the
      parent class.
    proto: The proto representation of the node.
    node_type: A string label of the node type.
  """

  def __init__(
      self,
      children: SequenceType[Node | ActionBase | CodeExecution] | None = None,
      name: str | None = None,
      *,
      node_id: int | None = None,
  ):
    super().__init__(children, name, node_id)

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Node:
    proto_object = super().proto
    if self.children:
      for child in self.children:
        proto_object.sequence.children.append(child.proto)
    else:
      proto_object.sequence.CopyFrom(
          behavior_tree_pb2.BehaviorTree.SequenceNode()
      )
    return proto_object

  @utils.classproperty
  def node_type(cls) -> str:  # pylint:disable=no-self-argument
    return 'sequence'

  @classmethod
  def _create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree.SequenceNode
  ) -> Sequence:
    node = cls()
    for child_node_proto in proto_object.children:
      node.children.append(Node.create_from_proto(child_node_proto))
    return node


class Parallel(NodeWithChildren):
  """BT node of type Parallel for behavior_tree_pb2.BehaviorTree.ParallelNode.

  The child nodes are all executed in parallel. Once all the children finish
  successfully, the node succeeds as well. If any of the children fail, the
  node fails.

  Attributes:
    children: The list of child nodes of the given node, inherited from the
      parent class.
    proto: The proto representation of the node.
    node_type: A string label of the node type.
  """

  def __init__(
      self,
      children: SequenceType[Node | ActionBase | CodeExecution] | None = None,
      name: str | None = None,
      *,
      node_id: int | None = None,
  ):
    super().__init__(children, name, node_id)

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Node:
    proto_object = super().proto
    if self.children:
      for child in self.children:
        proto_object.parallel.children.append(child.proto)
    else:
      proto_object.parallel.CopyFrom(
          behavior_tree_pb2.BehaviorTree.ParallelNode()
      )

    return proto_object

  @utils.classproperty
  def node_type(cls) -> str:  # pylint:disable=no-self-argument
    return 'parallel'

  @classmethod
  def _create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree.ParallelNode
  ) -> Parallel:
    node = cls()
    for child_node_proto in proto_object.children:
      node.children.append(Node.create_from_proto(child_node_proto))
    return node


class Selector(NodeWithChildren):
  """BT node of type Selector for behavior_tree_pb2.BehaviorTree.SelectorNode.

  The child nodes get executed in a sequence until any one of them succeeds.
  That is, first, the first child is executed, if that one fails, the next one
  is executed, and so on. Once any of the children succeed, the node succeeds.
  If all the children fail, the node fails.

  Attributes:
    branches: The list of selector branches, i.e., children with conditions.
    children: The list of child nodes of the given node, inherited from the
      parent class.
    proto: The proto representation of the node.
    node_type: A string label of the node type.
  """

  @dataclasses.dataclass
  class Branch:
    """Represents a SelectorNode.Branch."""

    condition: Condition | None
    node: Node

    @property
    def proto(
        self,
    ) -> behavior_tree_pb2.BehaviorTree.SelectorNode.Branch:
      return behavior_tree_pb2.BehaviorTree.SelectorNode.Branch(
          condition=self.condition.proto
          if self.condition is not None
          else None,
          node=self.node.proto,
      )

    @classmethod
    def _create_from_proto(
        cls,
        proto_object: behavior_tree_pb2.BehaviorTree.SelectorNode.Branch,
    ) -> Selector.Branch:
      node = cls(
          condition=Condition.create_from_proto(proto_object.condition)
          if proto_object.HasField('condition')
          else None,
          node=Node.create_from_proto(proto_object.node),
      )
      return node

  # Either the children: list[Node] from the super class or this must be filled,
  # but never both
  branches: list[Selector.Branch]

  def __init__(
      self,
      branches: (
          SequenceType[Union[Node, ActionBase, CodeExecution, Selector.Branch]]
          | None
      ) = None,
      name: str | None = None,
      *,
      children: SequenceType[Node | ActionBase | CodeExecution] | None = None,
      node_id: int | None = None,
  ):
    if branches and children:
      raise solutions_errors.InvalidArgumentError(
          'Either branches or children can be set, but not both.'
      )
    node_children = []
    node_branches = []
    if branches is not None:
      for branch_child in branches:
        if isinstance(branch_child, Selector.Branch):
          node_branches.append(branch_child)
        else:
          node_children.append(branch_child)
    if children is not None:
      node_children = children

    if node_children and node_branches:
      raise TypeError(
          'The children passed to a SelectorNode must either all be all'
          ' Selector.Branch or all Nodes, but not mixed.'
      )

    if node_children:
      super().__init__(children=node_children, name=name, node_id=node_id)
      self.branches = []
      print(
          'Passing nodes or skills directly to a selector is deprecated. Use a'
          ' Selector.Branch that contains a node and its condition explicitly.'
      )
    else:
      super().__init__(children=[], name=name, node_id=node_id)
      self.branches = node_branches

  def set_children(self, *children: Node) -> Node:
    self.branches = []
    return super().set_children(*children)

  def set_branches(self, *branches: Selector.Branch) -> Node:
    if isinstance(branches[0], list):
      self.branches = branches[0]
    else:
      self.branches = list(branches)
    self.children = []
    return self

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    representation = f'{type(self).__name__}({self._name_repr()}'
    if self.children:
      representation += 'children=['
      representation += ', '.join(map(str, self.children))
      representation += ']'
    if self.branches or not self.children:
      representation += 'branches=['
      representation += ', '.join(map(str, self.branches))
      representation += ']'
    representation += ')'
    return representation

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Node:
    proto_object = super().proto
    if self.branches:
      for branch in self.branches:
        proto_object.selector.branches.append(branch.proto)
    elif self.children:
      for child in self.children:
        proto_object.selector.children.append(child.proto)
    else:
      proto_object.selector.CopyFrom(
          behavior_tree_pb2.BehaviorTree.SelectorNode()
      )
    return proto_object

  @utils.classproperty
  def node_type(cls) -> str:  # pylint:disable=no-self-argument
    return 'selector'

  @classmethod
  def _create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree.SelectorNode
  ) -> Selector:
    """Creates a SelectorNode from a proto."""
    node = cls()
    if proto_object.children and proto_object.branches:
      raise ValueError(
          'The selector proto contains children and branches. Only one is'
          f' valid: {proto_object}'
      )
    for child_node_proto in proto_object.children:
      node.children.append(Node.create_from_proto(child_node_proto))
    for branch_node_proto in proto_object.branches:
      node.branches.append(
          Selector.Branch._create_from_proto(branch_node_proto)  # pylint:disable=protected-access
      )
    return node

  def dot_graph(
      self,
      node_id_suffix: str = '',
      node_label: Optional[str] = None,
      name: Optional[str] = None,
  ) -> Tuple[graphviz.Digraph, str]:
    dot_graph, node_name = Node.dot_graph(
        self, node_id_suffix, node_label, name or self.name
    )
    if self.children:
      _dot_append_children(
          dot_graph, node_name, self.children, node_id_suffix, 0
      )
    elif self.branches:
      branch_children = [b.node for b in self.branches]
      _dot_append_children(
          dot_graph, node_name, branch_children, node_id_suffix, 0
      )
    label_to_use = name or self.name or ''
    box_dot_graph = _dot_wrap_in_box(
        child_graph=dot_graph,
        name=label_to_use + node_id_suffix,
        label=label_to_use,
    )
    return box_dot_graph, node_name


class Retry(Node):
  """BT node of type Retry for behavior_tree_pb2.BehaviorTree.RetryNode.

  Runs the child node and retries if the child fails. After the given number
  of retries, the failure gets propagated up.

  Attributes:
    child: The child node of this node that is to be retried upon failure.
    recovery: An optional sub-tree that is executed if the child fails and there
      are still tries left to be performed. If the recovery fails, the retry
      node will fail immediately irrespective of the number of tries left.
    max_tries: Maximal number of times to execute the child before propagating
      the failure up.
    proto: The proto representation of the node.
    node_type: A string label of the node type.
    retry_counter: The key to access the retry counter on the blackboard, only
      available while inside the retry node.
  """

  child: Optional[Node]
  recovery: Optional[Node]
  max_tries: int

  def __init__(
      self,
      max_tries: int = 0,
      child: Node | ActionBase | CodeExecution | None = None,
      recovery: Node | ActionBase | CodeExecution | None = None,
      name: str | None = None,
      retry_counter_key: str | None = None,
      *,
      node_id: int | None = None,
  ):
    super().__init__(name, node_id)
    self.child = _transform_to_optional_node(child)
    self.recovery = _transform_to_optional_node(recovery)
    self.max_tries = max_tries
    self._retry_counter_key = retry_counter_key or 'retry_counter_' + str(
        uuid.uuid4()
    ).replace('-', '_')

  def set_child(self, child: Node | ActionBase | CodeExecution) -> Retry:
    self.child = _transform_to_optional_node(child)
    return self

  def has_child(self, node_id: int) -> bool:
    return self.child.node_id == node_id

  def remove_child(self, node_id: int) -> None:
    if self.child is None:
      raise ValueError('Retry node has no child set')

    if self.child.node_id != node_id:
      raise ValueError(
          f"Retry node's child has different ID {self.child.node_id} (expected"
          f' {node_id}'
      )

    self.child = None

  def set_recovery(self, recovery: Node | ActionBase | CodeExecution) -> Retry:
    self.recovery = _transform_to_optional_node(recovery)
    return self

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    recovery_str = f', recovery={str(self.recovery)}'
    return (
        f'{type(self).__name__}({self._name_repr()}max_tries={self.max_tries},'
        f' child={str(self.child)}{recovery_str})'
    )

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Node:
    proto_object = super().proto
    proto_object.retry.max_tries = self.max_tries
    if self.child is None:
      raise ValueError(
          'A Retry node has to have a child node but currently '
          'it is not set. Please call '
          'retry_node_instance.set_child(bt_node_instance).'
      )
    proto_object.retry.child.CopyFrom(self.child.proto)
    if self.recovery is not None:
      proto_object.retry.recovery.CopyFrom(self.recovery.proto)
    proto_object.retry.retry_counter_blackboard_key = self._retry_counter_key
    return proto_object

  @property
  def retry_counter(self) -> blackboard_value.BlackboardValue:
    return blackboard_value.BlackboardValue(
        fields=wrappers_pb2.Int64Value.DESCRIPTOR.fields_by_name,
        name=self._retry_counter_key,
        toplevel_value_type=wrappers_pb2.Int64Value,
        parent=None,
    )

  @utils.classproperty
  def node_type(cls) -> str:  # pylint:disable=no-self-argument
    return 'retry'

  @classmethod
  def _create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree.RetryNode
  ) -> Retry:
    retry = cls(
        max_tries=proto_object.max_tries,
        child=Node.create_from_proto(proto_object.child),
    )
    if proto_object.HasField('recovery'):
      retry.recovery = Node.create_from_proto(proto_object.recovery)
    retry._retry_counter_key = proto_object.retry_counter_blackboard_key
    return retry

  def dot_graph(
      self,
      node_id_suffix: str = '',
      node_label: Optional[str] = None,
      name: Optional[str] = None,
  ) -> Tuple[graphviz.Digraph, str]:
    label_to_use = node_label or ('retry ' + str(self.max_tries))
    name_to_use = name or self._name
    dot_graph, node_name = super().dot_graph(
        node_id_suffix, label_to_use, name_to_use
    )
    if self.child is not None:
      _dot_append_child(
          dot_graph, node_name, self.child, node_id_suffix + '_child'
      )
    if self.recovery is not None:
      _dot_append_child(
          dot_graph,
          node_name,
          self.recovery,
          node_id_suffix + '_recovery',
          edge_label='Recovery',
      )
    box_label = name_to_use or ''
    box_dot_graph = _dot_wrap_in_box(
        child_graph=dot_graph,
        name=box_label + node_id_suffix,
        label=box_label,
    )
    return box_dot_graph, node_name


class Fallback(Node):
  """BT node of type Fallback for behavior_tree_pb2.BehaviorTree.FallbackNode.

  A fallback node will try a number of actions until one succeeds, or all
  fail. It can be used to implement trees that try a number of options.

  Attributes:
    tries: The list of tries (child nodes with optional condition) of the
      fallback node.
    proto: The proto representation of the node.
    node_type: A string label of the node type.
  """

  @dataclasses.dataclass
  class Try:
    """Represents a FallbackNode.Try."""

    condition: Condition | None
    node: Node

    @property
    def proto(
        self,
    ) -> behavior_tree_pb2.BehaviorTree.FallbackNode.Try:
      return behavior_tree_pb2.BehaviorTree.FallbackNode.Try(
          condition=self.condition.proto
          if self.condition is not None
          else None,
          node=self.node.proto,
      )

    @classmethod
    def _create_from_proto(
        cls,
        proto_object: behavior_tree_pb2.BehaviorTree.FallbackNode.Try,
    ) -> Fallback.Try:
      node = cls(
          condition=Condition.create_from_proto(proto_object.condition)
          if proto_object.HasField('condition')
          else None,
          node=Node.create_from_proto(proto_object.node),
      )
      return node

  # Note that the FallbackNode does NOT have a children member. Even when
  # children are passed to the constructor or set via set_children() they are
  # automatically converted to tries.
  _tries: list[Fallback.Try]

  def __init__(
      self,
      children: list[Node | ActionBase | CodeExecution] | None = None,
      name: str | None = None,
      *,
      tries: (
          list[Node | ActionBase | CodeExecution | Fallback.Try] | None
      ) = None,
      node_id: int | None = None,
  ):
    super().__init__(name, node_id)
    self._tries = []
    if children is not None and tries is not None:
      raise ValueError(
          'The fallback cannot be initialized with both, children and tries, at'
          ' the same time.'
      )
    if tries is not None:
      for try_node in tries:
        if isinstance(try_node, Fallback.Try):
          self._tries.append(try_node)
        else:
          self._tries.append(
              Fallback.Try(condition=None, node=_transform_to_node(try_node))
          )

    if children is not None:
      for child in children:
        self._tries.append(
            Fallback.Try(condition=None, node=_transform_to_node(child))
        )

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    representation = f'{type(self).__name__}({self._name_repr()}tries=['
    representation += ', '.join(map(str, self.tries))
    representation += '])'
    return representation

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Node:
    proto_object = super().proto
    proto_object.fallback.CopyFrom(
        behavior_tree_pb2.BehaviorTree.FallbackNode()
    )
    for try_node in self._tries:
      proto_object.fallback.tries.append(try_node.proto)
    return proto_object

  @utils.classproperty
  def node_type(cls) -> str:  # pylint:disable=no-self-argument
    return 'fallback'

  @classmethod
  def _create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree.FallbackNode
  ) -> Fallback:
    """Instantiates class from a given FallbackNode proto.

    Args:
      proto_object: Proto of type FallbackNode to parse.

    Returns:
      Fallback class instance.

    Raises:
      ValueError: On invalid proto, in particular one that has children and
        tries set.
    """
    node = cls()
    if proto_object.children and proto_object.tries:
      raise ValueError(
          'The fallback proto contains children and tries. Only one is'
          f' valid: {proto_object}'
      )
    for child_node_proto in proto_object.children:
      node._tries.append(
          Fallback.Try(
              condition=None, node=Node.create_from_proto(child_node_proto)
          )
      )
    for try_node_proto in proto_object.tries:
      node._tries.append(  # pylint:disable=protected-access
          Fallback.Try._create_from_proto(try_node_proto)  # pylint:disable=protected-access
      )
    return node

  def set_children(self, *children: Node) -> Node:
    """Sets tries from regular nodes.

    This enables some compatibility with NodeWithChildren so that children can
    be set as nodes. They will be converted into tries without condition.

    Args:
      *children: Children to add/convert, either a list or variadic elements.

    Returns:
      self
    """
    if not children:
      return self

    if isinstance(children[0], list):
      self._tries = [
          Fallback.Try(condition=None, node=_transform_to_node(x))
          for x in children[0]
      ]
    else:
      self._tries = [
          Fallback.Try(condition=None, node=_transform_to_node(x))
          for x in children
      ]

    return self

  def has_child(self, node_id: int) -> bool:
    return any(child.node.node_id == node_id for child in self.tries)

  def remove_child(self, node_id: int) -> None:
    for i, child in enumerate(self._tries):
      if child.node.node_id == node_id:
        del self._tries[i]
        break

  @property
  def tries(self) -> list[Fallback.Try]:
    return self._tries

  def dot_graph(
      self,
      node_id_suffix: str = '',
      node_label: Optional[str] = None,
      name: Optional[str] = None,
  ) -> Tuple[graphviz.Digraph, str]:
    dot_graph, node_name = Node.dot_graph(
        self, node_id_suffix, node_label, name or self.name
    )
    if self.tries:
      try_nodes = [b.node for b in self.tries]
      _dot_append_children(dot_graph, node_name, try_nodes, node_id_suffix, 0)
    label_to_use = name or self.name or ''
    box_dot_graph = _dot_wrap_in_box(
        child_graph=dot_graph,
        name=label_to_use + node_id_suffix,
        label=label_to_use,
    )
    return box_dot_graph, node_name


class Loop(Node):
  """BT node of type Loop for behavior_tree_pb2.BehaviorTree.LoopNode.

  The loop node provides the ability to run a subtree repeatedly. It supports
  different bounding conditions: run until failure, run while a condition
  holds (while loop), or run a maximum number of times (for loops with break
  on error).

  When selected and a `while` condition is set, the condition is immediately
  evaluated. If it is satisfied, or if no `while` condition is given, the `do`
  child is executed. If `max_times` is not given or zero, the parameter is
  ignored.

  Additionally, if no `while` condition is added, the loop will run
  indefinitely until the child `do` child fails (taking on the semantics of a
  for-loop). If `max_times` is set, the loop will end after the given number
  of iterations, or if the `do` child fails.

  Attributes:
    do_child: The child node of this node that is to be run repeatedly.
    max_times: Maximal number of times to execute the child.
    while_condition: condition which indicates whether do should be executed.
    proto: The proto representation of the node.
    node_type: A string label of the node type.
    loop_counter: The key to access the loop counter on the blackboard, only
      available while inside the loop.
    for_each_protos: List of pre-defined protos to iterate over.
    for_each_value_key: The key to access the current value on the blackboard
      during for each loops.
    for_each_value: BlackboardValue that refers to the current iteration value.
      Only available when for_each_generator_cel_expression was set from a
      BlackboardValue via set_for_each_generator.
    for_each_generator_cel_expression: CEL expression to generate a list of
      protos. The loop iterates over the result of this list.
  """

  _for_each_value_key: Optional[str]
  _for_each_value: Optional[blackboard_value.BlackboardValue]
  _for_each_protos: Optional[List[protobuf_message.Message]]
  _for_each_generator_cel_expression: Optional[str]

  def __init__(
      self,
      max_times: int = 0,
      do_child: Node | ActionBase | CodeExecution | None = None,
      while_condition: Optional[Condition] = None,
      name: Optional[str] = None,
      loop_counter_key: Optional[str] = None,
      *,
      for_each_value_key: Optional[str] = None,
      for_each_protos: Optional[
          List[Union[protobuf_message.Message, skill_utils.MessageWrapper]]
      ] = None,
      for_each_generator_cel_expression: Optional[str] = None,
      node_id: int | None = None,
  ):
    super().__init__(name, node_id)

    self.do_child: Optional[Node] = _transform_to_optional_node(do_child)
    self.max_times: int = max_times
    self.while_condition: Optional[Condition] = while_condition
    self._loop_counter_key = loop_counter_key or 'loop_counter_' + str(
        uuid.uuid4()
    ).replace('-', '_')
    self._for_each_value_key = for_each_value_key
    self._for_each_value = None
    self._for_each_protos = Loop._for_each_proto_input_to_protos(
        for_each_protos
    )
    self._for_each_generator_cel_expression = for_each_generator_cel_expression
    if (
        self._for_each_protos is not None
        or self._for_each_generator_cel_expression is not None
    ):
      self._ensure_for_each_value_key()
    self._check_consistency()

    if (
        self._for_each_protos is not None
        or self._for_each_generator_cel_expression is not None
    ):
      print(
          'For each loops have been deprecated. Prefer to use a while loop'
          ' instead.'
      )

  def set_do_child(self, do_child: Node | ActionBase | CodeExecution) -> Loop:
    self.do_child = _transform_to_optional_node(do_child)
    return self

  def has_child(self, node_id: int) -> bool:
    return self.do_child.node_id == node_id

  def remove_child(self, node_id: int) -> None:
    if self.do_child is None:
      raise ValueError('Loop node has no do child set')

    if self.do_child.node_id != node_id:
      raise ValueError(
          "Loop node's do child has different ID"
          f' {self.do_child.node_id} (expected {node_id}'
      )

    self.do_child = None

  def set_while_condition(self, while_condition: Condition) -> Loop:
    """Sets the while condition for the loop.

    Setting it will make this loop node work as a while loop.

    Args:
      while_condition: The condition to set.

    Returns:
      The modified loop node.
    """
    self.while_condition = while_condition
    self._check_consistency()
    return self

  def set_for_each_value_key(self, key: Optional[str]) -> Loop:
    """Sets the blackboard key for the current value of a for each loop.

    Setting it anything other than 'None', will make this loop node work as a
    for each loop.

    Args:
      key: The blackboard key to set. Use 'None' to unset this property.

    Returns:
      The modified loop node.
    """
    self._for_each_value_key = key
    if self._for_each_value is not None:
      self._for_each_value.set_root_value_access_path(key)
    self._check_consistency()
    return self

  def set_for_each_protos(
      self,
      protos: Optional[
          List[
              Union[
                  protobuf_message.Message,
                  skill_utils.MessageWrapper,
                  object_world_resources.WorldObject,
                  object_world_resources.Frame,
              ]
          ]
      ],
  ) -> Loop:
    """Sets the messages to iterate over in a for each loop.

    The proto messages are packed into Any protos when the loop node is
    represented as a proto unless they are already an Any proto, in which case
    they are taken as is.
    Setting this make the loop node work as a for each loop.

    Args:
      protos: A list of protos to iterate over. If the list contains
        WorldObjects or Frames these are converted to a proto referencing the
        WorldObject or Frame.

    Returns:
      The modified loop node.
    """
    warnings.warn(
        'For each loops have been deprecated. Prefer to use a while loop'
        ' instead.',
        DeprecationWarning,
        stacklevel=2,
    )
    self._for_each_protos = Loop._for_each_proto_input_to_protos(protos)
    self._check_consistency()
    self._ensure_for_each_value_key()
    return self

  def set_for_each_generator(
      self, generator_value: blackboard_value.BlackboardValue
  ) -> Loop:
    """Sets the value to generate protos from to loop over in a for each loop.

    The passed in value must refer to a list of protos to iterate over in this
    for each loop. A common example is a repeated field in a skill result.
    When the loop node is selected for execution the list of protos is copied
    from the referred value and then the loop node is cycled for each of the
    values in the list.
    The value can also result in an AnyList proto, in which case the loop node
    iterates over each entry in the AnyList items field.
    Setting it anything other than 'None', will make this loop node work as a
    for each loop.

    Args:
      generator_value: The value to iterate over.

    Returns:
      The modified loop node.
    """
    warnings.warn(
        'For each loops have been deprecated. Prefer to use a while loop'
        ' instead.',
        DeprecationWarning,
        stacklevel=2,
    )
    self.set_for_each_generator_cel_expression(
        generator_value.value_access_path()
    )
    self._for_each_value = generator_value[0]
    self._for_each_value.set_root_value_access_path(self._for_each_value_key)
    return self

  def set_for_each_generator_cel_expression(
      self, cel_expression: Optional[str]
  ) -> Loop:
    """Sets the CEL expression to generate protos for a for each loop.

    When this loop node is selected for execution this CEL expression will be
    evaluated and it must either result in a list of protos to iterate over or
    an AnyList proto.
    Setting it anything other than 'None', will make this loop node work as a
    for each loop.

    Args:
      cel_expression: The expression to generate protos to loop over.

    Returns:
      The modified loop node.
    """
    warnings.warn(
        'For each loops have been deprecated. Prefer to use a while loop'
        ' instead.',
        DeprecationWarning,
        stacklevel=2,
    )
    self._for_each_generator_cel_expression = cel_expression
    self._check_consistency()
    self._ensure_for_each_value_key()
    return self

  @classmethod
  def _for_each_proto_input_to_protos(
      cls,
      inputs: Optional[
          List[
              Union[
                  protobuf_message.Message,
                  skill_utils.MessageWrapper,
                  object_world_resources.WorldObject,
                  object_world_resources.Frame,
              ]
          ]
      ],
  ) -> Optional[List[protobuf_message.Message]]:
    """Converts a list of possible inputs for the protos list to protos.

    For each loop nodes can only iterate over protos. It is often convenient
    to accept things like WorldObjects directly without the user having to
    convert this into a reference proto manually. Also protos in the solutions
    API are represented by MessageWrapper objects that are not directly a
    proto, but contain/wrap one.
    This functions performs the necessary conversions to proto when necessary.

    Args:
      inputs: List of values that can either be a proto message directly or a
        MessageWrapper. WorldObjects and Frames are also accepted and
        automatically converted to a proto referencing the WorldObject or Frame.

    Returns:
      A list of protos converted from the list of mixed inputs.
    """
    if inputs is None:
      return None
    if not inputs:
      raise solutions_errors.InvalidArgumentError(
          'Loop for_each protos cannot be empty'
      )
    protos: List[protobuf_message.Message] = []
    for value in inputs:
      if isinstance(value, protobuf_message.Message):
        protos.append(value)
      elif isinstance(value, skill_utils.MessageWrapper):
        if value.wrapped_message is not None:
          # pytype: disable=container-type-mismatch
          protos.append(value.wrapped_message)
      elif isinstance(value, object_world_resources.WorldObject):
        wo: object_world_resources.WorldObject = value
        protos.append(wo.reference)
      elif isinstance(value, object_world_resources.Frame):
        frame: object_world_resources.Frame = value
        protos.append(frame.reference)
      else:
        raise solutions_errors.InvalidArgumentError(
            f'Cannot set for_each proto "{str(value)}". Only protos or world'
            ' objects or frames are supported.'
        )
    return protos  # pytype: disable=bad-return-type

  def _ensure_for_each_value_key(self):
    """Ensures that a _for_each_value_key is present.

    If a key is already set, nothing is done. Otherwise a key with a random
    UUID is created.
    """
    if not self._for_each_value_key:
      self._for_each_value_key = 'for_each_value_' + str(uuid.uuid4()).replace(
          '-', '_'
      )

  def validate(self):
    """Validates the current loop node.

    Checks that the loop node is properly defined, i.e., there are no
    inconsistent properties set and all required fields are set.

    Raises:
      InvalidArgumentError: raised if the node is in a state that cannot be
        converted to a valid proto.
    """
    self._check_consistency()
    if (
        self._for_each_value_key is not None
        and self._for_each_generator_cel_expression is None
        and self._for_each_protos is None
    ):
      raise solutions_errors.InvalidArgumentError(
          'Loop node defines a for_each_value_key, but no way to generate'
          ' values to iterate over. Set either for_each_protos or'
          ' for_each_generator_cel_expression.'
      )

  def _check_consistency(self):
    """Checks necessary invariants of the loop node.

    This function only determines if fields are set that are inconsistent with
    each other, but not if all required fields are set.

    Raises:
      InvalidArgumentError: raised if the node is in a state that cannot be
        converted to a valid proto.
    """
    for_each_set_fields = []
    if self._for_each_value_key is not None:
      for_each_set_fields.append('for_each_value_key')
    if self._for_each_protos is not None:
      for_each_set_fields.append('for_each_protos')
    if self._for_each_generator_cel_expression is not None:
      for_each_set_fields.append('for_each_generator_cel_expression')
    if for_each_set_fields and self.while_condition is not None:
      set_fields_str = ', '.join(for_each_set_fields)
      raise solutions_errors.InvalidArgumentError(
          'Loop node defines for each properties'
          f' ({set_fields_str}) and a while condition. Only'
          ' one of these can be set at a time.'
      )
    if (
        self._for_each_protos is not None
        and self._for_each_generator_cel_expression is not None
    ):
      raise solutions_errors.InvalidArgumentError(
          'Loop node with for each defines both for_each_protos and a'
          ' for_each_generator_cel_expression. Exactly one must be defined.'
      )

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    representation = f'{type(self).__name__}'
    if self._for_each_generator_cel_expression is not None:
      representation += f' over {self._for_each_generator_cel_expression}'
    if self._for_each_protos is not None:
      representation += f' over {len(self._for_each_protos)} protos'
    representation += f'({self._name_repr()}'
    if self.while_condition is not None:
      representation += f'while_condition={repr(self.while_condition)}, '
    if self.max_times != 0:
      representation += f'max_times={self.max_times}, '
    representation += f'do_child={repr(self.do_child)})'
    return representation

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Node:
    self.validate()
    proto_object = super().proto
    if self.while_condition is not None:
      condition = getattr(proto_object.loop, 'while')
      condition.CopyFrom(self.while_condition.proto)
    if self._for_each_value_key is not None:
      proto_object.loop.for_each.value_blackboard_key = self._for_each_value_key
    if self._for_each_generator_cel_expression is not None:
      proto_object.loop.for_each.generator_cel_expression = (
          self._for_each_generator_cel_expression
      )
    if self._for_each_protos:
      any_list = getattr(proto_object.loop.for_each, 'protos')
      for proto in self._for_each_protos:
        if proto.DESCRIPTOR.full_name == 'google.protobuf.Any':
          any_proto = any_list.items.add()
          any_proto.CopyFrom(proto)
        else:
          any_proto = any_list.items.add()
          any_proto.Pack(proto)

    proto_object.loop.max_times = self.max_times
    if self.do_child is None:
      raise ValueError(
          'A Loop node has to have a do child node but currently '
          'it is not set. Please call '
          'loop_node_instance.set_do_child(bt_node_instance).'
      )
    proto_object.loop.do.CopyFrom(self.do_child.proto)
    proto_object.loop.loop_counter_blackboard_key = self._loop_counter_key
    return proto_object

  @property
  def loop_counter(self) -> blackboard_value.BlackboardValue:
    return blackboard_value.BlackboardValue(
        fields=wrappers_pb2.Int64Value.DESCRIPTOR.fields_by_name,
        name=self._loop_counter_key,
        toplevel_value_type=wrappers_pb2.Int64Value,
        parent=None,
    )

  @property
  def for_each_value_key(self) -> Optional[str]:
    return self._for_each_value_key

  @property
  def for_each_value(self) -> blackboard_value.BlackboardValue:
    if self._for_each_value is None:
      raise ValueError(
          'for_each_value is only available, when the for each loop was'
          ' configured with set_for_each_generator().'
      )
    return self._for_each_value

  @property
  def for_each_protos(self) -> Optional[List[protobuf_message.Message]]:
    return self._for_each_protos

  @property
  def for_each_generator_cel_expression(self) -> Optional[str]:
    return self._for_each_generator_cel_expression

  @utils.classproperty
  def node_type(cls) -> str:  # pylint:disable=no-self-argument
    return 'loop'

  @classmethod
  def _create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree.LoopNode
  ) -> Loop:
    """Created a Loop node class from a LoopNode proto."""
    condition = None
    if proto_object.HasField('while'):
      condition = Condition.create_from_proto(getattr(proto_object, 'while'))

    for_each_value_key = None
    for_each_protos = None
    for_each_generator_cel_expression = None
    if proto_object.HasField('for_each'):
      for_each_field = proto_object.for_each
      if for_each_field.value_blackboard_key:
        for_each_value_key = for_each_field.value_blackboard_key
      if for_each_field.HasField('protos'):
        for_each_protos = [proto for proto in for_each_field.protos.items]
      if (
          for_each_field.HasField('generator_cel_expression')
          and for_each_field.generator_cel_expression
      ):
        for_each_generator_cel_expression = (
            for_each_field.generator_cel_expression
        )

    loop = cls(
        max_times=proto_object.max_times,
        do_child=Node.create_from_proto(proto_object.do),
        while_condition=condition,
        loop_counter_key=proto_object.loop_counter_blackboard_key,
        for_each_value_key=for_each_value_key,
        for_each_protos=for_each_protos,
        for_each_generator_cel_expression=for_each_generator_cel_expression,
    )
    return loop

  def dot_graph(
      self,
      node_id_suffix: str = '',
      node_label: Optional[str] = None,
      name: Optional[str] = None,
  ) -> Tuple[graphviz.Digraph, str]:
    if node_label is None:
      label = 'loop'
      if self.max_times:
        label += ' ' + str(self.max_times)
      if self.while_condition:
        label += ' + while condition'
      if (
          self._for_each_generator_cel_expression is not None
          or self._for_each_protos is not None
      ):
        label += ' + for_each'
      node_label = label

    dot_graph, node_name = super().dot_graph(
        node_id_suffix, node_label, name or self._name
    )
    if self.do_child is not None:
      _dot_append_child(
          dot_graph, node_name, self.do_child, node_id_suffix + '_0'
      )

    label_to_use = name or self._name or ''
    box_dot_graph = _dot_wrap_in_box(
        child_graph=dot_graph,
        name=label_to_use + node_id_suffix,
        label=label_to_use,
    )
    return box_dot_graph, node_name


class Branch(Node):
  """BT node of type Branch for behavior_tree_pb2.BehaviorTree.BranchNode.

  A branch node has a condition and two designated children. On selection, it
  evaluates the condition. If the condition is satisfied, the `then` child is
  selected, otherwise the `else` child is selected.

  Attributes:
    if_condition: condition which indicates which child should be executed.
    then_child: Child to execute if the condition succeeds.
    else_child: Child to execute if the condition fails.
    proto: The proto representation of the node.
    node_type: A string label of the node type.
  """

  def __init__(
      self,
      if_condition: Condition | None = None,
      then_child: Node | ActionBase | CodeExecution | None = None,
      else_child: Node | ActionBase | CodeExecution | None = None,
      name: str | None = None,
      *,
      node_id: int | None = None,
  ):
    super().__init__(name, node_id)
    self.then_child: Optional[Node] = _transform_to_optional_node(then_child)
    self.else_child: Optional[Node] = _transform_to_optional_node(else_child)
    self.if_condition: Optional[Condition] = if_condition

  def set_then_child(
      self, then_child: Node | ActionBase | CodeExecution
  ) -> Branch:
    self.then_child = _transform_to_optional_node(then_child)
    return self

  def set_else_child(
      self, else_child: Node | ActionBase | CodeExecution
  ) -> Branch:
    self.else_child = _transform_to_optional_node(else_child)
    return self

  def set_if_condition(self, if_condition: Condition) -> Branch:
    self.if_condition = if_condition
    return self

  def has_child(self, node_id: int) -> bool:
    return (
        self.then_child is not None and self.then_child.node_id == node_id
    ) or (self.else_child is not None and self.else_child.node_id == node_id)

  def remove_child(self, node_id: int) -> None:
    if self.then_child is not None and self.then_child.node_id == node_id:
      self.then_child = None
      return

    if self.else_child is not None and self.else_child.node_id == node_id:
      self.else_child = None
      return

    raise ValueError(
        "Branch node's then and else children have different ID"
        f' {self.then_child.node_id} and {self.else_child.node_id} (expected'
        f' {node_id}'
    )

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    representation = f'{type(self).__name__}({self._name_repr()}'
    if self.if_condition is not None:
      representation += f'if_condition={repr(self.if_condition)}, '
    if self.then_child is not None:
      representation += f'then_child={repr(self.then_child)}, '
    if self.else_child is not None:
      representation += f'else_child={repr(self.else_child)}'
    representation += ')'
    return representation

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Node:
    if self.if_condition is None:
      raise ValueError(
          'A Branch node has to have a if condition but currently '
          'it is not set. Please call '
          'branch_node_instance.set_if_condition(condition_instance).'
      )
    if self.then_child is None and self.else_child is None:
      raise ValueError(
          'Branch node has neither a then nor an else child set. Please set '
          'at least one of them.'
      )

    proto_message = super().proto
    condition = getattr(proto_message.branch, 'if')
    condition.CopyFrom(self.if_condition.proto)

    if self.then_child is not None:
      proto_message.branch.then.CopyFrom(self.then_child.proto)
    if self.else_child is not None:
      else_proto = getattr(proto_message.branch, 'else')
      else_proto.CopyFrom(self.else_child.proto)
    return proto_message

  @utils.classproperty
  def node_type(cls) -> str:  # pylint:disable=no-self-argument
    return 'branch'

  @classmethod
  def _create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree.BranchNode
  ) -> Branch:
    """Creates a Branch node class from a BranchNode proto."""
    then_child = None
    else_child = None
    if proto_object.HasField('then'):
      then_child = Node.create_from_proto(proto_object.then)
    if proto_object.HasField('else'):
      else_child = Node.create_from_proto(getattr(proto_object, 'else'))
    branch = cls(
        if_condition=Condition.create_from_proto(getattr(proto_object, 'if')),
        then_child=then_child,
        else_child=else_child,
    )
    return branch

  def dot_graph(
      self,
      node_id_suffix: str = '',
      node_label: Optional[str] = None,
      name: Optional[str] = None,
  ) -> Tuple[graphviz.Digraph, str]:
    dot_graph, node_name = super().dot_graph(
        node_id_suffix, node_label, name or self._name
    )
    if self.then_child is not None:
      _dot_append_child(
          dot_graph,
          node_name,
          self.then_child,
          node_id_suffix + '_1',
          edge_label='then',
      )
    if self.else_child is not None:
      _dot_append_child(
          dot_graph,
          node_name,
          self.else_child,
          node_id_suffix + '_2',
          edge_label='else',
      )
    label_to_use = name or self._name or ''
    box_dot_graph = _dot_wrap_in_box(
        child_graph=dot_graph,
        name=label_to_use + node_id_suffix,
        label=label_to_use,
    )
    return box_dot_graph, node_name


class Data(Node):
  """BT node of type Data for behavior_tree_pb2.DataNode.

  A data node can be used to create, update, or remove data from the
  blackboard. Information stored there can be created by a CEL expression, a
  specific proto or a list of protos, or from a world query.

  Attributes:
    blackboard_key: blackboard key the node operates on
    cel_expression: CEL expression for create or update operation
    world_query: World query for create or update operation
    input_proto: Proto for create or update operation
    input_protos: Protos for create or update operation
    operation: describing the operation the data node will perform
    proto: The proto representation of the node.
    node_type: A string label of the node type.
    result: blackboard value to pass on the modified blackboard key.
  """

  _blackboard_key: str
  _operation: 'Data.OperationType'
  _cel_expression: Optional[str]
  _world_query: Optional[WorldQuery]
  _proto: Optional[
      protobuf_message.Message | skill_utils.MessageWrapper | any_pb2.Any
  ]
  _protos: Optional[
      List[protobuf_message.Message | skill_utils.MessageWrapper | any_pb2.Any]
  ]

  class OperationType(enum.Enum):
    """Defines the kind of operation to perform for the data node."""

    CREATE_OR_UPDATE = 1
    REMOVE = 2

  def __init__(
      self,
      *,
      blackboard_key: str = '',
      operation: 'Data.OperationType' = OperationType.CREATE_OR_UPDATE,
      cel_expression: Optional[str] = None,
      world_query: Optional[WorldQuery] = None,
      proto: Optional[
          protobuf_message.Message | skill_utils.MessageWrapper | any_pb2.Any
      ] = None,
      protos: Optional[
          List[
              protobuf_message.Message
              | skill_utils.MessageWrapper
              | any_pb2.Any
          ]
      ] = None,
      name: Optional[str] = None,
      node_id: int | None = None,
  ):
    super().__init__(name, node_id)
    self._decorators = None
    self._blackboard_key = blackboard_key
    self._operation = operation
    self._cel_expression = cel_expression
    self._world_query = world_query
    self._proto = proto
    self._protos = protos

    if self._cel_expression is not None:
      print(
          'The cel_expression field on Data nodes has been deprecated. In all'
          ' places that referenced the blackboard_key, inline the expression'
          ' from the deprecated cel_expression field instead.'
      )
    if self._world_query is not None:
      print(
          'The world_query option has been deprecated. Replace this, e.g., by a'
          ' custom skill or Python script node.'
      )
    if self._protos is not None:
      print(
          'The protos option has been deprecated. If used with a for each loop,'
          ' replace that by a while loop.'
      )

  def __repr__(self) -> str:
    """Returns a compact, human-readable string representation."""
    return (
        f'{type(self).__name__}({self._name_repr()},'
        f' blackboard_key="{self.blackboard_key}")'
    )

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree.Node:
    self.validate()

    if self._blackboard_key is None or not self._blackboard_key:
      raise solutions_errors.InvalidArgumentError(
          'Data node requires the blackboard_key argument as non-empty string'
      )

    proto_message = super().proto
    if self._operation == Data.OperationType.CREATE_OR_UPDATE:
      proto_message.data.create_or_update.blackboard_key = self._blackboard_key

      if self._cel_expression is not None:
        proto_message.data.create_or_update.cel_expression = (
            self._cel_expression
        )

      if self._world_query is not None:
        proto_message.data.create_or_update.from_world.proto.Pack(
            self._world_query.proto
        )
        for assignment in self._world_query.assignments:
          proto_message.data.create_or_update.from_world.assign.append(
              assignment
          )

      if self._proto is not None:
        if isinstance(self._proto, skill_utils.MessageWrapper):
          proto_message.data.create_or_update.proto.CopyFrom(
              self._proto.to_any()
          )
        elif isinstance(self._proto, any_pb2.Any):
          proto_message.data.create_or_update.proto.CopyFrom(self._proto)
        else:
          proto_message.data.create_or_update.proto.Pack(self._proto)

      if self._protos is not None:
        for p in self._protos:
          if isinstance(p, skill_utils.MessageWrapper):
            proto_message.data.create_or_update.protos.items.add().CopyFrom(
                p.to_any()
            )
          elif isinstance(p, any_pb2.Any):
            proto_message.data.create_or_update.protos.items.add().CopyFrom(p)
          else:
            proto_message.data.create_or_update.protos.items.add().Pack(p)

    elif self._operation == Data.OperationType.REMOVE:
      proto_message.data.remove.blackboard_key = self._blackboard_key

    else:
      raise solutions_errors.InvalidArgumentError(
          'Data node has no operation type set'
      )

    return proto_message

  @utils.classproperty
  def node_type(cls) -> str:  # pylint:disable=no-self-argument
    return 'data'

  def has_child(self, node_id: int) -> bool:
    return False

  def remove_child(self, node_id: int) -> None:
    raise ValueError('Data node does not have children to remove')

  def validate(self) -> None:
    """Validates the current input.

    This checks if one and only one input is set for the Data node.

    Raises:
      InvalidArgumentError: raised if the node is in a state that could not be
        converted to a valid proto.
    """
    if self._operation is None:
      raise solutions_errors.InvalidArgumentError(
          'Data node has no operation mode specified'
      )

    num_inputs = 0
    if self._cel_expression is not None:
      num_inputs += 1
    if self._world_query is not None:
      num_inputs += 1
    if self._proto is not None:
      num_inputs += 1
    if self._protos is not None:
      num_inputs += 1

    if (
        self._operation == Data.OperationType.CREATE_OR_UPDATE
        and num_inputs != 1
    ):
      raise solutions_errors.InvalidArgumentError(
          'Data node for create or update requires exactly 1 input'
          f' element, got {num_inputs}'
      )

  @property
  def result(self) -> Optional[blackboard_value.BlackboardValue]:
    """Gets blackboard value to pass on the modified blackboard key.

    Only valid for create or update nodes, not when removing a key.

    Returns:
      Blackboard value for create_or_update node, None otherwise.
    """
    self.validate()
    if self._operation == Data.OperationType.CREATE_OR_UPDATE:
      if self._world_query is not None:
        return blackboard_value.BlackboardValue(
            any_list_pb2.AnyList.DESCRIPTOR.fields_by_name,
            self._blackboard_key,
            any_list_pb2.AnyList,
            None,
        )

      if self._proto is not None:
        contained_proto = self._proto
        if isinstance(contained_proto, skill_utils.MessageWrapper):
          contained_proto = contained_proto.wrapped_message
          if contained_proto is None:
            raise solutions_errors.FailedPreconditionError(
                'The proto message in the Data node does not contain a message'
                ' (None).'
            )
        if isinstance(contained_proto, any_pb2.Any):
          raise solutions_errors.FailedPreconditionError(
              'Cannot create BlackboardValue for Data node containing an Any'
              ' proto (no descriptor available).'
          )
        return blackboard_value.BlackboardValue(
            contained_proto.DESCRIPTOR.fields_by_name,
            self._blackboard_key,
            contained_proto.__class__,
            None,
        )

      if self._protos is not None:
        return blackboard_value.BlackboardValue(
            any_list_pb2.AnyList.DESCRIPTOR.fields_by_name,
            self._blackboard_key,
            any_list_pb2.AnyList,
            None,
        )

    return None

  @property
  def blackboard_key(self) -> Optional[str]:
    return self._blackboard_key

  def set_blackboard_key(self, blackboard_key: str) -> Data:
    """Sets the blackboard key for this operation.

    Args:
      blackboard_key: blackboard key by which the value can be accessed in other
        nodes.

    Returns:
      self (for builder pattern)
    """
    self._blackboard_key = blackboard_key
    return self

  @property
  def operation(self) -> Data.OperationType:
    return self._operation

  def set_operation(self, operation: OperationType) -> Data:
    """Sets the mode of the performed operation.

    Args:
      operation: operation to perform, see enum.

    Returns:
      self (for builder pattern)
    """
    self._operation = operation
    if operation == Data.OperationType.REMOVE:
      self._cel_expression = None
      self._world_query = None
      self._proto = None
      self._protos = None
    return self

  @property
  def cel_expression(self) -> Optional[str]:
    return self._cel_expression

  def set_cel_expression(self, cel_expression: str) -> Data:
    """Sets the CEL expression to create or update a blackboard value.

    Args:
      cel_expression: CEL expression that may reference other blackboard values.

    Returns:
      self (for builder pattern)
    """
    if self._operation != Data.OperationType.CREATE_OR_UPDATE:
      raise solutions_errors.InvalidArgumentError(
          'Cannot set cel_expression on data node without operation'
          ' CREATE_OR_UPDATE'
      )
    self._cel_expression = cel_expression
    return self

  @property
  def world_query(self) -> Optional[WorldQuery]:
    return self._world_query

  def set_world_query(self, world_query: WorldQuery) -> Data:
    if self._operation != Data.OperationType.CREATE_OR_UPDATE:
      raise solutions_errors.InvalidArgumentError(
          'Cannot set world_query on data node without operation'
          ' CREATE_OR_UPDATE'
      )
    self._world_query = world_query
    return self

  @property
  def input_proto(
      self,
  ) -> Optional[
      protobuf_message.Message | skill_utils.MessageWrapper | any_pb2.Any
  ]:
    return self._proto

  def set_input_proto(
      self,
      proto: (
          protobuf_message.Message | skill_utils.MessageWrapper | any_pb2.Any
      ),
  ) -> Data:
    """Sets a specific proto for creating or updating a blackboard value.

    Args:
      proto: The proto to store in the blackboard

    Returns:
      self (for builder pattern)
    """
    if self._operation != Data.OperationType.CREATE_OR_UPDATE:
      raise solutions_errors.InvalidArgumentError(
          'Cannot set input proto on data node without operation'
          ' CREATE_OR_UPDATE'
      )
    self._proto = proto
    return self

  @property
  def input_protos(
      self,
  ) -> Optional[
      List[protobuf_message.Message | skill_utils.MessageWrapper | any_pb2.Any]
  ]:
    return self._protos

  def set_input_protos(
      self,
      protos: List[
          protobuf_message.Message | skill_utils.MessageWrapper | any_pb2.Any
      ],
  ) -> Data:
    """Sets list of specific protos for creating or updating a blackboard value.

    Args:
      protos: The protos to store in the blackboard (will be wrapped in an
        intrinsic_proto.executive.AnyList.

    Returns:
      self (for builder pattern)
    """
    if self._operation != Data.OperationType.CREATE_OR_UPDATE:
      raise solutions_errors.InvalidArgumentError(
          'Cannot set input protos on data node without operation'
          ' CREATE_OR_UPDATE'
      )
    self._protos = protos
    return self

  @classmethod
  def _create_from_proto(
      cls, proto_object: behavior_tree_pb2.BehaviorTree.DataNode
  ) -> Data:
    """Creates a new instances from data in a proto.

    Args:
      proto_object: Proto to import from.

    Returns:
      Instance of Data.

    Raises:
      InvalidArgumentError: if passed Node proto does not have the data field
        set to a valid configuration.
    """
    operation = Data.OperationType.CREATE_OR_UPDATE
    cel_expression = None
    world_query = None
    proto = None
    protos = None

    if proto_object.HasField('create_or_update'):
      create_or_update = proto_object.create_or_update

      blackboard_key = create_or_update.blackboard_key
      if create_or_update.HasField('cel_expression'):
        cel_expression = create_or_update.cel_expression
      if create_or_update.HasField('from_world'):
        world_query_proto = world_query_pb2.WorldQuery()
        create_or_update.from_world.proto.Unpack(world_query_proto)
        world_query = WorldQuery.create_from_proto(world_query_proto)
      if create_or_update.HasField('proto'):
        proto = create_or_update.proto
      protos = None
      if create_or_update.HasField('protos'):
        protos = [p for p in create_or_update.protos.items]

    elif proto_object.HasField('remove'):
      operation = Data.OperationType.REMOVE
      blackboard_key = proto_object.remove.blackboard_key
    else:
      raise solutions_errors.InvalidArgumentError(
          'Data node proto does not have any operation set'
      )

    data = cls(
        blackboard_key=blackboard_key,
        operation=operation,
        cel_expression=cel_expression,
        world_query=world_query,
        proto=proto,
        protos=protos,
    )

    data.validate()
    return data

  def dot_graph(
      self,
      node_id_suffix: str = '',
      node_label: Optional[str] = None,
      name: Optional[str] = None,
  ) -> Tuple[graphviz.Digraph, str]:
    """Converts this node suitable for inclusion in a dot graph.

    Args:
      node_id_suffix: A little string of form `_1_2`, which is just a suffix to
        make a unique node name in the graph. If the node names clash within the
        graph, they are merged into one, and we do not want to merge unrelated
        nodes.
      node_label: The label is typically just the type of the node. To use a
        different value, this argument can be used.
      name: name of the node as set by the user.

    Returns:
      Dot graph representation for this node.
    """
    return super().dot_graph(node_id_suffix, node_label, name or self._name)


class IdRecorder:
  """A visitor callable object that records tree ids and node ids."""

  tree_to_node_id_to_nodes: Mapping[BehaviorTree, Mapping[int, list[Node]]]
  tree_id_to_trees: Mapping[str, list[BehaviorTree]]

  def __init__(self):
    self.tree_to_node_id_to_nodes = collections.defaultdict(
        lambda: collections.defaultdict(list)
    )
    self.tree_id_to_trees = collections.defaultdict(list)

  def __call__(
      self,
      containing_tree: BehaviorTree,
      tree_object: Union[BehaviorTree, Node, Condition],
  ):
    if isinstance(tree_object, Node) and tree_object.node_id is not None:
      self.tree_to_node_id_to_nodes[containing_tree][
          tree_object.node_id
      ].append(tree_object)
    if (
        isinstance(tree_object, BehaviorTree)
        and tree_object.tree_id is not None
    ):
      self.tree_id_to_trees[tree_object.tree_id].append(tree_object)


class BehaviorTree:
  """Python wrapper around behavior_tree_pb2.BehaviorTree proto.

  Attributes:
    tree_id: A unique ID for this behavior tree.
    root: The root node of the tree of type Node.

  Example usage:
    bt = behavior_tree.BehaviorTree('my_behavior_tree_name')
    bt.set_root(behavior_tree.Sequence()
      .set_children(behavior_tree.Task(some_skill_action),
    behavior_tree.Task(some_other_skill_action)))
    print(bt.proto)   # prints the proto
    print(bt)         # prints a readable pseudo-code version of the instance
    bt.show()         # calling this in Jupyter would visualize the tree
  """

  tree_id: str | None
  root: Node | None
  _name: str
  _description: skills_pb2.Skill | None
  _return_value_expression: str | None
  _metadata: metadata_pb2.Metadata | None
  _referenced_assets: dict[str, process_asset_pb2.ProcessAsset.ProcessedAsset]
  _user_data_protos: dict[str, any_pb2.Any]
  _user_data_bytes: dict[str, bytes]

  def __init__(
      self,
      name: str | None = None,
      root: Node | ActionBase | CodeExecution | None = None,
      bt: BehaviorTree | behavior_tree_pb2.BehaviorTree | None = None,
      *,
      tree_id: str | None = None,
  ):
    """Initializes a behavior tree from individual arguments or as copy.

    Args:
      name: the name of the behavior tree, which defaults to '',
      root: a node of type Node to be set as the root of this tree,
      bt: BehaviorTree instance or BehaviorTree proto to copy from. The value of
        the `name` argument overwrites the value from the `bt` proto argument,
        if set.
      tree_id: Pre-determined tree ID, the format match the regex
        `[a-zA-Z0-9][a-zA-Z0-9_-]*` (for details see BehaviorTree proto docs).
    """
    root = _transform_to_optional_node(root)

    if bt is not None:
      if isinstance(bt, BehaviorTree):
        if bt.asset_metadata_proto is None:
          bt_copy = self.create_from_proto(bt.proto)
        else:
          bt_copy = self.create_from_proto(
              process_asset_pb2.ProcessAsset(
                  behavior_tree=bt.proto,
                  metadata=bt.asset_metadata_proto,
                  assets=bt.referenced_assets,
              )
          )
      elif isinstance(bt, behavior_tree_pb2.BehaviorTree):
        bt_copy = self.create_from_proto(bt)
      else:
        raise TypeError
      self.root = root or bt_copy.root
      self.tree_id = bt_copy.tree_id
      self._name = name or bt_copy.name
      self._description = bt_copy._description
      self._return_value_expression = bt_copy._return_value_expression
      self._metadata = bt_copy._metadata
      self._referenced_assets = bt_copy._referenced_assets
      self._user_data_protos = bt_copy._user_data_protos
      self._user_data_bytes = bt_copy._user_data_bytes
      return

    self.root = root
    self.tree_id = tree_id
    self._name: str = name or ''
    self._description = None
    self._return_value_expression = None
    self._metadata = None
    self._referenced_assets = {}
    self._user_data_protos = {}
    self._user_data_bytes = {}

  def __repr__(self) -> str:
    """Converts a BT into a compact, human-readable string representation.

    Returns:
      A behavior tree formatted as string using Python syntax.
    """
    return f'BehaviorTree({self._name_repr()}root={repr(self.root)})'

  def __eq__(self, other: BehaviorTree) -> bool:
    return self._to_proto(validate=False) == other._to_proto(validate=False)

  def __hash__(self) -> int:
    return hash(
        self._to_proto(validate=False).SerializeToString(deterministic=True)
    )

  def _name_repr(self) -> str:
    """Returns a snippet for the name attribute to be used in __repr__."""
    name_snippet = ''
    if self.name:
      name_snippet = f'name="{self.name}", '
    return name_snippet

  def set_root(self, root: Node | ActionBase | CodeExecution) -> Node:
    """Sets the root member to the given Node instance."""
    self.root = _transform_to_node(root)
    return self.root

  def set_asset_metadata(
      self,
      *,
      id: str | id_pb2.Id,  # pylint: disable=redefined-builtin
      vendor: str | vendor_pb2.Vendor,
      doc: str | documentation_pb2.Documentation | None = None,
      subprocess: bool = False,
  ):
    """Sets the asset metadata for this behavior tree.

    After calling this method, the behavior tree can be saved as a Process
    asset if it did not previously have any asset metadata.

    Note that you cannot set a display name via this method. This is taken equal
    to the behavior tree's name which you can set in the constructor or via the
    `name` property.

    Args:
      id: The id of the Process asset (required). Either as a string (e.g.
        'ai.intrinsic.my_process') or as an Id proto.
      vendor: The vendor of the Process asset (required). Either as a string or
        as a Vendor proto.
      doc: Optional documentation of the process. Either as a string or as a
        Documentation proto.
      subprocess: Whether to tag the process as a subprocess. This is, e.g.,
        required to list the process as a runnable "Skill" in the Process
        Explorer in the Frontend.

    Raises:
      TypeError: If any of the arguments are of an unsupported type.
      ValueError: If any of the arguments have an invalid value.
    """
    if not self.name:
      raise ValueError('Behavior tree must have a name.')

    if isinstance(id, str):
      # Will fail if id is empty
      id_proto = id_pb2.Id(
          package=id_utils.package_from(id), name=id_utils.name_from(id)
      )
    elif isinstance(id, id_pb2.Id):
      # Will fail below if id is invalid
      id_proto = id
    else:
      raise TypeError(f'Unsupported type for id: {type(id)}')

    if isinstance(vendor, str):
      vendor_proto = vendor_pb2.Vendor(display_name=vendor)
    elif isinstance(vendor, vendor_pb2.Vendor):
      vendor_proto = vendor
    else:
      raise TypeError(f'Unsupported type for vendor: {type(vendor)}')

    if not vendor_proto.display_name:
      raise ValueError('Vendor name cannot be empty.')

    if isinstance(doc, str):
      documentation_proto = documentation_pb2.Documentation(description=doc)
    elif isinstance(doc, documentation_pb2.Documentation):
      documentation_proto = doc
    elif doc is None:
      documentation_proto = None
    else:
      raise TypeError(f'Unsupported type for documentation: {type(doc)}')

    # We set all metadata fields, thus we can simply always create a new
    # Metadata proto.
    self._metadata = metadata_pb2.Metadata(
        id_version=id_pb2.IdVersion(id=id_proto),
        display_name=self.name,
        vendor=vendor_proto,
        documentation=documentation_proto,
        asset_type=asset_type_pb2.AssetType.ASSET_TYPE_PROCESS,
        asset_tag=(
            asset_tag_pb2.AssetTag.ASSET_TAG_SUBPROCESS
            if subprocess
            else asset_tag_pb2.AssetTag.ASSET_TAG_UNSPECIFIED
        ),
    )
    # This will clear the 'id_version' in the Skill proto because the new
    # metadata has no version set.
    self._sync_skill_proto_from_metadata()

  @property
  def name(self) -> str:
    """The display name of this behavior tree.

    For legacy processes, this is also the unique identifier of the process.
    """
    return self._name

  @name.setter
  def name(self, value: str):
    self._name = value
    if self._metadata is not None:
      self._metadata.display_name = value
    if self._description is not None:
      self._description.display_name = value

  @property
  def asset_metadata_proto(self) -> metadata_pb2.Metadata | None:
    """Returns the asset Metadata proto of the BehaviorTree.

    If not None, the behavior tree represents a Process asset and can be saved
    as such. Otherwise, it is a local, "anonymous" behavior tree or it
    represents a legacy process.

    You can use set_asset_metadata() to initialize or update the asset Metadata.
    """
    if self._metadata is None:
      return None
    result = metadata_pb2.Metadata()
    result.CopyFrom(self._metadata)
    return result

  @asset_metadata_proto.setter
  def asset_metadata_proto(self, metadata: metadata_pb2.Metadata):
    """Sets the asset Metadata proto of the BehaviorTree.

    For advanced use cases only. Automatically syncs redundant properties such
    as the name of the BehaviorTree to the provided metadata. Prefer
    set_metadata() to initialize or update the asset Metadata proto.

    Args:
      metadata: The new asset Metadata proto for the BehaviorTree.
    """
    self._metadata.CopyFrom(metadata)
    self._name = metadata.display_name
    self._sync_skill_proto_from_metadata()

  @property
  def referenced_assets(
      self,
  ) -> dict[str, process_asset_pb2.ProcessAsset.ProcessedAsset]:
    """Returns a copy of the referenced Assets map of the BehaviorTree.

    Always returns an empty dictionary if this BehaviorTree instance does not
    represent a Process Asset.
    """
    if self.asset_metadata_proto is None:
      return dict()
    return self._referenced_assets.copy()

  @referenced_assets.setter
  def referenced_assets(
      self,
      referenced_assets: dict[
          str, process_asset_pb2.ProcessAsset.ProcessedAsset
      ],
  ):
    """Sets the referenced Assets map of the BehaviorTree.

    Only allowed on BehaviorTree instances that represent a Process Asset.
    Throws an error if this instance does not represent a Process Asset.

    Args:
      referenced_assets: New map of referenced Assets.

    Raises:
      TypeError: When the BehaviorTree does not represent a Process Asset.
    """
    if self.asset_metadata_proto is None:
      raise TypeError(
          'Cannot set referenced Assets on a non-Asset BehaviorTree'
      )
    self._referenced_assets = referenced_assets.copy()

  @property
  def return_value_expression(self) -> cel.CelExpression | None:
    """Returns the return value expression of this behavior tree."""
    return (
        cel.CelExpression(self._return_value_expression)
        if self._return_value_expression is not None
        else None
    )

  @return_value_expression.setter
  def return_value_expression(
      self,
      value: blackboard_value.BlackboardValue | cel.CelExpression | str | None,
  ):
    """Sets the return value expression of this behavior tree."""
    if value is None:
      self._return_value_expression = None
    elif isinstance(value, blackboard_value.BlackboardValue):
      self._return_value_expression = value.value_access_path()
    elif isinstance(value, cel.CelExpression):
      self._return_value_expression = str(value)
    elif isinstance(value, str):
      self._return_value_expression = value
    else:
      raise TypeError(
          'return_value_expression must be BlackboardValue, CelExpression,'
          f' str, or None, got {type(value)}'
      )

  @property
  def proto(self) -> behavior_tree_pb2.BehaviorTree:
    """Returns the proto representation of the BehaviorTree."""
    return self._to_proto(validate=True)

  def _to_proto(self, validate: bool = True) -> behavior_tree_pb2.BehaviorTree:
    if validate:
      self.validate_id_uniqueness()
      self._validate_signatures()

    proto_object = behavior_tree_pb2.BehaviorTree(name=self.name)
    if self.root is not None:
      proto_object.root.CopyFrom(self.root.proto)
    if self.tree_id:
      proto_object.tree_id = self.tree_id
    if self._description is not None:
      proto_object.description.CopyFrom(self._description)
    if self._return_value_expression is not None:
      proto_object.return_value_expression = self._return_value_expression
    if self._user_data_protos:
      for k, m in self._user_data_protos.items():
        proto_object.user_data.data_any[k].CopyFrom(m)
    if self._user_data_bytes:
      for k, b in self._user_data_bytes.items():
        proto_object.user_data.data_bytes[k] = b
    return proto_object

  @classmethod
  def create_from_proto(
      cls,
      proto_object: (
          behavior_tree_pb2.BehaviorTree | process_asset_pb2.ProcessAsset
      ),
  ) -> BehaviorTree:
    """Instantiates a behavior tree from a corresponding proto.

    Args:
      proto_object: A BehaviorTree proto or a full ProcessAsset proto. If only a
        BehaviorTree proto is passed, the result will be be an "anonymous" tree
        which can be initialized as a Process asset by later calling
        set_metadata() on it.
    """
    if cls != BehaviorTree:
      raise TypeError(
          'create_from_proto can only be called on the BehaviorTree class'
      )

    if isinstance(proto_object, process_asset_pb2.ProcessAsset):
      if not proto_object.HasField('behavior_tree'):
        raise ValueError(
            'ProcessAsset proto must have a behavior_tree field but it is not'
            ' set.'
        )
      if not proto_object.HasField('metadata'):
        raise ValueError(
            'ProcessAsset proto must have a metadata field but it is not set.'
        )
      bt_proto = proto_object.behavior_tree
      metadata_proto = proto_object.metadata
      assets_map = dict(proto_object.assets)
    elif isinstance(proto_object, behavior_tree_pb2.BehaviorTree):
      bt_proto = proto_object
      metadata_proto = None
      assets_map = {}
    else:
      raise TypeError(
          'proto_object must be a BehaviorTree proto or a ProcessAsset proto'
      )

    bt = cls()
    if bt_proto.HasField('tree_id'):
      bt.tree_id = bt_proto.tree_id
    if bt_proto.HasField('root'):
      bt.root = Node.create_from_proto(bt_proto.root)
    bt._name = bt_proto.name
    if bt_proto.HasField('description'):
      bt._description = skills_pb2.Skill()
      bt._description.CopyFrom(bt_proto.description)
    if bt_proto.return_value_expression:
      bt._return_value_expression = bt_proto.return_value_expression
    if bt_proto.HasField('user_data'):
      for k, m in bt_proto.user_data.data_any.items():
        bt.set_user_data_proto_from_any(k, m)
      for k, b in bt_proto.user_data.data_bytes.items():
        bt.set_user_data_bytes(k, b)
    bt._metadata = metadata_proto
    bt._referenced_assets = assets_map

    return bt

  def generate_and_set_unique_id(self) -> str:
    """Generates a unique tree id and sets it for this tree."""
    if self.tree_id is not None:
      print(
          'Warning: Creating a new unique id, but this tree already had an id'
          f' ({self.tree_id})'
      )
    self.tree_id = str(uuid.uuid4())
    return self.tree_id

  def ensure_all_unique_ids(self) -> None:
    """Sets unique tree ID and IDs for all nodes in the tree where unset."""

    def ensure_node_unique_id(
        containing_tree: BehaviorTree,
        tree_object: Union[BehaviorTree, Node, Condition],
    ):
      del containing_tree  # unused

      if isinstance(tree_object, BehaviorTree):
        tree = cast(BehaviorTree, tree_object)
        if tree.tree_id is None:
          tree.generate_and_set_unique_id()

      elif isinstance(tree_object, Node):
        node = cast(Node, tree_object)
        if node.node_id is None:
          node.generate_and_set_unique_id()

    self.visit(ensure_node_unique_id)

  def delete_user_data_proto(self, key: str) -> None:
    if key in self._user_data_protos:
      del self._user_data_protos[key]

  def set_user_data_proto(
      self, key: str, proto: protobuf_message.Message
  ) -> BehaviorTree:
    """Sets a user data proto for a specified key.

    This will modify the user_data field of the tree proto to contain the
    specified proto encoded as Any proto in the data_any field.

    Args:
      key: key to store value at
      proto: proto to store

    Returns:
      self
    """
    packed = any_pb2.Any()
    packed.Pack(proto)
    self._user_data_protos[key] = packed
    return self

  def set_user_data_proto_from_any(
      self, key: str, any_proto: any_pb2.Any
  ) -> BehaviorTree:
    """Sets a user data proto for a specified key.

    This will take the Any proto as-is.

    Args:
      key: key to store value at
      any_proto: proto to store encoded as Any proto

    Returns:
      self
    """
    self._user_data_protos[key] = any_proto
    return self

  def delete_user_data_bytes(self, key: str) -> None:
    if key in self._user_data_bytes:
      del self._user_data_bytes[key]

  def set_user_data_bytes(self, key: str, value: bytes) -> BehaviorTree:
    """Sets a user data byte for a specified key.

    Args:
      key: key to store value at
      value: byte sequence to store

    Returns:
      self
    """
    self._user_data_bytes[key] = value
    return self

  @property
  def user_data_protos(self) -> dict[str, any_pb2.Any]:
    return self._user_data_protos

  @property
  def user_data_bytes(self) -> dict[str, bytes]:
    return self._user_data_bytes

  def _visit_condition(
      self,
      condition: Condition,
      callback: Callable[
          [BehaviorTree, Union[BehaviorTree, Node, Condition]], None
      ],
  ):
    # Invoke callback on condition
    callback(self, condition)

    # Visit children
    if isinstance(condition, SubTreeCondition):
      condition.tree.visit(callback)
    elif isinstance(condition, CompoundCondition):
      for child_condition in condition.conditions:
        self._visit_condition(child_condition, callback)
    elif isinstance(condition, Not):
      self._visit_condition(condition.condition, callback)

  def _visit_node(
      self,
      node: Node,
      callback: Callable[
          [BehaviorTree, Union[BehaviorTree, Node, Condition]], None
      ],
  ):
    # Invoke callback on node and its decorators
    if node.decorators is not None and node.decorators.condition is not None:
      self._visit_condition(node.decorators.condition, callback)
    callback(self, node)

    # Visit children
    if isinstance(node, SubTree):
      if node.behavior_tree is not None:
        node.behavior_tree.visit(callback)
    # Handle Selector before NodeWithChildren (Selector is a NodeWithChildren).
    elif isinstance(node, Selector):
      for child in node.children:
        self._visit_node(child, callback)
      for branch in node.branches:
        if branch.condition:
          self._visit_condition(branch.condition, callback)
        self._visit_node(branch.node, callback)
    elif isinstance(node, NodeWithChildren):
      for child in node.children:
        self._visit_node(child, callback)
    elif isinstance(node, Retry):
      if node.child is not None:
        self._visit_node(node.child, callback)
      if node.recovery is not None:
        self._visit_node(node.recovery, callback)
    elif isinstance(node, Fallback):
      for try_node in node.tries:
        if try_node.condition:
          self._visit_condition(try_node.condition, callback)
        self._visit_node(try_node.node, callback)
    elif isinstance(node, Loop):
      if node.while_condition is not None:
        self._visit_condition(node.while_condition, callback)
      if node.do_child is not None:
        self._visit_node(node.do_child, callback)
    elif isinstance(node, Branch):
      if node.if_condition is not None:
        self._visit_condition(node.if_condition, callback)
      if node.then_child is not None:
        self._visit_node(node.then_child, callback)
      if node.else_child is not None:
        self._visit_node(node.else_child, callback)

  def visit(
      self,
      callback: Callable[
          [BehaviorTree, Union[BehaviorTree, Node, Condition]], None
      ],
  ) -> None:
    """Visits this BehaviorTree recursively.

    All objects in the BehaviorTree are visited and the callback is called on
    every one. Objects can be
      * BehaviorTree objects, e.g., the tree itself, sub trees, or behavior
        trees in conditions
      * Node objects, e.g., Task or Sequence nodes
      * Condition objects, e.g., AllOf, Not or SubTreeCondition

    The callback is called for every object. For example, when called on a tree
    the callback is first called with the tree itself, when called on a
    SubtreeNode it is first called on the SubTreeNode and then on the sub-tree,
    when called on a SubTreeCondition it is first called on the condition and
    then on the tree within that condition.

    Callbacks are performed in an natural order for the different objects. For
    example, for a sequence node, its children are visited as in the node's
    order; for a loop node first its while condition is visited, then its
    do_child; a retry node first visits its child and then the recovery.

    Args:
      callback: Function (or any callable) to be called. This first argument
        will be the BehaviorTree containing the object in question, the second
        argument is the object itself, i.e., a BehaviorTree, Node or Condition.
    """
    callback(self, self)
    if self.root is not None:
      self._visit_node(self.root, callback)

  def find_tree_and_node_id(self, node_name: str) -> NodeIdentifier:
    """Searches the tree recursively for a node with name node_name.

    Args:
      node_name: Name of a node to search for in the tree.

    Returns:
      A NodeIdentifier referencing the tree id and node id for the node. The
      result can be passed to calls requiring a NodeIdentifier.

    Raises:
      solution_errors.NotFoundError if no matching node exists.
      solution_errors.InvalidArgumentError if there is more than one matching
        node or if the node or its tree do not have an id defined.
    """
    node_identifiers = self.find_tree_and_node_ids(node_name)

    if not node_identifiers:
      raise solutions_errors.NotFoundError(
          f'Could not find node with name {node_name} or determine its id'
      )
    if len(node_identifiers) > 1:
      raise solutions_errors.InvalidArgumentError(
          f'Could not find unique node for name {node_name}. Found the'
          f' following entries: {node_identifiers}.'
      )
    unique_node = node_identifiers[0]
    return unique_node

  def find_tree_and_node_ids(self, node_name: str) -> list[NodeIdentifier]:
    """Searches the tree recursively for all nodes with name node_name.

    This is usually used, when find_tree_and_node_id cannot find a unique
    solution, but a user tries to find a node for a loaded operation and does
    not want to reload the operation to assign a unique name as that would loose
    the current state.

    Args:
      node_name: Name of a node to search for in the tree.

    Returns:
      A list of NodeIdentifier referencing the tree id and node id for the
      node. The list contains information about all matching nodes, even if the
      nodes do not have a node or tree id. In that case the values are None.
    """
    node_identifiers = []

    def search_matching_name(
        containing_tree: BehaviorTree,
        tree_object: Union[BehaviorTree, Node, Condition],
    ):
      if (
          isinstance(tree_object, Node)
          and tree_object.name
          and tree_object.name == node_name
      ):
        if not containing_tree.tree_id or not tree_object.node_id:
          raise solutions_errors.InvalidArgumentError(
              f'Node with name {node_name!r} found, but it or its containing'
              ' tree does not have an ID set.'
          )

        node_identifiers.append(
            NodeIdentifier(
                tree_id=containing_tree.tree_id, node_id=tree_object.node_id
            )
        )

    self.visit(search_matching_name)
    return node_identifiers

  def find_trees_and_nodes_by_name(
      self, node_name: str
  ) -> list[tuple[BehaviorTree, Node]]:
    """Searches the tree recursively for nodes with given display name.

    Finds tree,node tuples where the node matches the name. tree here could be
    this tree, i.e., self or any contained trees, e.g., subtrees.

    Args:
      node_name: Name of a node to search for in the tree.

    Returns:
      A list of containing tree and nodes, where the node has the given
      (display) name. Note that the name is not necessariy unique, which is why
      there can be multiple such nodes.
    """
    trees_and_nodes: list[tuple[BehaviorTree, Node]] = []

    def search_matching_name(
        containing_tree: BehaviorTree,
        tree_object: Union[BehaviorTree, Node, Condition],
    ):
      if isinstance(tree_object, Node) and tree_object.name == node_name:
        nonlocal trees_and_nodes
        trees_and_nodes.append((containing_tree, cast(Node, tree_object)))

    self.visit(search_matching_name)
    return trees_and_nodes

  def find_nodes_by_name(self, node_name: str) -> list[Node]:
    """Searches the tree recursively for nodes with given display name.

    Args:
      node_name: Name of a node to search for in the tree.

    Returns:
      A list of nodes that have the given (display) name. Note that the name is
      not necessariy unique, which is why there can be multiple such nodes.
    """
    trees_and_nodes = self.find_trees_and_nodes_by_name(node_name)
    nodes: list[Node] = [node for tree, node in trees_and_nodes]
    return nodes

  def find_tree_and_node_by_id(
      self, node_id: int | NodeId
  ) -> tuple[BehaviorTree, Node]:
    """Searches the tree recursively for a node with the given ID.

    This will recurse into subtrees. The node_id must be unique within all
    sub-trees within this tree.

    Args:
      node_id: ID to search for in the tree.

    Returns:
      A tuple of the tree and node matching the node id.

    Raises:
      ValueError if no matching node exists or if there is more than one
      matching node.
    """
    trees_and_nodes: list[tuple[BehaviorTree, Node]] = []

    def search_matching_id(
        containing_tree: BehaviorTree,
        tree_object: Union[BehaviorTree, Node, Condition],
    ):
      if isinstance(tree_object, Node) and tree_object.node_id == node_id:
        nonlocal trees_and_nodes
        trees_and_nodes.append((containing_tree, cast(Node, tree_object)))

    self.visit(search_matching_id)

    if not trees_and_nodes:
      raise ValueError(f'Could not find node with id {node_id}')
    if len(trees_and_nodes) > 1:
      raise ValueError(
          f'Could not find unique node for id {node_id}. Found'
          f' {len(trees_and_nodes)} entries.'
      )
    return trees_and_nodes[0]

  def find_node_by_id(self, node_id: int) -> Node | None:
    """Searches the tree recursively for a node with the given ID.

    This will only look in this tree, it will not recurse into subtrees. An ID
    is only uniquely identified within the context of a single tree.

    Args:
      node_id: ID to search for in the tree.

    Returns:
      The node if found in this tree, None if no node found with the ID.
    """
    node: Node | None = None

    def search_matching_id(
        containing_tree: BehaviorTree,
        tree_object: Union[BehaviorTree, Node, Condition],
    ):
      if containing_tree.tree_id != self.tree_id:
        return

      if isinstance(tree_object, Node) and tree_object.node_id == node_id:
        nonlocal node
        node = cast(Node, tree_object)

    self.visit(search_matching_id)
    return node

  def _find_containing_tree_for_node(self, node: Node) -> BehaviorTree:
    """Finds the nested BehaviorTree that contains node."""
    containing_tree_for_node = None

    def search_containing_tree(
        containing_tree: BehaviorTree,
        tree_object: Union[BehaviorTree, Node, Condition],
    ):
      nonlocal containing_tree_for_node
      if isinstance(tree_object, Node) and tree_object == node:
        containing_tree_for_node = containing_tree

    self.visit(search_containing_tree)
    return containing_tree_for_node

  def get_node_identifier(
      self,
      node_in_tree: NodeInTreeType,
      autogenerate_missing_ids: bool = False,
  ) -> NodeIdentifier:
    """Gets an actual NodeIdentifier from a NodeInTreeType.

    Accepts any of the types in NodeInTreeType and returns the proper
    NodeIdentifier matching a node in this tree.

    Args:
      node_in_tree: A NodeInTreeType for a node.
      autogenerate_missing_ids: If true, will try to ensure valid ids by
        generating ids if not set.

    Raises:
      ValueError if the node_in_tree is not in this tree.
    """
    if isinstance(node_in_tree, NodeIdentifier):
      return node_in_tree

    def node_identifier_for(tree: BehaviorTree, node: Node):
      if autogenerate_missing_ids:
        if not tree.tree_id:
          tree.generate_and_set_unique_id()
        if not node.node_id:
          node.generate_and_set_unique_id()
      if not tree.tree_id or not node.node_id:
        raise solutions_errors.InvalidArgumentError(
            'Found matching node, but it does not have identifiers set.'
        )
      return NodeIdentifier(tree_id=tree.tree_id, node_id=node.node_id)

    if isinstance(node_in_tree, NodeName):
      trees_and_nodes = self.find_trees_and_nodes_by_name(node_in_tree)
      if not trees_and_nodes:
        raise ValueError(f'Could not find node with name {node_in_tree}')
      if len(trees_and_nodes) > 1:
        raise ValueError(
            f'Could not find unique node for name {node_in_tree}. Found'
            f' {len(trees_and_nodes)} entries.'
        )
      tree_for_node = trees_and_nodes[0][0]
      node = trees_and_nodes[0][1]
      return node_identifier_for(tree_for_node, node)
    if isinstance(node_in_tree, NodeId):
      tree_and_node = self.find_tree_and_node_by_id(node_in_tree)
      return node_identifier_for(tree_and_node[0], tree_and_node[1])
    if isinstance(node_in_tree, Node):
      tree_for_node = self._find_containing_tree_for_node(node_in_tree)
      if tree_for_node is None:
        raise ValueError('node must be in the given tree')
      return node_identifier_for(tree_for_node, node_in_tree)
    raise TypeError(
        f'node_in_tree of type {type(node_in_tree)} is not an accepted type'
    )

  def remove_node(self, node_id: int) -> None:
    """Removes a given node from this behavior tree.

    This is a local operation on this tree. It will recurse but only delete a
    node in this tree, not in encapsulated sub trees (not in sub tree nodes).

    This requires that the tree has a valid tree id set (tree_id is not None).

    Args:
      node_id: ID of node to remove. This is only unique with respect to this
        tree (but not named sub trees).

    Raises:
      RuntimeError: if this behavior tree has no tree_id set.
    """

    if self.tree_id is None:
      raise RuntimeError(
          'Nodes can only be removed for trees with a valid tree ID'
      )

    def remove_matching_tree_and_node_id(
        containing_tree: BehaviorTree,
        tree_object: Union[BehaviorTree, Node, Condition],
    ):
      if containing_tree.tree_id != self.tree_id:
        return

      if isinstance(tree_object, BehaviorTree):
        tree = cast(BehaviorTree, tree_object)
        if tree.root is not None and tree.root.node_id == node_id:
          self.root = None
      elif isinstance(tree_object, Node):
        node = cast(Node, tree_object)
        if node.has_child(node_id):
          node.remove_child(node_id)

    self.visit(remove_matching_tree_and_node_id)

  def validate_id_uniqueness(self) -> None:
    """Validates if all ids in the tree are unique.

    The current BehaviorTree object is checked recursively and any non-unique
    ids are highlighted. The function only works locally, i.e., only this tree
    and its SubTrees, Conditions, etc. are verified, but not the uniqueness of
    any referred PBTs or uniqueness across any other tree ids currently loaded
    in the executive.

    Raises:
      solution_errors.InvalidArgumentError if uniqueness is violated. The error
      message gives further information on which ids are non-consistent.
    """

    def tree_object_string(tree_object: Union[BehaviorTree, Node]):
      """Creates a string representation that helps identifying the object."""

      tree_object_str = (
          # pylint:disable-next=protected-access
          f'{tree_object.__class__.__name__}({tree_object._name_repr()})'
      )
      if (
          isinstance(tree_object, BehaviorTree)
          and tree_object.tree_id is not None
      ):
        tree_object_str += f' [tree_id="{tree_object.tree_id}"]'
      if isinstance(tree_object, Node) and tree_object.node_id is not None:
        tree_object_str += f' [node_id="{tree_object.node_id}"]'
      else:
        tree_object_str += ' [<unknown-id>]'
      return tree_object_str

    id_recorder = IdRecorder()
    self.visit(id_recorder)

    violations = []
    for tree, node_id_to_nodes in id_recorder.tree_to_node_id_to_nodes.items():
      for node_id, nodes in node_id_to_nodes.items():
        if len(nodes) > 1:
          violation_explanation = (
              f'  * {tree_object_string(tree)} contains'
              f' {len(nodes)} nodes with id {node_id}: '
          )
          violation_explanation += ', '.join(map(tree_object_string, nodes))
          violations.append(violation_explanation)
    for tree_id, trees in id_recorder.tree_id_to_trees.items():
      if len(trees) > 1:
        violation_explanation = (
            f'  * The tree contains {len(trees)} trees with id "{tree_id}": '
        )
        violation_explanation += ', '.join(map(tree_object_string, trees))
        violations.append(violation_explanation)
    if violations:
      violation_msg = (
          'The BehaviorTree violates uniqueness of tree ids or node ids'
          ' (per tree):\n'
      ) + '\n'.join(violations)
      raise solutions_errors.InvalidArgumentError(violation_msg)

  def _validate_signatures(self) -> None:
    """Validates signatures in the behavior tree.

    Checks that signatures are not used more than once in the behavior tree. The
    check includes the top-level signature of the BehaviorTree itself and all
    Python script node signatures in the tree. This ensures compatibility with
    process editors which have no concept of sharing message definitions within
    a single process.

    This check is not strictly required for a successful execution of the
    process and thus - to allow for advanced use cases - it is restricted to
    signatures which were generated with the ProtoBuilder class.

    Raises:
      ValueError: If reused signatures are found.
    """
    src_names_and_signatures: list[tuple[str, proto_building.Signature]] = []

    if self._description is not None:
      sig = self.get_signature()
      # pylint: disable-next=protected-access
      if proto_building._is_generated_signature(sig):
        src_names_and_signatures.append(('main behavior tree', sig))

    def collect_task_node_signatures(
        containing_tree: BehaviorTree,
        tree_object: Union[BehaviorTree, Node, Condition],
    ) -> None:
      del containing_tree  # unused
      if (
          isinstance(tree_object, Task)
          and tree_object.code_execution is not None
          and isinstance(tree_object.code_execution, PythonScript)
          # pylint: disable-next=protected-access
          and proto_building._is_generated_signature(
              tree_object.code_execution.signature_with_args.signature
          )
      ):
        src_name = (
            f"node '{tree_object.name}'"
            if tree_object.name is not None
            else 'node <unnamed>'
        )
        src_names_and_signatures.append(
            (src_name, tree_object.code_execution.signature_with_args.signature)
        )

    self.visit(collect_task_node_signatures)

    # It's not enough to check for reused instances of Signature objects. The
    # user might have used, e.g., copy.deepcopy(), and the Signature object from
    # the BehaviorTree get's created on-the-fly and is always unique. Instead,
    # we check for duplicate message names where "duplicate name" means that a
    # message name is used by two different signatures. If the parameter and
    # return value message name are the same for a single signature we allow it.
    seen_message_names: dict[str, str] = {}
    colliding_sources: list[str] = []
    for source_name, sig in src_names_and_signatures:
      # Collect non-empty message names in a set.
      message_names = {
          sig.parameter_message_full_name,
          sig.return_value_message_full_name,
      } - {''}
      for name in message_names:
        if name in seen_message_names:
          prev_source = seen_message_names[name]
          if prev_source not in colliding_sources:
            colliding_sources.append(prev_source)
          if source_name not in colliding_sources:
            colliding_sources.append(source_name)
        else:
          seen_message_names[name] = source_name

    if colliding_sources:
      max_sources_to_display = 3
      if len(colliding_sources) > max_sources_to_display:
        first_n_str = ', '.join(colliding_sources[:max_sources_to_display])
        remaining = len(colliding_sources) - max_sources_to_display
        sources_str = f'{first_n_str}, ... <{remaining} more>'
      else:
        sources_str = ', '.join(colliding_sources)
      raise ValueError(
          f'Reused signatures found. The signatures of [{sources_str}] have'
          ' messages that are reused at least once in the tree. This can lead'
          ' to compatibility problems with other process editors. To avoid'
          ' this problem, create a separate signature object for each task node'
          ' and/or the behavior tree itself.'
      )

  def _initialize_skill_proto_for_pbt(self, skill_id: str, display_name: str):
    """Initializes the Skill proto for the initialize_pbt* methods below."""

    if self._metadata is None:
      # This BehaviorTree represents a legacy process.
      if not skill_id:
        raise solutions_errors.InvalidArgumentError(
            'skill_id must be given for a BehaviorTree that does not have asset'
            ' metadata.'
        )
      self._description = skills_pb2.Skill(
          id=skill_id,
          display_name=display_name,
          behavior_tree_description=skills_pb2.BehaviorTreeDescription(),
      )
    else:
      # This BehaviorTree represents a Process asset. The Skill proto already
      # exists and has an id and display name.
      assert self._description is not None
      if skill_id:
        raise solutions_errors.InvalidArgumentError(
            'skill_id must not be given for a BehaviorTree that already has'
            ' asset metadata.'
        )
      if display_name:
        raise solutions_errors.InvalidArgumentError(
            'display_name must not be given for a BehaviorTree that already has'
            ' asset metadata.'
        )

  def initialize_pbt(
      self,
      *,
      parameter_proto_schema: str,
      proto_builder: proto_building.ProtoBuilder,
      return_value_proto_schema: str | None = None,
      parameter_message_full_name: str = '',
      return_value_message_full_name: str = '',
      skill_id: str = '',
      display_name: str = '',
  ):
    """Initializes a behavior tree to be a parameterizable behavior tree.

    For this a behavior tree must have a parameter and return value description.
    To specify this parameters or return values are given as a proto schema.
    If that proto schema contains only a single message, the full message name
    is extracted from the schema. Otherwise the full name must be given
    explicitly.

    To create the PBT as a Process asset, call set_asset_metadata() before
    calling this method and do not pass skill_id or display_name.

    Args:
      parameter_proto_schema: A full proto schema for the PBT parameters.
      proto_builder: An instance of the proto builder service.
      return_value_proto_schema: An optional proto schema for the return value.
        If omitted and return_value_message_full_name is set, the parameter
        schema is used for the return value as well.
      parameter_message_full_name: The full name of the parameter message.
      return_value_message_full_name: The full name of the return value proto.
      skill_id: The skill id that this PBT registers under. If the BehaviorTree
        already has asset metadata, this must not be given.
      display_name: The name to display the PBT with. If the BehaviorTree
        already has asset metadata, this must not be given.
    """
    self._initialize_skill_proto_for_pbt(skill_id, display_name)

    pseudo_file = self._description.id.replace('.', '_')

    if not parameter_proto_schema:
      raise solutions_errors.InvalidArgumentError(
          'initialize_pbt requires parameter_proto_schema to be given.'
      )
    param_descriptor_set = proto_builder.compile(
        pseudo_file + '_params.proto', parameter_proto_schema
    )
    if parameter_message_full_name:
      param_full_name = parameter_message_full_name
    else:
      param_full_name = bt_util.get_single_message_name_from_file(
          param_descriptor_set, pseudo_file + '_params.proto'
      )
    pd = skills_pb2.ParameterDescription(
        parameter_message_full_name=param_full_name
    )
    pd.parameter_descriptor_fileset.CopyFrom(param_descriptor_set)
    self._description.parameter_description.CopyFrom(pd)

    if return_value_proto_schema:
      return_descriptor_set = proto_builder.compile(
          pseudo_file + '_return.proto', return_value_proto_schema
      )
      if return_value_message_full_name:
        return_full_name = return_value_message_full_name
      else:
        return_full_name = bt_util.get_single_message_name_from_file(
            return_descriptor_set, pseudo_file + '_return.proto'
        )
      rd = skills_pb2.ReturnValueDescription(
          return_value_message_full_name=return_full_name
      )
      rd.descriptor_fileset.CopyFrom(return_descriptor_set)
      self._description.return_value_description.CopyFrom(rd)
    elif return_value_message_full_name:
      rd = skills_pb2.ReturnValueDescription(
          return_value_message_full_name=return_value_message_full_name
      )
      rd.descriptor_fileset.CopyFrom(param_descriptor_set)
      self._description.return_value_description.CopyFrom(rd)

  def initialize_pbt_with_protos(
      self,
      *,
      parameter_proto: Optional[type[AnyType]] = None,
      return_value_proto: Optional[type[AnyType]] = None,
      skill_id: str = '',
      display_name: str = '',
  ):
    """Initializes a behavior tree to be a parameterizable behavior tree.

    Unlike `initialize_pbt`, this method is used to define PBTs with
    already-compiled protos that can have dependencies on other custom protos.

    The passed in protos must be compiled python protos that have a DESCRIPTOR
    property.

    To create the PBT as a Process asset, call set_asset_metadata() before
    calling this method and do not pass skill_id or display_name.

    Args:
      parameter_proto: The compiled proto message type for the parameters. (For
        example, `my_pbt_pb2.InputMessage`)
      return_value_proto: The compile proto message type for the return value.
      skill_id: The skill id that this PBT registers under. If the BehaviorTree
        already has asset metadata, this must not be given.
      display_name: The name to display the PBT with. If the BehaviorTree
        already has asset metadata, this must not be given.
    """
    self._initialize_skill_proto_for_pbt(skill_id, display_name)

    if parameter_proto:
      parameter_message_name, parameter_file_descriptor_set = (
          bt_util.build_file_descriptor_set(parameter_proto)
      )
      pd = skills_pb2.ParameterDescription(
          parameter_message_full_name=parameter_message_name
      )
      pd.parameter_descriptor_fileset.CopyFrom(parameter_file_descriptor_set)
      self._description.parameter_description.CopyFrom(pd)
    if return_value_proto:
      return_value_message_name, return_value_file_descriptor_set = (
          bt_util.build_file_descriptor_set(return_value_proto)
      )
      rd = skills_pb2.ReturnValueDescription(
          return_value_message_full_name=return_value_message_name
      )
      rd.descriptor_fileset.CopyFrom(return_value_file_descriptor_set)
      self._description.return_value_description.CopyFrom(rd)

  def set_signature(
      self, signature: proto_building.Signature | None = None
  ) -> None:
    """Sets the signature of the behavior tree.

    Sets the parameter and/or return value message of the behavior tree from the
    given signature.

    Args:
      signature: The signature defining the parameter and return value message.
        If set to None, an empty signature will be used (=no parameter and
        return value message).

    Raises:
      TypeError: If set_asset_metadata() has not been called beforehand.
    """
    if self._description is None:
      raise TypeError(
          'set_asset_metadata() must be called before set_signature()'
      )

    if signature is None:
      signature = proto_building.Signature()

    if signature.parameter_message_full_name:
      self._description.parameter_description.parameter_message_full_name = (
          signature.parameter_message_full_name
      )
      self._description.parameter_description.parameter_descriptor_fileset.CopyFrom(
          signature.file_descriptor_set
      )
    else:
      self._description.ClearField('parameter_description')

    if signature.return_value_message_full_name:
      self._description.return_value_description.return_value_message_full_name = (
          signature.return_value_message_full_name
      )
      self._description.return_value_description.descriptor_fileset.CopyFrom(
          signature.file_descriptor_set
      )
    else:
      self._description.ClearField('return_value_description')

  def get_signature(self) -> proto_building.Signature:
    """Returns the signature of the behavior tree.

    Also see set_signature().

    Returns:
      The signature as a proto_building.Signature object.

    Raises:
      TypeError: If the behavior tree has no signature.
    """
    if self._description is None:
      raise TypeError('BehaviorTree has no description with a signature')

    return proto_building.Signature(
        # The message names can be empty.
        parameter_message_full_name=(
            self._description.parameter_description.parameter_message_full_name
        ),
        return_value_message_full_name=(
            self._description.return_value_description.return_value_message_full_name
        ),
        # The file descriptor sets can be unset/empty and hence the merged set
        # can also be emtpy.
        file_descriptor_set=descriptors.merge_file_descriptor_sets([
            self._description.parameter_description.parameter_descriptor_fileset,
            self._description.return_value_description.descriptor_fileset,
        ]),
    )

  @property
  def params(self) -> blackboard_value.BlackboardValue:
    """Returns the blackboard value for the parameters of this behavior tree.

    Returns a BlackboardValue referencing the parameters key if this tree is a
    parameterizable behavior tree.
    """
    if self._description is None:
      raise TypeError(
          "description is not set. 'params' is only available for callable"
          ' processes.'
      )
    if not self._description.HasField('parameter_description'):
      raise TypeError(
          "parameter_description is not set. 'params' is only available for"
          ' processes with parameters.'
      )

    pool, message_classes = (
        skill_utils.generate_proto_infra_from_filedescriptorset(
            self._description.parameter_description.parameter_descriptor_fileset
        )
    )
    full_name = (
        self._description.parameter_description.parameter_message_full_name
    )
    descriptor = pool.FindMessageTypeByName(full_name)

    return blackboard_value.BlackboardValue(
        descriptor.fields_by_name,
        'params',
        message_classes[full_name],
        None,
    )

  def dot_graph(self) -> graphviz.Digraph:
    """Converts the given behavior tree into a graphviz dot representation.

    Returns:
      An instance of graphviz.Digraph, which is a tree-shaped directed graph.
    """
    dot_graph = graphviz.Digraph()
    dot_graph.name = self.name
    dot_graph.graph_attr = {'label': self.name if self.name else '<unnamed>'}
    dot_graph.graph_attr.update(_SUBTREE_DOT_ATTRIBUTES)
    if self.root is not None:
      subtree_dot_graph, _ = self.root.dot_graph()
      dot_graph.subgraph(subtree_dot_graph)
    return dot_graph

  def show(self) -> None:
    return ipython.display_if_ipython(self.dot_graph())

  def _sync_skill_proto_from_metadata(self):
    """Syncs the Skill proto from the Metadata proto."""

    # We only update parts of the Skill proto, thus we reuse the existing Skill
    # proto if it exists.
    if self._description is None:
      self._description = skills_pb2.Skill()

    self._description.behavior_tree_description.SetInParent()

    self._description.id = id_utils.id_from_proto(self._metadata.id_version.id)
    if self._metadata.id_version.version:
      self._description.id_version = id_utils.id_version_from_proto(
          self._metadata.id_version
      )
    else:
      self._description.ClearField('id_version')
    self._description.package_name = self._metadata.id_version.id.package
    self._description.skill_name = self._metadata.id_version.id.name
    self._description.display_name = self.name
    self._description.description = (
        self._metadata.documentation.description
        if self._metadata.HasField('documentation')
        else ''
    )
