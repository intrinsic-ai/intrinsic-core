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

"""gRPC TrainService implementation for IOC pose estimator training and asset creation."""

import collections
import logging
import uuid

from google.longrunning import operations_pb2
from google.protobuf import any_pb2
from google.protobuf import empty_pb2
import grpc
import numpy as np

from intrinsic_perception.intrinsic.perception.service.ioc_train_service.proto import ioc_pose_estimator_params_pb2
from intrinsic_perception.intrinsic.perception.service.ioc_train_service.proto import ioc_train_service_config_pb2
from intrinsic.assets import interface_utils
from intrinsic.assets.data.proto.v1 import data_asset_pb2
from intrinsic.assets.data.proto.v1 import referenced_data_pb2
from intrinsic.assets.dependencies import utils as asset_utils
from intrinsic.assets.proto import installed_assets_pb2
from intrinsic.assets.proto import installed_assets_pb2_grpc
from intrinsic.assets.proto import metadata_pb2
from intrinsic.math.python import ros_proto_conversion
from intrinsic.perception.proto.v1 import perception_model_pb2
from intrinsic.perception.proto.v1 import train_service_pb2
from intrinsic.perception.proto.v1 import train_service_pb2_grpc
from intrinsic.scene.proto.v1 import scene_object_pb2
from intrinsic.util.proto import descriptors

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


def _extract_cas_and_transform_from_scene_object(
    scene_object: scene_object_pb2.SceneObject,
) -> tuple[str, np.ndarray]:
  """Extracts the CAS URI and root transform from a SceneObject.

  Inspects entity links and geometry components in the modern SceneObject schema
  to locate the CAS mesh reference URI (`intcas://...`) and calculate the composed
  transform from root to shape.

  Args:
    scene_object: The SceneObject protobuf to extract CAS URI and transform from.

  Returns:
    A tuple of (cas_uri, root_t_shape) where cas_uri is the CAS URI string (or empty
    string if not found), and root_t_shape is the 4x4 float64 transformation matrix.
  """
  cas_uri = ""
  root_t_shape = np.eye(4, dtype=np.float64)

  if not scene_object.entities:
    return cas_uri, root_t_shape

  if len(scene_object.entities) > 1:
    raise ValueError(
        "Training service only supports scene objects consisting of a single"
        f" mesh, found {len(scene_object.entities)}",
    )
  entity = scene_object.entities[0]

  if entity.HasField("link") and entity.link.HasField("geometry_component"):
    gc = entity.link.geometry_component

    root_t_entity = np.eye(4, dtype=np.float64)
    if entity.HasField("parent_t_this"):
      root_t_entity = ros_proto_conversion.pose_from_proto(
          entity.parent_t_this, normalize_quaternion=True
      ).matrix4x4()

    ref_t_shape = np.eye(4, dtype=np.float64)

    for geom_set in gc.named_geometries.values():
      for transformed_geom in geom_set.named_geometries.values():
        if transformed_geom.geometry.HasField("geo_ref"):
          refs = transformed_geom.geometry.geo_ref
          if refs.renderable_ref.startswith("intcas://"):
            cas_uri = refs.renderable_ref
          elif refs.exact_geometry_ref.startswith("intcas://"):
            cas_uri = refs.exact_geometry_ref

        if transformed_geom.ref_t_shape.HasField("matrix4d"):
          vals = list(transformed_geom.ref_t_shape.matrix4d.values)
          if len(vals) == 16:
            ref_t_shape = np.array(vals, dtype=np.float64).reshape(
                (4, 4), order="F"
            )

        if cas_uri:
          break

      if cas_uri:
        break

    root_t_shape = root_t_entity @ ref_t_shape
    if cas_uri:
      return cas_uri, root_t_shape

  return cas_uri, root_t_shape


