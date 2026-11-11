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

"""IOC Pose Estimator Service."""

import logging

from google.longrunning import operations_pb2
from google.longrunning import operations_pb2_grpc
from google.protobuf import text_format
from google.protobuf.empty_pb2 import Empty
import grpc
from specification.protocol import open_inference_grpc_pb2_grpc

from intrinsic_perception.intrinsic.perception.service.ioc_pose_estimator.proto import ioc_service_config_pb2
from intrinsic.assets import interface_utils
from intrinsic.assets.dependencies import utils as asset_utils
from intrinsic.assets.install import installed_assets_client
from intrinsic.assets.proto import installed_assets_pb2
from intrinsic.assets.proto import installed_assets_pb2_grpc
from intrinsic.assets.proto.v1 import resolved_dependency_pb2
from intrinsic.perception.proto.v1 import pose_estimation_service_pb2 as v1_pose_estimation_service_pb2
from intrinsic.perception.proto.v1 import pose_estimation_service_pb2_grpc as v1_pose_estimation_service_pb2_grpc
from intrinsic.storage.content_addressable_storage.proto import cas_service_pb2_grpc
from intrinsic.util.grpc import interceptor

CAS_ADDRESS = (
    "content-addressable-storage.app-intrinsic-base.svc.cluster.local:9747"
)
_GRPC_OPTIONS = [
    ("grpc.max_receive_message_length", -1),
    ("grpc.max_send_message_length", -1),
    ("grpc.max_message_length", -1),
]


def _installed_assets_interface_uri() -> str:
  return (
      f"{interface_utils.GRPC_URI_PREFIX}"
      f"{installed_assets_pb2.DESCRIPTOR.services_by_name['InstalledAssets'].full_name}"
  )


def _operations_interface_uri() -> str:
  return (
      f"{interface_utils.GRPC_URI_PREFIX}"
      f"{operations_pb2.DESCRIPTOR.services_by_name['Operations'].full_name}"
  )


def _connect_to_inference_service(
    inference_service_dependency: resolved_dependency_pb2.ResolvedDependency,
) -> open_inference_grpc_pb2_grpc.GRPCInferenceServiceStub:
  """Connects to the ML inference service and stores stub for reuse."""
  logging.info("Connecting to ML inference service...")
  grpc_info = next(iter(inference_service_dependency.interfaces.values())).grpc
  inference_channel = grpc.intercept_channel(
      grpc.insecure_channel(
          grpc_info.connection.address, options=_GRPC_OPTIONS
      ),
      interceptor.HeaderAdderInterceptor(
          lambda: [(m.key, m.value) for m in grpc_info.connection.metadata]
      ),
  )
  inference_stub = open_inference_grpc_pb2_grpc.GRPCInferenceServiceStub(
      inference_channel
  )
  logging.info(
      "Connected to ML inference service at %s",
      grpc_info.connection.address,
  )
  return inference_stub


class IocPoseEstimatorService(
    v1_pose_estimation_service_pb2_grpc.PoseEstimationServiceServicer,
):
  """Implementation of the IOC Pose Estimator Service."""

  def __init__(
      self,
      config: ioc_service_config_pb2.IocPoseEstimatorServiceConfig,
  ):
    """Initializes the IocPoseEstimatorService.

    Args:
        config: Initial IocPoseEstimatorServiceConfig.
    """
    self._config = config

    installed_assets_channel, operations_channel = (
        asset_utils.connect_many_with_runtime_asset_fallback_for_asset_migration_only(
            self._config.intrinsic_runtime,
            [
                _installed_assets_interface_uri(),
                _operations_interface_uri(),
            ],
            grpc_options=_GRPC_OPTIONS,
        )
    )
    self._assets_client = installed_assets_client.InstalledAssetsClient(
        installed_assets_pb2_grpc.InstalledAssetsStub(installed_assets_channel),
        operations_pb2_grpc.OperationsStub(operations_channel),
    )
    cas_channel = grpc.insecure_channel(CAS_ADDRESS, options=_GRPC_OPTIONS)
    self._cas_stub = cas_service_pb2_grpc.ContentAddressableStorageServiceStub(
        cas_channel
    )

    if not self._config.HasField("inference_service"):
      raise ValueError(
          "inference_service dependency must be specified in the service"
          " config."
      )
    logging.info("connecting to inference service")
    self._ml_service_stub = _connect_to_inference_service(
        self._config.inference_service
    )

    logging.info(
        "IocPoseEstimatorService initialized with config: %s",
        text_format.MessageToString(self._config),
    )

  def RunPoseEstimation(
      self,
      request: v1_pose_estimation_service_pb2.RunPoseEstimationRequest,
      context: grpc.ServicerContext = None,
  ) -> v1_pose_estimation_service_pb2.RunPoseEstimationResponse:
    logging.info("RunPoseEstimation called")

    if not request.HasField("asset_id") or not request.asset_id.name:
      error_msg = "RunPoseEstimationRequest must include a populated asset_id."
      logging.error(error_msg)
      if context is not None:
        context.abort(grpc.StatusCode.INVALID_ARGUMENT, error_msg)
      raise ValueError(error_msg)

    error_msg = "No pose estimation model configured."
    logging.error(error_msg)
    if context is not None:
      context.abort(grpc.StatusCode.UNIMPLEMENTED, error_msg)
    raise NotImplementedError(error_msg)

  def DeletePoseEstimation(
      self,
      request: v1_pose_estimation_service_pb2.DeletePoseEstimationRequest,
      context: grpc.ServicerContext = None,
  ) -> Empty:
    del request, context
    logging.info("DeletePoseEstimation called")
    return Empty()
