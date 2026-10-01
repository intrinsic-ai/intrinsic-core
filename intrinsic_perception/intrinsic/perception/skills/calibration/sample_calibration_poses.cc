// Copyright 2026 Intrinsic Innovation LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "intrinsic/perception/skills/calibration/sample_calibration_poses.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <random>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "google/protobuf/message.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/icon/equipment/equipment_utils.h"
#include "intrinsic/icon/proto/cart_space_conversion.h"
#include "intrinsic/icon/proto/eigen_conversion.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/icon/skills/util/position_part_util.h"
#include "intrinsic/icon/utils/arm_utils.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/math/units.h"
#include "intrinsic/motion_planning/motion_planner_client.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning_error.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/service/motion_planner_service_asset_utils.h"
#include "intrinsic/motion_planning/skills/motion_planning_util.h"
#include "intrinsic/perception/calibration/pose_sampling.h"
#include "intrinsic/perception/proto/v1/camera_setup.pb.h"
#include "intrinsic/perception/skills/calibration/sample_calibration_poses.pb.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/cc/skill_utils.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/sysinfo.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/world/objects/frame.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/transform_node.h"
#include "intrinsic/world/objects/world_object.h"
#include "intrinsic/world/proto/collision_settings.pb.h"
#include "intrinsic/world/util/object_reference_utils.h"

namespace intrinsic {
namespace skills {

std::unique_ptr<SkillInterface> SampleCalibrationPoses::CreateSkill() {
  return std::make_unique<SampleCalibrationPoses>(
      std::make_unique<icon::DefaultChannelFactory>());
}

namespace {

using intrinsic_proto::perception::v1::CAMERA_SETUP_UNSPECIFIED;

constexpr int kNumTriedPosesPerRequestedSample = 10;

// Returns true if the motion planning error indicates a collision or
// unreachable/out of limits planning failure.
bool IsCollisionOrUnreachable(
    const intrinsic_proto::motion_planning::v1::MotionPlanningError& error) {
  if (error.has_collision_error() || error.has_joint_limit_error()) {
    return true;
  }
  if (error.has_ik_error()) {
    using intrinsic_proto::motion_planning::v1::ErrorContext;
    const auto context = error.ik_error().error_context();
    return context == ErrorContext::IK_COLLISION ||
           context == ErrorContext::IK_NO_SOLUTIONS_FOUND ||
           context == ErrorContext::IK_NO_SOLUTIONS_FOUND_W_COLLISION_CHECKING;
  }
  return false;
};

// Returns true if the status indicates a collision or unreachable/out of limits
// planning failure.
bool HasCollisionOrUnreachableError(const absl::Status& status) {
  bool has_collision_or_unreachable_error = false;
  status.ForEachPayload(
      [&](absl::string_view type_url, const absl::Cord& payload) {
        if (has_collision_or_unreachable_error) return;
        has_collision_or_unreachable_error = [&]() {
          if (absl::StrContains(type_url, "MotionPipelineError")) {
            intrinsic_proto::motion_planning::v1::MotionPipelineError error;
            error.ParseFromCord(payload);
            if (absl::c_any_of(error.motion_planning_error(),
                               IsCollisionOrUnreachable))
              return true;
          }
          if (absl::StrContains(type_url, "MotionPlanningError")) {
            intrinsic_proto::motion_planning::v1::MotionPlanningError error;
            error.ParseFromCord(payload);
            if (IsCollisionOrUnreachable(error)) return true;
          }
          return false;
        }();
      });
  return has_collision_or_unreachable_error;
}

absl::Status ValidateParams(
    const intrinsic_proto::skills::SampleCalibrationPosesParams& params) {
  if (IsEmptyObjectReference(params.calibration_object())) {
    return absl::InvalidArgumentError("calibration_object cannot be empty.");
  }
  if (!(params.has_randomized_box_params() ||
        params.has_pre_calibration_params())) {
    return absl::InvalidArgumentError(
        "Unknown parameter sampling strategy selected.");
  }
  if (params.minimum_margin() < 0) {
    return absl::InvalidArgumentError(
        absl::StrFormat("minimum_margin must be a positive value, but is %.2f.",
                        params.minimum_margin()));
  }

  if (params.calibration_type() == CAMERA_SETUP_UNSPECIFIED) {
    return absl::InvalidArgumentError("Invalid CameraSetup.");
  }

  if (params.has_pre_calibration_params()) {
    const double kPreCalibrationMaxDistanceMinThreshold = 0.001;
    const double kPreCalibrationMaxAngleDegreesMinThreshold = 1;

    if (params.pre_calibration_params().max_distance() <
        kPreCalibrationMaxDistanceMinThreshold) {
      return absl::InvalidArgumentError(
          absl::StrFormat("pre_calibration_max_distance must be >= %.5f [m].",
                          kPreCalibrationMaxDistanceMinThreshold));
    }
    if (params.pre_calibration_params().max_angle_degrees() <
        kPreCalibrationMaxAngleDegreesMinThreshold) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "pre_calibration_max_angle_degrees must be >= %.2f [degrees].",
          kPreCalibrationMaxAngleDegreesMinThreshold));
    }
  }

