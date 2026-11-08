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

"""Compare protos under test."""

import copy
from typing import Optional
from typing import Union
import unittest

from google.protobuf import descriptor
from google.protobuf import message
from google.protobuf import text_format


def _clear_field(proto: message.Message, field_path: str) -> None:
  """Clears field_path in proto.

  field_path contains field names separated by '.' into the proto, e.g.,
  my_sub_message.my_repeated_field.my_field.
  A field is removed by calling ClearField.

  Args:
    proto: A proto message to be modified.
    field_path: The path to the field to be cleared.
  """

  next_field_name, _, path_suffix = field_path.partition(".")
  if next_field_name not in proto.DESCRIPTOR.fields_by_name:
    raise ValueError(
        f"Field {next_field_name} in field path {field_path} does not refer to"
        f" a known field for message {proto.DESCRIPTOR.full_name}."
    )

  # root case, field_path was just a field
  if not path_suffix:
    proto.ClearField(next_field_name)
    return

  # next_field can refer to:
  # - a submessage (or oneof of submessages)
  # - a repeated field of messages
  next_field: descriptor.FieldDescriptor = proto.DESCRIPTOR.fields_by_name[
      next_field_name
  ]
  if next_field.type != descriptor.FieldDescriptor.TYPE_MESSAGE:
    raise ValueError(
        f"Field {next_field_name} in field path {field_path} does not refer to"
        f" a message field for message {proto.DESCRIPTOR.full_name}."
    )

  if next_field.is_repeated:
    sub_field_list = getattr(proto, next_field_name)
    for sub_message in sub_field_list:
      _clear_field(sub_message, path_suffix)
    return

  if not proto.HasField(next_field_name):
    return
  sub_message = getattr(proto, next_field_name)
  _clear_field(sub_message, path_suffix)


def _sort_repeated_fields(proto: message.Message, deduplicate: bool) -> None:
  """Sorts all repeated fields including in submessages.

  This is typically called to have a canonical order of repeated fields in the
  message for comparison. Thus no particular order is guaranteed, but only that
  the order is deterministic for multiple calls on equal messages.

  Args:
    proto: A proto message to be modified.
    deduplicate: Determines if duplicate elements in repeated fields should be
      removed.
  """

  # recurse first, then sort
  field: descriptor.FieldDescriptor
  for field in proto.DESCRIPTOR.fields:
    if field.type != descriptor.FieldDescriptor.TYPE_MESSAGE:
      continue
    # At this point field can be
    # - just a single message
    # - a repeated field (list) of messages
    # - a map to a scalar value
    # - a map to message values
    if field.is_repeated:
      sub_field_list = getattr(proto, field.name)

      if (
          field.type == descriptor.FieldDescriptor.TYPE_MESSAGE
          and field.message_type.has_options
          and field.message_type.GetOptions().map_entry
          and field.message_type.fields_by_name["value"].type
          != descriptor.FieldDescriptor.TYPE_MESSAGE
      ):
        # this is a map to build in types (not to message) - nothing to recurse
        continue

      if (
          field.type == descriptor.FieldDescriptor.TYPE_MESSAGE
          and field.message_type.has_options
          and field.message_type.GetOptions().map_entry
      ):
        # this is a map to messages
        for _, sub_message in sub_field_list.items():
          _sort_repeated_fields(sub_message, deduplicate)
      else:
        # this is just a repeated field of messages
        for sub_message in sub_field_list:
          _sort_repeated_fields(sub_message, deduplicate)
    elif proto.HasField(field.name):
      # a single message field
      sub_message = getattr(proto, field.name)
      _sort_repeated_fields(sub_message, deduplicate)

  # now, sort each field, where sub-fields are already sorted (and thus
  # canonical)
  for field in proto.DESCRIPTOR.fields:
    if not field.is_repeated:
      continue

    if (
        field.type == descriptor.FieldDescriptor.TYPE_MESSAGE
        and field.message_type.has_options
        and field.message_type.GetOptions().map_entry
    ):
      continue  # do not sort maps

    sub_field_list = getattr(proto, field.name)
    if not sub_field_list:
      continue

    if field.type == descriptor.FieldDescriptor.TYPE_MESSAGE:
      key_fn = text_format.MessageToString
    else:
      key_fn = lambda x: x
    sub_field_list.sort(key=key_fn)
    sub_field_list_no_duplicates = []
    prev = None
    for sub_msg in sub_field_list:
      if not deduplicate or (prev is None or key_fn(prev) != key_fn(sub_msg)):
        sub_field_list_no_duplicates.append(sub_msg)
      prev = sub_msg
    del sub_field_list[:]
    sub_field_list.extend(sub_field_list_no_duplicates)


