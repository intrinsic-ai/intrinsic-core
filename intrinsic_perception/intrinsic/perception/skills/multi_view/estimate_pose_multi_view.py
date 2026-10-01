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

"""Estimates poses of all instances of a given part using multiple cameras.

Current implementation constraints
1. Works with 2 or more cameras (recommended 3 or 4)
2. Plane containing optical centres of the 3 cameras should not intersect the
   scene / objects of interest.
3. All cameras used must provide images with
   the same resolution.
"""

import time
from typing import List

from absl import logging
import grpc
import numpy as np

from incode.ml.services.utils import otel_tracing
from intrinsic.geometry.proto import oriented_bounding_box_pb2
from intrinsic.math.python import proto_conversion
from intrinsic.perception.proto.v1 import pose_estimate_in_root_pb2
from intrinsic.perception.proto.v1 import pose_estimate_pb2
from intrinsic.perception.service.python import pose_estimation_client_utils
from intrinsic.perception.service.python import pose_estimation_service_client
from intrinsic.perception.skills.multi_view import estimate_pose_multi_view_pb2
from intrinsic.perception.skills.multi_view import multi_view_pose_utils
from intrinsic.platform.pubsub.python import pubsub
from intrinsic.skills.proto import footprint_pb2
from intrinsic.skills.python import skill_interface as skl
from intrinsic.util import decorators
from intrinsic.world.python import object_world_ids
from intrinsic.world.python import object_world_resources
from intrinsic.world.python.object_world_client import ObjectWorldClient

PERCEPTION_SLOT = "perception"



def _populate_return_value(
    pose_estimates: list[pose_estimate_pb2.PoseEstimate],
) -> estimate_pose_multi_view_pb2.EstimatePoseMultiViewResult:
  """Populates EstimatePoseMultiViewResult given PoseEstimationResult."""
  return_value = estimate_pose_multi_view_pb2.EstimatePoseMultiViewResult(
      estimates=[],
      root_ts_target=[],
  )
  for pose_estimate in pose_estimates:
    pose_estimate_proto = pose_estimate_in_root_pb2.PoseEstimateInRoot(
        id=pose_estimate.id,
        root_t_target=pose_estimate.camera_t_target,
        score=pose_estimate.score,
    )
    return_value.estimates.append(pose_estimate_proto)
    return_value.root_ts_target.append(pose_estimate.camera_t_target)
  return return_value


def _remove_poses_outside_roi(
    roi: oriented_bounding_box_pb2.OrientedBoundingBox3,
    pose_estimates_world: List[pose_estimate_pb2.PoseEstimate],
) -> List[pose_estimate_pb2.PoseEstimate]:
  """Filters pose estimates to keep only those within the region of interest.

  Note that this function only checks the origin of the target's coordinate
  frame, not the target's mesh vertices.
  """
  dimensions = np.array([roi.dimensions.x, roi.dimensions.y, roi.dimensions.z])
  obb_t_world = proto_conversion.pose_from_proto(roi.ref_t_box).inverse()
  filtered_pose_estimates = []
  for pose_estimate_world_proto in pose_estimates_world:
    world_t_target = proto_conversion.pose_from_proto(
        pose_estimate_world_proto.camera_t_target
    )
    obb_t_target = obb_t_world.multiply(world_t_target)
    center = obb_t_target.translation

    within_bounding_box_limits = np.all(np.abs(center) <= dimensions / 2)
    if within_bounding_box_limits:
      filtered_pose_estimates.append(pose_estimate_world_proto)

  logging.info(
      "Number of filtered poses outside ROI: %s",
      len(pose_estimates_world) - len(filtered_pose_estimates),
  )
  return filtered_pose_estimates


def _update_object_pose(
    world: ObjectWorldClient,
    pose_estimates_root: List[pose_estimate_pb2.PoseEstimate],
    object_instance: object_world_resources.WorldObject,
) -> None:
  """Updates the object's pose in the world using the best pose estimate."""
  if not pose_estimates_root:
    raise ValueError(
        "Cannot update object pose. Pose estimation resulted in no detections."
    )

  best_pose_estimate = max(
      pose_estimates_root, key=lambda estimate: estimate.score
  )

  # Pose is in root (world) frame, which goes into camera_t_target field in the proto
  root_t_pose_estimate = best_pose_estimate.camera_t_target
  world.update_transform(
      node_a=world.get_transform_node(
          object_world_ids.ObjectWorldResourceId("root")
      ),
      node_b=world.get_transform_node(object_instance.transform_node_reference),
      a_t_b=proto_conversion.pose_from_proto(root_t_pose_estimate),
      node_to_update=world.get_transform_node(
          object_instance.transform_node_reference
      ),
  )


