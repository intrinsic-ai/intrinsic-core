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

"""Entrypoint binary for the IOC Train Service gRPC server."""

from concurrent import futures
import logging
import pathlib
from typing import Sequence

from absl import app
from absl import flags
import grpc

from intrinsic_perception.intrinsic.perception.service.ioc_train_service.proto import ioc_train_service_config_pb2
from intrinsic_perception.intrinsic.perception.service.ioc_train_service.service import ioc_train_service
from intrinsic.perception.proto.v1 import train_service_pb2_grpc
from intrinsic.resources.proto import runtime_context_pb2

logging.basicConfig(
    level=logging.INFO,
    format=(
        "[%(asctime)s] <%(filename)s:%(lineno)d> %(levelname)s]-\t %(message)s"
    ),
)

_DEFAULT_PORT = 50052

_RUNTIME_CONTEXT_FILE = flags.DEFINE_string(
    "runtime_context_file",
    "/etc/intrinsic/runtime_config.pb",
    "Path to the runtime context file.",
)

_GRPC_OPTIONS = [
    ("grpc.max_receive_message_length", -1),
    ("grpc.max_send_message_length", -1),
    ("grpc.max_message_length", -1),
]


def main(argv: Sequence[str]) -> None:
  """Runs the IOC Train Service gRPC server.

  Args:
    argv: Command line arguments.

  Raises:
    app.UsageError: If unexpected command line arguments are provided.
    RuntimeError: If binding to the specified runtime context port fails.
  """
  if len(argv) > 1:
    raise app.UsageError("Too many command-line arguments.")

  runtime_context: runtime_context_pb2.RuntimeContext | None = None
  ioc_train_service_config = (
      ioc_train_service_config_pb2.IocTrainServiceConfig()
  )
  if pathlib.Path(_RUNTIME_CONTEXT_FILE.value).exists():
    with open(_RUNTIME_CONTEXT_FILE.value, "rb") as f:
      runtime_context = runtime_context_pb2.RuntimeContext.FromString(f.read())
      if runtime_context.HasField("config") and runtime_context.config.Is(
          ioc_train_service_config_pb2.IocTrainServiceConfig.DESCRIPTOR
      ):
        runtime_context.config.Unpack(ioc_train_service_config)

  logging.info("--- Starting Custom Sideloaded IOC TrainService ---")

  server = grpc.server(
      futures.ThreadPoolExecutor(max_workers=10), options=_GRPC_OPTIONS
  )
  train_service_pb2_grpc.add_TrainServiceServicer_to_server(
      ioc_train_service.IocTrainService(config=ioc_train_service_config), server
  )

  if runtime_context is not None and runtime_context.port > 0:
    endpoint = f"[::]:{runtime_context.port}"
    added_port = server.add_insecure_port(endpoint)
    if added_port != runtime_context.port:
      raise RuntimeError(f"Failed to bind port {runtime_context.port}")
  else:
    endpoint = f"[::]:{_DEFAULT_PORT}"
    server.add_insecure_port(endpoint)

  server.start()
  logging.info("-- IOC TrainService listening on %s --", endpoint)
  server.wait_for_termination()


if __name__ == "__main__":
  app.run(main)