  if (params.has_randomized_box_params()) {
    if (params.randomized_box_params().num_samples() <
        SampleCalibrationPoses::kMinNumSamples) {
      return absl::InvalidArgumentError(
          absl::StrFormat("num_samples must be >= %d.",
                          SampleCalibrationPoses::kMinNumSamples));
    }
    const eigenmath::VectorXd sample_box_halfsize = RepeatedDoubleToVectorXd(
        params.randomized_box_params().sample_box_halfsize().value());
    if (sample_box_halfsize.size() != 3) {
      return absl::InvalidArgumentError(
          "sample_box_halfsize must be a vector of length 3.");
    }
    if (sample_box_halfsize.minCoeff() <= 0) {
      return absl::InvalidArgumentError(
          "sample_box_halfsize must be greater than 0.");
    }
  }

  return absl::OkStatus();
}

struct PoseWithJointConfiguration {
  std::optional<Pose3d> base_t_flange;
  std::optional<eigenmath::VectorXd> joint_configuration;
};

// Sample poses for pre-calibration. The sequence is
// deterministic so that repeated calls to this skill can be relied upon - even
// though we used unplanned moves.
std::vector<PoseWithJointConfiguration> SamplePreCalibrationPoses(
    const Pose3d& base_t_flange_initial, const double max_distance,
    const double max_angle_degrees) {
  const std::vector<eigenmath::Vector3d> offsets_uniform = {
      eigenmath::Vector3d(1, 0, 0),       eigenmath::Vector3d(0, 1, 0),
      eigenmath::Vector3d(0, 0, 1),       eigenmath::Vector3d(-1, 0, 0),
      eigenmath::Vector3d(0, -1, 0),      eigenmath::Vector3d(0, 0, -1),
      eigenmath::Vector3d(0.5, 0.5, 0.5), eigenmath::Vector3d(-0.5, -0.5, -0.5),
  };
  // A different rotation axis corresponding to each offset vector. Can be any
  // deterministic sequence of vectors, so we just re-use the offset vectors as
  // axes.
  const std::vector<eigenmath::Vector3d>& rotation_axes = offsets_uniform;

  std::vector<PoseWithJointConfiguration> result;
  result.reserve(offsets_uniform.size());
  for (int i = 0; i < offsets_uniform.size(); i++) {
    // Compute offset poses from the initial pose of the flange. The offset
    // poses are 'max_distance' away from the initial pose and rotated about
    // different axes by 'max_angle_degrees'.
    // Note that the offsets are relative to the robot's flange frame and not to
    // the calibration object attached to the robot. This is because the
    // calibration object cannot be assumed to have a correct pose in the world.
    Pose3d flange_t_calibration_pose;
    flange_t_calibration_pose.translation() = max_distance * offsets_uniform[i];
    flange_t_calibration_pose.setRotationMatrix(
        Eigen::AngleAxisd(DegToRad(max_angle_degrees),
                          rotation_axes[i].normalized())
            .matrix());

    // Combine 'base_t_flange_initial' with 'flange_t_calibration_pose' such
    // that the rotation of 'flange_t_calibration_pose' is relative to the
    // robot's base frame.
    result.emplace_back(PoseWithJointConfiguration{
        .base_t_flange = Pose3d(flange_t_calibration_pose.rotationMatrix() *
                                    base_t_flange_initial.rotationMatrix(),
                                flange_t_calibration_pose.translation() +
                                    base_t_flange_initial.translation())});
  }
  return result;
}

