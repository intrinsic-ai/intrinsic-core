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

"""CLI tool to register and sideload IOC pose estimator models using the IOC Train Service."""

import time
from typing import Sequence

from absl import app
from absl import flags
from google.longrunning import operations_pb2
import grpc

from intrinsic_perception.intrinsic.perception.service.ioc_train_service.proto import ioc_pose_estimator_params_pb2
from intrinsic_perception.intrinsic.perception.service.ioc_train_service.service import ioc_train_service
from intrinsic.assets import id_utils
from intrinsic.assets.proto import id_pb2
from intrinsic.assets.proto import installed_assets_pb2
from intrinsic.assets.proto import installed_assets_pb2_grpc
from intrinsic.assets.proto import view_pb2
from intrinsic.perception.proto.v1 import pose_estimation_config_pb2
from intrinsic.perception.proto.v1 import pose_range_pb2
from intrinsic.perception.proto.v1 import target_pb2
from intrinsic.perception.proto.v1 import train_service_pb2
from intrinsic.perception.proto.v1 import train_service_pb2_grpc
from intrinsic.scene.proto.v1 import scene_object_pb2
from intrinsic.solutions import deployments

_VERSION = "0.0.1"

_ADDRESS = flags.DEFINE_string(
    "address",
    None,
    help="Direct cluster/ingress address (e.g. 'localhost:1234').",
)
_CLUSTER = flags.DEFINE_string(
    "cluster",
    None,
    help="Target cluster name.",
)
_ORG = flags.DEFINE_string(
    "org",
    None,
    help="Target organization name.",
)
_SCENE_OBJECT_ID = flags.DEFINE_string(
    "scene_object_id",
    None,
    help=(
        "Asset ID of the scene object (e.g., 'ai.intrinsic.new_object' or"
        " 'new_object')."
    ),
    required=True,
)
_POSE_ESTIMATOR_ID = flags.DEFINE_string(
    "pose_estimator_id",
    None,
    help=(
        "Asset ID of the pose estimator. Needs to be a valid id, that follows"
        " the format <package>.<name>, for example ai.intrinsic.pose_estimator"
    ),
    required=True,
)
_REFINEMENT_ITERS = flags.DEFINE_integer(
    "refinement_iters",
    3,
    help="Optional number of refinement iterations for FoundationPose.",
)
_CONFIDENCE_THRESHOLD = flags.DEFINE_float(
    "confidence_threshold",
    0.6,
    help="Optional confidence / detection score threshold for the detector.",
)
_VISIBILITY_THRESHOLD = flags.DEFINE_float(
    "visibility_threshold",
    0.6,
    help="Optional visibility threshold for detected objects.",
)
_TEXT_PROMPT = flags.DEFINE_string(
    "text_prompt",
    None,
    help="Optional text prompt for SAM 3 zero-shot instance segmentation.",
)

FLAGS = flags.FLAGS


def get_create_training_job_request(
    scene_object: scene_object_pb2.SceneObject,
    part_name: str,
    pose_estimator_id: str,
    refinement_iters: int | None = None,
    confidence_threshold: float | None = None,
    visibility_threshold: float | None = None,
    text_prompt: str | None = None,
    min_distance: float = 0.5,
    max_distance: float = 1.5,
) -> train_service_pb2.CreateTrainingJobRequest:
  """Returns a CreateTrainingJobRequest for a custom IOC pose estimator.

  Args:
    scene_object: SceneObject proto defining the target geometry.
    part_name: Target part identifier string.
    pose_estimator_id: Full ID of the pose estimator asset (e.g., 'package.name').
    refinement_iters: Optional number of refinement iterations for pose estimation.
    confidence_threshold: Optional minimum confidence / detection score threshold.
    visibility_threshold: Optional minimum visibility threshold.
    text_prompt: Optional text prompt for SAM 3 zero-shot segmentation.
    min_distance: Minimum camera distance in meters.
    max_distance: Maximum camera distance in meters.

  Returns:
    Configured CreateTrainingJobRequest proto.
  """
  pose_estimation_config = pose_estimation_config_pb2.PoseEstimationConfig()

  target = target_pb2.Target(
      id=part_name,
      scene_object=scene_object,
      pose_range=pose_range_pb2.PoseRange(
          min_distance=min_distance,
          max_distance=max_distance,
      ),
  )
  pose_estimation_config.targets.append(target)

  params = ioc_pose_estimator_params_pb2.IocPoseEstimatorParams()
  if refinement_iters is not None:
    params.refinement_iters = refinement_iters
  if confidence_threshold is not None:
    params.confidence_threshold = confidence_threshold
  if visibility_threshold is not None:
    params.visibility_threshold = visibility_threshold
  if text_prompt is not None:
    params.text_prompt = text_prompt

  pose_estimation_config.inference_params.Pack(params)

  package = id_utils.package_from(pose_estimator_id)
  name = id_utils.name_from(pose_estimator_id)

  create_training_job_request = train_service_pb2.CreateTrainingJobRequest(
      pose_estimation_config=pose_estimation_config,
      pose_estimator_type=train_service_pb2.PoseEstimatorType.POSE_ESTIMATOR_TYPE_UNSPECIFIED,
      asset_metadata=train_service_pb2.AssetMetadata(
          asset_name=name,
          id_version=id_pb2.IdVersion(
              id=id_pb2.Id(package=package, name=name), version=_VERSION
          ),
      ),
  )
  return create_training_job_request