class EstimatePoseMultiView(skl.Skill):
  """Estimates poses of all instances of a given part using multiple cameras."""

  def __init__(self, *args, **kwargs):
    super().__init__(*args, **kwargs)
    # Set up distributed tracing hooks.
    otel_tracing.setup_tracing(
        service_name="estimate_pose_multi_view",
        service_namespace="ai.intrinsic.ml_onprem_skills",
    )

  pubsub_ = pubsub.PubSub()

  @decorators.overrides(skl.Skill)
  def execute(
      self,
      request: skl.ExecuteRequest[
          estimate_pose_multi_view_pb2.EstimatePoseMultiViewParams
      ],
      context: skl.ExecuteContext,
  ) -> estimate_pose_multi_view_pb2.EstimatePoseMultiViewResult:
    start_skill = time.perf_counter()

    if not request.params.capture_data:
      raise skl.InvalidSkillParametersError(
          "No capture_data found. Please provide capture_data."
      )

    perception_service_connection_info = context.resource_handles[
        PERCEPTION_SLOT
    ].connection_info

    if request.params.update_object_pose.update_pose:
      logging.info(request.params.update_object_pose.object_to_update)

    # Establish communication channel
    channel = pose_estimation_client_utils.create_channel(
        connection_info=perception_service_connection_info,
        data_logger_context=context.logging_context.data_logger_context,
    )
    client = (
        pose_estimation_service_client.PoseEstimationServiceClient.from_channel(
            grpc_channel=channel
        )
    )

    try:
      asset_id = pose_estimation_client_utils.to_asset_id(
          request.params.pose_estimator
      )
      inference_timeout_secs = (
          request.params.inference_timeout_sec
          if request.params.inference_timeout_sec > 0
          else None
      )
      if request.params.HasField("publish_annotated_image"):
        publish_annotated_image = request.params.publish_annotated_image
      else:
        publish_annotated_image = True

      roi_proto = None
      if request.params.HasField("region_of_interest"):
        if request.params.region_of_interest.HasField("roi_params"):
          roi_proto = request.params.region_of_interest.roi_params

      # Build request and run pose estimation
      with otel_tracing.tracer.start_as_current_span(
          "EstimatePoseMultiView.run_pose_estimation"
      ) as span:
        run_request = pose_estimation_client_utils.run_pose_estimation_request_from_capture_data(
            asset_id=asset_id,
            capture_data=request.params.capture_data,
            roi=roi_proto,
            inference_timeout_secs=inference_timeout_secs,
            publish_annotated_image=publish_annotated_image,
            data_logger_context=context.logging_context.data_logger_context,
            log_full_request=request.params.log_debug_data,
        )

      try:
        response = client.run_pose_estimation(run_request)

      except grpc.RpcError as e:
        if (
            request.params.capture_data
            and e.code() == grpc.StatusCode.INVALID_ARGUMENT
        ):
          raise RuntimeError(
              "Pose estimation service rejected the request with"
              f" INVALID_ARGUMENT: {e.details()}. Please verify that the"
              " capture_data is provided correctly and that the"
              " PoseEstimationService is compatible with this skill version."
          ) from e
        raise
      pose_estimates_root = list(response.pose_estimates)

      # Apply ROI filtering if set.
      if (
          roi_proto is not None
          and request.params.region_of_interest.remove_points_outside_region_of_interest
      ):
        pose_estimates_root = _remove_poses_outside_roi(
            roi=roi_proto,
            pose_estimates_world=pose_estimates_root,
        )

      # Validate number of instances
      if len(pose_estimates_root) < request.params.min_num_instances:
        raise ValueError(
            "Pose estimation resulted in"
            f" {len(pose_estimates_root)} detections. Expected at least"
            f" {request.params.min_num_instances} instances."
        )

      # Update world object pose if requested
      if (
          request.params.update_object_pose.update_pose
          and context.object_world is not None
      ):
        object_instance: object_world_resources.WorldObject = (
            context.object_world.get_object(
                object_reference=request.params.update_object_pose.object_to_update
            )
        )
        _update_object_pose(
            world=context.object_world,
            pose_estimates_root=pose_estimates_root,
            object_instance=object_instance,
        )

      return_value: estimate_pose_multi_view_pb2.EstimatePoseMultiViewResult = (
          _populate_return_value(
              pose_estimates=pose_estimates_root,
          )
      )

      multi_view_pose_utils.log_pose_estimation_results(
          list(return_value.estimates)
      )
      logging.info("Skill took %f seconds.", time.perf_counter() - start_skill)
      return return_value
    finally:
      channel.close()

  def preview(
      self,
      request: skl.PreviewRequest[
          estimate_pose_multi_view_pb2.EstimatePoseMultiViewParams
      ],
      context: skl.PreviewContext,
  ) -> estimate_pose_multi_view_pb2.EstimatePoseMultiViewResult:
    # A no-op implementation which returns an empty set of poses as a result.
    return estimate_pose_multi_view_pb2.EstimatePoseMultiViewResult()

  @decorators.overrides(skl.Skill)
  def get_footprint(
      self,
      request: skl.GetFootprintRequest[
          estimate_pose_multi_view_pb2.EstimatePoseMultiViewParams
      ],
      context: skl.GetFootprintContext,
  ) -> footprint_pb2.Footprint:
    footprint = footprint_pb2.Footprint(lock_the_universe=False)
    if request.params.HasField("update_object_pose"):
      if request.params.update_object_pose.update_pose:
        object_to_update = request.params.update_object_pose.object_to_update
        footprint.object_reservation.extend([
            multi_view_pose_utils.object_reference_to_reservation(
                context.object_world, object_to_update
            )
        ])

    return footprint
