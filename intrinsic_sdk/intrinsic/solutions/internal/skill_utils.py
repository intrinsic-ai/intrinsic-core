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

"""Utility functions for working with skill classes."""

from __future__ import annotations

import collections
import dataclasses
import datetime
import enum
import inspect
import textwrap
import time
import typing
from typing import Any
from typing import Callable
from typing import cast
from typing import Container
from typing import Iterable
from typing import Mapping
from typing import Optional
from typing import Sequence
from typing import Type
from typing import Union

from google.protobuf import any_pb2
from google.protobuf import descriptor
from google.protobuf import descriptor_pb2
from google.protobuf import descriptor_pool
from google.protobuf import duration_pb2
from google.protobuf import message
from google.protobuf import message_factory
import grpc

from intrinsic.assets.proto.v1 import resolved_dependency_pb2
from intrinsic.icon.proto import joint_space_pb2
from intrinsic.math.proto import pose_pb2
from intrinsic.math.python import data_types
from intrinsic.math.python import proto_conversion as math_proto_conversion
from intrinsic.math.python import ros_proto_conversion
from intrinsic.motion_planning.proto import motion_target_pb2
from intrinsic.perception.proto.v1 import pose_estimator_id_pb2
from intrinsic.resources.proto import resource_handle_pb2
from intrinsic.skills.client import skill_registry_client
from intrinsic.solutions import blackboard_value
from intrinsic.solutions import cel
from intrinsic.solutions import pose_estimation
from intrinsic.solutions import provided
from intrinsic.solutions import utils
from intrinsic.solutions import worlds
from intrinsic.util.proto import descriptors
from intrinsic.world.proto import collision_settings_pb2
from intrinsic.world.proto import object_world_refs_pb2
from intrinsic.world.proto import robot_payload_pb2
from intrinsic.world.python import object_world_resources
from intrinsic.world.robot_payload.python import robot_payload
from third_party.ros2.ros_interfaces.jazzy.geometry_msgs.msg import pose_pb2 as ros_pose_pb2

_PYTHON_PACKAGE_SEPARATOR = "."
_PROTO_PACKAGE_SEPARATOR = "."

RESOURCE_SLOT_DECONFLICT_SUFFIX = "_resource"

INTRINSIC_TYPE_URL_PREFIX = "type.intrinsic.ai/"
INTRINSIC_TYPE_URL_AREA_SKILLS = "skills"
INTRINSIC_TYPE_URL_AREA_ASSETS = "assets"
TYPE_URL_SEPARATOR = "/"


def module_for_generated_skill(skill_package: str) -> str:
  """Generates the module name for a generated skill class.

  This module does not exist at runtime but there may be a type stub for it.

  Args:
    skill_package: The skill package name, e.g., 'ai.intrinsic'.

  Returns:
    A module name string, e.g., 'intrinsic.solutions.skills.ai.intrinsic'.
  """
  skills_python_package = __name__.replace(".internal.skill_utils", ".skills")
  if skill_package:
    return skills_python_package + "." + skill_package
  else:
    return skills_python_package


def type_url_prefix_for_skill(skill_info: provided.SkillInfo) -> str:
  """Generates a skill specific type URL prefix.

  The specific prefix allows lookup of a packed proto by the proto registry.

  Args:
    skill_info: SkillInfo of the skill for which the type URL prefix should be
      generated.

  Returns:
    A type URL prefix that can be passed, e.g., into Any.Pack.
  """
  return f"{INTRINSIC_TYPE_URL_PREFIX}{skill_info.type_url_area}{TYPE_URL_SEPARATOR}{skill_info.id}"


@dataclasses.dataclass
class ParameterInformation:
  """Class for collecting information about a parameter."""

  has_default: bool
  name: str
  default: Any
  doc_string: list[str]
  message_full_name: Optional[str]
  enum_full_name: Optional[str]


# This must map according to:
# https://developers.google.com/protocol-buffers/docs/proto3#scalar
_PYTHONIC_SCALAR_FIELD_TYPE = {
    descriptor.FieldDescriptor.TYPE_BOOL: bool,
    descriptor.FieldDescriptor.TYPE_BYTES: bytes,
    descriptor.FieldDescriptor.TYPE_DOUBLE: float,
    descriptor.FieldDescriptor.TYPE_FIXED32: int,
    descriptor.FieldDescriptor.TYPE_FIXED64: int,
    descriptor.FieldDescriptor.TYPE_FLOAT: float,
    descriptor.FieldDescriptor.TYPE_INT32: int,
    descriptor.FieldDescriptor.TYPE_INT64: int,
    descriptor.FieldDescriptor.TYPE_SFIXED32: int,
    descriptor.FieldDescriptor.TYPE_SFIXED64: int,
    descriptor.FieldDescriptor.TYPE_SINT32: int,
    descriptor.FieldDescriptor.TYPE_SINT64: int,
    descriptor.FieldDescriptor.TYPE_STRING: str,
    descriptor.FieldDescriptor.TYPE_UINT32: int,
    descriptor.FieldDescriptor.TYPE_UINT64: int,
}

_PYTHONIC_SCALAR_DEFAULT_VALUE = {
    descriptor.FieldDescriptor.TYPE_BOOL: False,
    descriptor.FieldDescriptor.TYPE_BYTES: b"",
    descriptor.FieldDescriptor.TYPE_DOUBLE: 0.0,
    descriptor.FieldDescriptor.TYPE_FIXED32: 0,
    descriptor.FieldDescriptor.TYPE_FIXED64: 0,
    descriptor.FieldDescriptor.TYPE_FLOAT: 0.0,
    descriptor.FieldDescriptor.TYPE_INT32: 0,
    descriptor.FieldDescriptor.TYPE_INT64: 0,
    descriptor.FieldDescriptor.TYPE_SFIXED32: 0,
    descriptor.FieldDescriptor.TYPE_SFIXED64: 0,
    descriptor.FieldDescriptor.TYPE_SINT32: 0,
    descriptor.FieldDescriptor.TYPE_SINT64: 0,
    descriptor.FieldDescriptor.TYPE_STRING: "",
    descriptor.FieldDescriptor.TYPE_UINT32: 0,
    descriptor.FieldDescriptor.TYPE_UINT64: 0,
}


def pythonic_field_type(
    field_descriptor: descriptor.FieldDescriptor,
    wrapper_classes: dict[str, Type[MessageWrapper]],
    enum_classes: dict[str, Type[enum.IntEnum]],
) -> Type[Any]:
  """Returns a 'pythonic' type based on the field_descriptor.

  Can be a Protobuf message for types for which we provide no native conversion.

  Args:
    field_descriptor: The Protobuf descriptor for the field
    wrapper_classes: Map from proto message names to corresponding message
      wrapper classes.
    enum_classes: Map from full proto enum names to corresponding enum classes.

  Returns:
    The Python type of the field.

  Raises:
    KeyError: If the auto-converted types cannot be determined.
  """
  if field_descriptor.type == descriptor.FieldDescriptor.TYPE_MESSAGE:
    message_full_name = field_descriptor.message_type.full_name
    result_type = wrapper_classes[message_full_name]

    # If we have an auto-conversion function for this message, add the
    # corresponding Pythonic types.
    if message_full_name in _PYTHONIC_TO_MESSAGE_AUTO_CONVERSIONS:
      auto_converted_types = _PYTHONIC_TO_MESSAGE_AUTO_CONVERSIONS[
          message_full_name
      ].converted_types
      result_type = Union[auto_converted_types, result_type]

    return result_type

  if field_descriptor.type == descriptor.FieldDescriptor.TYPE_ENUM:
    return enum_classes[field_descriptor.enum_type.full_name]

  return _PYTHONIC_SCALAR_FIELD_TYPE[field_descriptor.type]


