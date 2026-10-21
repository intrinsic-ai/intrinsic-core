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

"""Helpers for viewing and manipulating proto descriptors."""

from collections.abc import Iterable
import textwrap

from google.protobuf import descriptor
from google.protobuf import descriptor_pb2
from google.protobuf import descriptor_pool


def _add_file_and_imports(
    file_descriptors: dict[str, descriptor.FileDescriptor],
    current_file: descriptor.FileDescriptor,
):
  """Adds the current file to file_descriptors if not present.

  Recursively adds dependencies until all dependencies (along with transients)
  are present in file_descriptors.

  Args:
    file_descriptors: A dictionary mapping file descriptor name to file
      descriptors. Modified to add transitive dependencies of current_file
    current_file: The file descriptor of the current file.
  """
  if current_file.name in file_descriptors:
    return

  file_descriptors[current_file.name] = current_file
  for dependency_file_descriptor in current_file.dependencies:
    _add_file_and_imports(file_descriptors, dependency_file_descriptor)


def gen_file_descriptor_set(
    msg_descriptor: descriptor.Descriptor | Iterable[descriptor.Descriptor],
) -> descriptor_pb2.FileDescriptorSet:
  """Generates a FileDescriptorSet given a Descriptor.

  Args:
    msg_descriptor: The message descriptor(s) of interest.

  Returns:
    A descriptor_pb2.FileDescriptorSet containing the file descriptors of all
    transitive dependencies of msg_descriptor.
  """
  msg_descriptors: Iterable[descriptor.Descriptor] = (
      msg_descriptor
      if isinstance(msg_descriptor, Iterable)
      else [msg_descriptor]
  )
  file_descriptor_set = descriptor_pb2.FileDescriptorSet()
  file_descriptors: dict[str, descriptor.FileDescriptor] = {}
  for msg_descriptor in msg_descriptors:
    _add_file_and_imports(file_descriptors, msg_descriptor.file)
    for file_descriptor in file_descriptors.values():
      file_descriptor.CopyToProto(file_descriptor_set.file.add())
  return file_descriptor_set


def merge_file_descriptor_sets(
    sets: Iterable[descriptor_pb2.FileDescriptorSet],
) -> descriptor_pb2.FileDescriptorSet:
  """Merges the files of the given file descriptor sets into a single set.

  Merges the given file descriptor sets into a single set without duplicate
  files. If a file name appears in more than one input file descriptor set this
  checks that all instances are identical.

  Args:
    sets: The file descriptor sets to merge.

  Returns:
    The merged file descriptor set.

  Raises:
    TypeError: If a file is contained in more that one input file descriptor set
      but the file's instances are not identical.
  """
  files: dict[str, descriptor_pb2.FileDescriptorProto] = {}
  result_set = descriptor_pb2.FileDescriptorSet()

  for input_set in sets:
    for file in input_set.file:
      if file.name not in files:
        # This makes a copy of 'file'
        result_set.file.append(file)
        files[file.name] = file
      elif file != files[file.name]:
        raise ValueError(
            f'"{file.name}" contained in more than one input file descriptor'
            ' set but file instances are not identical'
        )

  # Check that the merged FileDescriptorSet is valid.
  try:
    pool = create_descriptor_pool(result_set)
    for file in result_set.file:
      # FindFileByName forces lazy resolution of the file and its dependencies,
      # which validates that all dependencies are present.
      pool.FindFileByName(file.name)
  except Exception as e:
    raise ValueError(
        'failed to generate a valid merged FileDescriptorSet (sets were likely'
        ' built at different times).'
    ) from e

  return result_set


