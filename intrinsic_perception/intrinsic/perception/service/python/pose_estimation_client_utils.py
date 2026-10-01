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

"""Utility library for preparing PoseEstimationService requests and channels."""

import base64
from typing import List
from typing import Optional

from google.protobuf import duration_pb2
import grpc

from intrinsic.assets import id_utils
from intrinsic.assets import interface_utils
from intrinsic.assets.dependencies import utils as asset_utils
from intrinsic.assets.proto import id_pb2
from intrinsic.assets.proto.v1 import resolved_dependency_pb2
from intrinsic.logging.proto import context_pb2
from intrinsic.math.python import proto_conversion
from intrinsic.perception.client.v1.python.camera import data_classes
from intrinsic.perception.proto.v1 import capture_data_pb2
from intrinsic.perception.proto.v1 import capture_result_pb2
from intrinsic.perception.proto.v1 import pose_estimation_service_pb2
from intrinsic.perception.proto.v1 import pose_estimator_id_pb2
from intrinsic.perception.proto.v1 import pose_priors_pb2
from intrinsic.resources.proto import resource_handle_pb2
from intrinsic.util.grpc import connection
from intrinsic.util.grpc import interceptor

_GRPC_OPTIONS = [
    ("grpc.max_receive_message_length", 1000 * 1024 * 1024),
    ("grpc.max_send_message_length", 1000 * 1024 * 1024),
    ("grpc.max_message_length", 1000 * 1024 * 1024),
]
_LOGGING_CONTEXT_METADATA_KEY = "x-intrinsic-logging-context"
_DEFAULT_TIMEOUT_IN_SECONDS = 900
_INGRESS_ADDRESS = "istio-ingressgateway.app-ingress.svc.cluster.local:80"


def to_asset_id(
    pose_estimator_id: pose_estimator_id_pb2.PoseEstimatorId,
) -> id_pb2.Id:
  """Converts a PoseEstimatorId proto to an Id proto.

  Args:
    pose_estimator_id: The input PoseEstimatorId proto.

  Returns:
    An Id proto mapped with the package name and target id.
  """
  return id_utils.id_proto_from(
      package=pose_estimator_id.package or "ai.intrinsic",
      name=pose_estimator_id.id,
  )


def _get_connection_params(
    connection_info: Optional[
        resource_handle_pb2.ResourceConnectionInfo
    ] = None,
) -> connection.ConnectionParams:
  """Returns the connection params for the perception service."""
  if connection_info is not None:
    return connection.ConnectionParams(
        address=connection_info.grpc.address,
        instance_name=connection_info.grpc.server_instance,
        header=connection_info.grpc.header,
    )
  return connection.ConnectionParams(
      address=_INGRESS_ADDRESS, instance_name=None, header=None
  )


def create_channel(
    connection_info: Optional[
        resource_handle_pb2.ResourceConnectionInfo
    ] = None,
    data_logger_context: Optional[context_pb2.Context] = None,
) -> grpc.Channel:
  """Creates a gRPC channel with configured interceptors and options.

  Args:
    connection_info: Connection parameters for the target service.
    data_logger_context: Optional logging context metadata to inject.

  Returns:
    An intercepted gRPC Channel instance.
  """
  connection_params = _get_connection_params(connection_info)
  channel = grpc.insecure_channel(
      connection_params.address, options=_GRPC_OPTIONS
  )
  channel = grpc.intercept_channel(
      channel, interceptor.HeaderAdderInterceptor(connection_params.headers)
  )
  return _add_logging_context_to_channel(channel, data_logger_context)


def _pose_estimation_service_interface_uri() -> str:
  """Returns the gRPC interface URI for the PoseEstimationService."""
  return (
      f"{interface_utils.GRPC_URI_PREFIX}"
      f"{pose_estimation_service_pb2.DESCRIPTOR.services_by_name['PoseEstimationService'].full_name}"
  )


def _add_logging_context_to_channel(
    channel: grpc.Channel,
    data_logger_context: Optional[context_pb2.Context] = None,
) -> grpc.Channel:
  """Intercepts a channel to inject the data logger context header."""
  if data_logger_context is None:
    return channel

  header = (
      _LOGGING_CONTEXT_METADATA_KEY,
      base64.urlsafe_b64encode(
          data_logger_context.SerializeToString(),
      ).decode(),
  )
  return grpc.intercept_channel(
      channel, interceptor.HeaderAdderInterceptor(lambda: [header])
  )


def create_channel_from_resolved_dependency(
    dep: resolved_dependency_pb2.ResolvedDependency,
    data_logger_context: Optional[context_pb2.Context] = None,
) -> grpc.Channel:
  """Creates an intercepted gRPC channel from a ResolvedDependency.

  Args:
    dep: The resolved dependency for the perception service.
    data_logger_context: Optional logging context metadata to inject.

  Returns:
    An intercepted gRPC Channel instance.
  """
  channel = asset_utils.connect(
      dep=dep,
      iface=_pose_estimation_service_interface_uri(),
      grpc_options=_GRPC_OPTIONS,
  )
  return _add_logging_context_to_channel(channel, data_logger_context)