def get_scene_object(
    installed_assets_stub: installed_assets_pb2_grpc.InstalledAssetsStub,
    package_name: str,
    scene_object_name: str,
) -> scene_object_pb2.SceneObject:
  """Returns the scene object for the given package and name.

  Args:
    installed_assets_stub: gRPC stub for the InstalledAssets service.
    package_name: Package name of the asset.
    scene_object_name: Name of the scene object asset.

  Returns:
    The unpacked SceneObject proto from the installed asset deployment data.
  """
  installed_asset = installed_assets_stub.GetInstalledAsset(
      installed_assets_pb2.GetInstalledAssetRequest(
          id=id_pb2.Id(
              package=package_name,
              name=scene_object_name,
          ),
          view=view_pb2.AssetViewType.ASSET_VIEW_TYPE_FULL,
      )
  )
  scene_object = (
      installed_asset.deployment_data.scene_object.manifest.assets.scene_object_model
  )
  return scene_object


def training_job_completed(
    training_job_name: str,
    train_service_stub: (
        train_service_pb2_grpc.TrainServiceStub
        | train_service_pb2_grpc.TrainServiceServicer
    ),
) -> bool:
  """Checks whether a training job operation has completed.

  Args:
    training_job_name: The operation name of the training job.
    train_service_stub: TrainService gRPC stub or servicer.

  Returns:
    True if the training job is done and succeeded, False if still running.

  Raises:
    ValueError: If the training job completed with an error.
  """
  operation_response = train_service_stub.GetTrainingJob(
      operations_pb2.GetOperationRequest(name=training_job_name)
  )
  if operation_response.done:
    if operation_response.HasField("error"):
      raise ValueError(
          f"[FAILED] - Training job {training_job_name} failed with error:"
          f" {operation_response.error}"
      )
    else:
      print(f"Training job {training_job_name} completed successfully.")
      return True
  return False


def save_pose_estimator(
    training_job_name: str,
    pose_estimator_name: str,
    package_name: str,
    train_service_stub: (
        train_service_pb2_grpc.TrainServiceStub
        | train_service_pb2_grpc.TrainServiceServicer
    ),
) -> train_service_pb2.SaveTrainingJobResponse:
  """Saves the pose estimator as an asset.

  Args:
    training_job_name: Operation name of the completed training job.
    pose_estimator_name: Name to assign to the saved pose estimator asset.
    package_name: Package name to assign to the saved pose estimator asset.
    train_service_stub: TrainService gRPC stub or servicer.

  Returns:
    The SaveTrainingJobResponse proto from the service.
  """
  save_training_job_request = train_service_pb2.SaveTrainingJobRequest(
      name=training_job_name,
      asset_id=id_pb2.Id(
          package=package_name,
          name=pose_estimator_name,
      ),
  )
  response = train_service_stub.SaveTrainingJob(save_training_job_request)
  print(f"Saved pose estimator {pose_estimator_name}.")
  return response


def delete_training_job(
    training_job_name: str,
    train_service_stub: (
        train_service_pb2_grpc.TrainServiceStub
        | train_service_pb2_grpc.TrainServiceServicer
    ),
) -> None:
  """Deletes a training job operation to clean up resources on the train service.

  Args:
    training_job_name: Operation name of the training job.
    train_service_stub: TrainService gRPC stub or servicer.
  """
  delete_training_job_request = operations_pb2.DeleteOperationRequest(
      name=training_job_name
  )
  train_service_stub.DeleteTrainingJob(delete_training_job_request)
  print(f"Deleted training job {training_job_name}.")


