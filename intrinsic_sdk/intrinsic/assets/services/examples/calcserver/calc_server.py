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

"""Implementation of a basic calculator service."""

from __future__ import annotations

from typing import Any

from absl import logging
import grpc

from intrinsic.assets.dependencies import utils
from intrinsic.assets.services.examples.calcserver import calc_server_pb2
from intrinsic.assets.services.examples.calcserver import calc_server_pb2_grpc

_CONVERSION_DATASET_INTERFACE = (
    'data://intrinsic_proto.services.ConversionDataset'
)


class CalculatorServiceServicer(calc_server_pb2_grpc.CalculatorServicer):
  """Performs basic calculator operations."""

  def __init__(self, config: calc_server_pb2.CalculatorConfig):
    """Initializes the object.

    Args:
      config: The calculator configuration.
    """
    self._config = config

  def _get_conversion_dataset(
      self,
      context: grpc.ServicerContext,
  ) -> calc_server_pb2.ConversionDataset:
    """Fetches the conversion dataset from the config."""
    if not self._config.HasField('conversion_dataset'):
      context.abort(
          grpc.StatusCode.FAILED_PRECONDITION,
          'Conversion dataset dependency not configured',
      )

    dataset = calc_server_pb2.ConversionDataset()
    try:
      any_payload = utils.get_data_payload(
          self._config.conversion_dataset, _CONVERSION_DATASET_INTERFACE
      )
      any_payload.Unpack(dataset)
    except grpc.RpcError as e:
      context.abort(e.code(), e.details())
    except Exception as e:
      context.abort(grpc.StatusCode.INTERNAL, f'failed to get dataset: {e}')

    return dataset

  def _get_linear_params(
      self,
      unit: calc_server_pb2.Unit | None,
      unit_name: str,
      context: grpc.ServicerContext,
  ) -> tuple[float, float]:
    """Retrieves the linear conversion parameters with respect to the base unit."""
    if unit:
      if not unit.HasField('linear'):
        context.abort(
            grpc.StatusCode.UNIMPLEMENTED,
            f'unsupported conversion type for unit: {unit_name}',
        )
      return unit.linear.factor, unit.linear.offset

    context.abort(grpc.StatusCode.NOT_FOUND, f'unit not found: {unit_name}')

  def Calculate(
      self,
      request: calc_server_pb2.CalculatorRequest,
      context: grpc.ServicerContext,
  ) -> calc_server_pb2.CalculatorResponse:
    result = 0

    if self._config.reverse_order:
      a = request.y
      b = request.x
    else:
      a = request.x
      b = request.y

    if request.operation == calc_server_pb2.CALCULATOR_OPERATION_ADD:
      result = a + b
      logging.info('%d + %d = %d', a, b, result)
    elif request.operation == calc_server_pb2.CALCULATOR_OPERATION_MULTIPLY:
      result = a * b
      logging.info('%d * %d = %d', a, b, result)
    elif request.operation == calc_server_pb2.CALCULATOR_OPERATION_SUBTRACT:
      result = a - b
      logging.info('%d - %d = %d', a, b, result)
    elif request.operation == calc_server_pb2.CALCULATOR_OPERATION_DIVIDE:
      if b == 0:
        logging.info('Cannot divide by 0 (%d / %d)', a, b)
        context.abort(grpc.StatusCode.INVALID_ARGUMENT, 'Cannot divide by 0')
      result = a // b
      logging.info('%d / %d = %d', a, b, result)
    else:
      context.abort(
          grpc.StatusCode.UNIMPLEMENTED,
          f'Unsupported operation: {request.operation}',
      )

    return calc_server_pb2.CalculatorResponse(result=result)

  def Convert(
      self,
      request: calc_server_pb2.ConvertRequest,
      context: grpc.ServicerContext,
  ) -> calc_server_pb2.ConvertResponse:
    # Validation
    if not request.category:
      context.abort(grpc.StatusCode.INVALID_ARGUMENT, 'category not specified')
    if not request.from_unit:
      context.abort(
          grpc.StatusCode.INVALID_ARGUMENT,
          'from unit not specified',
      )
    if not request.to_unit:
      context.abort(grpc.StatusCode.INVALID_ARGUMENT, 'to unit not specified')

    dataset = self._get_conversion_dataset(context)

    target_category = None
    for category in dataset.categories:
      if category.name == request.category:
        target_category = category
        break

    if not target_category:
      context.abort(
          grpc.StatusCode.INVALID_ARGUMENT,
          f'category not found: {request.category}',
      )

    # Check that the base unit is not redefined in the units list.
    for unit in target_category.units:
      if unit.name == target_category.base_unit:
        context.abort(
            grpc.StatusCode.FAILED_PRECONDITION,
            f'base unit {unit.name} redefined in units list',
        )

    # Add the base unit manually to the units list to simplify lookup.
    base_unit_proto = calc_server_pb2.Unit(
        name=target_category.base_unit,
        linear=calc_server_pb2.LinearConversion(factor=1.0, offset=0.0),
    )
    target_category.units.append(base_unit_proto)

    from_unit = None
    to_unit = None
    for unit in target_category.units:
      if unit.name == request.from_unit:
        from_unit = unit
      if unit.name == request.to_unit:
        to_unit = unit

    from_factor, from_offset = self._get_linear_params(
        from_unit, request.from_unit, context
    )
    to_factor, to_offset = self._get_linear_params(
        to_unit, request.to_unit, context
    )

    if self._config.reverse_order:
      from_factor, to_factor = to_factor, from_factor
      from_offset, to_offset = to_offset, from_offset

    if to_factor == 0:
      context.abort(
          grpc.StatusCode.FAILED_PRECONDITION,
          f'invalid factor 0 for unit: {request.to_unit}',
      )

    value = request.value
    base_val = (value * from_factor) + from_offset
    result = (base_val - to_offset) / to_factor

    logging.info(
        '%f %s = %f %s', value, request.from_unit, result, request.to_unit
    )
    return calc_server_pb2.ConvertResponse(result=result)