def _field_to_pose_3d(field_value: data_types.Pose3) -> pose_pb2.Pose:
  """Converts the field_value to pose_pb2.Pose."""
  if not isinstance(field_value, data_types.Pose3):
    raise TypeError(f"Value {field_value} not a Pose3")
  return math_proto_conversion.pose_to_proto(field_value)


def _field_to_ros_pose_3d(field_value: data_types.Pose3) -> ros_pose_pb2.Pose:
  """Converts the field_value to pose_pb2.Pose."""
  if not isinstance(field_value, data_types.Pose3):
    raise TypeError(f"Value {field_value} not a Pose3")
  return ros_proto_conversion.pose_to_proto(field_value)


def _field_to_object_reference(
    # This needs to accept 'TransformNode' (and not 'WorldObject') to be
    # compatible with the world path notation. E.g., we want to be able to pass
    # 'world.my_object' to a skill parameter of type 'ObjectReference', and
    # 'world.__getattr__()' will return a 'WorldObject' as a 'TransformNode'.
    field_value: object_world_resources.TransformNode,
) -> object_world_refs_pb2.ObjectReference:
  """Converts a field_value to object_world_refs_pb2.ObjectReference."""
  if isinstance(field_value, object_world_resources.WorldObject):
    return field_value.reference
  raise TypeError(
      f"Value: {field_value} is not convertible to an ObjectReference."
  )


def _field_to_frame_reference(
    # This needs to accept 'TransformNode' (and not 'Frame') to be compatible
    # with the world path notation. E.g., we want to be able to pass
    # 'world.my_global_frame' to a skill parameter of type 'FrameReference', and
    # 'world.__getattr__()' will return a 'Frame' as a 'TransformNode'.
    field_value: object_world_resources.TransformNode,
) -> object_world_refs_pb2.FrameReference:
  """Converts a field_value to object_world_refs_pb2.FrameReference."""
  if isinstance(field_value, object_world_resources.Frame):
    return field_value.reference
  raise TypeError(
      f"Value: {field_value} is not convertible to an FrameReference."
  )


def _field_to_transform_node_reference(
    field_value: object_world_resources.TransformNode,
) -> object_world_refs_pb2.TransformNodeReference:
  """Converts a field_value to TransformNodeReference.

  Args:
    field_value: The value that should be converted.

  Returns:
    A TransformNodeReference with an object or frame reference.
  """
  if isinstance(field_value, object_world_resources.TransformNode):
    return field_value.transform_node_reference
  raise TypeError(
      f"Value: {field_value} is not convertible to a TransformNodeReference."
  )


def _field_to_object_or_entity_reference(
    field_value: object_world_resources.WorldObject,
) -> collision_settings_pb2.ObjectOrEntityReference:
  """Converts a field_value to ObjectOrEntityReference.

  Args:
    field_value: The value that should be converted. Currently, only world
      objects are supported because entities are not yet accessible (i.e., there
      is no object_world_resources.ObjectEntity type).

  Returns:
    An ObjectOrEntityReference containing an object reference.
  """
  if not isinstance(field_value, object_world_resources.WorldObject):
    raise TypeError(
        f"Value: {field_value} is not convertible to an"
        " ObjectOrEntityReference."
    )
  return collision_settings_pb2.ObjectOrEntityReference(
      object=field_value.reference
  )


def _field_to_motion_planning_cartesian_motion_target(
    field_value: worlds.CartesianMotionTarget,
) -> motion_target_pb2.CartesianMotionTarget:
  """Converts a field_value to the object world CartesianMotionTarget."""
  if isinstance(field_value, worlds.CartesianMotionTarget):
    return field_value.proto
  raise TypeError(
      f"Value: {field_value} is not convertible to a CartesianMotionTarget."
  )


def _field_to_joint_vec_target(
    field_value: object_world_resources.JointConfiguration,
) -> joint_space_pb2.JointVec:
  field_message = joint_space_pb2.JointVec()
  if isinstance(field_value, object_world_resources.JointConfiguration):
    field_message.joints.extend(field_value.joint_position)
    return field_message
  raise TypeError(f"Cannot convert {field_value} to JointVec.")


def _field_to_collision_settings(
    field_value: worlds.CollisionSettings,
) -> collision_settings_pb2.CollisionSettings:
  if isinstance(field_value, worlds.CollisionSettings):
    return field_value.proto
  raise TypeError(f"Cannot convert {field_value} to CollisionSettings.")


def _field_to_duration(
    field_value: Union[datetime.timedelta, float, int],
) -> duration_pb2.Duration:
  """Create a Duration object from various inputs.

  This will transform datetime.timedelta and ints/floats.

  Args:
    field_value: The value to transform to duration_pb2.Duration. If it is a
      float or int value the value is interpreted as seconds.

  Returns:
    duration_pb2.Duration object from the input.

  Raises:
    TypeError if the field_value is not one of the expected types.
  """
  if isinstance(field_value, datetime.timedelta):
    duration_proto = duration_pb2.Duration()
    duration_proto.FromTimedelta(field_value)
    return duration_proto
  elif isinstance(field_value, int):
    duration_proto = duration_pb2.Duration()
    duration_proto.FromSeconds(field_value)
    return duration_proto
  elif isinstance(field_value, float):
    duration_proto = duration_pb2.Duration()
    i, d = divmod(field_value, 1)
    duration_proto.seconds = int(i)
    duration_proto.nanos = int(d * 1e9)
    return duration_proto
  raise TypeError(
      "Expected value of type int, float or datetime.timedelta, got"
      f" {type(field_value)}"
  )


def _field_to_pose_estimator_id(
    field_value: pose_estimation.PoseEstimatorId,
) -> pose_estimator_id_pb2.PoseEstimatorId:
  if isinstance(field_value, pose_estimation.PoseEstimatorId):
    return pose_estimator_id_pb2.PoseEstimatorId(id=field_value.id)
  raise TypeError(f"Cannot convert {field_value} to PoseEstimatorId.")


def _field_to_robot_payload(
    field_value: robot_payload.RobotPayload,
) -> robot_payload_pb2.RobotPayload:
  """Converts the field_value to robot_payload_pb2.RobotPayload."""
  if not isinstance(field_value, robot_payload.RobotPayload):
    raise TypeError(f"Value {field_value} not a RobotPayload")
  return robot_payload.payload_to_proto(field_value)


def _field_to_resolved_dependency(
    field_value: Union[
        provided.ResourceHandle,
        resource_handle_pb2.ResourceHandle,
    ],
) -> resolved_dependency_pb2.ResolvedDependency:
  """Converts a resource handle to a ResolvedDependency proto."""
  if isinstance(field_value, provided.ResourceHandle):
    return resolved_dependency_pb2.ResolvedDependency(name=field_value.name)
  elif isinstance(field_value, resource_handle_pb2.ResourceHandle):
    return resolved_dependency_pb2.ResolvedDependency(name=field_value.name)
  raise TypeError(
      f"Value: {field_value} is not convertible to a ResolvedDependency."
  )


@dataclasses.dataclass
class _AutoConversion:
  """Encapsulates an auto-conversion function together with metadata about it.

  Attributes:
    field_to_message: A function that converts a Pythonic field value to a
      corresponding message. Must have a parameter called 'field_value' with a
      type annotation indicating the types that can be converted.
    converted_types: The Python types that can be converted to a message. Either
      a single type (e.g. "int") or a Union of multiple types (e.g. "Union[int,
      str]").
  """

  field_to_message: Callable[[Any], message.Message]
  converted_types: Type[Any] = dataclasses.field(init=False)

  def __post_init__(self):
    type_hints = typing.get_type_hints(self.field_to_message)
    if "field_value" not in type_hints:
      raise KeyError(
          f"Auto-conversion function {self.field_to_message.__name__} must have"
          " a parameter called 'field_value' with a type annotation indicating"
          " the types that can be converted",
      )
    self.converted_types = type_hints["field_value"]


