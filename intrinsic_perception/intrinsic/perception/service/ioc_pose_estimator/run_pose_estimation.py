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

"""Solution builder script to set up and run the IOC pose estimation pipeline.

Connects to a running Intrinsic solution, sets up the `capture_images` and
`estimate_pose_multi_view` skills connected to the `ioc_pose_estimator_service`,
executes the pose estimation process on the solution executive, and retrieves and
displays the estimated 6D object poses.
"""

from typing import Any
from typing import Sequence

from absl import app
from absl import flags
import grpc

from intrinsic.assets import id_utils
from intrinsic.math.python import proto_conversion
from intrinsic.perception.proto.v1 import pose_estimator_id_pb2
from intrinsic.perception.skills.multi_view import estimate_pose_multi_view_pb2
from intrinsic.solutions import deployments
from intrinsic.solutions import execution
from intrinsic.solutions import provided

# Global default configuration values
_DEFAULT_POSE_ESTIMATOR_ID: str = "ai.intrinsic.review_validation"
_DEFAULT_CAMERA_NAME: str = "orbbec_gemini_335le"
_DEFAULT_SERVICE_NAME: str = "ioc_pose_estimator_service"
_DEFAULT_SENSOR_IDS: Sequence[int] = (1, 4)
_DEFAULT_MIN_NUM_INSTANCES: int = 1

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
_POSE_ESTIMATOR_ID = flags.DEFINE_string(
    "pose_estimator_id",
    _DEFAULT_POSE_ESTIMATOR_ID,
    help=(
        "Asset ID of the pose estimator data asset. Needs to be a valid ID "
        "following the format <package>.<name>, for example "
        "'ai.intrinsic.my_pose_estimator'."
    ),
)
_CAMERA_NAME = flags.DEFINE_string(
    "camera_name",
    None,
    help=(
        "Name of the camera resource to use (defaults to"
        f" '{_DEFAULT_CAMERA_NAME}' or auto-detected from solution resources)."
    ),
)
_SERVICE_NAME = flags.DEFINE_string(
    "service_name",
    _DEFAULT_SERVICE_NAME,
    help=(
        "Name of the IOC pose estimator service resource "
        f"(defaults to '{_DEFAULT_SERVICE_NAME}')."
    ),
)
_SENSOR_IDS = flags.DEFINE_list(
    "sensor_ids",
    ["1", "4"],
    help=(
        "List of sensor IDs to capture from the camera (defaults to ['1', '4'] "
        "for RGB and Depth on Orbbec Gemini 335LE)."
    ),
)
_MIN_NUM_INSTANCES = flags.DEFINE_integer(
    "min_num_instances",
    _DEFAULT_MIN_NUM_INSTANCES,
    help=(
        "Minimum number of detected object instances required (skill fails if "
        "fewer instances are detected)."
    ),
)
_LOG_DEBUG_DATA = flags.DEFINE_bool(
    "log_debug_data",
    True,
    help="Whether to log debug data and images during skill execution.",
)
_TIMEOUT_SEC = flags.DEFINE_integer(
    "timeout_sec",
    None,
    help="Optional inference timeout in seconds.",
)

FLAGS = flags.FLAGS


def _has_resource(solution: deployments.Solution, name: str) -> bool:
  """Checks if a resource exists in the solution resources."""
  if isinstance(solution.resources, dict):
    return name in solution.resources
  try:
    _ = solution.resources[name]
    return True
  except (KeyError, AttributeError, TypeError):
    return False


def _list_resource_names(solution: deployments.Solution) -> list[str]:
  """Helper to list available resource names from solution resources."""
  if isinstance(solution.resources, dict):
    return list(solution.resources.keys())
  if hasattr(solution.resources, "__dir__"):
    return [
        name for name in dir(solution.resources) if not name.startswith("_")
    ]
  return [getattr(r, "name", str(r)) for r in solution.resources]


def _list_resource_handles(
    solution: deployments.Solution,
) -> list[provided.ResourceHandle]:
  """Helper to list all ResourceHandle objects from solution resources."""
  if isinstance(solution.resources, dict):
    return list(solution.resources.values())
  return list(solution.resources)


def get_camera_resource(
    solution: deployments.Solution,
    camera_name: str | None = None,
) -> provided.ResourceHandle:
  """Retrieves the camera resource handle from the solution.

  Args:
    solution: The connected Solution instance.
    camera_name: Optional explicit camera resource name.

  Returns:
    The matching ResourceHandle.

  Raises:
    ValueError: If no suitable camera resource can be found or resolved.
  """
  if camera_name:
    if not _has_resource(solution, camera_name):
      available = _list_resource_names(solution)
      raise ValueError(
          f"Camera resource '{camera_name}' not found in solution. "
          f"Available resources: {available}"
      )
    return solution.resources[camera_name]

  # Check default camera name
  if _has_resource(solution, _DEFAULT_CAMERA_NAME):
    return solution.resources[_DEFAULT_CAMERA_NAME]

  # Auto-detect resource with CameraConfig capability
  handles = _list_resource_handles(solution)
  camera_resources = [
      handle
      for handle in handles
      if hasattr(handle, "types") and "CameraConfig" in handle.types
  ]
  if len(camera_resources) == 1:
    return camera_resources[0]
  elif len(camera_resources) > 1:
    names = [r.name for r in camera_resources]
    raise ValueError(
        f"Multiple camera resources found: {names}. "
        "Please specify which camera to use via --camera_name."
    )
  else:
    raise ValueError(
        "No camera resource with 'CameraConfig' capability found in the"
        " solution."
    )


