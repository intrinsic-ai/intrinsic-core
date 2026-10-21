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

"""IOC Pose Estimator Service.

Coordinates 2D detection/segmentation (RF-DETR) and 6D pose estimation (FoundationPose).
"""

import io
import logging
import os
import signal
import threading

import cv2
from google.longrunning import operations_pb2
from google.longrunning import operations_pb2_grpc
from google.protobuf import text_format
from google.protobuf.empty_pb2 import Empty
import grpc
import numpy as np
from specification.protocol import open_inference_grpc_pb2_grpc
import trimesh

from intrinsic_perception.intrinsic.perception.service.ioc_pose_estimator.proto import ioc_service_config_pb2
from intrinsic_perception.intrinsic.perception.service.ioc_pose_estimator.service import pose_estimator_model
from intrinsic_perception.intrinsic.perception.service.ioc_pose_estimator.service import segmentation_model
from intrinsic_perception.intrinsic.perception.service.ioc_train_service.proto import ioc_pose_estimator_params_pb2
from intrinsic.assets import interface_utils
from intrinsic.assets.dependencies import utils as asset_utils
from intrinsic.assets.install import installed_assets_client
from intrinsic.assets.proto import id_pb2
from intrinsic.assets.proto import installed_assets_pb2
from intrinsic.assets.proto import installed_assets_pb2_grpc
from intrinsic.assets.proto.v1 import resolved_dependency_pb2
from intrinsic.math.python import pose3
from intrinsic.math.python import proto_conversion
from intrinsic.math.python import rotation3
from intrinsic.perception.client.v1.python import image_utils
from intrinsic.perception.client.v1.python.camera import data_classes
from intrinsic.perception.proto.v1 import capture_result_pb2
from intrinsic.perception.proto.v1 import image_buffer_pb2
from intrinsic.perception.proto.v1 import perception_model_pb2
from intrinsic.perception.proto.v1 import pose_estimation_service_pb2 as v1_pose_estimation_service_pb2
from intrinsic.perception.proto.v1 import pose_estimation_service_pb2_grpc as v1_pose_estimation_service_pb2_grpc
from intrinsic.perception.skills.multi_view import multi_view_pose_utils
from intrinsic.platform.pubsub.python import pubsub
from intrinsic.storage.content_addressable_storage.proto import cas_service_pb2_grpc
from intrinsic.storage.content_addressable_storage.python import client_helpers
from intrinsic.util.grpc import interceptor

CAS_ADDRESS = (
    "content-addressable-storage.app-intrinsic-base.svc.cluster.local:9747"
)
_GRPC_OPTIONS = [
    ("grpc.max_receive_message_length", -1),
    ("grpc.max_send_message_length", -1),
    ("grpc.max_message_length", -1),
]

_DEFAULT_REFINEMENT_ITERATIONS: int = 3
_DEFAULT_CONFIDENCE_THRESHOLD: float = 0.6
_DEFAULT_VISIBILITY_THRESHOLD: float = 0.6
_DEFAULT_BATCH_SIZE: int = 240

_VISUALIZATION_DIR = "/tmp/pose_estimation_debug/"
_DETECTION_IMAGE_TOPIC: str = "ioc_pose_estimator/detections"
_POSE_IMAGE_TOPIC: str = "ioc_pose_estimator/poses"


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