// Computes possible joint space solution for the given base_t_flange
// pose, which can also be reached by a planned motion.
absl::StatusOr<eigenmath::VectorXd> ComputeJointSpaceSolution(
    const Pose3d& base_t_flange, const world::ObjectWorldClient& world,
    motion_planning::MotionPlannerClient& planner,
    const intrinsic_proto::world::ObjectReference& robot_ref,
    const bool ensure_same_branch_ik) {
  INTR_ASSIGN_OR_RETURN(const auto robot, world.GetKinematicObject(robot_ref));
  // TODO(darkoz): Add collision settings to skill params.
  intrinsic_proto::world::CollisionSettings collision_settings;
  intrinsic_proto::motion_planning::CartesianMotionTarget cartesian_target;
  *cartesian_target.mutable_frame() = robot.TransformNodeReference();
  *cartesian_target.mutable_tool() =
      robot.GetSingleIsoFlangeFrame()->TransformNodeReference();
  *cartesian_target.mutable_offset() = ToProto(base_t_flange);

  // Compute collision-free same branch IK solutions for the given pose.
  auto ik_result =
      planner.ComputeIk(robot, cartesian_target,
                        {.collision_settings = collision_settings,
                         .ensure_same_branch = ensure_same_branch_ik});

  // If pose is reachable, check whether a path is feasible to the first IK
  // solution.
  if (ik_result.ok() && !ik_result.value().solutions.empty()) {
    // Construct RobotSpecification for planning a trajectory.
    // It is fine to use inf Cartesian limits because that is the default if
    // nothing is set. The planned trajectory is only used to validate if there
    // is a feasible motion to the IK solution of the sampled pose.
    auto robot_specification =
        ::intrinsic::motion_planning::CreateRobotSpecification(
            robot, /*start_configuration=*/std::nullopt);

    // Construct MotionSpecification for planning a trajectory to the IK
    // solution.
    intrinsic_proto::motion_planning::v1::MotionSpecification
        motion_specification;
    auto motion_segment = motion_specification.add_motion_segments();
    *motion_segment->mutable_target()->mutable_joint_position() =
        icon::ToJointVecProto(ik_result.value().solutions[0]);
    auto cart_limits = icon::ToProto(CartesianLimits::Unlimited());
    motion_segment->mutable_cartesian_limits()->set_max_rotational_acceleration(
        cart_limits.max_rotational_acceleration());
    motion_segment->mutable_cartesian_limits()->set_max_rotational_velocity(
        cart_limits.max_rotational_velocity());
    motion_segment->mutable_cartesian_limits()->set_max_translational_velocity(
        cart_limits.max_translational_velocity().at(0));
    motion_segment->mutable_cartesian_limits()
        ->set_max_translational_acceleration(
            cart_limits.max_translational_acceleration().at(0));

    // Plan trajectory.
    auto trajectory_result = planner.PlanTrajectory(
        robot_specification, motion_specification,
        {.path_planning_time_out = 10.0},
        "skills.SampleCalibrationPoses/ComputeJointSpaceSolution");

    // Return the IK solution if trajectory planning is successful.
    if (trajectory_result.ok()) {
      return ik_result.value().solutions[0];
    }

    // Return the status if trajectory planning failed. This means that there is
    // no feasible motion to the joint configuration.
    return trajectory_result.status();
  }

  return ik_result.status();
}

