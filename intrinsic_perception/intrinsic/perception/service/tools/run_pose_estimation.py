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

"""Solution builder script to set up and run the pose estimation pipeline.

Connects to a running Intrinsic solution, sets up the `capture_images` and
`estimate_pose_multi_view` skills connected to the pose estimator service,
executes the pose estimation process on the solution executive, and retrieves
and displays the estimated 6D object poses.

NOTE: `ioc_pose_estimator_service` is currently unimplemented and calling
`RunPoseEstimation` on it always raises an `UNIMPLEMENTED` error.
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
_POSE_ESTIMATOR_SERVICES: Sequence[str] = (
    "ioc_pose_estimator_service",
    "nvidia_pose_estimator_service",
)
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
_CAMERAS = flags.DEFINE_multi_string(
    "cameras",
    [f"{_DEFAULT_CAMERA_NAME}:{','.join(str(s) for s in _DEFAULT_SENSOR_IDS)}"],
    help=(
        "Camera resource name and comma-separated sensor IDs in the format "
        "'<camera_name>:<sensor_id1>[,<sensor_id2>,...]' (defaults to "
        "'orbbec_gemini_335le:1,4' for RGB and Depth on Orbbec Gemini 335LE). "
        "Can be specified multiple times for multi-view capture (e.g. "
        "--cameras='cam_1:1,4' --cameras='cam_2:2')."
    ),
)
_SERVICE_NAME = flags.DEFINE_enum(
    "service_name",
    None,
    _POSE_ESTIMATOR_SERVICES,
    help=(
        "Name of the pose estimator service resource to use (or auto-detected "
        "from solution resources if omitted). Note that "
        "'ioc_pose_estimator_service' is currently unimplemented."
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
    camera_name: str,
) -> provided.ResourceHandle:
  """Retrieves the camera resource handle from the solution.

  Args:
    solution: The connected Solution instance.
    camera_name: Camera resource name.

  Returns:
    The matching ResourceHandle.

  Raises:
    ValueError: If camera_name is empty or cannot be found in the solution.
  """
  if not camera_name:
    raise ValueError("Camera resource name must be provided.")
  if not _has_resource(solution, camera_name):
    available = _list_resource_names(solution)
    raise ValueError(
        f"Camera resource '{camera_name}' not found in solution. "
        f"Available resources: {available}"
    )
  return solution.resources[camera_name]


def get_perception_service_resource(
    solution: deployments.Solution,
    service_name: str | None = None,
) -> provided.ResourceHandle:
  """Retrieves the pose estimator service resource handle from the solution.

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


def parse_cameras(
    cameras_flag: Sequence[str] | None,
) -> dict[str, list[int]]:
  """Parses the --cameras flag into a mapping of camera name to sensor IDs.

  Each entry must have the format '<camera_name>:<sensor_id1>[,<sensor_id2>,...]'.

  Args:
    cameras_flag: Sequence of camera specification strings from --cameras.

  Returns:
    An ordered dict mapping each camera resource name to its list of sensor IDs.

  Raises:
    ValueError: If cameras_flag is empty, an entry is malformed, a camera name
      is empty or duplicated, or sensor IDs after ':' are empty/non-integer.
  """
  if not cameras_flag:
    raise ValueError("At least one camera must be specified via --cameras.")

  cameras: dict[str, list[int]] = {}
  for entry in cameras_flag:
    entry = entry.strip()
    if not entry:
      raise ValueError("Camera specification in --cameras must not be empty.")

    if ":" not in entry:
      raise ValueError(
          f"Invalid camera specification '{entry}'. Expected format:"
          " '<camera_name>:<sensor_id1>[,<sensor_id2>,...]'."
      )

    camera_name, sensor_ids_str = entry.split(":", 1)
    camera_name = camera_name.strip()
    if not camera_name:
      raise ValueError(f"Camera resource name must be provided in '{entry}'.")
    sensor_ids = [
        int(s.strip()) for s in sensor_ids_str.split(",") if s.strip()
    ]
    if not sensor_ids:
      raise ValueError(
          f"At least one sensor ID must be provided after ':' in '{entry}'."
      )

    if camera_name in cameras:
      raise ValueError(
          f"Duplicate camera name '{camera_name}' specified in --cameras."
      )
    cameras[camera_name] = sensor_ids

  return cameras