def run_pose_estimation_request_from_capture_data(
    asset_id: id_pb2.Id,
    capture_data: list[capture_data_pb2.CaptureData],
    roi: Optional[object] = None,
    inference_timeout_secs: Optional[int] = None,
    publish_annotated_image: bool = False,
    data_logger_context: Optional[context_pb2.Context] = None,
    log_full_request: bool = False,
) -> pose_estimation_service_pb2.RunPoseEstimationRequest:
  """Prepares the RunPoseEstimationRequest from capture results.

  Args:
    asset_id: The asset id of the pose estimator.
    capture_data: list of capture data protos. The service takes care of the
      retrieval of images from KV store.
    roi: Optional region of interest to restrict estimation to.
    inference_timeout_secs: Optional timeout in seconds for running inference.
    publish_annotated_image: If true, publishes the annotated frames.
    data_logger_context: Optional logging context metadata.
    log_full_request: If true, logs full debug request/result on service.

  Returns:
    A RunPoseEstimationRequest proto.
  """
  run_config = pose_estimation_service_pb2.PoseEstimationRunConfig(
      publish_annotated_image=publish_annotated_image,
      log_full_request=log_full_request,
      timeout=duration_pb2.Duration(
          seconds=inference_timeout_secs or _DEFAULT_TIMEOUT_IN_SECONDS
      ),
  )
  if data_logger_context is not None and (
      publish_annotated_image or log_full_request
  ):
    run_config.logging_context.CopyFrom(data_logger_context)
  if roi is not None:
    run_config.pose_priors.region_of_interest.CopyFrom(roi)
  request = pose_estimation_service_pb2.RunPoseEstimationRequest(
      asset_id=asset_id,
      capture_data_list=pose_estimation_service_pb2.CaptureDataList(
          capture_data=capture_data,
      ),
      pose_estimation_run_config=run_config,
  )

  return request


def run_pose_estimation_request_from_capture_results(
    asset_id: id_pb2.Id,
    capture_results: List[data_classes.CaptureResult],
    roi: Optional[object] = None,
    inference_timeout_secs: Optional[int] = None,
    publish_annotated_image: bool = False,
    data_logger_context: Optional[context_pb2.Context] = None,
    log_full_request: bool = False,
) -> pose_estimation_service_pb2.RunPoseEstimationRequest:
  """Prepares the RunPoseEstimationRequest from capture results.

  Args:
    asset_id: The asset id of the pose estimator.
    capture_results: Captured camera frame data.
    roi: Optional region of interest to restrict estimation to.
    inference_timeout_secs: Optional timeout in seconds for running inference.
    publish_annotated_image: If true, publishes the annotated frames.
    data_logger_context: Optional logging context metadata.
    log_full_request: If true, logs full debug request/result on service.

  Returns:
    A RunPoseEstimationRequest proto.
  """
  if not capture_results:
    raise ValueError("No capture results found.")

  sensor_images = []
  camera_index = 1
  for capture_result in capture_results:
    for sensor_name, sensor_image in capture_result.sensor_images.items():
      if sensor_image.world_t_sensor is None:
        continue
      sensor_image_proto = sensor_image.proto
      sensor_image_proto.sensor_config.camera_t_sensor.CopyFrom(
          proto_conversion.pose_to_proto(sensor_image.world_t_sensor)
      )
      sensor_image_proto.sensor_config.id = camera_index
      camera_index += 1
      sensor_images.append(sensor_image_proto)
  # 3. Construct RunPoseEstimationRequest
  run_config = pose_estimation_service_pb2.PoseEstimationRunConfig(
      publish_annotated_image=publish_annotated_image,
      log_full_request=log_full_request,
      timeout=duration_pb2.Duration(
          seconds=inference_timeout_secs or _DEFAULT_TIMEOUT_IN_SECONDS
      ),
  )
  if data_logger_context is not None and (
      publish_annotated_image or log_full_request
  ):
    run_config.logging_context.CopyFrom(data_logger_context)
  if roi is not None:
    run_config.pose_priors.region_of_interest.CopyFrom(roi)

  capture_result_final_v1 = capture_result_pb2.CaptureResult(
      capture_at=capture_results[0].proto.capture_at,
      sensor_images=sensor_images,
  )
  request = pose_estimation_service_pb2.RunPoseEstimationRequest(
      asset_id=asset_id,
      capture_result=capture_result_final_v1,
      pose_estimation_run_config=run_config,
  )

  return request


def get_connection_params(
    connection_info: Optional[
        resource_handle_pb2.ResourceConnectionInfo
    ] = None,
) -> connection.ConnectionParams:
  """Returns the connection params for the perception service.

  Args:
    connection_info: Optional connection info proto.

  Returns:
    Connection parameters extracted from the input or default ingress address.
  """
  if connection_info is not None:
    return connection.ConnectionParams(
        address=connection_info.grpc.address,
        instance_name=connection_info.grpc.server_instance,
        header=connection_info.grpc.header,
    )
  return connection.ConnectionParams(
      address=_INGRESS_ADDRESS, instance_name=None, header=None
  )
