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

"""Defines the ObjectWorldClient class.

The ObjectWorldClient is used to access all elements in the object world in
Python.
"""

from collections.abc import Iterable
from collections.abc import Iterator
from collections.abc import Mapping
import contextlib
from typing import cast
from typing import Optional
from typing import Union
import warnings

from google.protobuf import any_pb2
from google.protobuf import struct_pb2
import grpc

from intrinsic.geometry.proto import geometry_service_pb2
from intrinsic.geometry.proto import geometry_service_pb2_grpc
from intrinsic.geometry.proto import geometry_storage_refs_pb2 as geometry_storage_refs_pb2_v0
from intrinsic.geometry.proto.v1 import geometry_storage_refs_pb2 as geometry_storage_refs_pb2_v1
from intrinsic.icon.equipment import icon_equipment_pb2
from intrinsic.icon.proto import cart_space_pb2
from intrinsic.kinematics.types import joint_limits_pb2
from intrinsic.math.python import data_types
from intrinsic.math.python import proto_conversion as math_proto_conversion
from intrinsic.resources.proto import resource_handle_pb2
from intrinsic.scene.proto.v1 import scene_object_pb2
from intrinsic.util.grpc import error_handling
from intrinsic.world.proto import geometry_component_pb2
from intrinsic.world.proto import object_world_refs_pb2
from intrinsic.world.proto import object_world_service_pb2
from intrinsic.world.proto import object_world_service_pb2_grpc
from intrinsic.world.proto import object_world_updates_pb2
from intrinsic.world.python import object_world_ids
from intrinsic.world.python import object_world_resources
from intrinsic.world.robot_payload.python import robot_payload

# Convenience constant for an ObjectEntityFilter that selects only the base
# entity.
INCLUDE_BASE_ENTITY = object_world_refs_pb2.ObjectEntityFilter(
    include_base_entity=True
)

# Convenience constant for an ObjectEntityFilter that selects only the final
# entity.
INCLUDE_FINAL_ENTITY = object_world_refs_pb2.ObjectEntityFilter(
    include_final_entity=True
)

# Convenience constant for an ObjectEntityFilter that selects all entities.
INCLUDE_ALL_ENTITIES = object_world_refs_pb2.ObjectEntityFilter(
    include_all_entities=True
)

ICON2_POSITION_PART_KEY = 'Icon2PositionPart'


class CreateObjectError(ValueError):
  """A failure occurred while creating an object."""


def list_world_ids(
    stub: object_world_service_pb2_grpc.ObjectWorldServiceStub,
) -> list[str]:
  """Lists all worlds in the world service.

  Args:
    stub: The object world service stub.

  Returns:
    A list with the ids of all worlds.
  """
  request = object_world_service_pb2.ListWorldsRequest()
  response: object_world_service_pb2.ListWorldsResponse = stub.ListWorlds(
      request
  )

  return [world.id for world in response.world_metadatas]


def _has_grpc_status(
    grpc_error: grpc.RpcError, grpc_status: grpc.StatusCode
) -> bool:
  return cast(grpc.Call, grpc_error).code() == grpc_status


def _get_path_from_root(
    world_object: object_world_resources.WorldObject,
    id_to_object: dict[
        object_world_ids.ObjectWorldResourceId,
        object_world_resources.WorldObject,
    ],
) -> str:
  """Returns the full path names of world_object based on a id_to_object dict.

  Args:
    world_object: The object to get the full name for
    id_to_object: A dict mapping of all objects in the world

  Returns:
    A . joined full path that can be addressed from the world.
    E.g., "" for the root object; "my_robot.my_gripper" for a gripper attached
    to a robot object.
  """
  if world_object.id == object_world_ids.ROOT_OBJECT_ID:
    return ''

  names: list[str] = [world_object.name]
  parent_id = world_object.parent_id
  while parent_id != object_world_ids.ROOT_OBJECT_ID:
    parent = id_to_object[parent_id]
    names.append(parent.name)
    parent_id = parent.parent_id

  return '.'.join(reversed(names))


def _get_transform_node_reference(
    reference: Union[
        object_world_ids.WorldObjectName,
        object_world_resources.TransformNode,
        object_world_refs_pb2.TransformNodeReference,
    ],
) -> object_world_refs_pb2.TransformNodeReference:
  """Returns the TransformNodeReference for the given reference.

  This helper is used to translate from the union to a concrete instance of
  TransformNodeReference.

  Args:
    reference: The id of the transform node or a reference (by id or name) to
      the transform node.

  Returns:
    A TransformNodeReference that can be used to address the input reference.
  """
  if isinstance(reference, str):
    # This is the case if object_reference is a
    # object_world_ids.WorldObjectName. To allow the usage with Python strings
    # as argument in non type checked environments like jupyter it checks for
    # type str here.
    return object_world_refs_pb2.TransformNodeReference(
        by_name=object_world_refs_pb2.TransformNodeReferenceByName(
            object=object_world_refs_pb2.ObjectReferenceByName(
                object_name=reference
            )
        )
    )
  elif isinstance(reference, object_world_refs_pb2.TransformNodeReference):
    return reference
  elif isinstance(reference, object_world_resources.TransformNode):
    return reference.transform_node_reference
  else:
    raise TypeError(
        'Only TransformNodeReference, TransformNode or WorldObjectName are '
        'valid input types.'
    )


