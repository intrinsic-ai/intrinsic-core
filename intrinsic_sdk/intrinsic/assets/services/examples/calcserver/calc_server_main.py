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

"""Basic calculator service."""

from collections.abc import Sequence
from concurrent import futures

from absl import app
from absl import flags
from absl import logging
import grpc

from intrinsic.assets.services.examples.calcserver import calc_server
from intrinsic.assets.services.examples.calcserver import calc_server_pb2
from intrinsic.assets.services.examples.calcserver import calc_server_pb2_grpc
from intrinsic.resources.proto import runtime_context_pb2

_RUNTIME_CONTEXT_FILE = flags.DEFINE_string(
    'runtime_context_file',
    '/etc/intrinsic/runtime_config.pb',
    (
        'Path to the runtime context file containing'
        ' intrinsic_proto.config.RuntimeContext binary proto.'
    ),
)


def main(argv: Sequence[str]) -> None:
  del argv  # unused

  with open(_RUNTIME_CONTEXT_FILE.value, 'rb') as f:
    runtime_context = runtime_context_pb2.RuntimeContext.FromString(f.read())
    logging.info('Runtime context level: %d', runtime_context.level)

  config = calc_server_pb2.CalculatorConfig()
  if not runtime_context.config.Unpack(config):
    raise RuntimeError('Failed to unpack config.')

  server = grpc.server(
      futures.ThreadPoolExecutor(max_workers=4),
      options=(('grpc.so_reuseport', 0),),
  )

  calc_server_pb2_grpc.add_CalculatorServicer_to_server(
      calc_server.CalculatorServiceServicer(config), server
  )

  # Initialize server with insecure port.
  endpoint = f'[::]:{runtime_context.port}'
  added_port = server.add_insecure_port(endpoint)
  if added_port != runtime_context.port:
    raise RuntimeError(f'Failed to use port {runtime_context.port}')
  server.start()

  logging.info('--------------------------------')
  logging.info('-- Calculator service listening on %s', endpoint)
  logging.info('--------------------------------')

  server.wait_for_termination()


if __name__ == '__main__':
  app.run(main)