_PYTHONIC_TO_MESSAGE_AUTO_CONVERSIONS = {
    pose_pb2.Pose.DESCRIPTOR.full_name: _AutoConversion(_field_to_pose_3d),
    ros_pose_pb2.Pose.DESCRIPTOR.full_name: _AutoConversion(
        _field_to_ros_pose_3d
    ),
    joint_space_pb2.JointVec.DESCRIPTOR.full_name: _AutoConversion(
        _field_to_joint_vec_target
    ),
    collision_settings_pb2.CollisionSettings.DESCRIPTOR.full_name: (
        _AutoConversion(_field_to_collision_settings)
    ),
    collision_settings_pb2.ObjectOrEntityReference.DESCRIPTOR.full_name: (
        _AutoConversion(_field_to_object_or_entity_reference)
    ),
    motion_target_pb2.CartesianMotionTarget.DESCRIPTOR.full_name: (
        _AutoConversion(_field_to_motion_planning_cartesian_motion_target)
    ),
    duration_pb2.Duration.DESCRIPTOR.full_name: _AutoConversion(
        _field_to_duration
    ),
    object_world_refs_pb2.FrameReference.DESCRIPTOR.full_name: _AutoConversion(
        _field_to_frame_reference
    ),
    object_world_refs_pb2.TransformNodeReference.DESCRIPTOR.full_name: (
        _AutoConversion(_field_to_transform_node_reference)
    ),
    object_world_refs_pb2.ObjectReference.DESCRIPTOR.full_name: _AutoConversion(
        _field_to_object_reference
    ),
    pose_estimator_id_pb2.PoseEstimatorId.DESCRIPTOR.full_name: _AutoConversion(
        _field_to_pose_estimator_id
    ),
    robot_payload_pb2.RobotPayload.DESCRIPTOR.full_name: _AutoConversion(
        _field_to_robot_payload
    ),
    resolved_dependency_pb2.ResolvedDependency.DESCRIPTOR.full_name: (
        _AutoConversion(_field_to_resolved_dependency)
    ),
}


def pythonic_to_proto_message(
    field_value: Any, message_descriptor: descriptor.Descriptor
) -> message.Message:
  """Performs a conversion to a Protobuf message from 'pythonic' field_value.

  If there is no special conversion, or the field_value is already
  in the correct message type, then the field_value is returned untouched.

  Args:
    field_value: The value to be placed in the corresponding message field.
    message_descriptor: The Protobuf descriptor for the message.

  Returns:
    A Protobuf message containing the desire value from field_value.

  Raises:
    TypeError if the field_value is not convertible to a message of the type
    indicated by message_descriptor.
  """
  if (
      isinstance(field_value, message.Message)
      and field_value.DESCRIPTOR.full_name == message_descriptor.full_name
  ):
    return field_value
  if isinstance(field_value, MessageWrapper):
    return field_value.wrapped_message
  # Provide implicit conversion for some non-message types.
  if message_descriptor.full_name in _PYTHONIC_TO_MESSAGE_AUTO_CONVERSIONS:
    return _PYTHONIC_TO_MESSAGE_AUTO_CONVERSIONS[
        message_descriptor.full_name
    ].field_to_message(field_value)
  raise TypeError(
      "Type of value {} is of type {}. Must be convertible to message type {}"
      .format(field_value, type(field_value), message_descriptor.full_name)
  )


def _pythonic_to_proto_enum_value(
    field_value: Any, field_enum_type: descriptor.EnumDescriptor
) -> int:
  """Converts an enum value to a proto enum value (=int).

  Args:
    field_value: The value to be converted.
    field_enum_type: The Protobuf descriptor for the enum.

  Returns:
    The given value as an int.

  Raises:
    TypeError if the given value is not an int or int-like.
  """
  # This also catches instances of IntEnum which is a subclass of int.
  if isinstance(field_value, int):
    return field_value

  raise TypeError(
      f"Value '{field_value}' is of type {type(field_value)}. Must be"
      " convertible to an 'int' representing a value of the enum"
      f" {field_enum_type.full_name}"
  )


def _check_no_multiply_defined_oneof(
    field_descriptor_map: Mapping[str, descriptor.FieldDescriptor],
    field_names: Iterable[str],
):
  """Checks that no elements of field_names are associated with the same oneof.

  All field_names must be contained in field_descriptor_map.keys().

  Args:
    field_descriptor_map: A map of field_names to FieldDescriptors.
    field_names: A list of field names to check.

  Raises:
    ValueError if multiple elements of field_names are fields of the same
    containing oneof.
  """
  oneofs = {}
  for field_name in field_names:
    containing_oneof = field_descriptor_map[field_name].containing_oneof
    if containing_oneof is not None:
      if containing_oneof in oneofs.keys():
        raise ValueError(
            "Multiple fields have the same containing oneof . "
            f"Fields: '{field_name}' and '{oneofs[containing_oneof].name}'. "
            f"Oneof: '{containing_oneof.name}'"
        )
      oneofs[containing_oneof] = field_descriptor_map[field_name]