def _append_file_descriptor_set_to_pool(
    pool: descriptor_pool.DescriptorPool,
    file_set: descriptor_pb2.FileDescriptorSet,
) -> None:
  """Appends a given file descriptor set to a file descriptor pool.

  Runs through all file descriptors and appends them one-by-one to the pool.

  Args:
    pool: Protobuf descriptor pool to append descriptors to
    file_set: proto containing the set of descriptors to add.
  """
  file_by_name = {file_proto.name: file_proto for file_proto in file_set.file}

  # N.B. GetMessages() file protos must be added in topo ordering to satisfy the
  # cpp impl of python Protobuf. If this is not done, we get strange errors
  # like "Message.field: 'FieldType' seems to be defined in 'file.proto',
  # which is not imported by '#.proto'.  To use it here, please add the
  # necessary import"
  def add_file(file_proto: descriptor_pb2.FileDescriptorProto):
    for dependency in file_proto.dependency:
      if dependency in file_by_name:
        # Remove from elements to be visited, in order to cut cycles.
        add_file(file_by_name.pop(dependency))
    pool.Add(file_proto)

  while file_by_name:
    add_file(file_by_name.popitem()[1])


def create_descriptor_pool(
    file_set: descriptor_pb2.FileDescriptorSet,
) -> descriptor_pool.DescriptorPool:
  """Returns a DescriptorPool containing all files in the given file set.

  Args:
    file_set: The file descriptors to seed the descriptor pool with.

  Returns:
    A descriptor pool containing all descriptors from the given file_set.
  """
  pool = descriptor_pool.DescriptorPool()
  _append_file_descriptor_set_to_pool(pool, file_set)
  return pool


def _add_location_with_path(
    path: list[int],
    full_name: str,
    source_code_info: descriptor_pb2.SourceCodeInfo,
    locations: dict[str, descriptor_pb2.SourceCodeInfo.Location],
):
  for location in source_code_info.location:
    if path == location.path and (
        location.leading_detached_comments
        or location.leading_comments
        or location.trailing_comments
    ):
      locations[full_name] = location


def _extract_locations_from_message_type(
    message_type: descriptor_pb2.DescriptorProto,
    message_full_name: str,
    path: list[int],
    source_code_info: descriptor_pb2.SourceCodeInfo,
    locations: dict[str, descriptor_pb2.SourceCodeInfo.Location],
):
  _add_location_with_path(path, message_full_name, source_code_info, locations)

  for i, nested_message_type in enumerate(message_type.nested_type):
    nested_path = path + [
        descriptor_pb2.DescriptorProto.NESTED_TYPE_FIELD_NUMBER,
        i,
    ]
    _extract_locations_from_message_type(
        nested_message_type,
        f'{message_full_name}.{nested_message_type.name}',
        nested_path,
        source_code_info,
        locations,
    )

  for i, enum_type in enumerate(message_type.enum_type):
    enum_path = path + [
        descriptor_pb2.DescriptorProto.ENUM_TYPE_FIELD_NUMBER,
        i,
    ]
    _extract_locations_from_enum_type(
        enum_type,
        f'{message_full_name}.{enum_type.name}',
        enum_path,
        source_code_info,
        locations,
    )

  for i, field in enumerate(message_type.field):
    field_path = path + [descriptor_pb2.DescriptorProto.FIELD_FIELD_NUMBER, i]
    _add_location_with_path(
        field_path,
        f'{message_full_name}.{field.name}',
        source_code_info,
        locations,
    )


def _extract_locations_from_enum_type(
    enum_type: descriptor_pb2.EnumDescriptorProto,
    enum_full_name: str,
    path: list[int],
    source_code_info: descriptor_pb2.SourceCodeInfo,
    locations: dict[str, descriptor_pb2.SourceCodeInfo.Location],
):
  _add_location_with_path(path, enum_full_name, source_code_info, locations)

  for i, value in enumerate(enum_type.value):
    value_path = path + [
        descriptor_pb2.EnumDescriptorProto.VALUE_FIELD_NUMBER,
        i,
    ]
    value_full_name = f'{enum_full_name}.{value.name}'
    _add_location_with_path(
        value_path, value_full_name, source_code_info, locations
    )