// Randomly samples n reachable and collision-free poses, where n is the amount
// of requested samples, based on 'randomized_box_params'. The sampled poses are
// checked for reachability and collisions until enough valid poses are found.
// Returns an error if not enough valid poses could be found.
template <typename URBG>
absl::StatusOr<std::vector<PoseWithJointConfiguration>> SampleRandomWaypoints(
    const Pose3d& base_t_flange, const world::ObjectWorldClient& world,
    const intrinsic_proto::world::ObjectReference& robot_ref,
    intrinsic_proto::perception::v1::CameraSetup calibration_type,
    const world::KinematicObject& robot,
    const world::WorldObject& calibration_object,
    const world::Frame& camera_frame, const world::Frame& flange,
    const int num_samples, const eigenmath::Vector3d& sample_box_halfsize,
    const double rotation_randomization_angle_degrees,
    const double rotation_randomization_roll_angle_degrees,
    const bool ensure_same_branch_ik,
    const int num_tried_poses_per_requeseted_sample, bool multithreading,
    URBG&& engine, motion_planning::MotionPlannerClient& planner) {
  Pose3d base_t_stationary_object;
  // 'attached_object' is either the camera or the calibration object depending
  // on the calibration case. In both cases it is attached to the robot flange.
  Pose3d flange_t_attached_object;
  if (calibration_type ==
      intrinsic_proto::perception::v1::CAMERA_SETUP_STATIONARY) {
    // Camera is the center.
    INTR_ASSIGN_OR_RETURN(base_t_stationary_object,
                          world.GetTransform(robot, camera_frame));
    INTR_ASSIGN_OR_RETURN(flange_t_attached_object,
                          world.GetTransform(flange, calibration_object));
  } else {
    // The calibration object is the center.
    INTR_ASSIGN_OR_RETURN(base_t_stationary_object,
                          world.GetTransform(robot, calibration_object));
    INTR_ASSIGN_OR_RETURN(flange_t_attached_object,
                          world.GetTransform(flange, camera_frame));
  }

  const int num_poses_to_try =
      num_samples * num_tried_poses_per_requeseted_sample;

  std::vector<PoseWithJointConfiguration> valid_waypoints;
  valid_waypoints.reserve(num_samples);

  // Records if an actual error happened in any of the threads.
  absl::Status error_status = absl::OkStatus();
  absl::Mutex mutex;
  int num_checked_poses = 0;
  const auto sample_pose = [&]() {
    while (true) {
      Pose3d base_t_obj_unchecked;
      {
        absl::MutexLock lock(mutex);
        // Stop if we encountered an error in any of the threads.
        if (!error_status.ok()) {
          break;
        }
        // If we have not checked enough poses, continue with the next sample,
        // otherwise exit.
        if (num_checked_poses < num_poses_to_try) {
          num_checked_poses++;
        } else {
          break;
        }

        // Lock `engine`.
        base_t_obj_unchecked = perception::SampleRandomPose(
            base_t_flange, flange_t_attached_object, base_t_stationary_object,
            {.sample_box_halfsize = sample_box_halfsize,
             .rotation_randomization_angle_radians =
                 DegToRad(rotation_randomization_angle_degrees),
             .rotation_randomization_roll_angle_radians =
                 DegToRad(rotation_randomization_roll_angle_degrees)},
            engine);
      }
      auto joint_space_solution =
          ComputeJointSpaceSolution(base_t_obj_unchecked, world, planner,
                                    robot_ref, ensure_same_branch_ik);
      absl::StatusOr<PoseWithJointConfiguration> checked_pose;
      if (joint_space_solution.ok()) {
        checked_pose = PoseWithJointConfiguration{
            .base_t_flange = base_t_obj_unchecked,
            .joint_configuration = joint_space_solution.value(),
        };
      } else {
        checked_pose = joint_space_solution.status();
      }

      absl::MutexLock lock(mutex);
      if (HasCollisionOrUnreachableError(checked_pose.status())) {
        // If the sampled pose cannot be reached or is in collision, skip it.
        continue;
      } else if (!checked_pose.ok()) {
        // If we encountered an actual error set the error status for
        // communication to other threads and exit.
        error_status = checked_pose.status();
        break;
      }
      // If we found enough valid poses, exit.
      if (valid_waypoints.size() == num_samples) {
        break;
      }
      // If we made it this far, we have a valid pose - save it.
      valid_waypoints.push_back(*checked_pose);
    }
  };

  const int num_threads =
      std::min(num_poses_to_try, std::max(NumCPUs() - 1, 1));

  if (multithreading) {
    std::vector<Thread> threads;
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
      threads.push_back(Thread(sample_pose));
    }

    for (auto& thread : threads) {
      thread.join();
    }
  } else {
    sample_pose();
  }

  if (!error_status.ok()) {
    return error_status;
  }

  LOG(INFO) << "Found " << valid_waypoints.size()
            << " reachable and collision-free poses after trying "
            << num_checked_poses << " randomly sampled poses.";

  if (valid_waypoints.size() < num_samples) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Failed to find %d reachable and collision-free poses.", num_samples));
  }
  return valid_waypoints;
}

}  // namespace