def set_fields_in_msg(
    msg: message.Message, fields: dict[str, Any]
) -> list[str]:
  """Sets the fields in the msg.

  Args:
    msg: Protobuf message to set fields of.
    fields: Dictionary of the fields to set in the message keyed by field name.

  Returns:
    List of keys in fields applied to msg.

  Raises:
    KeyError: If a set of fields in fields would apply to the same oneof in msg.
    TypeError: A field in fields has a mismatched type compared to the field in
      msg.
  """
  field_descriptor_map = msg.DESCRIPTOR.fields_by_name
  consumed_fields = []

  for field_name in fields.keys():
    if field_name not in field_descriptor_map:
      raise KeyError(
          f'Field "{field_name}" does not exist in message'
          f' "{msg.DESCRIPTOR.full_name}".'
      )

  try:
    _check_no_multiply_defined_oneof(field_descriptor_map, fields.keys())
  except ValueError as e:
    raise KeyError(
        "Multiple parameters to be set are part of the same oneof"
    ) from e

  for field_name, arg_value in fields.items():
    if arg_value is None:
      msg.ClearField(field_name)
      consumed_fields.append(field_name)
      continue

    if isinstance(
        arg_value, blackboard_value.BlackboardValue | cel.CelExpression
    ):
      continue

    field_desc = field_descriptor_map[field_name]
    field_type = field_desc.type
    field_message_type = field_desc.message_type
    field_enum_type = field_desc.enum_type
    if field_descriptor_map[field_name].is_repeated:
      if (
          field_type == descriptor.FieldDescriptor.TYPE_MESSAGE
          and field_message_type.GetOptions().map_entry
      ):
        msg.ClearField(field_name)
        map_field = getattr(msg, field_name)
        if isinstance(arg_value, set):
          raise TypeError(
              "Got set where expected dict, initialized like {'foo', 1}'"
              " instead of {'foo': 1}?"
          )

        value_type = field_desc.message_type.fields_by_name["value"]

        for k, v in arg_value.items():
          if isinstance(
              v, blackboard_value.BlackboardValue | cel.CelExpression
          ):
            raise TypeError(
                f"Cannot set field {field_name}['{k}'] from blackboard"
                " value, not supported for maps"
            )

          if value_type.type == descriptor.FieldDescriptor.TYPE_MESSAGE:
            if isinstance(v, MessageWrapper):
              map_field[k].CopyFrom(v.wrapped_message)
            elif isinstance(v, message.Message):
              # The messages are from different pools, therefore go through
              # serialization and parsing again
              map_field[k].ParseFromString(v.SerializeToString())
            else:
              raise TypeError(
                  f"Cannot set field {field_name}['{k}'] from non-message value"
              )
          else:
            map_field[k] = v

      else:
        if not isinstance(arg_value, Iterable):
          raise TypeError(
              f"Field {field_name} is a repeated field and expects a list of"
              f" values, got {type(arg_value)}. Consider wrapping it in a list"
              " if you want to pass a single value."
          )

        repeated_field = getattr(msg, field_name)
        del repeated_field[:]  # clear field default since value was provided
        for value in arg_value:

          # TODO(b/193079283): Drop this serialization round trip Weird dance
          # or else we get a type error on type SubMessage is not equivalent
          # to type SubMessage. This is because self._param_message comes from
          # a descriptor pool built from the DescriptorFileSet we receive on
          # the wire, whereas the argument could be built from a descriptor
          # pool based on the compiled-in proto messages. This means that
          # Python's isinstance() doesn't recognize these as the same type.

          if isinstance(
              value, blackboard_value.BlackboardValue | cel.CelExpression
          ):
            if field_message_type is not None:
              repeated_field.add()
            elif field_enum_type is not None:
              repeated_field.append(0)
            else:
              repeated_field.append(_PYTHONIC_SCALAR_DEFAULT_VALUE[field_type])

          elif field_message_type is not None:
            repeated_field.add().ParseFromString(
                pythonic_to_proto_message(
                    value, field_message_type
                ).SerializeToString()
            )

          elif field_enum_type is not None:
            repeated_field.append(
                _pythonic_to_proto_enum_value(value, field_enum_type)
            )
          else:
            try:
              # Leave assignment to the underlying message. This automatically
              # handles possible type-conversions (e.g., passing an int to a
              # float field) and type-mismatches (e.g., passing a str to an
              # integer field will raise a TypeError).
              repeated_field.append(value)
            except TypeError as e:
              raise TypeError(
                  f"field: {field_name}, with values {fields[field_name]}"
                  f" contained an invalid value: '{value}' ({e})"
              ) from e

    elif field_type == descriptor.FieldDescriptor.TYPE_MESSAGE:
      submessage = getattr(msg, field_name)

      # TODO(b/193079283): Drop this serialization round trip
      # Weird dance or else we get a type error on type SubMessage is not
      # equivalent to type Submessage. This is because self._param_message
      # comes from a descriptor pool built from the DescriptorFileSet
      # we receive on the wire, whereas the argument could be built from a
      # descriptor pool based on the compiled-in proto messages. This means
      # that Python's isinstance() doesn't recognize these as the same type.

      submessage.ParseFromString(
          pythonic_to_proto_message(
              arg_value, field_message_type
          ).SerializeToString()
      )
    elif field_type == descriptor.FieldDescriptor.TYPE_ENUM:
      enum_value = _pythonic_to_proto_enum_value(arg_value, field_enum_type)
      setattr(msg, field_name, enum_value)
    else:
      try:
        # Leave assignment to the underlying message. This automatically handles
        # possible type-conversions (e.g., passing an int to a float field) and
        # type-mismatches (e.g., passing a str to an integer field will raise a
        # TypeError).
        setattr(msg, field_name, arg_value)
      except TypeError as e:
        raise TypeError(
            f"value '{arg_value}' for field '{field_name}' ({e})"
        ) from e
    consumed_fields.append(field_name)
  return consumed_fields


def set_params(
    param_message: message.Message,
    blackboard_params: dict[str, str],
    args: dict[str, Any],
) -> list[str]:
  """Sets the parameters in the given msg and blackboard params.

  Regular values will be directly set in the given message and BlackboardValues
  and CelExpressions will be added as assigments to 'blackboard_params'.

  Args:
    param_message: Protobuf message to set fields of.
    blackboard_params: Mapping from parameter paths to CEL expressions to which
      to add additional assignments.

  Returns:
    List of keys in args which were successfully consumed by this method.

  Raises:
    TypeError: If one of the args has an invalid type.
  """
  consumed_args = []
  param_message_descriptor = cast(
      descriptor.Descriptor, param_message.DESCRIPTOR
  )

  fields: dict[str, Any] = {}
  for param_name, value in args.items():
    if param_name in param_message_descriptor.fields_by_name:
      # Explicitly passing None means "use default value", nothing to do here.
      if value is None:
        consumed_args.append(param_name)
        continue

      if isinstance(value, blackboard_value.BlackboardValue):
        blackboard_params[param_name] = value.value_access_path()
        consumed_args.append(param_name)
        continue

      if isinstance(value, cel.CelExpression):
        blackboard_params[param_name] = str(value)
        consumed_args.append(param_name)
        continue

      fields[param_name] = value

      if isinstance(value, MessageWrapper):
        for k, v in value.blackboard_params.items():
          blackboard_params[param_name + "." + k] = v

      if isinstance(value, list):
        if value and (
            isinstance(value[0], MessageWrapper)
            or isinstance(value[0], blackboard_value.BlackboardValue)
            or isinstance(value[0], cel.CelExpression)
        ):
          # Guard this check for list non-scalar list values to allow
          # assigning a VectorNd from a list[float].
          if not param_message_descriptor.fields_by_name[
              param_name
          ].is_repeated:
            raise TypeError(
                f"Cannot set field {param_name} to list, not a repeated field"
            )
        for i, listelem in enumerate(value):
          if isinstance(listelem, MessageWrapper):
            for k, v in listelem.blackboard_params.items():
              blackboard_params[param_name + f"[{i}]." + k] = v
          elif isinstance(listelem, blackboard_value.BlackboardValue):
            blackboard_params[param_name + f"[{i}]"] = (
                listelem.value_access_path()
            )
          elif isinstance(listelem, cel.CelExpression):
            blackboard_params[param_name + f"[{i}]"] = str(listelem)

      if isinstance(value, dict):
        field = param_message_descriptor.fields_by_name[param_name]
        if (
            not field.is_repeated
            or field.type != descriptor.FieldDescriptor.TYPE_MESSAGE
            or not field.message_type.GetOptions().map_entry
        ):
          raise TypeError(
              f"Cannot set field {param_name} to dict, not a map field"
          )

  params_consumed_by_set_fields = set_fields_in_msg(param_message, fields)
  consumed_args.extend(params_consumed_by_set_fields)

  return consumed_args


def determine_failed_generate_proto_infra_from_filedescriptorset(
    filedescriptor_set: descriptor_pb2.FileDescriptorSet,
) -> str:
  """Determines, which class failed to generate the infrastructure pieces for.

  Proceeds like generate_proto_infra_from_filedescriptorset, but generate
  classes for protos individually, so that an external function can determine,
  what message is not importing correctly.

  Args:
    filedescriptor_set: The file descriptor set (a set of file descriptors,
      which each contain a set of proto descriptors) that would also be passed
      to generate_proto_infra_from_filedescriptorset.

  Returns:
    The last proto name that was tried to generate a class for and failed. Empty
    string, if none failed.
  """
  desc_pool = descriptors.create_descriptor_pool(filedescriptor_set)

  last_tried = ""
  try:
    for file_proto in filedescriptor_set.file:
      last_tried = file_proto.name
      message_factory.GetMessageClassesForFiles([file_proto.name], desc_pool)
  except NotImplementedError:
    return last_tried
  return ""


def generate_proto_infra_from_filedescriptorset(
    filedescriptor_set: descriptor_pb2.FileDescriptorSet,
) -> tuple[
    descriptor_pool.DescriptorPool,
    dict[str, Type[message.Message]],
]:
  """Generates the infrastructure pieces to deal with protos from a given set.

  This function creates a hermetic descriptor pool from that, a message factory
  based on that pool, and a mapping from type names to proto classes. This is
  the typical infrastructure required to deal with such messages in a hermetic
  fashion, i.e., without importing apriori known proto message packages.

  Args:
    filedescriptor_set: To publicly document a proto-based interface a
      transitive closure of proto descriptors is required. This is given as a
      file descriptor set (a set of file descriptors, which each contain a set
      of proto descriptors). It is provided by a discovery API.

  Returns:
    Tuple consisting of a proto descriptor pool populated with the proto types
    from the input file descriptor set, a message factory that can create
    messages from that pool, and a mapping from type names (of protos in the
    pool) to message classes.
  """
  desc_pool = descriptors.create_descriptor_pool(filedescriptor_set)
  message_classes = message_factory.GetMessageClassesForFiles(
      [file_proto.name for file_proto in filedescriptor_set.file], desc_pool
  )
  additional_msg_classes = {}
  for name, msg in message_classes.items():
    _get_nested_classes(msg.DESCRIPTOR, name, additional_msg_classes)

  for key, msg in additional_msg_classes.items():
    message_classes[key] = message_factory.GetMessageClass(msg)
  return desc_pool, message_classes