class IocTrainService(train_service_pb2_grpc.TrainServiceServicer):
  """TrainService that creates a Pose Estimator from a Scene Object."""

  def __init__(
      self,
      config: ioc_train_service_config_pb2.IocTrainServiceConfig | None = None,
      assets_stub: installed_assets_pb2_grpc.InstalledAssetsStub | None = None,
      max_pending_jobs: int = 100,
  ) -> None:
    """
    Args:
      config: Optional IocTrainServiceConfig containing resolved dependencies.
      assets_stub: Optional gRPC stub for InstalledAssets service. If not
        provided, connects via `config.intrinsic_runtime`.
      max_pending_jobs: Maximum number of pending jobs to retain in the LRU cache.
    """
    logging.info("Initializing IocTrainService...")
    self._config = (
        config or ioc_train_service_config_pb2.IocTrainServiceConfig()
    )
    self._max_pending_jobs = max_pending_jobs
    self._pending_jobs: collections.OrderedDict[
        str, perception_model_pb2.PerceptionModel
    ] = collections.OrderedDict()
    if assets_stub is not None:
      self.assets_stub = assets_stub
    else:
      channel = asset_utils.connect_with_runtime_asset_fallback_for_asset_migration_only(
          self._config.intrinsic_runtime,
          _installed_assets_interface_uri(),
          grpc_options=_GRPC_OPTIONS,
      )
      self.assets_stub = installed_assets_pb2_grpc.InstalledAssetsStub(channel)

  def _validate_request(
      self, request: train_service_pb2.CreateTrainingJobRequest
  ) -> None:
    if request.pose_estimator_type not in {
        train_service_pb2.POSE_ESTIMATOR_TYPE_UNSPECIFIED,
        train_service_pb2.POSE_ESTIMATOR_TYPE_ONE_SHOT,
    }:
      type_name = train_service_pb2.PoseEstimatorType.Name(
          request.pose_estimator_type
      )
      raise ValueError(
          f"Unsupported pose estimator type: {type_name}. IocTrainService only"
          " supports custom IOC pose estimators"
          " (POSE_ESTIMATOR_TYPE_ONE_SHOT and"
          " POSE_ESTIMATOR_TYPE_UNSPECIFIED)."
      )

    if not request.pose_estimation_config.HasField("inference_params"):
      raise ValueError(
          "CreateTrainingJobRequest must include inference_params in"
          " pose_estimation_config."
      )
    ioc_params = ioc_pose_estimator_params_pb2.IocPoseEstimatorParams()
    if not request.pose_estimation_config.inference_params.Unpack(ioc_params):
      raise ValueError(
          "inference_params must contain an IocPoseEstimatorParams message."
          " Found type_url:"
          f" {request.pose_estimation_config.inference_params.type_url}"
      )

    if not request.pose_estimation_config.targets:
      raise ValueError(
          "CreateTrainingJobRequest must contain at least one target in"
          " pose_estimation_config.targets."
      )

  def CreateTrainingJob(
      self,
      request: train_service_pb2.CreateTrainingJobRequest,
      context: grpc.ServicerContext | None = None,
  ) -> operations_pb2.Operation:
    """Creates a training job operation for pose estimation.

    Performs heavy processing: extracts CAS references, transforms, and builds
    the PerceptionModel from incoming scene objects.

    Args:
      request: The request containing pose estimation configuration and metadata.
      context: Optional gRPC servicer context.

    Returns:
      A long-running Operation protobuf representing the queued training job.
    """
    del context
    if not request.asset_metadata.asset_name:
      raise ValueError("please provide an asset name")
    asset_name = request.asset_metadata.asset_name

    logging.info(
        "CreateTrainingJob called for asset: %s (type: %s)",
        asset_name,
        train_service_pb2.PoseEstimatorType.Name(request.pose_estimator_type),
    )
    self._validate_request(request)

    op_name = f"operations/ioc-train-{uuid.uuid4().hex[:8]}"

    perception_model = perception_model_pb2.PerceptionModel()

    for req_target in request.pose_estimation_config.targets:
      target_id = req_target.id or asset_name
      mesh_key = f"/poseEstimators/{asset_name}/{target_id}.glb"
      cas_uri = ""

      if req_target.HasField("scene_object"):
        cas_uri, _ = _extract_cas_and_transform_from_scene_object(
            req_target.scene_object
        )

      if not cas_uri:
        raise ValueError(
            "No valid CAS geometry reference found in scene object for target"
            f" '{target_id}'. Cannot create pose estimator without a valid CAS"
            " mesh."
        )

      # Match official train service: use target_t_mesh if specified in request, otherwise Identity
      final_target_t_mesh = np.eye(4, dtype=np.float64)
      if req_target.HasField("mesh") and req_target.mesh.HasField(
          "target_t_mesh"
      ):
        ttm = req_target.mesh.target_t_mesh
        if len(ttm.linear.values) >= 9:
          final_target_t_mesh[:3, :3] = np.array(
              ttm.linear.values[:9], dtype=np.float64
          ).reshape((3, 3), order="F")
        final_target_t_mesh[0, 3] = ttm.translation.x
        final_target_t_mesh[1, 3] = ttm.translation.y
        final_target_t_mesh[2, 3] = ttm.translation.z

      ref_data = referenced_data_pb2.ReferencedData()
      ref_data.reference = cas_uri
      perception_model.data_references[mesh_key].CopyFrom(ref_data)

      target_entry = perception_model.pose_estimation_config.targets.add()
      target_entry.id = target_id
      target_entry.mesh.filename = mesh_key
      target_entry.mesh.target_t_mesh.linear.rows = 3
      target_entry.mesh.target_t_mesh.linear.cols = 3
      target_entry.mesh.target_t_mesh.linear.values.extend(
          final_target_t_mesh[:3, :3].flatten(order="F").tolist()
      )
      target_entry.mesh.target_t_mesh.translation.x = float(
          final_target_t_mesh[0, 3]
      )
      target_entry.mesh.target_t_mesh.translation.y = float(
          final_target_t_mesh[1, 3]
      )
      target_entry.mesh.target_t_mesh.translation.z = float(
          final_target_t_mesh[2, 3]
      )
      logging.info(
          "Linked real CAD target '%s' -> mesh '%s' (%s) with"
          " target_t_mesh:\n%s",
          target_id,
          mesh_key,
          cas_uri,
          final_target_t_mesh,
      )

    if request.pose_estimation_config.HasField("inference_params"):
      perception_model.pose_estimation_config.inference_params.CopyFrom(
          request.pose_estimation_config.inference_params
      )
    if request.pose_estimation_config.HasField("params"):
      perception_model.pose_estimation_config.params.CopyFrom(
          request.pose_estimation_config.params
      )

    self._pending_jobs[op_name] = perception_model
    self._pending_jobs.move_to_end(op_name)
    if len(self._pending_jobs) > self._max_pending_jobs:
      evicted_name, _ = self._pending_jobs.popitem(last=False)
      logging.info(
          "LRU cache full: evicted oldest pending job %s", evicted_name
      )

    op = operations_pb2.Operation(name=op_name, done=False)
    return op

  def GetTrainingJob(
      self,
      request: operations_pb2.GetOperationRequest,
      context: grpc.ServicerContext | None = None,
  ) -> operations_pb2.Operation:
    """Retrieves the status of a training job operation.

    Args:
      request: The request containing the operation name to poll.
      context: Optional gRPC servicer context.

    Returns:
      An Operation protobuf with status indicating job progress and completion.
    """
    del context
    logging.info(
        "GetTrainingJob called for %s",
        request.name,
    )
    op = operations_pb2.Operation(name=request.name, done=True)
    status = train_service_pb2.TrainingJobStatus(progress=1.0)
    any_meta = any_pb2.Any()
    any_meta.Pack(status)
    op.metadata.CopyFrom(any_meta)
    return op

  def SaveTrainingJob(
      self,
      request: train_service_pb2.SaveTrainingJobRequest,
      context: grpc.ServicerContext | None = None,
  ) -> train_service_pb2.SaveTrainingJobResponse:
    """Saves a completed training job as an installed DataAsset.

    Performs lightweight storage without reprocessing geometry transforms.

    Args:
      request: The request specifying the operation name and target asset ID.
      context: Optional gRPC servicer context.

    Returns:
      A SaveTrainingJobResponse containing the IdVersion of the saved asset.

    Raises:
      ValueError: If no targets are found in the training job or asset_id is invalid.
      Exception: If the asset upload to the assets service fails.
    """
    del context
    logging.info(
        "SaveTrainingJob called for %s -> creating DataAsset with real Scene"
        " Object CAD CAS references...",
        request.asset_id.name
        if request.HasField("asset_id")
        else "<unspecified>",
    )

    if not request.HasField("asset_id") or not request.asset_id.name:
      raise ValueError(
          "SaveTrainingJobRequest must provide an asset_id with a non-empty"
          " name."
      )
    # Default package to "ai.intrinsic" if omitted to conform with comment in
    # the SaveTrainingJobRequest proto
    if not request.asset_id.package:
      request.asset_id.package = "ai.intrinsic"

    perception_model = self._pending_jobs.get(request.name)

    if (
        not perception_model
        or not perception_model.pose_estimation_config.targets
    ):
      raise ValueError(
          "No targets found in training job request for"
          f" {request.asset_id.name}"
      )

    self._pending_jobs.move_to_end(request.name)

    meta = metadata_pb2.Metadata()
    meta.asset_type = 5  # ASSET_TYPE_DATA
    meta.id_version.id.CopyFrom(request.asset_id)
    meta.display_name = request.asset_id.name
    meta.vendor.display_name = "Intrinsic"

    data_asset = data_asset_pb2.DataAsset()
    data_asset.metadata.CopyFrom(meta)
    data_asset.data.Pack(perception_model)
    data_asset.file_descriptor_set.CopyFrom(
        descriptors.gen_file_descriptor_set(
            perception_model_pb2.PerceptionModel.DESCRIPTOR
        )
    )

    create_req = installed_assets_pb2.CreateInstalledAssetRequest(
        asset=installed_assets_pb2.CreateInstalledAssetRequest.Asset(
            data=data_asset
        ),
        policy=installed_assets_pb2.UpdatePolicy.UPDATE_POLICY_ADD_NEW_ONLY,
    )
    logging.info("Uploading saved pose estimator to live asset registry...")
    try:
      self.assets_stub.CreateInstalledAsset(create_req)
      logging.info(
          "Successfully uploaded pose estimator asset: %s.%s",
          request.asset_id.package,
          request.asset_id.name,
      )
    except grpc.RpcError as e:
      code = e.code() if hasattr(e, "code") else "UNKNOWN"
      details = e.details() if hasattr(e, "details") else str(e)
      logging.error(
          "Failed to upload saved pose estimator asset to cluster (gRPC"
          " %s): %s",
          code,
          details,
      )
      raise
    except Exception as e:
      logging.error("Failed to upload saved pose estimator asset: %s", e)
      raise

    return train_service_pb2.SaveTrainingJobResponse(id_version=meta.id_version)

  def DeleteTrainingJob(
      self,
      request: operations_pb2.DeleteOperationRequest,
      context: grpc.ServicerContext | None = None,
  ) -> empty_pb2.Empty:
    """Deletes a training job operation and frees cached resources.

    Args:
      request: DeleteOperationRequest with the operation name to delete.
      context: Optional gRPC servicer context.

    Returns:
      An Empty protobuf message.
    """
    del context
    logging.info("DeleteTrainingJob called for %s", request.name)
    if not request.name:
      raise ValueError("DeleteOperationRequest must specify a non-empty name.")

    removed = self._pending_jobs.pop(request.name, None)
    if removed is not None:
      logging.info("Deleted pending training job: %s", request.name)
    else:
      logging.warning(
          "DeleteTrainingJob: Job %s not found in pending cache", request.name
      )

    return empty_pb2.Empty()