template <typename URBG>
absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
SampleCalibrationPoses::ExecuteWithRandomEngine(const ExecuteRequest& request,
                                                ExecuteContext& context,
                                                bool multithreading,
                                                URBG&& engine) {
  INTR_ASSIGN_OR_RETURN(
      auto params,
      request.params<intrinsic_proto::skills::SampleCalibrationPosesParams>());

  INTR_RETURN_IF_ERROR(ValidateParams(params));

  world::ObjectWorldClient& world = context.object_world();

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<motion_planning::MotionPlannerClient>
          motion_planner_service_asset_client,
      GetMotionPlannerServiceAssetClient(world.GetWorldID(),
                                         params.motion_planner_service()));
  // TODO(b/524634328): Remove the Fallback Logic from the Skills.
  motion_planning::MotionPlannerClient& planner =
      (motion_planner_service_asset_client != nullptr)
          ? *motion_planner_service_asset_client
          : context.motion_planner();

  // Extract all required equipment.
  const EquipmentPack equipment_pack = context.equipment();
  INTR_ASSIGN_OR_RETURN(
      const intrinsic_proto::resources::ResourceHandle robot_handle,
      equipment_pack.GetHandle(kRobotEquipmentSlot));

  // Backup initial robot joint configuration so robot can return to this
  // configuration after calibration.
  eigenmath::VectorXd robot_start_joint_configuration;
  // We need to talk to ICON directly because the world could be stale.
  INTR_ASSIGN_OR_RETURN(const auto connection_config,
                        skills::GetConnectionParamsFromHandle(robot_handle));
  absl::StatusOr<intrinsic_proto::icon::Icon2PositionPart> position_part =
      equipment_pack.Unpack<intrinsic_proto::icon::Icon2PositionPart>(
          kRobotEquipmentSlot, icon::kIcon2PositionPartKey);
  if (!position_part.ok()) {
    return absl::FailedPreconditionError(
        absl::StrCat("ICON Equipment at slot ", kRobotEquipmentSlot,
                     " does not have a position part."));
  }

  INTR_ASSIGN_OR_RETURN(
      skills::ArmPartInformation arm_part_info,
      skills::GetArmPartInformation(position_part.value(), world,
                                    params.has_arm_part()
                                        ? std::make_optional(params.arm_part())
                                        : std::nullopt));

  INTR_ASSIGN_OR_RETURN(const world::KinematicObject robot,
                        world.GetKinematicObject(arm_part_info.object));
  INTR_ASSIGN_OR_RETURN(const world::Frame flange,
                        robot.GetSingleIsoFlangeFrame());
  INTR_ASSIGN_OR_RETURN(const world::WorldObject calibration_object,
                        world.GetObject(params.calibration_object()));

  INTR_ASSIGN_OR_RETURN(
      auto channel,
      icon_channel_factory_->MakeChannel(
          connection_config, connect::kGrpcClientConnectDefaultTimeout));
  icon::Client icon_client(channel);
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::icon::PartStatus part_status,
                        icon_client.GetSinglePartStatus(arm_part_info.name));
  robot_start_joint_configuration = icon::GetSensedJointPosition(part_status);

  // Update the joint positions in the world from the sensed joint positions.
  INTR_RETURN_IF_ERROR(
      world.UpdateJointPositions(robot, robot_start_joint_configuration));
  INTR_ASSIGN_OR_RETURN(const Pose3d base_t_flange_initial,
                        world.GetTransform(robot, flange));

  std::vector<PoseWithJointConfiguration> sampled_waypoints;
  if (params.has_pre_calibration_params()) {
    const ::intrinsic_proto::skills::PreCalibrationParams&
        sampling_strategy_params = params.pre_calibration_params();
    sampled_waypoints = SamplePreCalibrationPoses(
        base_t_flange_initial, sampling_strategy_params.max_distance(),
        sampling_strategy_params.max_angle_degrees());
  } else if (params.has_randomized_box_params()) {
    INTR_ASSIGN_OR_RETURN(
        const world::Frame camera_frame,
        world.GetFrame(WorldObjectName(params.camera().object().name()),
                       SensorFrameName()));
    const ::intrinsic_proto::skills::RandomizedBoxParams&
        sampling_strategy_params = params.randomized_box_params();
    const eigenmath::VectorXd sample_box_halfsize = RepeatedDoubleToVectorXd(
        sampling_strategy_params.sample_box_halfsize().value());
    INTR_ASSIGN_OR_RETURN(
        sampled_waypoints,
        SampleRandomWaypoints(
            base_t_flange_initial, world, arm_part_info.object,
            params.calibration_type(), robot, calibration_object, camera_frame,
            flange, sampling_strategy_params.num_samples(), sample_box_halfsize,
            sampling_strategy_params.rotation_randomization_angle_degrees(),
            sampling_strategy_params
                .rotation_randomization_roll_angle_degrees(),
            sampling_strategy_params.ensure_same_branch_ik(),
            kNumTriedPosesPerRequestedSample, multithreading, engine, planner));
  }

  auto sampling_result =
      std::make_unique<intrinsic_proto::skills::SampleCalibrationPosesResult>();

  for (const auto& sample : sampled_waypoints) {
    auto* constraint = sampling_result->add_sample_calibration_poses_result();
    if (sample.joint_configuration.has_value()) {
      constraint->mutable_joint_position()->mutable_joints()->Assign(
          sample.joint_configuration.value().begin(),
          sample.joint_configuration.value().end());
    } else if (sample.base_t_flange.has_value()) {
      *constraint->mutable_cartesian_pose()->mutable_moving_frame() =
          flange.TransformNodeReference();
      *constraint->mutable_cartesian_pose()->mutable_target_frame() =
          robot.TransformNodeReference();
      *constraint->mutable_cartesian_pose()->mutable_target_frame_offset() =
          ToProto(sample.base_t_flange.value());
    }
  }

  return sampling_result;
}