def _get_nested_classes(
    desc: descriptor.Descriptor,
    name: str,
    additional_msg_classes: dict[str, descriptor.Descriptor],
):
  """Generates a mapping from type names to proto classes for nested types.

  Args:
    desc: Descriptor to inspect for nested_types.
    name: prefix for the type name.
    additional_msg_classes: map in which the nested types are collected.
  """
  for nested_type in desc.nested_types:
    type_name = name + "." + nested_type.name
    if type_name not in additional_msg_classes:
      additional_msg_classes[type_name] = nested_type
      _get_nested_classes(nested_type, type_name, additional_msg_classes)


def collect_message_classes_to_wrap(
    message_descriptor: descriptor.Descriptor,
    message_classes: dict[str, Type[message.Message]],
    collected_classes: dict[str, Type[message.Message]],
    collected_enums: dict[str, descriptor.EnumDescriptor],
) -> None:
  """Collects message classes for which wrapper classes should be generated.

  Recursively collects all message types and enums which are used by the
  fields of the message specified by 'message_descriptor'.
  The collected message and enum types will be recorded in the collected_classes
  and collected_enums inputs.

  Args:
    message_descriptor: proto descriptor
    message_classes: Mapping from full proto message name to message class for
      all messages in the skill's hermetic descriptor pool.
    collected_classes: Subset of classes from 'message_classes' which have been
      collected up to this point.
    collected_enums: Map from full proto enum names to enum descriptors,
      containing all collected enums up to this point.
  """

  if message_descriptor.full_name in collected_classes:
    return
  collected_classes[message_descriptor.full_name] = message_classes[
      message_descriptor.full_name
  ]
  for field in message_descriptor.fields:
    if field.enum_type is not None:
      enum_type = typing.cast(descriptor.EnumDescriptor, field.enum_type)
      collected_enums[enum_type.full_name] = enum_type
    elif (
        field.message_type is not None
        and field.message_type.full_name in message_classes
    ):
      if field.message_type.GetOptions().map_entry:
        # The field is a map and 'field.message_type' is an auto-generated entry
        # type (see https://protobuf.dev/programming-guides/proto3/#backwards).
        # Since map keys cannot have a message type, we only consider the type
        # of the map's values for collection.
        map_value_field = field.message_type.fields_by_name["value"]
        if map_value_field.message_type is not None:
          message_descriptor = map_value_field.message_type
        else:
          continue
      else:
        message_descriptor = field.message_type
      collect_message_classes_to_wrap(
          message_descriptor,
          message_classes,
          collected_classes,
          collected_enums,
      )


class MessageWrapper:
  """Message wrapper base.

  We wrap all messages which are used as parameter or return values.
  This enables us to introspect the types and generate
  documentation and augment the constructors with meta-information for
  auto-completion. Additionally, this allows us to handle BlackboardValues for
  parameterization of the message, as they need to be handled specially and
  cannot be added to the message directly.
  """

  # Limit the attributes which instances of this class can have so that
  # assignments such as my_msg_wrapper.non_existing_field = 42 will raise an
  # AttributeError.
  __slots__ = [
      "_wrapped_message",
      "_blackboard_params",
  ]

  _wrapped_message: Optional[message.Message]
  _blackboard_params: dict[str, Any]

  def __init__(self):
    """This constructor normally will not be called from outside."""
    self._wrapped_message = None
    self._blackboard_params = {}

  def to_any(self) -> any_pb2.Any:
    any_msg = any_pb2.Any()
    any_msg.Pack(
        self.wrapped_message,
        type_url_prefix=type_url_prefix_for_skill(self._skill_info),
    )
    return any_msg

  def _set_parameter(self, key: str, value: Any):
    if self.wrapped_message is None:
      raise ValueError(
          f"Cannot set field {key} as the wrapped message is None."
      )

    consumed = set_params(
        self.wrapped_message, self._blackboard_params, {key: value}
    )

    if not consumed:
      raise KeyError(
          f'Field "{key}" does not exist in message'
          f' "{self.wrapped_message.DESCRIPTOR.full_name}".'
      )

  @property
  def wrapped_message(self) -> Optional[message.Message]:
    if hasattr(self, "_wrapped_message"):
      return self._wrapped_message
    return None

  @utils.classproperty
  def wrapped_type(cls) -> Type[message.Message]:  # pylint:disable=no-self-argument
    return cls._wrapped_type

  @property
  def blackboard_params(self) -> dict[str, str]:
    return self._blackboard_params

  def __setattr__(self, name: str, value: Any):
    """Sets a parameter in the underlying message, if part of the message.

    This is necessary to support the current syntax when initializing messages
    which usually creates the message object first and then adds the arguments.

    Args:
      name: The parameter name.
      value: The value for the parameter.

    Raises:
      KeyError: If a set of fields in fields would apply to the same oneof in
      msg.
      TypeError: If passing a value that does not match a field's type.
    """
    msg = self.wrapped_message

    if msg is not None and name in msg.DESCRIPTOR.fields_by_name:
      self._set_parameter(name, value)
    else:
      super().__setattr__(name, value)


def _gen_wrapper_class(
    wrapped_type: Type[message.Message],
    skill_info: provided.SkillInfo,
) -> Type[Any]:
  """Generates a new message wrapper class type.

  We need to do this because we already need the constructor to pass instance
  information and therefore need to overload __init__. In order to be able to
  augment it with meta info for auto-completion, we need to dynamically generate
  it. Since __init__ is a class and not an instance method, we cannot simply
  assign the function, but need to generate an entire type for it.

  Args:
    wrapped_type: Message to wrap.
    skill_info: Information about the skill name, package, etc.

  Returns:
    A new type for a MessageWrapper sub-class.
  """

  return type(
      # E.g.: 'Pose'
      wrapped_type.DESCRIPTOR.name,
      (MessageWrapper,),
      {
          "__doc__": _gen_class_docstring(wrapped_type, skill_info),
          # E.g.: 'move_robot.intrinsic_proto.Pose'.
          "__qualname__": (
              skill_info.skill_name + "." + wrapped_type.DESCRIPTOR.full_name
          ),
          # E.g.: 'intrinsic.solutions.skills.ai.intrinsic'.
          "__module__": module_for_generated_skill(skill_info.package_name),
          "_wrapped_type": wrapped_type,
          "_skill_info": skill_info,
          "__slots__": [],  # no additonal slots compared to base class
      },
  )