def main(argv: Sequence[str]) -> None:
  """Main CLI execution flow for registering a pose estimator using IOC train service.

  Args:
    argv: Command line arguments.
  """
  del argv

  address = _ADDRESS.value
  cluster_name = _CLUSTER.value
  org_name = _ORG.value

  if not id_utils.is_id(_POSE_ESTIMATOR_ID.value):
    raise ValueError("invalid pose estimator id passed")
  if not id_utils.is_id(_SCENE_OBJECT_ID.value):
    raise ValueError("invalid pose scene object id passed")

  package_name = id_utils.package_from(_POSE_ESTIMATOR_ID.value)
  pose_estimator_name = id_utils.name_from(_POSE_ESTIMATOR_ID.value)

  scene_object_package = id_utils.package_from(_SCENE_OBJECT_ID.value)
  scene_object_name = id_utils.name_from(_SCENE_OBJECT_ID.value)

  if address:
    if org_name or cluster_name:
      raise ValueError(
          "When --address is specified, --org and --cluster must not be set."
      )
    print(f"Connecting to address {address}...")
    solution = deployments.connect(address=address)
  elif org_name and cluster_name:
    print(f"Connecting to cluster {cluster_name} ({org_name})...")
    solution = deployments.connect(org=org_name, cluster=cluster_name)
  else:
    raise ValueError(
        "Either --address or both --org and --cluster must be provided."
    )
  print("Successfully connected to solutions SDK!")

  # 1. Connect to cluster for Assets
  installed_assets_stub = installed_assets_pb2_grpc.InstalledAssetsStub(
      solution.grpc_channel
  )

  # 2. Use IocTrainService to register and sideload the Pose Estimator into cluster assets
  train_service_stub = ioc_train_service.IocTrainService(
      assets_stub=installed_assets_stub
  )

  print(
      f"Fetching scene object metadata for {package_name}.{scene_object_name}"
      " from cluster..."
  )
  try:
    scene_object = get_scene_object(
        installed_assets_stub=installed_assets_stub,
        package_name=scene_object_package,
        scene_object_name=scene_object_name,
    )
  except grpc.RpcError as e:
    print(
        "Failed to fetch scene object"
        f" '{scene_object_package}.{scene_object_name}': {e}"
    )
    return

  print(
      "Submitting training job to generate Pose Estimator:"
      f" {pose_estimator_name} (iters: {_REFINEMENT_ITERS.value}, conf:"
      f" {_CONFIDENCE_THRESHOLD.value}, vis: {_VISIBILITY_THRESHOLD.value},"
      f" prompt: {_TEXT_PROMPT.value!r})"
  )
  create_training_job_request = get_create_training_job_request(
      scene_object=scene_object,
      part_name=scene_object_name,
      pose_estimator_id=_POSE_ESTIMATOR_ID.value,
      refinement_iters=_REFINEMENT_ITERS.value,
      confidence_threshold=_CONFIDENCE_THRESHOLD.value,
      visibility_threshold=_VISIBILITY_THRESHOLD.value,
      text_prompt=_TEXT_PROMPT.value,
  )

  try:
    create_job_response = train_service_stub.CreateTrainingJob(
        create_training_job_request
    )
    print(f"Training Job started! Operation ID: {create_job_response.name}")
    if create_job_response.name.startswith("operations/ioc-train-"):
      print("[SUCCESS] Verified intercepted by our running IOC TrainService!")
    else:
      print(
          "[WARNING] Training job operation name of unknown format (Operation:"
          f" {create_job_response.name}). Are you talking to the right train"
          " service?"
      )
  except grpc.RpcError as e:
    print(f"Failed to start training: {e}")
    return

  print("Polling for training completion...")
  while True:
    try:
      is_done = training_job_completed(
          create_job_response.name, train_service_stub
      )
      if is_done:
        break
    except grpc.RpcError as e:
      print(f"Failed getting job status: {e}")
    time.sleep(1.0)

  print("Training finished! Triggering saving routine on ioc_train_service...")
  save_pose_estimator(
      training_job_name=create_job_response.name,
      pose_estimator_name=pose_estimator_name,
      package_name=package_name,
      train_service_stub=train_service_stub,
  )

  print("Cleaning up training job operation...")
  delete_training_job(
      training_job_name=create_job_response.name,
      train_service_stub=train_service_stub,
  )
  print(f"Complete! Pose estimator {package_name}.{pose_estimator_name} saved.")


if __name__ == "__main__":
  app.run(main)
