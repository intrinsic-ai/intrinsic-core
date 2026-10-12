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

"""
Binary for the inference service.

This binary is intended to run as a intrinsic_service that creates a local
Triton inference server instance and exposes a open inference protocol endpoint
to other services.
"""

from concurrent import futures
import os
from typing import Sequence

from absl import app
from absl import logging
import grpc
from tritonclient.grpc import service_pb2_grpc as triton_pb2_grpc

from intrinsic_inference.assets.inference_service import factory_imports  # pylint: disable=unused-import
from intrinsic_inference.assets.inference_service import inference_service
from intrinsic_inference.assets.inference_service.v1 import inference_service_config_pb2
from intrinsic_inference.core import inference_runner as inference_runner_lib
from intrinsic_inference.core import model_assets_managers_factory
from intrinsic_inference.core import model_controller_triton
from intrinsic_inference.core import telemetry_otel
from intrinsic.assets.data.proto.v1 import data_assets_pb2_grpc
from intrinsic.assets.dependencies import utils
from intrinsic.assets.install import installed_assets_client
from intrinsic.assets.services.proto.v1 import service_state_pb2_grpc as state_grpc
from intrinsic.resources.proto import runtime_context_pb2
from intrinsic.storage.content_addressable_storage.proto import cas_service_pb2_grpc

_RUNTIME_CONFIG_PATH = "/etc/intrinsic/runtime_config.pb"
_TRITON_REPO_PATH = "/models"
_INFERENCE_SERVER_URL = "unix:///dev/shm/triton.sock:0"
_CAS_ADDRESS = (
    "content-addressable-storage.app-intrinsic-base.svc.cluster.local:9747"
)
_TRACER_NAME = "inference_service"
_ML_TRACER_NAME = "ai.intrinsic.ml"
_OTEL_COLLECTOR_ENDPOINT = "oc-agent.app-intrinsic-base.svc.cluster.local:4317"
_MODEL_POLL_INTERVAL = 5.0  # 5 second interval for polling InstalledAssets.
_PLATFORM_CHANNELS_READY_TIMEOUT = 10.0  # 10 seconds.
_TRITON_CHANNEL_READY_TIMEOUT = 1200  # 20 minutes.
_TRITON_MAX_RECONNECT_BACKOFF_MS = 2000  # 2 seconds.


def _get_runtime_context() -> runtime_context_pb2.RuntimeContext:
  with open(_RUNTIME_CONFIG_PATH, "rb") as f:
    return runtime_context_pb2.RuntimeContext.FromString(f.read())