def get_perception_service_resource(
    solution: deployments.Solution,
    service_name: str | None = None,
) -> provided.ResourceHandle:
  """Retrieves the IOC pose estimator service resource handle from the solution.

  Args:
    solution: The connected Solution instance.
    service_name: Optional explicit service resource name.

  Returns:
    The matching ResourceHandle.

  Raises:
    ValueError: If no suitable perception service resource can be found.
  """
  if service_name:
    if not _has_resource(solution, service_name):
      available = _list_resource_names(solution)
      raise ValueError(
          f"Perception service resource '{service_name}' not found in solution."
          f" Available resources: {available}"
      )
    return solution.resources[service_name]

  # Check default service name
  if _has_resource(solution, _DEFAULT_SERVICE_NAME):
    return solution.resources[_DEFAULT_SERVICE_NAME]

  # Auto-detect resource with PoseEstimationService capability
  handles = _list_resource_handles(solution)
  service_resources = [
      handle
      for handle in handles
      if hasattr(handle, "types")
      and "intrinsic_proto.perception.v1.PoseEstimationService" in handle.types
  ]
  if len(service_resources) == 1:
    return service_resources[0]
  elif len(service_resources) > 1:
    names = [r.name for r in service_resources]
    raise ValueError(
        f"Multiple perception service resources found: {names}. "
        "Please specify which service to use via --service_name."
    )
  else:
    raise ValueError(
        "No service resource with 'PoseEstimationService' capability found in"
        " the solution."
    )


def create_pose_estimation_pipeline(
    solution: deployments.Solution,
    camera_resource: provided.ResourceHandle,
    perception_resource: provided.ResourceHandle,
    pose_estimator_id: str,
    sensor_ids: Sequence[int] | None = _DEFAULT_SENSOR_IDS,
    min_num_instances: int = _DEFAULT_MIN_NUM_INSTANCES,
    log_debug_data: bool = True,
    timeout_sec: int | None = None,
) -> tuple[Any, Any]:
  """Constructs the capture_images and estimate_pose_multi_view skills for the pipeline.

  Args:
    solution: Connected Solution instance.
    camera_resource: ResourceHandle for the RGB-D camera.
    perception_resource: ResourceHandle for the IOC Pose Estimator Service.
    pose_estimator_id: Full ID of the pose estimator asset (e.g.
      'ai.intrinsic.my_pose_estimator').
    sensor_ids: Optional list of sensor IDs to capture from the camera (e.g. [1, 4] for RGB-D).
    min_num_instances: Minimum number of detected instances required.
    log_debug_data: Whether to enable debug data logging in the skills.
    timeout_sec: Optional inference timeout in seconds.

  Returns:
    A tuple of (capture_images_skill, estimate_pose_multi_view_skill).

  Raises:
    ValueError: If pose_estimator_id is not a valid Intrinsic ID format.
  """
  if not id_utils.is_id(pose_estimator_id):
    raise ValueError(
        f"Invalid pose estimator ID '{pose_estimator_id}'. Expected format:"
        " <package>.<name> (e.g. 'ai.intrinsic.my_pose_estimator')"
    )

  package_name = id_utils.package_from(pose_estimator_id)
  name = id_utils.name_from(pose_estimator_id)

  skills = solution.skills

  # 1. Set up capture_images skill with sensor_ids (e.g. 1 for RGB and 4 for Depth)
  capture_images_kwargs: dict[str, Any] = {
      "camera": camera_resource,
      "log_debug_data": log_debug_data,
  }
  if sensor_ids is not None:
    capture_images_kwargs["sensor_ids"] = [int(sid) for sid in sensor_ids]

  capture_images_skill = skills.ai.intrinsic.capture_images(
      **capture_images_kwargs
  )

  pose_estimator_proto = pose_estimator_id_pb2.PoseEstimatorId(
      id=name,
      package=package_name,
  )

  # 2. Set up estimate_pose_multi_view skill connected to IOC Pose Estimator Service
  estimate_pose_kwargs: dict[str, Any] = {
      "perception": perception_resource,
      "pose_estimator": pose_estimator_proto,
      "capture_data": [capture_images_skill.result.capture_data],
      "min_num_instances": min_num_instances,
      "log_debug_data": log_debug_data,
  }
  if timeout_sec is not None and timeout_sec > 0:
    estimate_pose_kwargs["inference_timeout_sec"] = timeout_sec

  estimate_pose_skill = skills.ai.intrinsic.estimate_pose_multi_view(
      **estimate_pose_kwargs
  )

  return capture_images_skill, estimate_pose_skill