class ObjectWorldClient:
  """Provides access to a remote world in the world service.

  The ObjectWorldClient is a Python client for the object world service. It
  gives access to objects and frames in the world and returns Python objects for
  frames and objects.

  Attributes:
    world_id: The world's ID.
  """

  @property
  def world_id(self) -> str:
    return self._world_id

  def __init__(
      self,
      world_id: str,
      stub: object_world_service_pb2_grpc.ObjectWorldServiceStub,
      geometry_service_stub: Optional[
          geometry_service_pb2_grpc.GeometryServiceStub
      ] = None,
  ):
    self._stub: object_world_service_pb2_grpc.ObjectWorldServiceStub = stub

    if geometry_service_stub is not None:
      self._geometry_service_stub: (
          geometry_service_pb2_grpc.GeometryServiceStub
      ) = geometry_service_stub
    else:
      self._geometry_service_stub = None

    self._world_id: str = world_id

  def list_object_names(self) -> list[object_world_ids.WorldObjectName]:
    """Lists the names of all objects in the world service.

    Returns:
      A list with the names of all objects in the world.
    """
    object_names = self._get_object_names()
    return object_names

  def list_object_full_paths(self) -> list[str]:
    """Lists the full path names of all objects from the world namespace.

    Returns:
      A list with the fully qualified names of all objects in the world.
      E.g. ['robot', 'robot.gripper', 'robot.gripper.workpiece', 'workcell']
    """
    objects = self.list_objects()
    id_to_object = {world_object.id: world_object for world_object in objects}
    result: list[str] = []
    for world_object in objects:
      if world_object.id == object_world_ids.ROOT_OBJECT_ID:
        continue
      result.append(_get_path_from_root(world_object, id_to_object))
    return result

  def _create_object_with_auto_type(
      self, world_object: object_world_service_pb2.Object
  ) -> object_world_resources.WorldObject:
    """Creates an object from a object proto."""
    return object_world_resources.create_object_with_auto_type(
        world_object, self._stub
    )

  def list_objects(
      self,
      query_objects: Optional[
          list[object_world_refs_pb2.ObjectReference]
      ] = None,
  ) -> list[object_world_resources.WorldObject]:
    """List all objects in the world service.

    Args:
      query_objects: Optional list of object references to use as a query. If
        specified only objects that are in the query will be returned in the
        result.

    Returns:
      A list with all objects in the world.
    """
    response = self._stub.ListObjects(
        object_world_service_pb2.ListObjectsRequest(
            world_id=self._world_id,
            query_objects=[] if query_objects is None else query_objects,
            view=object_world_updates_pb2.ObjectView.FULL,
        )
    )
    return [
        self._create_object_with_auto_type(world_object)
        for world_object in response.objects
    ]

  @error_handling.retry_on_grpc_unavailable
  def _get_object_proto(
      self,
      object_reference: Union[
          object_world_ids.WorldObjectName,
          object_world_refs_pb2.ObjectReference,
          resource_handle_pb2.ResourceHandle,
      ],
  ) -> object_world_service_pb2.Object:
    """Returns the proto of an object by its unique name."""
    request = object_world_service_pb2.GetObjectRequest()
    request.world_id = self._world_id
    if isinstance(object_reference, str):
      # This is the case if object_reference is a
      # object_world_ids.WorldObjectName. To allow the usage with Python strings
      # as argument in non type checked environments like jupyter it checks for
      # type str here.
      request.object.by_name.object_name = object_reference
    elif isinstance(object_reference, object_world_refs_pb2.ObjectReference):
      request.object.CopyFrom(object_reference)
    elif isinstance(object_reference, resource_handle_pb2.ResourceHandle):
      request.resource_handle_name = object_reference.name
    else:
      raise TypeError(
          'Only ObjectReference,  WorldObjectName or ResourceHandle are '
          'valid input types.'
      )
    request.view = object_world_updates_pb2.ObjectView.FULL
    return self._stub.GetObject(request)

  def _get_transform_node_by_id(
      self, resource_id: object_world_ids.ObjectWorldResourceId
  ) -> object_world_resources.TransformNode:
    """Returns a transform node by its id."""
    # If we are given an id, it could be either an object or a frame. Since
    # there is no dedicated endpoint for this yet, first try to get as an
    # object, else try getting as a frame.
    try:
      return self.get_object(
          object_world_refs_pb2.ObjectReference(id=resource_id)
      )
    except grpc.RpcError as object_error:
      if not _has_grpc_status(object_error, grpc.StatusCode.NOT_FOUND):
        raise

      try:
        return self.get_frame(
            object_world_refs_pb2.FrameReference(id=resource_id)
        )
      except grpc.RpcError as frame_error:
        if not _has_grpc_status(frame_error, grpc.StatusCode.NOT_FOUND):
          raise
        raise LookupError(
            'No object or frame corresponding to the '
            f'id "{resource_id}" was found.'
        ) from frame_error

  def _get_transform_node_by_name(
      self, reference: object_world_refs_pb2.TransformNodeReferenceByName
  ) -> object_world_resources.TransformNode:
    """Returns a transform node by its name."""
    if reference.HasField('object'):
      return self.get_object(
          object_world_ids.WorldObjectName(reference.object.object_name)
      )
    elif reference.HasField('frame'):
      return self.get_frame(
          object_world_ids.FrameName(reference.frame.frame_name),
          object_world_ids.WorldObjectName(reference.frame.object_name),
      )
    else:
      raise ValueError(
          'Unsupported type of TransformNodeReference whose nested '
          'TransformNodeReferenceByName has neither "object" nor "frame" '
          'set.'
      )

  def get_transform_node(
      self,
      reference: Union[
          object_world_ids.ObjectWorldResourceId,
          object_world_refs_pb2.TransformNodeReference,
      ],
  ) -> object_world_resources.TransformNode:
    """Returns a transform node (object or frame).

    Args:
      reference: The id of the transform node or a reference (by id or name) to
        the transform node.

    Returns:
      The transform node in the world. Either a Frame or a subclass of
      WorldObject.
    """
    if isinstance(reference, object_world_ids.ObjectWorldResourceId):
      return self._get_transform_node_by_id(reference)
    elif isinstance(reference, object_world_refs_pb2.TransformNodeReference):
      if reference.HasField('id'):
        return self._get_transform_node_by_id(
            object_world_ids.ObjectWorldResourceId(reference.id)
        )
      elif reference.HasField('by_name'):
        return self._get_transform_node_by_name(reference.by_name)
      else:
        raise ValueError(
            'Unsupported type of TransformNodeReference'
            ' which has neither "id" nor "by_name" set.'
        )
    else:
      raise TypeError(
          'Only ObjectWorldResourceId and TransformNodeReference are supported.'
      )

  def get_object(
      self,
      object_reference: Union[
          object_world_ids.WorldObjectName,
          object_world_refs_pb2.ObjectReference,
          resource_handle_pb2.ResourceHandle,
      ],
  ) -> object_world_resources.WorldObject:
    """Returns an object by its unique name.

    Args:
      object_reference: The name or reference of the object.

    Returns:
      An object in the world as an instance of WorldObject or a subclass
      thereof.
    """
    return self._create_object_with_auto_type(
        self._get_object_proto(object_reference)
    )

  def get_kinematic_object(
      self,
      object_reference: Union[
          object_world_ids.WorldObjectName,
          object_world_refs_pb2.ObjectReference,
          resource_handle_pb2.ResourceHandle,
      ],
  ) -> object_world_resources.KinematicObject:
    """Returns a kinematic object by its unique name.

    Args:
      object_reference: The name of the object, a reference to the object or the
        name of the equipment associated with the object.

    Returns:
      A kinematic object in the world.

    Raises:
      ValueError: The requested object has no kinematic component and cannot be
      used as kinematic object.
    """
    if (
        isinstance(object_reference, resource_handle_pb2.ResourceHandle)
        and ICON2_POSITION_PART_KEY in object_reference.resource_data
    ):
      icon_position_part = icon_equipment_pb2.Icon2PositionPart()
      object_reference.resource_data[ICON2_POSITION_PART_KEY].contents.Unpack(
          icon_position_part
      )
      if icon_position_part.world_robot_collection_name:
        return self.get_kinematic_object(
            object_world_ids.WorldObjectName(
                icon_position_part.world_robot_collection_name
            )
        )
    return object_world_resources.KinematicObject(
        self._get_object_proto(object_reference), self._stub
    )

  @error_handling.retry_on_grpc_unavailable
  def get_frame(
      self,
      frame_reference: Union[
          object_world_refs_pb2.FrameReference, object_world_ids.FrameName
      ],
      object_name: Optional[
          Union[
              object_world_ids.WorldObjectName,
              resource_handle_pb2.ResourceHandle,
          ]
      ] = None,
  ) -> object_world_resources.Frame:
    """Returns a frame by its reference.

    This method accepts a FrameReference or the names of frame and parent
    object as argument. Expected uses look like:

    world.get_frame(object_world_ids.FrameName('my_frame'),
        object_world_ids.WorldObjectName('my_object'))

    world.get_frame(object_world_refs_pb2.FrameReference(
        by_name=object_world_refs_pb2.FrameReferenceByName(
            frame_name='my_frame',
            object_name='my_object')))

    Note, if you provide an object by its resource handle, then the frame must
    be referenced by the frame's name.

    Only in Jupyter does it also work with normal strings:

    world.get_frame('my_frame', 'my_object')

    Args:
      frame_reference: A reference to the requested frame.
      object_name: The optional reference to the frames parent object.

    Returns:
      The frame in the world.
    """
    request = object_world_service_pb2.GetFrameRequest(world_id=self._world_id)
    if (
        isinstance(frame_reference, object_world_refs_pb2.FrameReference)
        and object_name is None
    ):
      request.frame.CopyFrom(frame_reference)
    elif isinstance(frame_reference, str) and isinstance(object_name, str):
      # This is the case if frame_reference is a object_world_ids.FrameName. To
      # allow the usage with Python strings as argument in non type checked
      # environments like jupyter it checks for type str here.
      request.frame.by_name.object_name = object_name
      request.frame.by_name.frame_name = frame_reference
    elif isinstance(
        object_name, resource_handle_pb2.ResourceHandle
    ) and isinstance(frame_reference, str):
      return self.get_object(object_name).get_frame(frame_reference)
    else:
      raise TypeError('get_frame is called with the wrong arguments.')
    return object_world_resources.Frame(
        self._stub.GetFrame(request), self._stub
    )

  @error_handling.retry_on_grpc_unavailable
  def get_transform(
      self,
      node_a: Union[
          object_world_ids.WorldObjectName,
          object_world_resources.TransformNode,
          object_world_refs_pb2.TransformNodeReference,
      ],
      node_b: Union[
          object_world_ids.WorldObjectName,
          object_world_resources.TransformNode,
          object_world_refs_pb2.TransformNodeReference,
      ],
  ) -> data_types.Pose3:
    """Get the transform between two nodes in the world.

    Args:
      node_a: The first transform node.
      node_b: the second transform node.

    Returns:
      The transform 'a_t_b', i.e., the pose of 'node_b' in the space of
      'node_a'. 'node_a' and 'node_b' can be arbitrary nodes in the transform
      tree of the world and don't have to be parent and child.
    """
    response = self._stub.GetTransform(
        object_world_service_pb2.GetTransformRequest(
            world_id=self._world_id,
            node_a=_get_transform_node_reference(node_a),
            node_b=_get_transform_node_reference(node_b),
        )
    )
    return math_proto_conversion.pose_from_proto(response.a_t_b)

  @error_handling.retry_on_grpc_unavailable
  def update_transform(
      self,
      node_a: Union[
          object_world_ids.WorldObjectName,
          object_world_resources.TransformNode,
          object_world_refs_pb2.TransformNodeReference,
      ],
      node_b: Union[
          object_world_ids.WorldObjectName,
          object_world_resources.TransformNode,
          object_world_refs_pb2.TransformNodeReference,
      ],
      a_t_b: data_types.Pose3,
      node_to_update: Optional[
          Union[
              object_world_ids.WorldObjectName,
              object_world_resources.TransformNode,
              object_world_refs_pb2.TransformNodeReference,
          ]
      ] = None,
  ) -> None:
    """Updates the pose between two nodes.

    If node_to_update is None this updates the pose between two neighboring
    nodes 'node_a' and 'node_b' such that the transform between the two becomes
    'a_t_b'. If 'node_b' is the direct child of 'node_a', 'node_b.parent_t_this'
    is updated; if 'node_a' is the direct child of 'node_b',
    'node_a.parent_t_this' is updated; otherwise, an error will be returned.

    If node_to_update is not None this updates the pose of the given
    'node_to_update' in the space of its parent  (i.e.,
    'node_to_update.parent_t_this') such that the transform between  'node_a'
    and 'node_b' becomes 'a_t_b'. Returns an error if 'node_to_update'
    is not located on the path from 'node_a' to 'node_b'. It is valid to set
    'node_a'=='node_to_update' or 'node_b'=='node_to_update'.

    Args:
      node_a: First transform node.
      node_b: The second transform node.
      a_t_b: The desired transform between the two nodes 'node_a' and 'node_b'.
      node_to_update: Optional transform node whose pose (between itself and its
        parent) shall be updated. Can be left out if 'node_a' and 'node_b' are
        neighbors in the transform tree, then the pose of the child node will be
        updated.

    Raises:
      RpcError: Error communicating with the world service.
    """
    request = object_world_updates_pb2.UpdateTransformRequest(
        world_id=self._world_id,
        node_a=_get_transform_node_reference(node_a),
        node_b=_get_transform_node_reference(node_b),
        a_t_b=math_proto_conversion.pose_to_proto(a_t_b),
        view=object_world_updates_pb2.ObjectView.BASIC,
    )

    if node_to_update is not None:
      request.node_to_update.CopyFrom(
          _get_transform_node_reference(node_to_update)
      )

    self._stub.UpdateTransform(request)

  @property
  def stub(self) -> object_world_service_pb2_grpc.ObjectWorldServiceStub:
    """Returns the gRPC stub."""
    return self._stub

  @error_handling.retry_on_grpc_unavailable
  def update_object_name(
      self,
      object_to_update: object_world_resources.WorldObject,
      new_name: object_world_ids.WorldObjectName,
      *,
      name_is_global_alias: Optional[bool] = None,
  ) -> None:
    """Changes the name of the given object to the given name.

    Args:
      object_to_update: The object that should be updated.
      new_name: The new name of the object.
      name_is_global_alias: Deprecated and ignored. All object names are global
        aliases.

    Raises:
      InvalidArgumentError: The new name is already used for another object.
    """
    if name_is_global_alias is not None:
      warnings.warn(
          'The `name_is_global_alias` parameter in `update_object_name` is'
          ' deprecated and ignored. All object names are global aliases.',
          category=DeprecationWarning,
          stacklevel=2,
      )
    self._stub.UpdateObjectName(
        object_world_updates_pb2.UpdateObjectNameRequest(
            world_id=self._world_id,
            object=object_world_refs_pb2.ObjectReference(
                id=object_to_update.id
            ),
            name=new_name,
            view=object_world_updates_pb2.ObjectView.BASIC,
            name_is_global_alias=True,
        )
    )

  @error_handling.retry_on_grpc_unavailable
  def update_frame_name(
      self,
      frame_to_update: object_world_resources.Frame,
      new_name: object_world_ids.FrameName,
  ) -> None:
    """Changes the name of the given frame to the given name.

    Args:
      frame_to_update: The frame that should be updated.
      new_name: The name of the new frame.

    Raises:
      InvalidArgumentError: The new name is already used for another frame under
      the same object.
    """

    self._stub.UpdateFrameName(
        object_world_updates_pb2.UpdateFrameNameRequest(
            world_id=self._world_id,
            frame=object_world_refs_pb2.FrameReference(id=frame_to_update.id),
            name=new_name,
        )
    )

  @error_handling.retry_on_grpc_unavailable
  def update_joint_positions(
      self,
      kinematic_object: object_world_resources.KinematicObject,
      joint_positions: list[float],
      joint_names: Optional[list[str]] = None,
  ) -> None:
    """Sets the joint positions of the kinematic object to the given values.

    Args:
      kinematic_object: The kinematic object that should be changed.
      joint_positions: The new joint positions in radians (for revolute joints)
        or meters (for prismatic joints).
      joint_names: Optional joint names to correspond to the given
        joint_positions must either be empty or match the size of the
        joint_positions list.
    """
    self._stub.UpdateObjectJoints(
        object_world_updates_pb2.UpdateObjectJointsRequest(
            world_id=self._world_id,
            object=kinematic_object.reference,
            joint_positions=joint_positions,
            joint_names=[] if joint_names is None else joint_names,
            view=object_world_updates_pb2.ObjectView.BASIC,
        )
    )

  @error_handling.retry_on_grpc_unavailable
  def update_joint_application_limits(
      self,
      kinematic_object: object_world_resources.KinematicObject,
      joint_limits: joint_limits_pb2.JointLimitsUpdate,
  ) -> None:
    """Sets the joint application limits of the kinematic object to the given values.

    Args:
      kinematic_object: The kinematic object that should be changed.
      joint_limits: The new joint limits. The field JointLimits.max_effort is
        currently not supported and will be ignored.
    """
    self._stub.UpdateObjectJoints(
        object_world_updates_pb2.UpdateObjectJointsRequest(
            world_id=self._world_id,
            object=kinematic_object.reference,
            joint_application_limits=joint_limits,
            view=object_world_updates_pb2.ObjectView.BASIC,
        )
    )

  @error_handling.retry_on_grpc_unavailable
  def update_kinematic_object_joint_configurations(
      self,
      kinematic_object: object_world_resources.KinematicObject,
      named_joint_configurations_to_remove: Iterable[str] | None = None,
      named_joint_configurations_to_set: (
          Iterable[object_world_updates_pb2.NamedJointConfiguration] | None
      ) = None,
  ):
    """Updates the named joint configurations of the kinematic object.

    Args:
      kinematic_object: The kinematic object that should be changed.
      named_joint_configurations_to_remove: A list of named joint configurations
        to remove.
      named_joint_configurations_to_set: A list of named joint configurations to
        update. Overrides existing named joint configurations. Creates new
        configurations if the name is not already in use.
    """
    self._stub.UpdateKinematicObjectProperties(
        object_world_updates_pb2.UpdateKinematicObjectPropertiesRequest(
            world_id=self._world_id,
            object=kinematic_object.reference,
            named_joint_configurations_to_remove=named_joint_configurations_to_remove,
            named_joint_configurations_to_set=named_joint_configurations_to_set,
        ),
    )

  @error_handling.retry_on_grpc_unavailable
  def update_kinematic_object_cartesian_limits(
      self,
      kinematic_object: object_world_resources.KinematicObject,
      limits: cart_space_pb2.CartesianLimits,
  ) -> None:
    """Sets the cartesian limits of the kinematic object to the given values.

    Args:
      kinematic_object: The kinematic object that should be changed.
      limits: The new cartesian limits.
    """
    self._stub.UpdateKinematicObjectProperties(
        object_world_updates_pb2.UpdateKinematicObjectPropertiesRequest(
            world_id=self._world_id,
            object=kinematic_object.reference,
            cartesian_limits=limits,
        )
    )

  @error_handling.retry_on_grpc_unavailable
  def update_kinematic_object_payload(
      self,
      kinematic_object: object_world_resources.KinematicObject,
      payload: robot_payload.RobotPayload,
  ) -> None:

    self._stub.UpdateKinematicObjectProperties(
        object_world_updates_pb2.UpdateKinematicObjectPropertiesRequest(
            world_id=self._world_id,
            object=kinematic_object.reference,
            mounted_payload=robot_payload.payload_to_proto(payload),
        )
    )

  @error_handling.retry_on_grpc_unavailable
  def update_joint_system_limits(
      self,
      kinematic_object: object_world_resources.KinematicObject,
      joint_limits: joint_limits_pb2.JointLimitsUpdate,
  ) -> None:
    """Sets the joint system limits of the kinematic object to the given values.

    Args:
      kinematic_object: The kinematic object that should be changed.
      joint_limits: The new joint system limits. The field
        JointLimits.max_effort is currently not supported and will be ignored.
    """
    self._stub.UpdateObjectJoints(
        object_world_updates_pb2.UpdateObjectJointsRequest(
            world_id=self._world_id,
            object=kinematic_object.reference,
            joint_system_limits=joint_limits,
            view=object_world_updates_pb2.ObjectView.BASIC,
        )
    )

  @error_handling.retry_on_grpc_unavailable
  def _call_reparent_object(
      self,
      child_object: object_world_resources.WorldObject,
      new_parent: (
          object_world_resources.WorldObject | object_world_resources.Frame
      ),
      entity_filter: object_world_refs_pb2.ObjectEntityFilter,
  ) -> None:
    """Reparents an object to a new parent object or frame.

    Args:
      child_object: The object that should be reparented.
      new_parent: The new parent object or frame.
      entity_filter: The object entity filter.
    """
    request = object_world_updates_pb2.ReparentObjectRequest(
        world_id=self._world_id,
        object=object_world_refs_pb2.ObjectReference(id=child_object.id),
    )

    if isinstance(new_parent, object_world_resources.WorldObject):
      request.parent_object.CopyFrom(
          object_world_refs_pb2.ObjectReferenceWithEntityFilter(
              reference=object_world_refs_pb2.ObjectReference(id=new_parent.id),
              entity_filter=entity_filter,
          )
      )
    elif isinstance(new_parent, object_world_resources.Frame):
      request.parent_frame.CopyFrom(
          object_world_refs_pb2.FrameReference(id=new_parent.id)
      )
    else:
      raise ValueError(f'Unsupported new parent type: {type(new_parent)}')

    self._stub.ReparentObject(request)

  def reparent_object(
      self,
      child_object: object_world_resources.WorldObject,
      new_parent: (
          object_world_resources.WorldObject | object_world_resources.Frame
      ),
  ) -> None:
    """Reparents an object to a new parent object or frame.

    Leaves the global pose of the reparented object unaffected (i.e.,
    "parent_t_object" might change but "root_t_object" will not change).

    If the new parent object is a kinematic object, this method attaches the
    child object to the parent's base object entity (also see
    reparent_object_to_final_entity()).

    Args:
      child_object: The object that should be reparented.
      new_parent: The new parent object.
    """
    self._call_reparent_object(
        child_object,
        new_parent,
        object_world_refs_pb2.ObjectEntityFilter(include_base_entity=True),
    )

  def reparent_object_to(
      self,
      child_object: object_world_resources.WorldObject,
      new_parent: object_world_resources.WorldObject,
      entity_filter: object_world_refs_pb2.ObjectEntityFilter,
  ) -> None:
    """Reparents an object to a new parent object.

    Leaves the global pose of the reparented object unaffected (i.e.,
    "parent_t_object" might change but "root_t_object" will not change).

    This method attaches the child object to the parent's object entity that
    matches the given filter.

    Args:
      child_object: The object that should be reparented.
      new_parent: The new parent object.
      entity_filter: The object entity filter.
    """
    self._call_reparent_object(child_object, new_parent, entity_filter)

  def reparent_object_to_final_entity(
      self,
      child_object: object_world_resources.WorldObject,
      new_parent: object_world_resources.KinematicObject,
  ) -> None:
    """Reparents an object to the final entity of a kinematic object.

    Leaves the global pose of the reparented object unaffected (i.e.,
    "parent_t_object" might change but "root_t_object" will not change).

    If a final entity of the new parent object cannot be determined uniquely, an
    error will be returned.

    Args:
      child_object: The object that should be reparented.
      new_parent: The new parent object, which must be a kinematic object.
    """
    self._call_reparent_object(
        child_object,
        new_parent,
        object_world_refs_pb2.ObjectEntityFilter(include_final_entity=True),
    )

  @error_handling.retry_on_grpc_unavailable
  def _call_toggle_collisions(
      self,
      toggle_mode: object_world_updates_pb2.ToggleMode,
      first_object: object_world_resources.WorldObject,
      second_object: object_world_resources.WorldObject,
      first_entity_filter: object_world_refs_pb2.ObjectEntityFilter,
      second_entity_filter: object_world_refs_pb2.ObjectEntityFilter,
  ) -> None:
    return self._stub.ToggleCollisions(
        object_world_updates_pb2.ToggleCollisionsRequest(
            world_id=self._world_id,
            toggle_mode=toggle_mode,
            object_a=object_world_refs_pb2.ObjectReferenceWithEntityFilter(
                reference=object_world_refs_pb2.ObjectReference(
                    id=first_object.id
                ),
                entity_filter=first_entity_filter,
            ),
            object_b=object_world_refs_pb2.ObjectReferenceWithEntityFilter(
                reference=object_world_refs_pb2.ObjectReference(
                    id=second_object.id
                ),
                entity_filter=second_entity_filter,
            ),
            view=object_world_updates_pb2.ObjectView.BASIC,
        )
    )

  def disable_collisions(
      self,
      first_object: object_world_resources.WorldObject,
      second_object: object_world_resources.WorldObject,
      *,
      first_entity_filter: object_world_refs_pb2.ObjectEntityFilter = INCLUDE_ALL_ENTITIES,
      second_entity_filter: object_world_refs_pb2.ObjectEntityFilter = INCLUDE_ALL_ENTITIES,
  ) -> None:
    """Disables collisions between two objects.

    Disables collision detection between all pairs (a, b) of entities where a is
    a  entity of 'object_a' and selected by 'entity_filter_a' and b is an entity
    of 'object_b' and selected by 'entity_filter_b'.

    Succeeds and has no effect if collisions were already disabled.

    Args:
      first_object: The first object.
      second_object: The second object.
      first_entity_filter: Entity filter for the first object. By default all
        object entities will be included.
      second_entity_filter: Entity filter for the second object. By default all
        object entities will be included.
    """
    self._call_toggle_collisions(
        object_world_updates_pb2.TOGGLE_MODE_DISABLE,
        first_object,
        second_object,
        first_entity_filter,
        second_entity_filter,
    )

  def enable_collisions(
      self,
      first_object: object_world_resources.WorldObject,
      second_object: object_world_resources.WorldObject,
      *,
      first_entity_filter: object_world_refs_pb2.ObjectEntityFilter = INCLUDE_ALL_ENTITIES,
      second_entity_filter: object_world_refs_pb2.ObjectEntityFilter = INCLUDE_ALL_ENTITIES,
  ) -> None:
    """Enables collisions between two objects.

    Enables collision detection between all pairs (a, b) of entities where a is
    an entity of 'object_a' and selected by 'entity_filter_a' and b is an entity
    of 'object_b' and selected by 'entity_filter_b'.

    Succeeds and has no effect if collisions were already enabled.

    Args:
      first_object: The first object.
      second_object: The second object.
      first_entity_filter: Entity filter for the first object. By default all
        object entities will be included.
      second_entity_filter: Entity filter for the second object. By default all
        object entities will be included.
    """
    self._call_toggle_collisions(
        object_world_updates_pb2.TOGGLE_MODE_ENABLE,
        first_object,
        second_object,
        first_entity_filter,
        second_entity_filter,
    )

  @error_handling.retry_on_grpc_unavailable
  def _get_objects_and_frames_under_root(
      self,
  ) -> tuple[
      dict[
          object_world_ids.WorldObjectName,
          object_world_refs_pb2.ObjectReference,
      ],
      dict[object_world_ids.FrameName, object_world_refs_pb2.FrameReference],
  ]:
    """Returns name to reference dicts for both objects and frames under the root object namespace."""
    # This is a special helper method to enable __dir__ and __get_attr__ with
    # only one Rpc.
    world_objects_proto = self._stub.ListObjects(
        object_world_service_pb2.ListObjectsRequest(
            world_id=self._world_id,
            view=object_world_updates_pb2.ObjectView.BASIC,
        )
    ).objects

    object_name_to_ref: dict[
        object_world_ids.WorldObjectName, object_world_refs_pb2.ObjectReference
    ] = dict()
    frame_name_to_ref: dict[
        object_world_ids.FrameName, object_world_refs_pb2.FrameReference
    ] = dict()

    for world_object in world_objects_proto:
      if (
          world_object.name_is_global_alias
          or world_object.parent.id == object_world_ids.ROOT_OBJECT_ID
      ):
        object_name_to_ref[world_object.name] = (
            object_world_refs_pb2.ObjectReference(id=world_object.id)
        )

      if world_object.id == object_world_ids.ROOT_OBJECT_ID:
        for frame in world_object.frames:
          frame_name_to_ref[frame.name] = object_world_refs_pb2.FrameReference(
              by_name=object_world_refs_pb2.FrameReferenceByName(
                  object_name=object_world_ids.ROOT_OBJECT_NAME,
                  frame_name=frame.name,
              )
          )

    return object_name_to_ref, frame_name_to_ref

  @error_handling.retry_on_grpc_unavailable
  def _get_object_names(self) -> list[object_world_ids.WorldObjectName]:
    """Returns the object names and the root object with a single Rpc."""
    world_objects_proto = self._stub.ListObjects(
        object_world_service_pb2.ListObjectsRequest(
            world_id=self._world_id,
            view=object_world_updates_pb2.ObjectView.BASIC,
        )
    ).objects

    object_names: list[object_world_ids.WorldObjectName] = list()

    for world_object in world_objects_proto:
      object_names.append(world_object.name)

    return object_names

  def __getattr__(self, name: str) -> object_world_resources.TransformNode:
    object_ref_from_name, frame_ref_from_name = (
        self._get_objects_and_frames_under_root()
    )
    if object_world_ids.WorldObjectName(name) in object_ref_from_name:
      return self._create_object_with_auto_type(
          self._get_object_proto(
              object_ref_from_name[object_world_ids.WorldObjectName(name)]
          )
      )
    elif object_world_ids.FrameName(name) in frame_ref_from_name:
      return self.get_frame(
          frame_ref_from_name[object_world_ids.FrameName(name)]
      )
    else:
      # __getattr__is only allowed to throw AttributeErrors. If it throws other
      # errors like a RpcError this has non obvious side-effects, for example
      # autocomplete in jupyter is broken.
      raise AttributeError(
          f'{self.__repr__()} does not have an object or member with name'
          f' "{name}". Object names need to either be the name of an object'
          ' below root or the name of an object which has the'
          ' "name_is_global_alias" option enabled.'
      )

  def __dir__(self) -> list[str]:
    object_name_to_ref, frame_name_to_ref = (
        self._get_objects_and_frames_under_root()
    )
    return sorted(
        [str(object_name) for object_name in object_name_to_ref.keys()]
        + [str(frame_name) for frame_name in frame_name_to_ref.keys()]
        + object_world_resources._list_public_methods(self)
    )

  def __repr__(self) -> str:
    return f'<ObjectWorldClient(world_id={self._world_id})>'

  def _create_frame_string_lines(
      self,
      frame: object_world_resources.Frame,
      frame_map: dict[object_world_ids.FrameName, object_world_resources.Frame],
  ) -> list[str]:
    """Creates a frame string with all child frames."""
    elements: list[str] = []
    elements += [f'{frame.name}: Frame(id={frame.id})']

    for frame_name in sorted(frame.child_frame_names):
      elements += self._add_indent(
          self._create_frame_string_lines(frame_map[frame_name], frame_map),
          is_frame=True,
      )
    return elements

  def _create_object_string_lines(
      self,
      node: object_world_resources.WorldObject,
      node_map: dict[
          object_world_ids.ObjectWorldResourceId,
          object_world_resources.WorldObject,
      ],
  ) -> list[str]:
    """Creates a nice object string with all child objects."""
    elements: list[str] = []
    elements.append(f'{node.name}: {node.__class__.__name__}(id={node.id})')
    child_id_and_names: list[
        tuple[object_world_ids.ObjectWorldResourceId, str]
    ] = [(child_id, node_map[child_id].name) for child_id in node.child_ids]

    for child_id, _ in sorted(child_id_and_names, key=lambda x: x[1]):
      elements += self._add_indent(
          self._create_object_string_lines(node_map[child_id], node_map)
      )

    frame_map: dict[
        object_world_ids.FrameName, object_world_resources.Frame
    ] = {frame.name: frame for frame in node.frames}
    for frame_name in node.child_frame_names:
      elements += self._add_indent(
          self._create_frame_string_lines(frame_map[frame_name], frame_map),
          is_frame=True,
      )
    return elements

  def _add_indent(
      self,
      object_string_lines: list[str],
      *,
      is_frame: bool = False,
      is_top_level: bool = False,
  ) -> list[str]:
    """Indents the string to create a tree like structure."""
    lines: list[str] = []
    for line_idx, line in enumerate(object_string_lines):
      if line_idx == 0:
        if is_frame:
          line = '-> ' + line
        else:
          line = '=> ' + line
      else:
        line = '   ' + line
      if not is_top_level:
        line = '|' + line
      lines.append(line)
    return lines

  def __str__(self) -> str:
    world_objects_map: dict[
        object_world_ids.ObjectWorldResourceId,
        object_world_resources.WorldObject,
    ] = {world_object.id: world_object for world_object in self.list_objects()}
    lines: list[str] = []
    lines.append(f'World(world_id={self._world_id})')
    lines += self._add_indent(
        self._create_object_string_lines(
            world_objects_map[object_world_ids.ROOT_OBJECT_ID],
            world_objects_map,
        ),
        is_top_level=True,
    )
    return '\n'.join(lines)

  @error_handling.retry_on_grpc_unavailable
  def delete_object(
      self,
      world_object: object_world_resources.WorldObject,
      *,
      force: bool = False,
      reparent_child_objects: bool = False,
  ) -> None:
    """Deletes an object.

    Only one of force or reparent_child_objects should be set to True.
    Neither setting affects the frames grouped under the deleted object
    itself. Those frames will always be deleted regardless of these settings.

    Arguments:
      world_object: The object to delete.
      force: Enables force deletion to remove objects including their children.
      reparent_child_objects: Reparents child objects to the deleted object's
        parent while retaining their relative pose wrt the parent object.
    """
    self._stub.DeleteObject(
        object_world_updates_pb2.DeleteObjectRequest(
            world_id=self._world_id,
            force=force,
            reparent_child_objects=reparent_child_objects,
            object=world_object.reference,
        )
    )

  @error_handling.retry_on_grpc_unavailable
  def delete_frame(
      self, frame: object_world_resources.Frame, *, force: bool = False
  ) -> None:
    """Deletes a frame.

    Arguments:
      frame: The frame to delete.
      force: Enables force deletion to remove frames including their children.
    """
    self._stub.DeleteFrame(
        object_world_updates_pb2.DeleteFrameRequest(
            world_id=self._world_id, force=force, frame=frame.reference
        )
    )

  @error_handling.retry_on_grpc_unavailable
  def _call_create_frame(
      self, request: object_world_updates_pb2.CreateFrameRequest
  ) -> object_world_service_pb2.Frame:
    return self._stub.CreateFrame(request)

  def create_frame(
      self,
      frame_name: object_world_ids.FrameName,
      parent: Optional[object_world_resources.TransformNode] = None,
      parent_t_frame: Optional[data_types.Pose3] = data_types.Pose3(),
  ) -> object_world_resources.Frame:
    """Creates a new frame in the world.

    Arguments:
      frame_name: The name of the new frame. Must be unique amongst all frames
        under the same object.
      parent: The object or frame under which the new frame shall be created.
        Default is the root object.
      parent_t_frame: The transform between the parent and the new frame.
        Default is a identity transform.

    Returns:
      The created frame.
    """

    request = object_world_updates_pb2.CreateFrameRequest(
        world_id=self._world_id,
        new_frame_name=frame_name,
        parent_t_new_frame=math_proto_conversion.pose_to_proto(parent_t_frame),
    )
    if isinstance(parent, object_world_resources.WorldObject):
      request.parent_object_with_filter.reference.CopyFrom(parent.reference)
    elif isinstance(parent, object_world_resources.Frame):
      request.parent_frame.CopyFrom(parent.reference)
    elif parent is None:
      request.parent_object_with_filter.reference.id = (
          object_world_ids.ROOT_OBJECT_ID
      )
    else:
      raise TypeError(f'Cannot use {parent} as parent frame or object.')
    return object_world_resources.Frame(
        world_frame=self._call_create_frame(request), stub=self._stub
    )

  @error_handling.retry_on_grpc_unavailable
  def _call_reparent_frame(
      self, request: object_world_updates_pb2.ReparentFrameRequest
  ) -> object_world_service_pb2.Frame:
    return self._stub.ReparentFrame(request)

  def reparent_frame(
      self,
      frame: object_world_resources.Frame,
      parent: object_world_resources.TransformNode,
  ) -> None:
    """Re-parent an existing frame in the world to another frame or object.

    Arguments:
      frame: The frame you want to reparent.
      parent: The object or frame under which the new frame shall be moved. If
        parent type is WorldObject, the frame will attach to the final entity.
    """
    if not isinstance(frame, object_world_resources.Frame):
      raise TypeError(f'Cannot use {frame} as Frame to reparent.')
    request = object_world_updates_pb2.ReparentFrameRequest(
        world_id=self._world_id,
        frame=frame.reference,
    )
    if isinstance(parent, object_world_resources.WorldObject):
      request.parent_object.CopyFrom(
          object_world_refs_pb2.ObjectReferenceWithEntityFilter(
              reference=object_world_refs_pb2.ObjectReference(id=parent.id),
              entity_filter=object_world_refs_pb2.ObjectEntityFilter(
                  include_final_entity=True
              ),
          )
      )
    elif isinstance(parent, object_world_resources.Frame):
      request.parent_frame.CopyFrom(parent.reference)
    else:
      raise TypeError(f'Cannot use {parent} as parent Frame or WorldObject.')
    object_world_resources.Frame(
        world_frame=self._call_reparent_frame(request), stub=self._stub
    )

  @error_handling.retry_on_grpc_unavailable
  def _call_create_object(
      self, request: object_world_updates_pb2.CreateObjectRequest
  ) -> object_world_service_pb2.Object:
    return self._stub.CreateObject(request)

  def create_object(
      self,
      *,
      object_name: object_world_ids.WorldObjectName,
      geometry_spec: Union[
          scene_object_pb2.SceneObject,
          geometry_component_pb2.GeometryComponent,
      ],
      parent: Optional[object_world_resources.WorldObject] = None,
      parent_object_t_created_object: data_types.Pose3 = data_types.Pose3(),
      user_data: Mapping[str, any_pb2.Any] | None = None,
  ) -> None:
    """Adds a new object to the world.

    Arguments:
      object_name: The name of the newly created object.
      geometry_spec: The geometry information for the new object. Can be either
        a SceneObject or a GeometryComponent.
      parent: The parent object the new object will be attached to.
      parent_object_t_created_object: The transform between the parent object
        and the new object.
      user_data: Arbitrary data to be attached to the object.

    Raises:
      CreateObjectError: If the call to the ObjectWorldService fails.
      TypeError: If the geometry_spec is neither a SceneObject nor a
        GeometryComponent.
    """

    # First construct the common parts of the request.
    req = object_world_updates_pb2.CreateObjectRequest(
        world_id=self._world_id,
        name=object_name,
        name_is_global_alias=True,
        parent_object_t_created_object=math_proto_conversion.pose_to_proto(
            parent_object_t_created_object
        ),
        user_data=user_data,
    )

    if parent is not None:
      req.parent_object.reference.CopyFrom(parent.reference)
    else:
      req.parent_object.reference.id = object_world_ids.ROOT_OBJECT_ID
    req.parent_object.entity_filter.CopyFrom(INCLUDE_FINAL_ENTITY)

    # Handle different geometry types
    if isinstance(geometry_spec, scene_object_pb2.SceneObject):
      req.create_from_scene_object.scene_object.CopyFrom(geometry_spec)
    elif isinstance(geometry_spec, geometry_component_pb2.GeometryComponent):
      req.create_single_entity_object.geometry_component.CopyFrom(geometry_spec)
    else:
      raise TypeError(f'Unsupported geometry type: {type(geometry_spec)}')

    try:
      self._call_create_object(request=req)
    except grpc.RpcError as err:
      raise CreateObjectError(f'Create object failed: {err}') from err

  def register_geometry(
      self,
      *,
      geometry: geometry_service_pb2.CreateGeometryRequest,
  ) -> geometry_storage_refs_pb2_v0.GeometryStorageRefs:
    """Registers geometry so that it can be referenced to create an object.

    Arguments:
      geometry: Geometry data to be registered.

    Raises:
      RuntimeError: if ObjectWorldClient was not configured with the geometry
      service client.

    Returns:
      Opaque references corresponding to the registered geometry.
    """

    if self._geometry_service_stub is None:
      raise RuntimeError(
          'ObjectWorldClient has not been configured to register geometry data.'
      )

    return self._geometry_service_stub.CreateGeometry(
        geometry
    ).geometry_storage_refs_v0

  def register_geometry_v1(
      self,
      *,
      geometry: geometry_service_pb2.CreateGeometryRequest,
  ) -> geometry_storage_refs_pb2_v1.GeometryStorageRefs:
    """Registers geometry so that it can be referenced to create an object.

    Arguments:
      geometry: Geometry data to be registered.

    Raises:
      RuntimeError: if ObjectWorldClient was not configured with the geometry
      service client.

    Returns:
      Opaque references corresponding to the registered geometry.
    """

    if self._geometry_service_stub is None:
      raise RuntimeError(
          'ObjectWorldClient has not been configured to register geometry data.'
      )

    return self._geometry_service_stub.CreateGeometry(
        geometry
    ).geometry_storage_refs

  def create_geometry_object(
      self,
      *,
      object_name: object_world_ids.WorldObjectName,
      geometry_component: geometry_component_pb2.GeometryComponent,
      parent: Optional[object_world_resources.WorldObject] = None,
      parent_object_t_created_object: data_types.Pose3 = data_types.Pose3(),
      user_data: Mapping[str, any_pb2.Any] | None = None,
  ) -> None:
    """Adds a geometry object to the world.

    Arguments:
      object_name: The name of the newly created object.
      geometry_component: Geometry information for the object to be added.
      parent: The parent object the new object will be attached to.
      parent_object_t_created_object: The transform between the parent object
        and the new object.
      user_data: Arbitrary data to be attached to the object.

    Raises:
      CreateObjectError: If the call to the ObjectWorldService fails.
    """
    self.create_object(
        object_name=object_name,
        parent=parent,
        parent_object_t_created_object=parent_object_t_created_object,
        geometry_spec=geometry_component,
        user_data=user_data,
    )

  @error_handling.retry_on_grpc_unavailable
  def batch_update(
      self, updates: object_world_updates_pb2.ObjectWorldUpdates
  ) -> None:
    """Processes a number of world updates all at once.

    Arguments:
      updates: The set of updates to apply.
    """
    self._stub.UpdateWorldResources(
        object_world_service_pb2.UpdateWorldResourcesRequest(
            world_id=self._world_id,
            world_updates=updates,
            view=object_world_updates_pb2.ObjectView.BASIC,
        )
    )

  @error_handling.retry_on_grpc_unavailable
  def reset(self) -> None:
    """Restores the initial world from the world service.

    Overrides the current belief world.
    """
    self._stub.CloneWorld(
        object_world_service_pb2.CloneWorldRequest(
            world_id='init_world',
            cloned_world_id=self._world_id,
            allow_overwrite=True,
        )
    )

  @error_handling.retry_on_grpc_unavailable
  def _clone_world(self) -> str:
    return self._stub.CloneWorld(
        object_world_service_pb2.CloneWorldRequest(world_id=self._world_id)
    ).id

  @error_handling.retry_on_grpc_unavailable
  def _delete_world(self, world_id: str) -> None:
    self._stub.DeleteWorld(
        object_world_service_pb2.DeleteWorldRequest(world_id=world_id)
    )

  @contextlib.contextmanager
  def clone(self) -> Iterator['ObjectWorldClient']:
    """Clones the world and deletes the clone when exiting the context."""
    cloned_world_id = self._clone_world()
    try:
      yield ObjectWorldClient(
          cloned_world_id, self._stub, self._geometry_service_stub
      )
    finally:
      self._delete_world(cloned_world_id)