def create_pose_estimation_pipeline(
    solution: deployments.Solution,
    cameras: Sequence[tuple[provided.ResourceHandle, Sequence[int]]],
    perception_resource: provided.ResourceHandle,
    pose_estimator_id: str,
    min_num_instances: int = _DEFAULT_MIN_NUM_INSTANCES,
    log_debug_data: bool = True,
    timeout_sec: int | None = None,
) -> tuple[list[provided.SkillBase], provided.SkillBase]:
  """Constructs the capture_images and estimate_pose_multi_view skills for the pipeline.

  Args:
    solution: Connected Solution instance.
    cameras: Sequence of (camera_resource, sensor_ids) pairs (e.g.
      [(cam1, [1, 4]), (cam2, [2])]).
    perception_resource: ResourceHandle for the IOC Pose Estimator Service.
    pose_estimator_id: Full ID of the pose estimator asset (e.g.
      'ai.intrinsic.my_pose_estimator').
    min_num_instances: Minimum number of detected instances required.
    log_debug_data: Whether to enable debug data logging in the skills.
    timeout_sec: Optional inference timeout in seconds.

  Returns:
    A tuple of (capture_images_skills, estimate_pose_multi_view_skill).

  Raises:
    ValueError: If pose_estimator_id is not a valid Intrinsic ID format,
      cameras is empty, or sensor_ids for a camera is empty.
  """
  if not id_utils.is_id(pose_estimator_id):
    raise ValueError(
        f"Invalid pose estimator ID '{pose_estimator_id}'. Expected format:"
        " <package>.<name> (e.g. 'ai.intrinsic.my_pose_estimator')"
    )

  package_name = id_utils.package_from(pose_estimator_id)
  name = id_utils.name_from(pose_estimator_id)

  skills = solution.skills

  if not cameras:
    raise ValueError("At least one camera must be provided.")

  # 1. Set up capture_images skill(s) with sensor_ids
  capture_skills = []
  for cam, cam_sensor_ids in cameras:
    if not cam_sensor_ids:
      raise ValueError(
          f"At least one sensor ID must be provided for camera '{cam.name}'."
      )
    capture_skills.append(
        skills.ai.intrinsic.capture_images(
            camera=cam,
            log_debug_data=log_debug_data,
            sensor_ids=[int(sid) for sid in cam_sensor_ids],
        )
    )

  pose_estimator_proto = pose_estimator_id_pb2.PoseEstimatorId(
      id=name,
      package=package_name,
  )

  # 2. Set up estimate_pose_multi_view skill connected to IOC Pose Estimator Service
  estimate_pose_kwargs: dict[str, Any] = {
      "perception": perception_resource,
      "pose_estimator": pose_estimator_proto,
      "capture_data": [s.result.capture_data for s in capture_skills],
      "min_num_instances": min_num_instances,
      "log_debug_data": log_debug_data,
  }
  if timeout_sec is not None and timeout_sec > 0:
    estimate_pose_kwargs["inference_timeout_sec"] = timeout_sec

  estimate_pose_skill = skills.ai.intrinsic.estimate_pose_multi_view(
      **estimate_pose_kwargs
  )

  return capture_skills, estimate_pose_skill


def run_pose_estimation_pipeline(
    solution: deployments.Solution,
    capture_images_skills: Sequence[provided.SkillBase],
    estimate_pose_skill: provided.SkillBase,
) -> estimate_pose_multi_view_pb2.EstimatePoseMultiViewResult:
  """Executes the pipeline on the solution executive and returns the results.

  Args:
    solution: Connected Solution instance.
    capture_images_skills: Sequence of instantiated CaptureImages skills.
    estimate_pose_skill: Instantiated EstimatePoseMultiView skill.

  Returns:
    EstimatePoseMultiViewResult proto message containing detections and poses.
  """
  print("Executing pose estimation pipeline on executive...")
  # This runs capture_image_skills sequentially.
  solution.executive.run([*capture_images_skills, estimate_pose_skill])
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
    camera_sensor_ids = parse_cameras(_CAMERAS.value)
    cameras = [
        (get_camera_resource(solution, name), sids)
        for name, sids in camera_sensor_ids.items()
    ]
    perception_resource = get_perception_service_resource(
        solution, _SERVICE_NAME.value
    )
  except ValueError as e:
    print(f"Resource resolution error: {e}")
    return

  print(
      "Using cameras and sensor IDs:"
      f" { {cam.name: sids for cam, sids in cameras} }"
  )
  print(f"Using perception service resource: {perception_resource.name}")
  print(f"Using pose estimator ID: {pose_estimator_id}")

  try:
    capture_skills, estimate_skill = create_pose_estimation_pipeline(
        solution=solution,
        cameras=cameras,
        perception_resource=perception_resource,
        pose_estimator_id=pose_estimator_id,
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
        capture_images_skills=capture_skills,
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