def main(argv: Sequence[str]) -> None:
  if len(argv) > 1:
    raise app.UsageError("Too many command-line arguments.")

  # Initialize telemetry.
  telemetry_otel.setup_tracing(
      service_name=_TRACER_NAME,
      endpoint=_OTEL_COLLECTOR_ENDPOINT,
  )
  # Instrumentation for custom spans.
  telemetry_otel.initialize_telemetry(tracer_name=_ML_TRACER_NAME)

  logging.info("Initializing inference service...")

  if not os.path.exists(_TRITON_REPO_PATH):
    logging.info(
        "Triton repository folder doesn't exist, creating empty folder..."
    )
    os.makedirs(_TRITON_REPO_PATH, exist_ok=True)

  context = _get_runtime_context()
  logging.info("Runtime context: \n%s", context)

  service_config = inference_service_config_pb2.InferenceServiceConfig()
  if context.config.type_url:
    context.config.Unpack(service_config)

  installed_assets_channel = (
      utils.connect_with_runtime_asset_fallback_for_asset_migration_only(
          service_config.intrinsic_runtime,
          "grpc://intrinsic_proto.assets.v1.InstalledAssetsReader",
          grpc_options=inference_service.GRPC_OPTIONS,
      )
  )
  installed_assets_client_obj = (
      installed_assets_client.InstalledAssetsClient.from_channel(
          installed_assets_channel
      )
  )

  server = grpc.server(
      futures.ThreadPoolExecutor(max_workers=10),
      options=inference_service.GRPC_OPTIONS,
  )

  triton_channel = grpc.insecure_channel(
      _INFERENCE_SERVER_URL,
      options=inference_service.GRPC_OPTIONS
      + [
          ("grpc.max_reconnect_backoff_ms", _TRITON_MAX_RECONNECT_BACKOFF_MS),
      ],
  )
  data_assets_channel = (
      utils.connect_with_runtime_asset_fallback_for_asset_migration_only(
          service_config.intrinsic_runtime,
          "grpc://intrinsic_proto.data.v1.DataAssets",
          grpc_options=inference_service.GRPC_OPTIONS,
      )
  )
  cas_channel = grpc.insecure_channel(
      _CAS_ADDRESS,
      options=inference_service.GRPC_OPTIONS,
  )

  triton_stub = triton_pb2_grpc.GRPCInferenceServiceStub(triton_channel)
  data_assets_stub = data_assets_pb2_grpc.DataAssetsStub(data_assets_channel)
  cas_stub = cas_service_pb2_grpc.ContentAddressableStorageServiceStub(
      cas_channel
  )

  model_assets_manager = model_assets_managers_factory.create(
      "data_assets",
      repo_path=_TRITON_REPO_PATH,
      cas_stub=cas_stub,
      data_assets_stub=data_assets_stub,
      installed_assets_client=installed_assets_client_obj,
  )

  model_controller = model_controller_triton.ModelControllerTriton(
      repo_path=_TRITON_REPO_PATH,
      model_assets_manager=model_assets_manager,
      triton_stub=triton_stub,
  )

  inference_runner = inference_runner_lib.InferenceRunner(
      repo_path=_TRITON_REPO_PATH,
      triton_stub=triton_stub,
      model_controller=model_controller,
      poll_models_interval=_MODEL_POLL_INTERVAL,
      use_shm=True,
  )

  inference_servicer = inference_service.InferenceServiceServicer(
      inference_runner=inference_runner,
  )

  triton_pb2_grpc.add_GRPCInferenceServiceServicer_to_server(
      inference_servicer, server
  )
  state_grpc.add_ServiceStateServicer_to_server(inference_servicer, server)

  # Set port from runtime context.
  port = context.port
  added_port = server.add_insecure_port(f"[::]:{port}")
  if added_port != port:
    raise RuntimeError(f"Failed to use port {port}")

  # Start gRPC server early so the service can immediately respond to Status
  # checks and OIP queries while initializing.
  server.start()
  logging.info("gRPC server started, listening on port %d", port)

  try:
    # Wait for channels to be ready.
    logging.info("Waiting for dependency channels to be ready...")
    grpc.channel_ready_future(installed_assets_channel).result(
        timeout=_PLATFORM_CHANNELS_READY_TIMEOUT
    )
    logging.info("InstalledAssets channel ready!")
    grpc.channel_ready_future(data_assets_channel).result(
        timeout=_PLATFORM_CHANNELS_READY_TIMEOUT
    )
    logging.info("DataAssets channel ready!")
    grpc.channel_ready_future(cas_channel).result(
        timeout=_PLATFORM_CHANNELS_READY_TIMEOUT
    )
    logging.info("CAS channel ready!")
    grpc.channel_ready_future(triton_channel).result(
        timeout=_TRITON_CHANNEL_READY_TIMEOUT
    )
    logging.info("Inference channel ready!")
    inference_servicer.start()
    logging.info("Inference service successfully initialized and enabled.")
  except Exception:  # pylint: disable=broad-except
    logging.exception("Initialization failed; server running in ERROR state.")

  # Wait for termination and cleanup.
  server.wait_for_termination()
  server.stop(grace=5)
  inference_servicer.stop()


if __name__ == "__main__":
  app.run(main)