def _floats_in_tolerance(value_a: float, value_b: float, rtol: float) -> bool:
  return abs(value_a - value_b) <= rtol * max(abs(value_a), abs(value_b))


def _equalize_floats_in_tolerance(
    proto_a: message.Message, proto_b: message.Message, rtol: float
) -> None:
  """Replaces all floats in proto_a with floats from proto_b, if both are in rtol.

  All equivalent floating point values (floats and doubles) in proto_a will be
  replaced by the exact values from proto_b, such that there will be no more
  difference between these two messages regarding floats within rtol. This is
  typically called to facilitate a readable diff including non-float fields.

  Args:
    proto_a: A proto message to be modified.
    proto_b: A given proto message.
    rtol: A relative tolerance defining if the floats are considered equivalent.
      rtol is considered as a proportion of the float with the larger magnitude.
  """
  if proto_a.DESCRIPTOR != proto_b.DESCRIPTOR:
    return

  # Relevant fields to be handled by this function.
  # Directly:
  # - floats (float and double)
  # - repeated floats
  # - map to float
  # By recursion:
  # - message fields
  # - repeated messages
  # - map to messages
  proto_a_field_names = set(fd.name for fd, _ in proto_a.ListFields())
  proto_b_field_names = set(fd.name for fd, _ in proto_b.ListFields())
  for field_name in proto_a_field_names.intersection(proto_b_field_names):
    field: descriptor.FieldDescriptor = proto_a.DESCRIPTOR.fields_by_name[
        field_name
    ]

    value_a = getattr(proto_a, field.name)
    value_b = getattr(proto_b, field.name)

    if (
        field.type == descriptor.FieldDescriptor.TYPE_FLOAT
        or field.type == descriptor.FieldDescriptor.TYPE_DOUBLE
    ):
      if not field.is_repeated:
        # field is just a float
        if _floats_in_tolerance(value_a, value_b, rtol):
          setattr(proto_a, field.name, value_b)
      else:
        # field is a list of floats
        for index in range(min(len(value_a), len(value_b))):
          if _floats_in_tolerance(value_a[index], value_b[index], rtol):
            value_a[index] = value_b[index]

    if field.type != descriptor.FieldDescriptor.TYPE_MESSAGE:
      continue

    if field.is_repeated:
      if (
          field.message_type.has_options
          and field.message_type.GetOptions().map_entry
      ):
        value_type = field.message_type.fields_by_name["value"]
        # field is a map. We iterate over a copy of value_a.items() so that we
        # can safely modify the map while doing so.
        for key, mapped_value_a in list(value_a.items()):
          mapped_value_b = value_b.get(key)
          if mapped_value_b is None:
            continue
          if (
              value_type.type == descriptor.FieldDescriptor.TYPE_FLOAT
              or value_type.type == descriptor.FieldDescriptor.TYPE_DOUBLE
          ):
            # field is a map to floats
            if _floats_in_tolerance(mapped_value_a, mapped_value_b, rtol):
              value_a[key] = mapped_value_b
          elif value_type.type == descriptor.FieldDescriptor.TYPE_MESSAGE:
            # field is a map to messages - recurse
            _equalize_floats_in_tolerance(
                mapped_value_a, mapped_value_b, rtol=rtol
            )
      else:
        # field is a list of messages - recuse
        for sub_message_a, sub_message_b in zip(value_a, value_b):
          _equalize_floats_in_tolerance(sub_message_a, sub_message_b, rtol=rtol)
    else:
      # field is just a single message - recurse
      _equalize_floats_in_tolerance(value_a, value_b, rtol=rtol)


