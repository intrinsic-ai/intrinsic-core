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

"""Main entrypoint for the Nvidia Pose Estimator Service."""

from collections.abc import Sequence
from concurrent import futures
import logging
import pathlib

from absl import app
from absl import flags
from google.protobuf import text_format
import grpc

from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.proto import nvidia_service_config_pb2
from intrinsic.perception.proto.v1 import pose_estimation_service_pb2_grpc as v1_pose_estimation_service_pb2_grpc
from intrinsic.resources.proto import runtime_context_pb2

logging.basicConfig(
    level=logging.INFO,
    format=(
        "[%(asctime)s] <%(filename)s:%(lineno)d> %(levelname)s]-\t %(message)s"
    ),
    force=True,
)

# isort: split
# pylint: disable=g-import-not-at-top,wrong-import-position
from intrinsic_perception.intrinsic.perception.service.nvidia_pose_estimator.service import nvidia_pose_estimator_service  # noqa: E402

# pylint: enable=g-import-not-at-top,wrong-import-position

_DEFAULT_PORT = 50051

_RUNTIME_CONTEXT_FILE = flags.DEFINE_string(
    "runtime_context_file",
    "/etc/intrinsic/runtime_config.pb",
    (
        "Path to the runtime context file containing"
        " intrinsic_proto.config.RuntimeContext binary proto."
    ),
)

_GRPC_OPTIONS = [
    ("grpc.max_receive_message_length", -1),
    ("grpc.max_send_message_length", -1),
    ("grpc.max_message_length", -1),
]


def main(argv: Sequence[str]) -> None:
  if len(argv) > 1:
    raise app.UsageError("Too many command-line arguments.")
  logging.info("\n")

  runtime_context = None
  config = None
  service_config = nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig()

  if pathlib.Path(_RUNTIME_CONTEXT_FILE.value).exists():
    with open(_RUNTIME_CONTEXT_FILE.value, "rb") as f:
      runtime_context = runtime_context_pb2.RuntimeContext.FromString(f.read())
      logging.info(
          "Runtime context: \n%s",
          text_format.MessageToString(runtime_context),
      )

      if runtime_context.HasField("config"):
        if runtime_context.config.Is(
            nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig.DESCRIPTOR
        ):
          config = nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig()
          runtime_context.config.Unpack(config)
          service_config = config

      if config is not None:
        logging.info(
            "Service config (from runtime context): %s",
            text_format.MessageToString(config, print_unknown_fields=True),
        )
      else:
        logging.warning(
            "Runtime context does not contain a valid config. Using default."
        )

  logging.info("--- Starting NVIDIA Pose Estimator Server ---\n")

  server = grpc.server(
      futures.ThreadPoolExecutor(max_workers=10), options=_GRPC_OPTIONS
  )

  servicer = nvidia_pose_estimator_service.NvidiaPoseEstimatorService(
      config=service_config
  )
  logging.info(
      "Warming up models (FoundationPose -> FoundationStereo -> SAM3)..."
  )
  try:
    servicer.warmup()
  except Exception as e:  # pylint: disable=broad-except
    logging.warning("Service warmup encountered an issue: %s", e)

  v1_pose_estimation_service_pb2_grpc.add_PoseEstimationServiceServicer_to_server(
      servicer,
      server,
  )

  port = (
      runtime_context.port
      if runtime_context is not None and runtime_context.port > 0
      else _DEFAULT_PORT
  )
  endpoint = f"[::]:{port}"
  if server.add_insecure_port(endpoint) != port:
    raise RuntimeError(f"Failed to use port {port}")

  server.start()
  logging.info("-----------------------------------")
  logging.info("-- Server listening at port : %s ...", endpoint)
  logging.info("-----------------------------------")

  server.wait_for_termination()


if __name__ == "__main__":
  app.run(main)
