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

"""Classes used by the skill service at run time.

This file contains data types that are used by the skill service at runtime
to provide our internal framework access to metadata about skills. Classes
defined here should not be used in user-facing contexts.
"""

from collections.abc import Mapping
from collections.abc import Sequence
import dataclasses
import datetime

from google.protobuf import any_pb2
from google.protobuf import descriptor as proto_descriptor

from intrinsic.assets.proto import status_spec_pb2
from intrinsic.skills.proto import equipment_pb2
from intrinsic.skills.proto import skill_service_config_pb2
from intrinsic.skills.proto import skills_pb2


@dataclasses.dataclass(frozen=True)
class ParameterData:
  """Parameter data that are required by the skill service at runtime.

  Attributes:
   descriptor: The parameter descriptor.
   default_value: The default value of the parameter (optional).
  """

  descriptor: proto_descriptor.Descriptor
  default_value: any_pb2.Any | None = None


@dataclasses.dataclass(frozen=True)
class ReturnTypeData:
  """Return type data that are required by the skill service at runtime.

  Attributes:
    message_full_name: The return type proto's full message name (optional).
  """

  message_full_name: str | None = None


@dataclasses.dataclass(frozen=True)
class ExecutionOptions:
  """Execution options for a skill that are relevant to the skill services.

  Attributes:
    cancellation_ready_timeout: The amount of time the skill has to prepare for
      cancellation.
    execution_timeout: The execution timeout for the skill.
    supports_cancellation: True, if the skill supports cancellation.
  """

  cancellation_ready_timeout: datetime.timedelta = datetime.timedelta(
      seconds=30
  )
  # datetime.timedelta.max does not work through gRPC calls.
  # 100 years is practically infinite.
  execution_timeout: datetime.timedelta = datetime.timedelta(days=365 * 100)
  supports_cancellation: bool = False


@dataclasses.dataclass(frozen=True)
class ResourceData:
  """Data about resources for a skill relevant to the skill service.

  Attributes:
    required_resources: Mapping of resources to run the skill.
  """

  required_resources: Mapping[str, equipment_pb2.ResourceSelector]


@dataclasses.dataclass(frozen=True)
class StatusSpecs:
  """Status specifications the skill has declared.

  Attributes:
    specs: Mapping from code to status specification.
  """

  specs: Mapping[int, status_spec_pb2.StatusSpec]


@dataclasses.dataclass(frozen=True)
class SkillRuntimeData:
  """Data about skills that are relevant to the skills services.

  Attributes:
    parameter_data: The parameter data.
    return_type_data: The return data.
    execution_options: The execution options.
    resource_data: The resource data.
    status_specs: Expected status code definitions from manifest.
    skill_id: The skill id.
  """

  parameter_data: ParameterData
  return_type_data: ReturnTypeData
  execution_options: ExecutionOptions
  resource_data: ResourceData
  status_specs: StatusSpecs
  skill_id: str


def get_runtime_data_from(
    skill_service_config: skill_service_config_pb2.SkillServiceConfig,
    parameter_descriptor: proto_descriptor.Descriptor,
) -> SkillRuntimeData:
  # pyformat: disable
  """Constructs RuntimeData from the given skill service config & descriptors.

  This applies a default `cancellation_ready_timeout` of 30 seconds to the
  execution options if no timeout is specified, in order to match the behavior
  of the skill signature.

  Args:
    skill_service_config: The skill service config.
    parameter_descriptor: The parameter descriptor.

  Returns:
    Constructed SkillRuntimeData from given args.
  """
  # pyformat: enable
  execution_service_options_kwargs = {}
  if skill_service_config.execution_service_options.HasField(
      'cancellation_ready_timeout'
  ):
    duration_proto = (
        skill_service_config.execution_service_options.cancellation_ready_timeout
    )
    execution_service_options_kwargs['cancellation_ready_timeout'] = (
        datetime.timedelta(
            seconds=duration_proto.seconds,
            milliseconds=(duration_proto.nanos / 1e6),
        )
    )
  if skill_service_config.execution_service_options.HasField(
      'execution_timeout'
  ):
    duration_proto = (
        skill_service_config.execution_service_options.execution_timeout
    )
    execution_service_options_kwargs['execution_timeout'] = datetime.timedelta(
        seconds=duration_proto.seconds,
        milliseconds=(duration_proto.nanos / 1e6),
    )

  execute_opts = ExecutionOptions(
      supports_cancellation=skill_service_config.skill_description.execution_options.supports_cancellation,
      **execution_service_options_kwargs,
  )

  resource_data = dict(
      skill_service_config.skill_description.resource_selectors
  )

  status_specs = {s.code: s for s in skill_service_config.status_info}

  if skill_service_config.skill_description.parameter_description.HasField(
      'default_value'
  ):
    default_value = (
        skill_service_config.skill_description.parameter_description.default_value
    )
  else:
    default_value = None

  if skill_service_config.skill_description.HasField(
      'return_value_description'
  ):
    return_type_data = ReturnTypeData(
        message_full_name=skill_service_config.skill_description.return_value_description.return_value_message_full_name
    )
  else:
    return_type_data = ReturnTypeData()

  return SkillRuntimeData(
      parameter_data=ParameterData(
          descriptor=parameter_descriptor,
          default_value=default_value,
      ),
      return_type_data=return_type_data,
      execution_options=execute_opts,
      resource_data=ResourceData(resource_data),
      status_specs=StatusSpecs(status_specs),
      skill_id=skill_service_config.skill_description.id,
  )