def _validate_and_extract_perception_model_params(
    perception_model: perception_model_pb2.PerceptionModel,
) -> tuple[int, float, float]:
  """Validates and extracts IOC pose estimator hyperparameters from a PerceptionModel.

  Checks whether iteration number, confidence threshold, and visibility threshold
  are specified in the model's inference_params. Any unset values
  default to 3 iterations, 0.6 confidence threshold, and 0.6 visibility threshold.
  Any invalid or out of range values raise a ValueError

  Args:
    perception_model: The PerceptionModel proto unpacked from a DataAsset.

  Returns:
    A tuple of (refinement_iterations, confidence_threshold, visibility_threshold)
    guaranteed to contain valid configurations.
  """
  refinement_iterations = _DEFAULT_REFINEMENT_ITERATIONS
  confidence_threshold = _DEFAULT_CONFIDENCE_THRESHOLD
  visibility_threshold = _DEFAULT_VISIBILITY_THRESHOLD

  params = ioc_pose_estimator_params_pb2.IocPoseEstimatorParams()
  if perception_model.pose_estimation_config.inference_params.Unpack(params):
    if params.HasField("refinement_iters"):
      refinement_iterations = params.refinement_iters
    else:
      logging.info(
          "refinement iterations not set by user, falling back to %s",
          _DEFAULT_REFINEMENT_ITERATIONS,
      )

    if params.HasField("confidence_threshold"):
      confidence_threshold = params.confidence_threshold
    else:
      logging.info(
          "confidence threshold not set by user, falling back to %s",
          _DEFAULT_CONFIDENCE_THRESHOLD,
      )

    if params.HasField("visibility_threshold"):
      visibility_threshold = params.visibility_threshold
    else:
      logging.info(
          "visibility threshold not set by user, falling back to %s",
          _DEFAULT_VISIBILITY_THRESHOLD,
      )
  else:
    logging.info(
        "inference params not specified by user, using default parameters."
        " confidence_threshold: %s,"
        " visibility_threshold: %s,"
        " refinement_iterations: %s",
        _DEFAULT_CONFIDENCE_THRESHOLD,
        _DEFAULT_VISIBILITY_THRESHOLD,
        _DEFAULT_REFINEMENT_ITERATIONS,
    )

  if not 0.0 <= confidence_threshold <= 1.0:
    raise ValueError(
        "Invalid confidence threshold. Confidence threshold needs to be set"
        f" between 0.0 and 1.0, got {confidence_threshold}",
    )
  if not 0.0 <= visibility_threshold <= 1.0:
    raise ValueError(
        "Invalid visibility threshold. Visibility threshold needs to be set"
        f" between 0.0 and 1.0, got {visibility_threshold}",
    )
  if refinement_iterations < 0:
    raise ValueError(
        "Invalid refinement iterations. Refinement iterations must be larger"
        f" or equal to 0, got {refinement_iterations}"
    )

  return refinement_iterations, confidence_threshold, visibility_threshold