def _extract_locations_from_file(
    file: descriptor_pb2.FileDescriptorProto,
    locations: dict[str, descriptor_pb2.SourceCodeInfo.Location],
):
  for i, message_type in enumerate(file.message_type):
    path = [descriptor_pb2.FileDescriptorProto.MESSAGE_TYPE_FIELD_NUMBER, i]
    message_full_name = f'{file.package}.{message_type.name}'
    _extract_locations_from_message_type(
        message_type, message_full_name, path, file.source_code_info, locations
    )

  for i, enum_type in enumerate(file.enum_type):
    path = [descriptor_pb2.FileDescriptorProto.ENUM_TYPE_FIELD_NUMBER, i]
    enum_full_name = f'{file.package}.{enum_type.name}'
    _extract_locations_from_enum_type(
        enum_type, enum_full_name, path, file.source_code_info, locations
    )

  return None


def extract_comment_locations_from_file_descriptor_set(
    fds: descriptor_pb2.FileDescriptorSet,
) -> dict[str, descriptor_pb2.SourceCodeInfo.Location]:
  """Extracts comment locations from a file descriptor set.

  Extracts from the given file descriptor set a mapping from full names to
  corresponding SourceCodeInfo.Location objects. Entries are only generated for
  source code elements which have associated comments and only for the following
  types of source code elements:
    - messages    (example key: "intrinsic_proto.Pose")
    - fields      (example key: "intrinsic_proto.Pose.x")
    - enums       (example key: "intrinsic_proto.UpdateMode")
    - enum values (example key: "intrinsic_proto.UpdateMode.OVERWRITE")

  For example, assume the input is a file descriptor set created from the
  following, single proto file:

  ```
  syntax = "proto3";
  package intrinsic_proto;

  // Number detached comment

  // Number leading comment
  message Number { // Number trailing comment
  }
  ```

  Then the result would be:

  {
    "intrinsic_proto.Number": descriptor_pb2.SourceCodeInfo.Location(
      leading_detached_comments=[' Number detached comment\n'],
      leading_comments=' Number leading comment\n',
      trailing_comments=' Number trailing comment\n',
      path: ...,
      span: ...,
    )
  }
  """
  locations: dict[str, descriptor_pb2.SourceCodeInfo.Location] = {}

  for file in fds.file:
    _extract_locations_from_file(file, locations)

  return locations


def extract_attached_comments_from_file_descriptor_set(
    fds: descriptor_pb2.FileDescriptorSet,
) -> dict[str, str]:
  """Extracts attached comments from a file descriptor set.

  This is a convenience variant of
  extract_comment_locations_from_file_descriptor_set() above.

  Extracts from the given file descriptor set a mapping from full names to a
  concatenation of all leading and trailing comments (a multi-line string).
  Entries are only generated for source code elements which have associated
  comments and only for the following types of source code elements:
    - messages    (example key: "intrinsic_proto.Pose")
    - fields      (example key: "intrinsic_proto.Pose.x")
    - enums       (example key: "intrinsic_proto.UpdateMode")
    - enum values (example key: "intrinsic_proto.UpdateMode.OVERWRITE")

  For example, assume the input is a file descriptor set created from the
  following, single proto file:

  ```
  syntax = "proto3";
  package intrinsic_proto;

  // Number leading comment
  message Number { // Number trailing comment
  }
  ```

  Then the result would be:

  {
    "intrinsic_proto.Number":
      "Number leading comment\nNumber trailing comment\n"
  }

  """

  locations = extract_comment_locations_from_file_descriptor_set(fds)

  comments: dict[str, str] = {}
  for full_name, location in locations.items():
    comment = ''
    if location.leading_comments:
      comment += textwrap.dedent(location.leading_comments)
    if location.trailing_comments:
      comment += textwrap.dedent(location.trailing_comments)
    comments[full_name] = comment

  return comments