// Main function executing the skill logic. Validates parameters, and performs
// the sampling based on the requested sampling strategy.
absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
SampleCalibrationPoses::Execute(const ExecuteRequest& request,
                                ExecuteContext& context) {
  INTR_ASSIGN_OR_RETURN(
      auto params,
      request.params<intrinsic_proto::skills::SampleCalibrationPosesParams>());
  if (params.has_seed()) {
    // To get the same sequence of samples across different runs, we use a
    // std::mt19937 engine with a fixed seed and single threading.
    // TODO(dmitriim): Implement multithreading for fixed seeds.
    LOG(WARNING) << "Using a fixed seed for the random number generator "
                    "disables multithreading.";
    std::mt19937 engine(params.seed());
    return ExecuteWithRandomEngine(request, context, false, engine);
  } else {
    absl::BitGen engine;
    return ExecuteWithRandomEngine(request, context, true, engine);
  }
}

absl::StatusOr<std::unique_ptr<::google::protobuf::Message>>
SampleCalibrationPoses::Preview(const PreviewRequest& request,
                                PreviewContext& context) {
  return std::make_unique<
      intrinsic_proto::skills::SampleCalibrationPosesResult>();
}

absl::StatusOr<intrinsic_proto::skills::Footprint>
SampleCalibrationPoses::GetFootprint(const GetFootprintRequest& request,
                                     GetFootprintContext& context) const {
  intrinsic_proto::skills::Footprint result;
  // We lock the universe and thus prevent parallel execution for the execution
  // of calibration.
  result.set_lock_the_universe(true);
  return result;
}

}  // namespace skills
}  // namespace intrinsic