def _extract_rgb_depth_and_intrinsics(
    request: v1_pose_estimation_service_pb2.RunPoseEstimationRequest,
    pubsub_instance: pubsub.PubSub,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
  """Extracts RGB image, depth image, and camera intrinsics from a RunPoseEstimationRequest.

  Args:
    request: The incoming RunPoseEstimationRequest.
    pubsub_instance: PubSub client used to fetch capture data stored in KV store.

  Returns:
    A tuple of (rgb_image, depth_image, intrinsic_matrix).

  Raises:
    ValueError: If source_type is unspecified or if RGB image, depth image, or
      intrinsic matrix is missing.
  """

  capture_result = None
  if request.HasField("capture_data_list"):
    capture_results = multi_view_pose_utils.load_capture_results(
        request.capture_data_list.capture_data, pubsub_instance
    )
    if len(capture_results) != 1:
      raise ValueError(
          "Pose estimator service requires exactly one capture result to be"
          " specified in the Estimate Pose Multi View skill but got"
          f" {len(capture_results)}",
      )
    capture_result = capture_results[0]
  elif request.HasField("capture_result"):
    capture_result = request.capture_result
    if isinstance(capture_result, capture_result_pb2.CaptureResult):
      capture_result = data_classes.CaptureResult(capture_result)
  else:
    raise ValueError(
        "RunPoseEstimationRequest must specify a source_type"
        " ('capture_result' or 'capture_data_list')."
    )

  if not capture_result:
    raise ValueError("No capture results found in request.")

  rgb_img = None
  depth_img = None
  intrinsic_matrix = None

  for v in capture_result.sensor_images.values():
    pixel_type = v.proto.buffer.pixel_type

    # Intensity images (RGB or Grayscale)
    if pixel_type == image_buffer_pb2.PixelType.PIXEL_INTENSITY:
      if rgb_img is not None:
        raise ValueError(
            "Only single image inputs are supported by the IOC pose estimator,"
            " but got multiple image inputs."
        )
      if v.array.ndim > 3 or v.array.ndim < 2:
        raise ValueError(
            f"Got invalid image shape in capture results {v.array.shape}"
        )

      # convert to 3D array
      arr = v.array[:, :, np.newaxis] if v.array.ndim == 2 else v.array
      if arr.shape[2] == 1:
        rgb_img = np.repeat(arr, 3, axis=-1)
      elif arr.shape[2] == 3:
        rgb_img = arr
      elif arr.shape[2] == 4:
        rgb_img = arr[:, :, :3]
        logging.info(
            "pruning 4 dimensional image to 3 dimensions assumuing RGBA format"
        )
      else:
        raise ValueError(
            f"Got image input of incompatible shape {arr.shape}, expected"
            " either (H, W), (H, W, 1), (H, W, 3) or (H, W, 4)"
        )
      intrinsic_matrix = v.intrinsic_matrix
      logging.info(
          "[_extract_rgb_depth_and_intrinsics] Extracted intensity image"
          " and matching intrinsics from sensor '%s'.",
          v.sensor_name,
      )
    # Depth images
    elif pixel_type == image_buffer_pb2.PixelType.PIXEL_DEPTH:
      if depth_img is not None:
        raise ValueError(
            "Only single depth inputs are supported by the IOC pose estimator,"
            " but got multiple depth inputs."
        )
      depth_img = np.squeeze(v.array).astype(np.float32)
      if depth_img.ndim != 2:
        raise ValueError(
            f"Got invalid depth image shape {v.array.shape}, expected 2D map."
        )
      logging.info(
          "[_extract_rgb_depth_and_intrinsics] Extracted depth map from"
          " sensor '%s'.",
          v.sensor_name,
      )

  if rgb_img is None:
    raise ValueError(
        "Could not find a valid RGB image (shape HxWx3) in capture results."
    )
  if depth_img is None:
    raise ValueError(
        "Could not find a valid depth image (shape HxW, float32) in capture"
        " results."
    )
  if intrinsic_matrix is None:
    raise ValueError(
        "Could not find matching camera intrinsic matrix for RGB image in"
        " capture results."
    )

  return rgb_img, depth_img, intrinsic_matrix


def _matrix_and_translation_to_pose_proto(
    rot_mat: np.ndarray,
    trans_vec: np.ndarray,
):
  """Projects a 3x3 rotation matrix to SO(3) via SVD and converts to a Pose proto."""
  rot_mat_64 = np.array(rot_mat, dtype=np.float64).reshape(3, 3)
  u, _, vt = np.linalg.svd(rot_mat_64)
  d = np.linalg.det(u @ vt)
  rot_mat_ortho = u @ np.diag([1.0, 1.0, d]) @ vt

  rot = rotation3.Rotation3.from_matrix(rot_mat_ortho, rtol=1e-4, atol=1e-4)
  pose_obj = pose3.Pose3(
      rotation=rot,
      translation=np.array(trans_vec, dtype=np.float64).flatten(),
  )
  return proto_conversion.pose_to_proto(pose_obj)


class IocPoseEstimatorService(
    v1_pose_estimation_service_pb2_grpc.PoseEstimationServiceServicer,
):
  """Implementation of the IOC Pose Estimator Service absed on Foundationpose."""

  pubsub_ = pubsub.PubSub()

  def __init__(
      self,
      config: ioc_service_config_pb2.IocPoseEstimatorServiceConfig,
  ):
    """Initializes the IocPoseEstimatorService.

    Args:
        config: Initial IocPoseEstimatorServiceConfig.
    """
    self._config = config

    installed_assets_channel = asset_utils.connect_with_runtime_asset_fallback_for_asset_migration_only(
        self._config.intrinsic_runtime,
        _installed_assets_interface_uri(),
        grpc_options=_GRPC_OPTIONS,
    )
    operations_channel = asset_utils.connect_with_runtime_asset_fallback_for_asset_migration_only(
        self._config.intrinsic_runtime,
        _operations_interface_uri(),
        grpc_options=_GRPC_OPTIONS,
    )
    self._assets_client = installed_assets_client.InstalledAssetsClient(
        installed_assets_pb2_grpc.InstalledAssetsStub(installed_assets_channel),
        operations_pb2_grpc.OperationsStub(operations_channel),
    )
    cas_channel = grpc.insecure_channel(CAS_ADDRESS, options=_GRPC_OPTIONS)
    self._cas_stub = cas_service_pb2_grpc.ContentAddressableStorageServiceStub(
        cas_channel
    )
    self._cad_cache = {}
    self._params_cache = {}
    self._lock = threading.Lock()
    self._batch_size = (
        config.batch_size
        if config.HasField("batch_size")
        else _DEFAULT_BATCH_SIZE
    )

    # generate ML inference service stub
    self._ml_service_stub = None
    if self._config.HasField("inference_service"):
      logging.info("connecting to inference service")
      self._connect_to_inference_service(self._config.inference_service)
    else:
      logging.warning(
          "Could not find inference service to connect to, please configure"
          " inference service in the config."
      )

    if not self._config.HasField("segmentation_model"):
      raise ValueError(
          "segmentation_model dependency must be specified in the service"
          " config."
      )
    if not self._config.HasField("foundationpose_model"):
      raise ValueError(
          "foundationpose_model dependency must be specified in the service"
          " config."
      )

    self.segmentation_model = segmentation_model.SegmentationModel(
        ml_service_stub=self._ml_service_stub,
        model_dependency=self._config.segmentation_model,
    )
    self.pose_estimator_model = pose_estimator_model.PoseEstimationModel(
        ml_service_stub=self._ml_service_stub,
        model_dependency=self._config.foundationpose_model,
    )
    self._detection_image_pub = self.pubsub_.CreatePublisher(
        _DETECTION_IMAGE_TOPIC
    )
    self._pose_image_pub = self.pubsub_.CreatePublisher(_POSE_IMAGE_TOPIC)

    logging.info(
        "IocPoseEstimatorService initialized with config: %s",
        text_format.MessageToString(self._config),
    )

  def _connect_to_inference_service(
      self,
      inference_service_dependency: resolved_dependency_pb2.ResolvedDependency,
  ):
    """Connects to the ML inference service and stores stub for reuse."""
    logging.info("Connecting to ML inference service...")
    grpc_info = next(
        iter(inference_service_dependency.interfaces.values())
    ).grpc
    ml_inference_channel = grpc.intercept_channel(
        grpc.insecure_channel(
            grpc_info.connection.address, options=_GRPC_OPTIONS
        ),
        interceptor.HeaderAdderInterceptor(
            lambda: [(m.key, m.value) for m in grpc_info.connection.metadata]
        ),
    )
    self._ml_service_stub = (
        open_inference_grpc_pb2_grpc.GRPCInferenceServiceStub(
            ml_inference_channel
        )
    )
    logging.info(
        "Connected to ML inference service at %s",
        grpc_info.connection.address,
    )

  def _ensure_asset_loaded(
      self, asset_id: id_pb2.Id
  ) -> tuple[bytes, int, float, float]:
    """Loads CAD geometry and hyperparameters from CAS/DataAsset for the given asset."""
    cache_key = (asset_id.package, asset_id.name)

    with self._lock:
      if cache_key in self._cad_cache:
        iterations, confidence_threshold, visibility_threshold = (
            self._params_cache.get(
                cache_key,
                (
                    _DEFAULT_REFINEMENT_ITERATIONS,
                    _DEFAULT_CONFIDENCE_THRESHOLD,
                    _DEFAULT_VISIBILITY_THRESHOLD,
                ),
            )
        )
        return (
            self._cad_cache[cache_key],
            iterations,
            confidence_threshold,
            visibility_threshold,
        )

    logging.info(
        "Loading asset for pose estimation: %s.%s",
        asset_id.package,
        asset_id.name,
    )

    installed_asset = self._assets_client.get_installed_asset(asset_id)
    if installed_asset is None:
      raise ValueError(
          f"Installed asset '{asset_id.package}.{asset_id.name}' was not found."
      )

    if not (installed_asset.deployment_data.HasField("data")):
      raise ValueError(
          f"Installed asset '{asset_id.package}.{asset_id.name}' does not"
          " contain deployment data."
      )

    perception_model = perception_model_pb2.PerceptionModel()
    if not installed_asset.deployment_data.data.data.data.Unpack(
        perception_model
    ):
      raise ValueError(
          "Failed to unpack perception model from pose estimator data asset."
      )

    iterations, confidence_threshold, visibility_threshold = (
        _validate_and_extract_perception_model_params(perception_model)
    )

    cad_uri = None
    cad_path = None
    for path, ref_data in perception_model.data_references.items():
      if path.endswith((".glb", ".gltf", ".obj", ".ply")):
        cad_uri = ref_data.reference
        cad_path = path
        break

    if not cad_uri:
      raise ValueError(
          f"Asset '{asset_id.package}.{asset_id.name}' does not contain any CAD"
          " model (.obj, .ply, .glb, .gltf) in its data references."
      )

    logging.info(
        "Downloading CAD model from CAS (%s) for asset %s.%s",
        cad_uri,
        asset_id.package,
        asset_id.name,
    )
    raw_cad_bytes = client_helpers.get(self._cas_stub, cad_uri)
    if not raw_cad_bytes:
      raise ValueError(
          f"Downloaded 0 bytes for CAD model from CAS URI '{cad_uri}'."
      )
    logging.info(
        "Downloaded %d bytes of CAD model from CAS", len(raw_cad_bytes)
    )

    target_t_mesh = None
    if perception_model.HasField("pose_estimation_config"):
      n_targets = len(perception_model.pose_estimation_config.targets)
      if n_targets != 1:
        raise ValueError(
            "IOC pose estimator only supports single targets in pose"
            f" estimation config, but got {n_targets}"
        )

      target = perception_model.pose_estimation_config.targets[0]
      if target.mesh.HasField("target_t_mesh"):
        ttm = target.mesh.target_t_mesh
        mat = np.eye(4, dtype=np.float64)
        if ttm.HasField("linear"):
          if len(ttm.linear.values) != 9:
            raise ValueError(
                "Expected 9 values in target_t_mesh.linear for a 3x3 matrix,"
                f" but got {len(ttm.linear.values)}."
            )
          mat[:3, :3] = (
              np.array(ttm.linear.values, dtype=np.float64).reshape((3, 3)).T
          )
        mat[0, 3] = ttm.translation.x
        mat[1, 3] = ttm.translation.y
        mat[2, 3] = ttm.translation.z
        target_t_mesh = mat

    ext = os.path.splitext(cad_path)[1].lower().lstrip(".")
    try:
      loaded_mesh = trimesh.load(
          io.BytesIO(raw_cad_bytes), file_type=ext, force="mesh"
      )
      if isinstance(loaded_mesh, trimesh.Scene):
        loaded_mesh = loaded_mesh.dump(concatenate=True)
      if target_t_mesh is not None:
        loaded_mesh.apply_transform(target_t_mesh)
        logging.info(
            "Applied target_t_mesh transformation to CAD vertices: %s",
            target_t_mesh,
        )

      obj_export = loaded_mesh.export(file_type="obj")
      obj_bytes = (
          obj_export.encode("utf-8")
          if isinstance(obj_export, str)
          else bytes(obj_export)
      )
      logging.info(
          "Successfully converted CAD mesh (%s) to Wavefront OBJ format: %d"
          " vertices, %d faces, %d OBJ bytes",
          ext,
          len(loaded_mesh.vertices),
          len(loaded_mesh.faces),
          len(obj_bytes),
      )
    except Exception as e:
      raise ValueError(
          f"Failed to load or convert CAD mesh ({cad_path}) to OBJ: {e}"
      ) from e

    with self._lock:
      self._params_cache[cache_key] = (
          iterations,
          confidence_threshold,
          visibility_threshold,
      )
      self._cad_cache[cache_key] = obj_bytes

    return obj_bytes, iterations, confidence_threshold, visibility_threshold

  def _run_segmentation_model(
      self,
      rgb_img: np.ndarray,
      confidence_threshold: float,
      visibility_threshold: float,
  ) -> tuple[np.ndarray, np.ndarray]:
    logging.info("Processing RGB image through segmentation...")
    img_for_triton = rgb_img.astype(np.uint8)
    img_for_triton = np.transpose(img_for_triton, (2, 0, 1))
    img_for_triton = np.expand_dims(img_for_triton, axis=0)

    boxes, _, masks, _, vis_img = self.segmentation_model.run_inference(
        img_for_triton,
        confidence_threshold=confidence_threshold,
        visibility_threshold=visibility_threshold,
        return_vis=True,
    )

    logging.info("Found %d objects from Segmentor!", len(boxes))
    return masks, vis_img

  def _run_pose_model(
      self,
      rgb_img: np.ndarray,
      depth_img: np.ndarray,
      intrinsic_matrix: np.ndarray,
      masks: list[np.ndarray] | np.ndarray,
      cad_bytes: bytes,
      num_iterations: int,
      return_vis: bool = False,
  ):
    if cad_bytes is None:
      raise ValueError(
          "CAD model bytes are None. Make sure the asset has a valid CAD model"
          " loaded before running pose estimation."
      )
    if len(masks) == 0:
      logging.info("No objects detected in scene")
      if return_vis:
        return [], rgb_img.copy()
      return []

    logging.info(
        "Running batched FoundationPose on %d detected masks...", len(masks)
    )
    masks_uint8 = np.stack(
        [(m > 0).astype(np.uint8) * 255 for m in masks], axis=0
    )
    inference_kwargs = {
        "rgb": rgb_img,
        "depth": depth_img,
        "mask": masks_uint8,
        "intrinsic_matrix": intrinsic_matrix,
        "cad_bytes": cad_bytes,
        "num_iterations": num_iterations,
        "batch_size": self._batch_size,
    }
    if return_vis:
      inference_kwargs["return_vis"] = True

    infer_res = self.pose_estimator_model.run_inference(**inference_kwargs)

    vis_img = None
    if return_vis:
      if len(infer_res) == 4:
        rot_batch, trans_batch, conf_batch, vis_img = infer_res
      else:
        rot_batch, trans_batch, conf_batch = infer_res
    else:
      rot_batch, trans_batch, conf_batch = infer_res[:3]

    predicted_poses = []
    for idx in range(len(rot_batch)):
      rot_mat = np.array(rot_batch[idx], dtype=np.float32).reshape(3, 3)
      trans_vec = np.array(trans_batch[idx], dtype=np.float32).flatten()
      conf_val = float(conf_batch[idx].item())

      pose_proto = _matrix_and_translation_to_pose_proto(rot_mat, trans_vec)
      predicted_poses.append((f"object_{idx}", conf_val, pose_proto))
      logging.info(
          "Estimated pose for object %d with score %.3f",
          idx,
          conf_val,
      )

    if return_vis:
      return predicted_poses, vis_img
    return predicted_poses

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

    try:
      cad_bytes, iterations, confidence_threshold, visibility_threshold = (
          self._ensure_asset_loaded(request.asset_id)
      )
    except ValueError as e:
      logging.error("Failed loading asset %s: %s", request.asset_id.name, e)
      if context is not None:
        context.abort(grpc.StatusCode.INVALID_ARGUMENT, str(e))
      raise e

    try:
      rgb_img, depth_img, intrinsic_matrix = _extract_rgb_depth_and_intrinsics(
          request, self.pubsub_
      )
    except ValueError as e:
      logging.error("Failed to extract sensor data from request: %s", e)
      if context is not None:
        context.abort(grpc.StatusCode.INVALID_ARGUMENT, str(e))
      raise e

    # run the segmentation model
    masks, vis_img = self._run_segmentation_model(
        rgb_img,
        confidence_threshold=confidence_threshold,
        visibility_threshold=visibility_threshold,
    )

    predicted_poses, pose_vis_img = self._run_pose_model(
        rgb_img=rgb_img,
        depth_img=depth_img,
        intrinsic_matrix=intrinsic_matrix,
        masks=masks,
        cad_bytes=cad_bytes,
        num_iterations=iterations,
        return_vis=True,
    )

    # save visualizations
    try:
      os.makedirs(_VISUALIZATION_DIR, exist_ok=True)
      if vis_img is not None:
        det_filepath = os.path.join(_VISUALIZATION_DIR, "detections.png")
        bgr_img = cv2.cvtColor(vis_img, cv2.COLOR_RGB2BGR)
        cv2.imwrite(det_filepath, bgr_img)
        logging.info("Saved detection visualization to %s", det_filepath)

      if pose_vis_img is not None:
        pose_filepath = os.path.join(_VISUALIZATION_DIR, "poses.png")
        bgr_pose = cv2.cvtColor(pose_vis_img, cv2.COLOR_RGB2BGR)
        cv2.imwrite(pose_filepath, bgr_pose)
        logging.info("Saved pose estimation visualization to %s", pose_filepath)
    except Exception as e:
      logging.warning("Failed to save visualization image to disk: %s", e)

    # publish visualizations
    try:
      if vis_img is not None:
        vis_img_proto = image_utils.serialize_image_buffer(
            vis_img, pixel_type=image_buffer_pb2.PixelType.PIXEL_INTENSITY
        )
        self._detection_image_pub.Publish(vis_img_proto)
        logging.info(
            "Published detection visualization to topic %s",
            _DETECTION_IMAGE_TOPIC,
        )

      if pose_vis_img is not None:
        pose_vis_img_proto = image_utils.serialize_image_buffer(
            pose_vis_img, pixel_type=image_buffer_pb2.PixelType.PIXEL_INTENSITY
        )
        self._pose_image_pub.Publish(pose_vis_img_proto)
        logging.info(
            "Published pose estimation visualization to topic %s",
            _POSE_IMAGE_TOPIC,
        )
    except Exception as e:
      logging.warning("Failed to publish visualization images: %s", e)

    response = v1_pose_estimation_service_pb2.RunPoseEstimationResponse()
    for _, conf_val, pose_proto in predicted_poses:
      pose_est = response.pose_estimates.add()
      pose_est.id = request.asset_id.name
      pose_est.score = conf_val
      pose_est.camera_t_target.CopyFrom(pose_proto)
    return response

  def DeletePoseEstimation(
      self,
      request: v1_pose_estimation_service_pb2.DeletePoseEstimationRequest,
      context: grpc.ServicerContext = None,
  ) -> Empty:
    logging.info("DeletePoseEstimation called")
    if hasattr(request, "asset_id") and request.asset_id.name:
      cache_key = (request.asset_id.package, request.asset_id.name)
      with self._lock:
        self._cad_cache.pop(cache_key, None)
        self._params_cache.pop(cache_key, None)

    return Empty()