def _gen_init_fun(
    wrapped_type: Type[message.Message],
    skill_info: provided.SkillInfo,
    type_name: str,
    wrapper_classes: dict[str, Type[MessageWrapper]],
    enum_classes: dict[str, Type[enum.IntEnum]],
) -> Callable[[Any, Any], None]:
  """Generates custom __init__ class method with proper auto-completion info.

  Args:
    wrapped_type: Message to wrap.
    skill_info: Metadata of the skill.
    type_name: Type name of the object to wrap.
    wrapper_classes: Map from proto message names to corresponding message
      wrapper classes.
    enum_classes: Map from full proto enum names to corresponding enum wrapper
      classes.

  Returns:
    A function suitable to be used as __init__ function for a MessageWrapper
    derivative.
  """

  def new_init_fun(self, **kwargs) -> None:
    MessageWrapper.__init__(self)  # pytype: disable=wrong-arg-count
    self._wrapped_message = wrapped_type()  # pylint: disable=protected-access
    params_set = set_params(
        self._wrapped_message,  # pylint: disable=protected-access
        self._blackboard_params,  # pylint: disable=protected-access
        kwargs,
    )
    # Arguments which are not expected parameters.
    extra_args_set = set(kwargs.keys()) - set(params_set)
    if extra_args_set:
      raise KeyError(
          f'Fields {{{", ".join(sorted(extra_args_set))}}} do not exist in'
          f' message "{self._wrapped_message.DESCRIPTOR.full_name}".'
      )

  params = [
      inspect.Parameter(
          "self",
          inspect.Parameter.POSITIONAL_OR_KEYWORD,
          annotation="MessageWrapper_" + type_name,
      )
  ] + _gen_init_params(
      wrapped_type,
      wrapper_classes,
      enum_classes,
  )
  new_init_fun.__signature__ = inspect.Signature(params)
  new_init_fun.__annotations__ = collections.OrderedDict(
      [(p.name, p.annotation) for p in params]
  )
  new_init_fun.__doc__ = _gen_init_docstring(wrapped_type, skill_info)
  return new_init_fun


def _gen_class_docstring(
    wrapped_type: Type[message.Message],
    skill_info: provided.SkillInfo,
) -> str:
  """Generates the class docstring for a message wrapper class.

  Args:
    wrapped_type: Message to wrap.
    field_doc_strings: Dict mapping from field name to doc string comment.

  Returns:
    Python documentation string.
  """
  docstring: list[str] = [
      f"Proto message wrapper class for {wrapped_type.DESCRIPTOR.full_name}."
  ]
  message_doc_string = ""

  proto_comment = skill_info.get_proto_comment(
      wrapped_type.DESCRIPTOR.full_name
  )
  if proto_comment:
    docstring += [""]
    message_doc_string = proto_comment
  # Expect 80 chars width.
  is_first_line = True
  for doc_string_line in textwrap.dedent(message_doc_string).splitlines():
    wrapped_lines = textwrap.wrap(doc_string_line, 80)
    # Make sure that an empty line is wrapped to an empty line
    # and not removed. We assume that the skill author intended
    # the extra line break there unless it is the first line.
    if not wrapped_lines and is_first_line:
      is_first_line = False
      continue
    docstring += wrapped_lines

  return "\n".join(docstring)


def append_used_proto_full_names(
    skill_name: str,
    params: list[ParameterInformation],
    docstring: list[str],
) -> None:
  """Appends a list of all used message full names to the docstring.

  Appends to the given docstring a list of all used message full names in the
  given parameter list. The names in the list are printed in the form
  "my_skill.intrinsic_proto.Pose".

  The list should be included near the top of the given docstring. This helps
  in some IDEs. E.g., VS Code will only show "Pose" in the signature tooltip but
  it will show this docstring right below.

  Args:
    skill_name: Name of the parent skill.
    params: List of parameter details.
    docstring: Docstring to append to.
  """

  message_full_names = {
      p.message_full_name for p in params if p.message_full_name is not None
  }
  if message_full_names:
    docstring.append("\nThis method accepts the following proto messages:")
    for name in sorted(message_full_names):
      # Expect 80 chars width, subtract 4 for leading spaces in list.
      wrapped_lines = textwrap.wrap(f"{skill_name}.{name}", 76)
      docstring.append(f"  - {wrapped_lines[0]}")
      docstring.extend(f"    {line}" for line in wrapped_lines[1:])

  enum_full_names = {
      p.enum_full_name for p in params if p.enum_full_name is not None
  }
  if enum_full_names:
    docstring.append("\nThis method accepts the following proto enums:")
    for name in sorted(enum_full_names):
      # Expect 80 chars width, subtract 4 for leading spaces in list.
      wrapped_lines = textwrap.wrap(f"{skill_name}.{name}", 76)
      docstring.append(f"  - {wrapped_lines[0]}")
      docstring.extend(f"    {line}" for line in wrapped_lines[1:])


def _gen_init_docstring(
    wrapped_type: Type[message.Message],
    skill_info: provided.SkillInfo,
) -> str:
  """Generates the __init__ docstring for a message wrapper class.

  Args:
    wrapped_type: Message to wrap.
    skill_name: Name of the parent skill.
    parameter_description: The skill's parameter description.

  Returns:
    Python documentation string.
  """
  docstring: list[str] = [
      "Initializes an instance of"
      f" {skill_info.skill_name}.{wrapped_type.DESCRIPTOR.full_name}."
  ]

  message_fields = extract_docstring_from_message(wrapped_type, skill_info)

  append_used_proto_full_names(skill_info.skill_name, message_fields, docstring)

  if message_fields:
    docstring.append("\nFields:")
    message_fields.sort(
        key=lambda p: (p.has_default, p.name, p.default, p.doc_string)
    )
    for m in message_fields:
      field_name = f"{m.name}:"
      docstring.append(field_name.rjust(len(field_name) + 4))
      # Expect 80 chars width, subtract 8 for leading spaces in args string.
      for param_doc_string in m.doc_string:
        for line in textwrap.wrap(param_doc_string.strip(), 72):
          docstring.append(line.rjust(len(line) + 8))
      if m.has_default:
        default = f"Default value: {m.default}"
        docstring.append(default.rjust(len(default) + 8))
  return "\n".join(docstring)


def _gen_init_params(
    wrapped_type: Type[message.Message],
    wrapper_classes: dict[str, Type[MessageWrapper]],
    enum_classes: dict[str, Type[enum.IntEnum]],
) -> list[inspect.Parameter]:
  """Create argument typing information for a given message.

  Args:
    wrapped_type: Message to be wrapped.
    wrapper_classes: Map from proto message names to corresponding message
      wrapper classes.
    enum_classes: Map from full proto enum names to corresponding enum wrapper
      classes.

  Returns:
    List of extracted parameters with typing information.
  """
  param_info = extract_parameter_information_from_message(
      wrapped_type,
      wrapper_classes,
      enum_classes,
  )
  params = [p for p, _ in param_info]

  # Sort items without default arguments before the ones with defaults.
  # This is required to generate valid function signatures.
  params.sort(key=lambda f: f.default == inspect.Parameter.empty, reverse=True)
  return params


class MessageWrapperNamespace:
  """Common base class for message wrapper namespace classes.

  Subclasses correspond to proto packages and are defined as nested classes
  within the generated skill classes. Subclasses cannot be instantiated and only
  serve as a namespace for message wrapper classes and other, nested subclasses
  of MessageWrapperNamespace.

  See _gen_wrapper_namespace_class() and _attach_wrapper_class() below.
  """

  def __init__(self, *args, **kwargs) -> None:
    del args, kwargs
    raise RuntimeError(
        f"This class ({type(self).__qualname__}) serves only as a namespace and"
        " cannot be instantiated."
    )


def _gen_wrapper_namespace_class(
    name: str, proto_path: str, skill_name: str, skill_package: str
) -> Type[Any]:
  """Generates a class to be used as a namespace for nested wrapper classes.

  Args:
    name: Name of the class to generate.
    proto_path: Prefix of a full proto type name corresponding to the namespace
      class to generate. E.g. 'intrinsic_proto', 'intrinsic_proto.foo' or
      'intrinsic_proto.foo.Bar' (if 'Bar' is just required as a namespace and we
      don't have a message wrapper for it).
    skill_name: Name of the parent skill.
    skill_package: Package name of the parent skill.

  Returns:
    The generated namespace class.
  """
  return type(
      name,
      (MessageWrapperNamespace,),
      {
          "__name__": name,
          "__qualname__": skill_name + _PYTHON_PACKAGE_SEPARATOR + proto_path,
          "__module__": module_for_generated_skill(skill_package),
          "__doc__": (
              "Namespace class corresponding to the proto package or message"
              f" {proto_path}.\n\nCannot be instantiated."
          ),
      },
  )


