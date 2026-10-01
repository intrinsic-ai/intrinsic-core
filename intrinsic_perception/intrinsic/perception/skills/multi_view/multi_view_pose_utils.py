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

"""Library to for generic multi-view poses utility functions."""

from collections.abc import Sequence
import logging
import os

import cv2
import grpc
import numpy as np

from intrinsic.geometry.api.python import conversion_utils
from intrinsic.geometry.proto import geometry_service_pb2_grpc
from intrinsic.geometry.proto.v1 import geometry_pb2
from intrinsic.math.python import proto_conversion
from intrinsic.perception.client.v1.python.camera import data_classes
from intrinsic.perception.proto.v1 import capture_data_pb2
from intrinsic.perception.proto.v1 import capture_result_pb2
from intrinsic.perception.proto.v1 import pose_estimate_in_root_pb2
from intrinsic.perception.proto.v1 import target_pb2
from intrinsic.platform.pubsub.python import pubsub
from intrinsic.skills.proto import footprint_pb2
from intrinsic.world.proto import object_world_refs_pb2
from intrinsic.world.python import geometry_component_utils
from intrinsic.world.python import geometry_types
from intrinsic.world.python import object_world_client
from intrinsic.world.python import object_world_resources

_GRPC_OPTIONS = [
    ("grpc.max_receive_message_length", -1),
    ("grpc.max_send_message_length", -1),
    ("grpc.max_message_length", -1),
]


def get_object_mesh(geometry: geometry_pb2.Geometry, address: str) -> bytes:
  """Returns the renderable glb mesh of the given geometry."""
  channel = grpc.insecure_channel(address, options=_GRPC_OPTIONS)
  stub = geometry_service_pb2_grpc.GeometryServiceStub(channel)
  renderable = conversion_utils.get_renderable_from_geometry(geometry, stub)
  return renderable.glb_bytes


def write_mesh_to_disk(
    target: target_pb2.Target,
    instance: object_world_resources.WorldObject,
    address: str,
) -> bool:
  """Write object CAD model/mesh to local disk (if not already present) to be read by the rendering function."""

  try:
    link_entities = [
        e
        for e in instance.proto.entities.values()
        if e.HasField("geometry_component")
    ]
    assert (
        len(link_entities) == 1
    ), "Expected one link entity(with geometry component) to render."
    try:
      visual_geometries = geometry_component_utils.get_geometries_v1(
          link_entities[0].geometry_component,
          geometry_types.KIND_VISUAL_GEOMETRY,
      )
    except Exception as e:
      raise ValueError(
          "Expected one visual geometry for object to render."
      ) from e

    assert (
        len(visual_geometries) == 1
    ), "Expected one visual geometry for object to render."
    visual = next(iter(visual_geometries.values()))
    filename = target.mesh.filename
    if not filename.startswith("/tmp/"):
      filename = os.path.join("/tmp", os.path.basename(filename))
    os.makedirs(os.path.dirname(filename), exist_ok=True)
    if os.path.isfile(filename):
      logging.info(
          "Mesh for target %s already exists at %s.", target.id, filename
      )
      return True
    else:
      gltf_mesh_string = get_object_mesh(visual.geometry, address)
      with open(filename, "wb") as f:
        f.write(gltf_mesh_string)
      logging.info("Wrote mesh for target %s to %s.", target.id, filename)
      logging.debug(
          "gltf Mesh string for object %s with geometry_refs %s is %s.",
          instance.name,
          visual.geometry,
          gltf_mesh_string,
      )
      return True
  except OSError as e:
    logging.exception("Error in write mesh to disk %s", e)
    return False


def log_pose_estimation_results(
    pose_estimates: list[pose_estimate_in_root_pb2.PoseEstimateInRoot],
):
  """Logs pose estimation results in a tidy format.

  Args:
    pose_estimates: list of pose estimates.
  """
  logging.info("")
  logging.info("")
  logging.info("=============== Pose estimation results  =====================")
  logging.info("Number of parts found: %d", len(pose_estimates))
  for idx, result in enumerate(pose_estimates):
    logging.info("Pose %d for object %s", idx, result.id)
    logging.info("\troot_t_target:")
    logging.info(
        "\t\tPosition (xyz): [%s, %s, %s]",
        result.root_t_target.position.x,
        result.root_t_target.position.y,
        result.root_t_target.position.z,
    )
    logging.info(
        "\t\tOrientation (xyzw): [%s, %s, %s, %s]",
        result.root_t_target.orientation.x,
        result.root_t_target.orientation.y,
        result.root_t_target.orientation.z,
        result.root_t_target.orientation.w,
    )
    logging.info("\tVisibility Score: %f", result.visibility_score)
    logging.info("\tRefinement Score: %f", result.score)
  logging.info("==============================================================")


def render_bounding_box_to_image(
    image: np.ndarray, bounding_box: tuple[int, int, int, int]
) -> np.ndarray:
  image_with_bounding_box = cv2.rectangle(
      image,
      (bounding_box[1], bounding_box[0]),
      (bounding_box[3], bounding_box[2]),
      color=(0, 0, 255),
      thickness=2,
  )
  return image_with_bounding_box


def object_reference_to_reservation(
    world: object_world_client.ObjectWorldClient,
    object_ref: object_world_refs_pb2.ObjectReference,
) -> footprint_pb2.ObjectWorldReservation:
  """Returns a resource for the given object."""
  world_node = world.get_object(object_ref)

  return footprint_pb2.ObjectWorldReservation(
      object=object_world_refs_pb2.ObjectReferenceByName(
          object_name=world_node.name
      )
  )


# TODO(feuer): Add an end-to-end test, similar to cl/730766172.
def load_capture_results(
    capture_data: Sequence[capture_data_pb2.CaptureData],
    pub_sub: pubsub.PubSub,
) -> list[data_classes.CaptureResult]:
  """Loads capture results from capture data.

  Args:
    capture_data: Protos containing information about the capture result
      locations and camera poses.
    pub_sub: PubSub instance to use for loading the capture results.

  Returns:
    A list of capture results.
  """

  kv_stores = {}
  capture_results = []
  for capture_data_entry in capture_data:
    store = capture_data_entry.capture_result_location.store
    key = capture_data_entry.capture_result_location.key
    logging.info("Loading CaptureResult from KV Store %s at key %s", store, key)
    kv_store = kv_stores.get(store, pub_sub.KeyValueStore(store))
    # Try v1 first, then unversioned.
    capture_result = capture_result_pb2.CaptureResult()
    if not kv_store.Get(key).Unpack(capture_result):
      raise ValueError(
          "Incompatible capture result, are you using the latest version of"
          " capture images?"
      )

    world_t_camera = proto_conversion.pose_from_proto(
        capture_data_entry.world_t_camera
    )
    capture_results.append(
        data_classes.CaptureResult(
            capture_result=capture_result, world_t_camera=world_t_camera
        )
    )
  return capture_results