# pylint:disable-next=invalid-name
def assertProto2Equal(
    testobj: unittest.case.TestCase,
    proto_a: Union[message.Message, str, bytes],
    proto_b: Union[message.Message, str, bytes],
    *,
    ignored_fields: Optional[list[str]] = None,
    rtol: Optional[float] = None,
) -> None:
  """Asserts that two protos are equal.

  Args:
    testobj: The test case that called this comparison.
    proto_a: A proto or textproto to compare.
    proto_b: A proto or textproto to compare to.
    ignored_fields: List of field paths into the proto to be ignored during
      comparison.
    rtol: Relative tolerance to compare floating point values. If not set,
      floats are compared using string comparison.
  """
  if isinstance(proto_a, str | bytes) and isinstance(proto_b, str | bytes):
    raise TypeError("At least one Message instance required")
  if isinstance(proto_a, str | bytes):
    proto_a = text_format.Parse(proto_a, type(proto_b)())
  if isinstance(proto_b, str | bytes):
    proto_b = text_format.Parse(proto_b, type(proto_a)())

  copied = False
  if ignored_fields is not None:
    proto_a = copy.deepcopy(proto_a)
    proto_b = copy.deepcopy(proto_b)
    copied = True
    for field_path in ignored_fields:
      _clear_field(proto_a, field_path)
      _clear_field(proto_b, field_path)

  if rtol is not None:
    if not copied:
      proto_a = copy.deepcopy(proto_a)
      proto_b = copy.deepcopy(proto_b)
    _equalize_floats_in_tolerance(proto_a, proto_b, rtol)

  txt_a = text_format.MessageToString(proto_a)
  txt_b = text_format.MessageToString(proto_b)
  testobj.assertMultiLineEqual(txt_a, txt_b)


# pylint:disable-next=invalid-name
def assertProto2Contains(
    testobj: unittest.case.TestCase,
    proto_needle: Union[message.Message, str, bytes],
    proto_haystack: message.Message,
    *,
    ignored_fields: Optional[list[str]] = None,
) -> None:
  """Asserts that fields from proto_needle are set the same in proto_haystack.

  Args:
    testobj: The test case that called this comparison.
    proto_needle: A proto to compare with proto_haystack.
    proto_haystack: A proto that contains all fields in proto_needle and others.
    ignored_fields: List of field paths into the proto to be ignored during
      comparison.
  """
  if isinstance(proto_needle, str | bytes):
    proto_needle = text_format.Parse(proto_needle, proto_haystack.__class__())
  else:
    proto_needle = copy.deepcopy(proto_needle)
  proto_haystack = copy.deepcopy(proto_haystack)
  if ignored_fields is not None:
    for field_path in ignored_fields:
      _clear_field(proto_needle, field_path)
      _clear_field(proto_haystack, field_path)

  proto_needle_full = copy.deepcopy(proto_haystack)
  proto_needle_full.MergeFrom(proto_needle)

  _sort_repeated_fields(proto_needle_full, deduplicate=True)
  _sort_repeated_fields(proto_haystack, deduplicate=True)

  txt_needle = text_format.MessageToString(proto_needle_full)
  txt_haystack = text_format.MessageToString(proto_haystack)
  testobj.assertMultiLineEqual(txt_needle, txt_haystack)


# pylint:disable-next=invalid-name
def assertProto2SameElements(
    testobj: unittest.case.TestCase,
    proto_a: Union[message.Message, str, bytes],
    proto_b: Union[message.Message, str, bytes],
    *,
    ignored_fields: Optional[list[str]] = None,
    keep_duplicate_values: Optional[bool] = None,
) -> None:
  """Asserts that fields from proto_a and proto_b are the same.

  For repeated fields, both messages must have the same items, but count or
  order does not matter.
  The semantics are similar to, e.g., absltest.assertSameElements.
  This method does not care about any duplicates unless keep_duplicate_values
  is set to true.

  Args:
    testobj: The test case that called this comparison.
    proto_a: A proto or textproto to compare with proto_b.
    proto_b: The proto or textproto to compare to.
    ignored_fields: List of field paths into the proto to be ignored during
      comparison.
    keep_duplicate_values: Keep duplicate values before comparing. If not set or
      set to false, duplicate values will be considered one value. This makes it
      possible to compare similar to set semantics.
  """
  if isinstance(proto_a, str | bytes) and isinstance(proto_b, str | bytes):
    raise TypeError("At least one Message instance required")
  if isinstance(proto_a, str | bytes):
    proto_a = text_format.Parse(proto_a, type(proto_b)())
  if isinstance(proto_b, str | bytes):
    proto_b = text_format.Parse(proto_b, type(proto_a)())

  proto_a = copy.deepcopy(proto_a)
  proto_b = copy.deepcopy(proto_b)
  if ignored_fields is not None:
    for field_path in ignored_fields:
      _clear_field(proto_a, field_path)
      _clear_field(proto_b, field_path)

  deduplicate = True
  if keep_duplicate_values is not None and keep_duplicate_values:
    deduplicate = False

  _sort_repeated_fields(proto_a, deduplicate)
  _sort_repeated_fields(proto_b, deduplicate)

  txt_a = text_format.MessageToString(proto_a)
  txt_b = text_format.MessageToString(proto_b)
  testobj.assertMultiLineEqual(txt_a, txt_b)