def _add_enum_value_shortcuts(
    *, add_to_class: Type[Any], enum_class: Type[enum.IntEnum]
) -> None:
  """Adds enum shortcuts to the namespace of the given parent class.

  Adds shortcuts for all the values of the given enum to the namespace of the
  given parent class.

  Args:
    add_to_class: Class to add the enum shortcuts to.
    enum_class: Enum class to add the shortcuts for.
  """
  for enum_value in enum_class:
    if hasattr(add_to_class, enum_value.name):
      print(
          "Name collision for enum value"
          f" {add_to_class.__name__}.{enum_value.name}. Enum"
          f" {enum_class.__qualname__} is not the only one with a value called"
          f" {enum_value.name} in its package."
      )
    else:
      setattr(add_to_class, enum_value.name, enum_value)


def _attach_wrapper_class(
    parent_name: str,
    relative_name: str,
    parent_class: Type[Any],
    wrapper_class_to_attach: Type[MessageWrapper | enum.IntEnum],
    skill_name: str,
    skill_package: str,
) -> None:
  """Attaches the given wrapper class as a nested class under a skill class.

  Attaches the given message or enum wrapper class as a nested class under a
  skill class according to its full proto type name.

  This method essentially implements an insertion operation into a prefix tree.
  The prefixes are the parts of the full proto type name corresponding to the
  class that is to be attached. The root node of the tree is the skill class,
  inner nodes can be message wrapper namespace classes or message wrapper
  classes, leaf nodes can be message wrapper classes or enum wrapper classes.

  E.g. the message/enum wrapper class corresponding to the message or enum
  'intrinsic_proto.foo.Bar.Baz' will be attached as:
    skill class
      -> namespace class 'intrinsic_proto' (created on the fly)
      -> namespace class 'foo' (created on the fly)
      -> namespace class 'Bar' (created on the fly)
         OR message wrapper class 'Bar' (if the parent skill uses 'Bar')
      -> message/enum wrapper class 'Baz'
  If the parent skill uses 'Bar' and there is a message wrapper class for it,
  'Bar' must be attached before 'Baz' so that no namespace class gets created
  for 'Bar'.

  Args:
    parent_name: Full name of the parent class.
    relative_name: Current path relative to parent_name under which to attach.
    parent_class: Current parent under which to attach.
    wrapper_class_to_attach: Wrapper class to attach.
    skill_name: Name of the parent skill.
    skill_package: Package name of the parent skill.
  """

  if _PROTO_PACKAGE_SEPARATOR not in relative_name:
    if hasattr(parent_class, relative_name):
      raise AssertionError(
          f"Internal error: Parent class {parent_name} already has a nested"
          f" class {relative_name}. Wrong attachment order?"
      )
    setattr(parent_class, relative_name, wrapper_class_to_attach)

    # Add enum shortcuts inlined into the parent namespace, analogous to how the
    # underlying proto classes provide access to enum values (see
    # https://protobuf.dev/reference/python/python-generated/#enum).
    # For example, this makes
    #   move_robot.intrinsic_proto.MyEnum.MY_ENUM_VALUE_ONE
    # available as
    #   move_robot.intrinsic_proto.MY_ENUM_VALUE_ONE
    if issubclass(wrapper_class_to_attach, enum.IntEnum):
      _add_enum_value_shortcuts(
          add_to_class=parent_class, enum_class=wrapper_class_to_attach
      )

    return

  prefix = relative_name.split(_PROTO_PACKAGE_SEPARATOR)[0]
  child_name = (
      f"{parent_name}{_PROTO_PACKAGE_SEPARATOR}{prefix}"
      if parent_name
      else prefix
  )

  if not hasattr(parent_class, prefix):
    child_class = _gen_wrapper_namespace_class(
        prefix, child_name, skill_name, skill_package
    )
    setattr(parent_class, prefix, child_class)
  else:
    # In this case, 'child_class' is a namespace class or a message wrapper
    # class that serves as a namespace for a nested proto message or enum.
    child_class = getattr(parent_class, prefix)

  _attach_wrapper_class(
      child_name,
      relative_name.removeprefix(prefix).lstrip("."),
      child_class,
      wrapper_class_to_attach,
      skill_name,
      skill_package,
  )


def _gen_enum_class(
    enum_descriptor: descriptor.EnumDescriptor,
    skill_name: str,
    skill_package: str,
) -> Type[enum.IntEnum]:
  enum_values = {value.name: value.number for value in enum_descriptor.values}
  enum_class = enum.IntEnum(enum_descriptor.name, enum_values)
  enum_class.__qualname__ = skill_name + "." + enum_descriptor.full_name
  enum_class.__module__ = module_for_generated_skill(skill_package)
  return enum_class


def update_message_class_modules(
    cls: Type[Any],
    skill_info: provided.SkillInfo,
    message_classes_to_wrap: dict[str, Type[message.Message]],
    enum_descriptors_to_wrap: dict[str, descriptor.EnumDescriptor],
) -> tuple[dict[str, Type[MessageWrapper]], dict[str, Type[enum.IntEnum]]]:
  """Updates given class with type aliases.

  Creates aliases (members) in the given cls for the given nested classes.

  Args:
    cls: class to modify
    skill_info: SkillInfo containing name, parameters, etc.
    message_classes_to_wrap: Map from full proto message names to message
      classes, containing the message classes for which to generate wrapper
      classes under the skill class.
    enum_descriptors_to_wrap: Map from full proto enum names to enum
      descriptors, containing enums for which to generate enum classes under the
      skill class.

  Returns:
    A tuple of 1) a map from proto message names to corresponding message
    wrapper classes and 2) a map from full proto enum names to corresponding
    enum classes.
  """
  enum_classes: dict[str, Type[enum.IntEnum]] = {
      enum_full_name: _gen_enum_class(
          enum_desc, skill_info.skill_name, skill_info.package_name
      )
      for enum_full_name, enum_desc in enum_descriptors_to_wrap.items()
  }

  wrapper_classes: dict[str, Type[MessageWrapper]] = {}
  for message_full_name, message_type in message_classes_to_wrap.items():
    wrapper_class = _gen_wrapper_class(message_type, skill_info)
    wrapper_classes[message_full_name] = wrapper_class

  # The init function of a wrapper class may reference any other wrapper class
  # (proto definitions can be recursive!). So we can only generate the init
  # functions after all classes have been generated.
  for message_full_name, wrapper_class in wrapper_classes.items():
    wrapper_class.__init__ = _gen_init_fun(
        wrapper_class.wrapped_type,
        skill_info,
        message_full_name,
        wrapper_classes,
        enum_classes,
    )

  # Attach message classes to skill class in sorted order to ensure that nested
  # proto message are handled correctly. E.g., 'foo.Bar' needs to be attached
  # before 'foo.Bar.Nested' so that we don't create a namespace class for
  # 'foo.Bar' when inserting 'foo.Bar.Nested'. Note that we might still create
  # 'foo.Bar' as a namespace class if the skill uses 'foo.Bar.Nested' but not
  # 'foo.Bar'.
  for message_full_name in sorted(wrapper_classes):
    wrapper_class = wrapper_classes[message_full_name]
    _attach_wrapper_class(
        "",
        message_full_name,
        cls,
        wrapper_class,
        skill_info.skill_name,
        skill_info.package_name,
    )

  for enum_full_name, enum_class in enum_classes.items():
    _attach_wrapper_class(
        "",
        enum_full_name,
        cls,
        enum_class,
        skill_info.skill_name,
        skill_info.package_name,
    )

  # Add special enum shortcuts for enums that are nested directly within the
  # params message of the skill. For example, make
  #   move_robot.intrinsic_proto.MoveRobotParams.MyEnum.MY_ENUM_VALUE_ONE
  # available as
  #   move_robot.MY_ENUM_VALUE_ONE
  if cls.info.parameter_message_full_name:
    for enum_full_name, enum_class in enum_classes.items():
      if enum_full_name.startswith(cls.info.parameter_message_full_name):
        _add_enum_value_shortcuts(add_to_class=cls, enum_class=enum_class)

  return wrapper_classes, enum_classes


