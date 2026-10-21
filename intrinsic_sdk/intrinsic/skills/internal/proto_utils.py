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

"""Utility functions for skills related proto conversions."""

from google.protobuf import descriptor_pb2

from intrinsic.assets import id_utils
from intrinsic.skills.proto import skill_manifest_pb2
from intrinsic.skills.proto import skills_pb2
from intrinsic.util.proto import source_code_info_view_py


def proto_from_skill_manifest(
    manifest: skill_manifest_pb2.SkillManifest,
    file_descriptor_set: descriptor_pb2.FileDescriptorSet,
    version: str,
) -> skills_pb2.Skill:
  """Create Skill descriptor proto from skill manifest.

  Equivalent to intrinsic/skills/internal/skill_proto_utils.h;l=60-63

  Args:
    manifest: Skill to create descriptor proto for.
    file_descriptor_set: File descriptor set.
    version: The version of the skill. Must be in semver format.

  Returns:
    Skill descriptor proto.

  Raises:
    ValueError if version is not a valid semver version.
  """
  skill_proto = skills_pb2.Skill(
      skill_name=manifest.id.name,
      id=id_utils.id_from(manifest.id.package, manifest.id.name),
      package_name=manifest.id.package,
      description=manifest.documentation.description,
      id_version=id_utils.id_version_from(
          manifest.id.package, manifest.id.name, version
      ),
  )

  for key, val in manifest.dependencies.required_equipment.items():
    skill_proto.resource_selectors[key].CopyFrom(val)

  add_param_file_descriptor_set_without_source_code_from_manifest(
      manifest, file_descriptor_set, skill_proto
  )
  if manifest.HasField('return_type'):
    add_return_file_descriptor_set_without_source_code_from_manifest(
        manifest, file_descriptor_set, skill_proto
    )

  skill_proto.execution_options.supports_cancellation = (
      manifest.options.supports_cancellation
  )

  if manifest.HasField('parameter'):
    if manifest.parameter.HasField('default_value'):
      skill_proto.parameter_description.default_value.CopyFrom(
          manifest.parameter.default_value
      )

  return skill_proto


# TODO(b/317238679): See if we can combine this function with
# add_param_file_descriptor_set_without_source_code_from_manifest below.
def add_return_file_descriptor_set_without_source_code_from_manifest(
    manifest: skill_manifest_pb2.SkillManifest,
    return_file_descriptor_set: descriptor_pb2.FileDescriptorSet,
    skill_proto: skills_pb2.Skill,
):
  """Adds (or overwrites) the skill's return_type descriptor fileset.

  This also populates the return field comments. We remove source_code_info
  as it is no longer needed after the return value field comments are populated.

  Args:
    manifest: A skill manifest
    return_file_descriptor_set: A file descriptor set for the skill's
      return_type.
    skill_proto: A skill proto to which this function will add file descriptors.
  """
  return_description = skill_proto.return_value_description
  return_description.descriptor_fileset.CopyFrom(return_file_descriptor_set)
  return_description.return_value_message_full_name = (
      manifest.return_type.message_full_name
  )
  sci_view = source_code_info_view_py.SourceCodeInfoView()
  sci_view.Init(return_description.descriptor_fileset)
  return_description.return_value_field_comments.update(
      sci_view.GetNestedFieldCommentMap(
          return_description.return_value_message_full_name
      )
  )

  # TODO(b/317155401): if json_name field isn't needed, then it should be
  # cleared here.
  for file in return_description.descriptor_fileset.file:
    file.ClearField('source_code_info')


# TODO(b/317238679): See if we can combine this function with
# add_return_file_descriptor_set_without_source_code_from_manifest above.
def add_param_file_descriptor_set_without_source_code_from_manifest(
    manifest: skill_manifest_pb2.SkillManifest,
    parameter_file_descriptor_set: descriptor_pb2.FileDescriptorSet,
    skill_proto: skills_pb2.Skill,
):
  """Adds (or overwrites) the skill's parameter descriptor fileset.

  This also populates the parameter field comments. We remove source_code_info
  as it is no longer needed after the parameter field comments are populated.

  Args:
    manifest: A skill manifest
    parameter_file_descriptor_set: A file descriptor set for the skill's
      parameters.
    skill_proto: A skill proto to which this function will add file descriptors.
  """
  parameter_description = skill_proto.parameter_description
  parameter_description.parameter_descriptor_fileset.CopyFrom(
      parameter_file_descriptor_set
  )
  parameter_description.parameter_message_full_name = (
      manifest.parameter.message_full_name
  )
  sci_view = source_code_info_view_py.SourceCodeInfoView()
  sci_view.Init(parameter_description.parameter_descriptor_fileset)
  parameter_description.parameter_field_comments.update(
      sci_view.GetNestedFieldCommentMap(
          parameter_description.parameter_message_full_name
      )
  )
  # TODO(b/317155401): if json_name field isn't needed, then it should be
  # cleared here.
  for file in parameter_description.parameter_descriptor_fileset.file:
    file.ClearField('source_code_info')
