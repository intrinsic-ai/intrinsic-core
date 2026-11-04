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

"""Handles model loading and unloading for the Triton inference backend."""

import enum
import os

from absl import logging
from google.protobuf import text_format
import grpc
from tritonclient.grpc import model_config_pb2 as triton_model_pb2
from tritonclient.grpc import service_pb2 as triton_pb2
from tritonclient.grpc import service_pb2_grpc as triton_pb2_grpc

from intrinsic_inference.core import model_assets_manager_base
from intrinsic_inference.core import model_controller_base
from intrinsic_inference.core.v1 import ml_model_pb2


@enum.unique
class TritonAction(enum.Enum):
  LOAD = "load"
  UNLOAD = "unload"


class ModelControllerTriton(model_controller_base.ModelControllerBase):
  """Controls models for the Triton inference backend.

  Attributes:
    models: A mapping of model names to their loaded MlModel proto instances.
  """

  def __init__(
      self,
      repo_path: str,
      model_assets_manager: model_assets_manager_base.ModelAssetsManagerBase,
      triton_stub: triton_pb2_grpc.GRPCInferenceServiceStub,
  ) -> None:
    super().__init__(model_assets_manager=model_assets_manager)
    self._repo_path = repo_path
    self._triton_stub = triton_stub

  def _run_triton_action(
      self, model_proto: ml_model_pb2.MlModel, action: TritonAction
  ) -> None:
    """Helper to load or unload a model in Triton."""
    model_name = model_proto.model_config.name
    if action == TritonAction.LOAD:
      self._write_triton_config(model_proto)
      request = triton_pb2.RepositoryModelLoadRequest(model_name=model_name)
      action_fn = self._triton_stub.RepositoryModelLoad
    elif action == TritonAction.UNLOAD:
      request = triton_pb2.RepositoryModelUnloadRequest(model_name=model_name)
      action_fn = self._triton_stub.RepositoryModelUnload
    else:
      raise ValueError("Unknown triton action requested.")

    try:
      action_fn(request)
    except grpc.RpcError as e:
      logging.error(
          "Error %sing model '%s': %s", action.value, model_name, e.details()
      )
      raise
    logging.info("Successfully %sed model '%s'", action.value, model_name)

  def _load_model_impl(self, model_proto: ml_model_pb2.MlModel) -> None:
    """Loads a model into Triton, or reloads it if it is loaded already.

    Triton keeps serving a loaded model until its reload finished.

    Args:
      model_proto: The model to load.
    """
    self._run_triton_action(model_proto, action=TritonAction.LOAD)

  def _unload_model_impl(self, model_proto: ml_model_pb2.MlModel) -> None:
    """Unloads all versions of a model from Triton, if it is loaded."""
    model_name = model_proto.model_config.name
    try:
      self._run_triton_action(model_proto, action=TritonAction.UNLOAD)
    except grpc.RpcError:
      # Ask Triton whether the model is still loaded. It isn't e.g. after a
      # failed load or a Triton restart.
      ready = self._triton_stub.ModelReady(
          triton_pb2.ModelReadyRequest(name=model_name)
      ).ready
      if ready:
        raise
      logging.info("Model '%s' isn't loaded, nothing to unload", model_name)

  def _write_triton_config(self, model_proto: ml_model_pb2.MlModel) -> None:
    """Writes the backend_config of a model as its Triton config.

    Models without a backend_config, e.g. from a local repo, need to have a
    config.pbtxt on disk already.

    Args:
      model_proto: The model whose config to write.
    """
    model_name = model_proto.model_config.name
    config_path = os.path.join(self._repo_path, model_name, "config.pbtxt")
    if not model_proto.HasField("backend_config"):
      if not os.path.exists(config_path):
        raise RuntimeError(
            f"Model '{model_name}' has no backend_config and no config.pbtxt"
            f" at {config_path}."
        )
      logging.info("Using Triton config of model '%s' on disk", model_name)
      return

    triton_config = triton_model_pb2.ModelConfig()
    # Never falls back to the config on disk, which might belong to an older
    # version of the model.
    if not model_proto.backend_config.Unpack(triton_config):
      raise RuntimeError(
          f"The backend_config of model '{model_name}' is a"
          f" {model_proto.backend_config.type_url}, but Triton requires"
          f" {triton_config.DESCRIPTOR.full_name}."
      )
    logging.info("Writing Triton config for model '%s'...", model_name)
    os.makedirs(os.path.dirname(config_path), exist_ok=True)
    with open(config_path, "w", encoding="utf-8") as f:
      f.write(text_format.MessageToString(triton_config))