def deconflict_param_and_resources(
    resource_slot: str,
    param_names: Container[str],
    try_suffix: str = RESOURCE_SLOT_DECONFLICT_SUFFIX,
) -> str:
  """Deconflicts resource slot name from existing parameters.

  Resource slots and parameter names are two separate namespaces in the skill
  specification. But for our API purposes, they both become parameters to the
  same function, and thus cannot be the same.

  Args:
    resource_slot: resource slot name
    param_names: container with all regular parameter names
    try_suffix: suffix to append on conflict to try deconfliction

  Returns:
    This function checks whether the resource_slot is in param_names. If it is
    not, the resource_slot is returned unmodified. If it is contained, the
    try_suffix is added. If it still is contained, an exception is raised.
    Otherwise the extended name is returned.

  Raises:
    NameError: if resource_slot as well as resource_slot+try_suffix is already
      contained in param_names
  """
  if resource_slot not in param_names:
    return resource_slot

  # We have a conflict, a skill has a resource slot and a parameter with
  # the same name (we are mixing two namespaces here). Add suffix to
  # slot name to disambiguate
  try_slot = resource_slot + try_suffix
  if try_slot in param_names:
    # Still a conflict!? Ok, out of luck, we cannot recover that one
    raise NameError(
        f"'{resource_slot}' and '{try_slot}' are both parameter "
        "names and resource slots."
    )
  return try_slot


def _is_map_field(field: descriptor.FieldDescriptor) -> bool:
  """Returns True if the given field is a map."""
  return (
      field.is_repeated
      # Under the hood, a map is a repeated field with a special,
      # auto-generated message type. This type has the 'map_entry' flag set.
      and field.type == descriptor.FieldDescriptor.TYPE_MESSAGE
      and field.message_type.GetOptions().map_entry
  )


def _is_repeated_field(field: descriptor.FieldDescriptor) -> bool:
  """Returns True if the given field is a repeated field (but not a map!)."""
  return field.is_repeated and not _is_map_field(field)


def _extract_field_type_from_message_field(
    field: descriptor.FieldDescriptor,
    wrapper_classes: dict[str, Type[MessageWrapper]],
    enum_classes: dict[str, Type[enum.IntEnum]],
) -> Type[Any]:
  """Extracts the pythonic type of the given field.

  Extracts the type of the given message field to be used in the Python
  signature of a skill or message wrapper class.

  Args:
    field: The field for which to extract the Pythonic type.
    wrapper_classes: Map from proto message names to corresponding message
      wrapper classes.
    enum_classes: Map from full proto enum names to corresponding enum wrapper
      classes.

  Returns:
    The Pythonic type of the given field.
  """
  if _is_map_field(field):
    # Under the hood, map fields are repeated fields whose type is an
    # auto-generated message type with two fields called 'key' and 'value'.
    # See https://protobuf.dev/programming-guides/proto3/#backwards.
    key_type = pythonic_field_type(
        field.message_type.fields_by_name["key"], wrapper_classes, enum_classes
    )
    value_type = pythonic_field_type(
        field.message_type.fields_by_name["value"],
        wrapper_classes,
        enum_classes,
    )
    field_type = Union[dict[key_type, value_type], provided.ParamAssignment]
    field_type = Optional[field_type]
  elif _is_repeated_field(field):
    field_type = Union[
        Sequence[
            Union[
                pythonic_field_type(field, wrapper_classes, enum_classes),
                provided.ParamAssignment,
            ]
        ],
        provided.ParamAssignment,
    ]
    field_type = Optional[field_type]
  else:
    # Singular field (possibly optional or in a oneof).
    field_type = Union[
        pythonic_field_type(field, wrapper_classes, enum_classes),
        provided.ParamAssignment,
    ]
    field_type = Optional[field_type]

  return field_type


def extract_parameter_information_from_message(
    message_type: Type[message.Message],
    wrapper_classes: dict[str, Type[MessageWrapper]],
    enum_classes: dict[str, Type[enum.IntEnum]],
) -> list[tuple[inspect.Parameter, str]]:
  """Extracts signature parameters for a skill or wrapper class.

  Args:
    message_type: The proto message type to inspect.
    wrapper_classes: Map from proto message names to corresponding wrapper
      classes.
    enum_classes: Map from full proto enum names to corresponding enum wrappers.

  Returns:
    A list of tuples containing the signature Parameter and field name.
  """
  params: list[tuple[inspect.Parameter, str]] = []

  for field in message_type.DESCRIPTOR.fields:
    field_type = _extract_field_type_from_message_field(
        field,
        wrapper_classes,
        enum_classes,
    )

    params.append((
        inspect.Parameter(
            field.name,
            inspect.Parameter.KEYWORD_ONLY,
            annotation=field_type,
            default=None,
        ),
        field.name,
    ))

  return params


def extract_docstring_from_message(
    message_type: Type[message.Message],
    skill_info: provided.SkillInfo,
) -> list[ParameterInformation]:
  """Extracts docstring information for the fields of the given message.

  To be used for generating the __init__ docstring of a skill or message wrapper
  class.

  Args:
    message_type: The message type to extract docstring information for.
    skill_info: Metadata of the skill.

  Returns:
    List containing a ParameterInformation object describing for each field.
  """
  params: list[ParameterInformation] = []

  for field in message_type.DESCRIPTOR.fields:
    params.append(
        ParameterInformation(
            has_default=False,
            name=field.name,
            default=None,
            doc_string=[skill_info.get_proto_comment(field.full_name)],
            message_full_name=(
                field.message_type.full_name
                if field.message_type is not None
                else None
            ),
            enum_full_name=(
                field.enum_type.full_name
                if field.enum_type is not None
                else None
            ),
        )
    )

  return params


def wait_for_skill(
    skill_registry: skill_registry_client.SkillRegistryClient,
    skill_id: str,
    version: str = "",
    wait_duration: float = 60.0,
) -> None:
  """Polls the Skill registry until matching skill is found.

  Args:
    skill_registry: The skill registry to query.
    skill_id: Fully-qualified name of the skill to wait for.
    version: If non-empty, wait for this specific version of the skill.
    wait_duration: Time in seconds to wait before timing out.

  Raises:
    TimeoutError: If the wait_duration is exceeded and the skill still not
    available.
  """
  start_time = datetime.datetime.now()
  skill_id_version = f"{skill_id}.{version}"
  while True:
    if datetime.datetime.now() > start_time + datetime.timedelta(
        seconds=wait_duration
    ):
      raise TimeoutError(
          f"Timeout after {wait_duration}s while waiting for skill"
          f" {skill_id} to become available."
      )

    try:
      skill = skill_registry.get_skill(skill_id)
      if not version or skill_id_version == skill.id_version:
        break

    except grpc.RpcError:
      time.sleep(1)


def create_message_from_file_descriptor_set(
    file_descriptor_set: descriptor_pb2.FileDescriptorSet,
    message_full_name: str,
) -> message.Message:
  """Creates an empty message instance from a file descriptor set and name.

  Creates a single-use descriptor pool from the file desriptor set on-the-fly.

  Args:
    file_descriptor_set: The FileDescriptorSet containing the message
      definition.
    message_full_name: The fully qualified name of the message.

  Returns:
    An empty message instance.

  Raises:
    ValueError: If the message type is not found in the descriptor pool.
  """
  desc_pool = descriptors.create_descriptor_pool(file_descriptor_set)
  message_type = desc_pool.FindMessageTypeByName(message_full_name)
  if message_type is None:
    raise ValueError(
        f"Message type '{message_full_name}' not found in descriptor pool"
    )
  return message_factory.GetMessageClass(message_type)()