def run_pose_estimation_pipeline(
    solution: deployments.Solution,
    capture_images_skill: Any,
    estimate_pose_skill: Any,
) -> estimate_pose_multi_view_pb2.EstimatePoseMultiViewResult:
  """Executes the pipeline on the solution executive and returns the results.

  Args:
    solution: Connected Solution instance.
    capture_images_skill: Instantiated CaptureImages skill.
    estimate_pose_skill: Instantiated EstimatePoseMultiView skill.

  Returns:
    EstimatePoseMultiViewResult proto message containing detections and poses.
  """
  print("Executing pose estimation pipeline on executive...")
  skills_to_run = [capture_images_skill, estimate_pose_skill]
  solution.executive.run(skills_to_run)
  print("Pipeline execution finished successfully.")

  result: estimate_pose_multi_view_pb2.EstimatePoseMultiViewResult = (
      solution.executive.get_value(estimate_pose_skill.result)
  )
  return result


def display_results(
    result: estimate_pose_multi_view_pb2.EstimatePoseMultiViewResult,
) -> None:
  """Prints formatted results of the pose estimation.

  Args:
    result: EstimatePoseMultiViewResult message from skill execution.
  """
  num_estimates = len(result.estimates)
  print("\n================ Pose Estimation Results ================")
  print(f"Total detections: {num_estimates}")

  if num_estimates == 0:
    print("No object poses were detected.")
    return

  for idx, estimate in enumerate(result.estimates, start=1):
    pose = proto_conversion.pose_from_proto(estimate.root_t_target)
    trans = pose.translation
    rot = pose.rotation
    quat = rot.quaternion
    rpy_deg = rot.euler_angles(radians=False)

    print(f"\n--- Detection #{idx} ---")
    print(f"  Object ID:   {estimate.id}")
    print(f"  Score:       {estimate.score:.4f}")
    print(f"  Translation (x, y, z) [m]:")
    print(f"    [{trans[0]:.4f}, {trans[1]:.4f}, {trans[2]:.4f}]")
    print(f"  Rotation (quaternion [x, y, z, w]):")
    print(f"    [{quat.x:.4f}, {quat.y:.4f}, {quat.z:.4f}, {quat.w:.4f}]")
    print(f"  Rotation (RPY Euler angles [deg]):")
    print(
        f"    [roll: {rpy_deg[0]:.2f}°, pitch: {rpy_deg[1]:.2f}°, yaw:"
        f" {rpy_deg[2]:.2f}°]"
    )


def main(argv: Sequence[str]) -> None:
  """Main CLI execution flow for setting up and running pose estimation.

  Args:
    argv: Command line arguments.
  """
  del argv

  address = _ADDRESS.value
  cluster_name = _CLUSTER.value
  org_name = _ORG.value

  pose_estimator_id = _POSE_ESTIMATOR_ID.value or _DEFAULT_POSE_ESTIMATOR_ID
  if not id_utils.is_id(pose_estimator_id):
    raise ValueError(
        f"Invalid pose estimator ID '{pose_estimator_id}'. "
        "Needs to be a valid ID following format <package>.<name> (e.g."
        " 'ai.intrinsic.my_pose_estimator')"
    )

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

  try:
    camera_resource = get_camera_resource(solution, _CAMERA_NAME.value)
    perception_resource = get_perception_service_resource(
        solution, _SERVICE_NAME.value
    )
  except ValueError as e:
    print(f"Resource resolution error: {e}")
    return

  sensor_ids = (
      [int(s.strip()) for s in _SENSOR_IDS.value if s.strip()]
      if _SENSOR_IDS.value
      else list(_DEFAULT_SENSOR_IDS)
  )

  print(f"Using camera resource: {camera_resource.name}")
  print(f"Using sensor IDs: {sensor_ids}")
  print(f"Using perception service resource: {perception_resource.name}")
  print(f"Using pose estimator ID: {pose_estimator_id}")

  try:
    capture_skill, estimate_skill = create_pose_estimation_pipeline(
        solution=solution,
        camera_resource=camera_resource,
        perception_resource=perception_resource,
        pose_estimator_id=pose_estimator_id,
        sensor_ids=sensor_ids,
        min_num_instances=_MIN_NUM_INSTANCES.value,
        log_debug_data=_LOG_DEBUG_DATA.value,
        timeout_sec=_TIMEOUT_SEC.value,
    )
  except ValueError as e:
    print(f"Failed to create pose estimation pipeline: {e}")
    return

  try:
    result = run_pose_estimation_pipeline(
        solution=solution,
        capture_images_skill=capture_skill,
        estimate_pose_skill=estimate_skill,
    )
  except execution.ExecutionFailedError as e:
    print(f"Pipeline execution failed: {e}")
    return
  except grpc.RpcError as e:
    print(f"gRPC error during execution: {e}")
    return

  display_results(result)


if __name__ == "__main__":
  app.run(main)
