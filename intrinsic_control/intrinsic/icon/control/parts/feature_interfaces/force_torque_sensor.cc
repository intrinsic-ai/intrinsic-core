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

#include "intrinsic/icon/control/parts/feature_interfaces/force_torque_sensor.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/pseudo_inverse.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/linear_joint_acceleration_filter.h"
#include "intrinsic/icon/control/algorithms/wrench_stability_monitor.h"
#include "intrinsic/icon/control/algorithms/wrench_stability_monitor_factory.h"
#include "intrinsic/icon/control/parts/hal/force_torque_sensor_part/force_sensor_controller_migration_utils.h"
#include "intrinsic/icon/control/parts/hal/force_torque_sensor_part/hal_force_torque_sensor_part_config.pb.h"
#include "intrinsic/icon/control/parts/realtime_part_interface.h"
#include "intrinsic/icon/control/services/world_service.h"
#include "intrinsic/icon/flatbuffers/transform_view.h"
#include "intrinsic/icon/hal/hardware_interface_handle.h"
#include "intrinsic/icon/hal/interfaces/force_sensor.fbs.h"
#include "intrinsic/icon/hal/interfaces/force_torque.fbs.h"
#include "intrinsic/icon/hal/interfaces/force_torque_utils.h"
#include "intrinsic/icon/hal/interfaces/joint_state.fbs.h"
#include "intrinsic/icon/utils/clock.h"
#include "intrinsic/icon/utils/fixed_str_cat.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/icon/utils/sensor_utils.h"
#include "intrinsic/icon/utils/world_utils.h"
#include "intrinsic/kinematics/elements.h"
#include "intrinsic/kinematics/model_interface.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/kinematics/state.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/to_fixed_string.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/transform_utils.h"
#include "intrinsic/math/twist.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/transform_node.h"
#include "intrinsic/world/objects/world_object.h"
#include "intrinsic/world/proto/object_world_service.pb.h"

namespace intrinsic::icon {

namespace {
// This defines the default number of constant unprocessed FT readings before an
// error is returned. This is only used if no value is defined in the proto
// config.
constexpr int kDefaultMaxNumberConstantFTReadings = 10;

// Returns the Skeleton ElementId corresponding to `link_name`, if unambiguous.
//
// Link names are only unique within a single World object, but sometimes
// ForceTorqueSensor gets a target link name that belongs to a different object
// from the robot.
//
// This function performs two steps:
// 1. Search the members of `world_objects` for a entity called `link_name`.
//    If there is more than one, return FailedPrecondition.
// 2. Extract that entity's World ID.
// 2. Use `world_id_to_skeleton_id` to find the corresponding Skeleton
//    ElementId.
absl::StatusOr<kinematics::ElementId> FindElementIdForLinkName(
    absl::string_view link_name,
    absl::Span<const world::WorldObject> world_objects,
    const absl::flat_hash_map<std::string, kinematics::ElementId>&
        world_id_to_skeleton_id) {
  // First find the world ID for the target link. Error out if more than one
  // `KinematicObject` has an entity called `link_name`.
  std::string link_entity_id = "";
  for (const world::WorldObject& world_object : world_objects) {
    auto link_entity = absl::c_find_if(
        world_object.Proto().entities(), [&](const auto& id_and_entity) {
          return id_and_entity.second.name() == link_name;
        });
    if (link_entity == world_object.Proto().entities().end()) {
      continue;
    }
    if (!link_entity_id.empty()) {
      return absl::FailedPreconditionError(
          absl::StrCat("More than one of the World objects attached to the "
                       "robot (including the robot itself) has a link named '",
                       link_name, "'"));
    }
    link_entity_id = link_entity->second.id();
  }
  if (link_entity_id.empty()) {
    return absl::NotFoundError(
        absl::StrCat("'", link_name,
                     "' is not a link in any of the World objects attached to "
                     "the robot (including the robot itself)."));
  }
  // Look up Skeleton ID for the target link entity
  auto world_id_and_skeleton_id = world_id_to_skeleton_id.find(link_entity_id);
  if (world_id_and_skeleton_id == world_id_to_skeleton_id.end()) {
    return absl::NotFoundError(
        absl::StrCat("Cannot find Skeleton ID for link '", link_name, "'"));
  }
  return world_id_and_skeleton_id->second;
}

// Returns the jacobian for link_element_id specified in the (rotation) frame of
// link_element_id.
RealtimeStatusOr<eigenmath::Matrix6Nd> ComputeJacobianInLinkFrame(
    const JointStatePVA& current_joint_state,
    const kinematics::ElementId& link_element_id, kinematics::State& state) {
  INTRINSIC_RT_RETURN_IF_ERROR(state.SetStatePVA(current_joint_state));

  // This jacobian returns velocities in the base frame so we need to transform
  // them back to the links's frame.
  INTRINSIC_RT_ASSIGN_OR_RETURN(eigenmath::Matrix6Nd jacobian,
                                state.ComputeJacobian(link_element_id));
  INTRINSIC_RT_ASSIGN_OR_RETURN(Pose3d base_t_ft_sensor_link,
                                state.GetTransform(link_element_id));
  eigenmath::Matrix6d transform = eigenmath::Matrix6d::Zero();
  // Transpose is the same as inverse.
  transform.block(0, 0, 3, 3) =
      base_t_ft_sensor_link.rotationMatrix().transpose();
  transform.block(3, 3, 3, 3) =
      base_t_ft_sensor_link.rotationMatrix().transpose();
  eigenmath::Matrix6Nd jacobian_transformed = transform * jacobian;
  return jacobian_transformed;
}

// Computes a wrench from an external torque signal. Also updates `taring_data`
// to account for taring. If taring is in progress, return a zero wrench.
RealtimeStatusOr<eigenmath::Vector6d> ComputeWrenchFromExternalJointTorque(
    const eigenmath::VectorNd& external_joint_torque,
    const JointStatePVA& current_joint_state,
    const kinematics::ElementId& ft_sensor_link_element_id,
    kinematics::State& state, TaringData& taring_data) {
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      eigenmath::Matrix6Nd jacobian_in_link_frame,
      ComputeJacobianInLinkFrame(current_joint_state, ft_sensor_link_element_id,
                                 state));

  // TODO(b/341846729): Add smoothed version of damped pinv
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      eigenmath::MatrixNd pinv_J,
      ComputePseudoInverse(jacobian_in_link_frame.transpose(), 1e-5));
  eigenmath::Vector6d wrench =
      eigenmath::Vector6d(pinv_J * external_joint_torque);
  taring_data.Update(wrench);
  std::optional<eigenmath::Vector6d> tared_wrench = taring_data.GetTaredValue();
  return tared_wrench.value_or(eigenmath::Vector6d::Zero());
}

}  // namespace

ForceTorqueSensorFeature::ForceTorqueSensorFeature(
    ForceSensorConfig force_sensor_config,
    ForceSensorVariables force_sensor_variables,
    std::unique_ptr<intrinsic::kinematics::Skeleton> skeleton,
    std::unique_ptr<intrinsic::kinematics::State> kinematics_state,
    std::unique_ptr<WrenchStabilityMonitor> wrench_stability_monitor,
    std::variant<ForceTorqueStatusHardwareInterface,
                 JointTorqueStateHardwareInterface>
        input_status_handle,
    std::optional<ForceTorqueCommandHardwareInterface> force_torque_command,
    JointPositionStateHardwareInterface&& joint_position_handle,
    std::optional<JointVelocityStateHardwareInterface> joint_velocity_handle,
    std::optional<std::variant<
        HardwareInterfaceHandle<intrinsic_fbs::JointAccelerationState>,
        std::vector<intrinsic::icon::LinearJointAccelerationFilter>>>
        joint_acceleration)
    : config_(std::move(force_sensor_config)),
      force_sensor_variables_(std::move(force_sensor_variables)),
      skeleton_(std::move(skeleton)),
      kinematics_state_(std::move(kinematics_state)),
      wrench_stability_monitor_(std::move(wrench_stability_monitor)),
      input_status_handle_(std::move(input_status_handle)),
      force_torque_command_handle_(std::move(force_torque_command)),
      joint_position_handle_(std::move(joint_position_handle)),
      joint_velocity_handle_(std::move(joint_velocity_handle)),
      joint_acceleration_(std::move(joint_acceleration)),
      ndof_(joint_position_handle_->position()->size()) {}

// static
absl::StatusOr<std::unique_ptr<ForceTorqueSensorFeature>>
ForceTorqueSensorFeature::Create(
    const intrinsic_proto::icon::HalForceTorqueSensorPartConfig& config,
    const WorldService* world_service,
    std::variant<ForceTorqueStatusHardwareInterface,
                 JointTorqueStateHardwareInterface>
        input_status_handle,
    std::optional<ForceTorqueCommandHardwareInterface>
        force_torque_command_handle,
    JointPositionStateHardwareInterface joint_position_handle,
    absl::string_view robot_world_object_name,
    const double control_frequency_hz,
    std::optional<JointVelocityStateHardwareInterface> joint_velocity_handle,
    std::optional<JointAccelerationStateHardwareInterface>
        joint_acceleration_handle) {
  // Check for a valid configuration for the input.
  //
  // Either:
  //   * force_torque_status_handle AND force_torque_command_handle are
  //     provided
  //   * or, external_joint_torque_state_handle is provided and
  //     force_torque_command_handle is NOT provided
  if (std::holds_alternative<ForceTorqueStatusHardwareInterface>(
          input_status_handle) &&
      force_torque_command_handle) {
    // Ok.
  } else if (std::holds_alternative<JointTorqueStateHardwareInterface>(
                 input_status_handle) &&
             !force_torque_command_handle) {
    // Ok.
  } else {
    return absl::InvalidArgumentError(
        "Must either provide force_torque_status_handle and "
        "force_torque_command_handle together, or specify "
        "external_joint_torque_state_handle by itself.");
  }

  const int ndof = joint_position_handle->position()->size();
  std::optional<std::variant<
      HardwareInterfaceHandle<intrinsic_fbs::JointAccelerationState>,
      std::vector<intrinsic::icon::LinearJointAccelerationFilter>>>
      joint_acceleration = std::nullopt;

  if (joint_acceleration_handle.has_value()) {
    joint_acceleration = std::move(joint_acceleration_handle);
  } else if (config.has_linear_joint_acceleration_filter_config()) {
    // Initialize joint acceleration estimators.
    eigenmath::MatrixXd P_init = eigenmath::MatrixXd::Identity(
        icon::LinearJointAccelerationFilter::kStateDim,
        icon::LinearJointAccelerationFilter::kStateDim);
    std::vector<intrinsic::icon::LinearJointAccelerationFilter>
        joint_acceleration_estimators;
    // Initialize the set of linear filters.
    for (size_t dof = 0; dof < joint_position_handle->position()->size();
         dof++) {
      INTR_ASSIGN_OR_RETURN(
          auto acceleration_filter,
          icon::LinearJointAccelerationFilter::Create(
              config.linear_joint_acceleration_filter_config(),
              control_frequency_hz, P_init));
      INTRINSIC_RT_LOG(INFO)
          << "Acceleration estimator for joint " << dof << " converged in "
          << acceleration_filter.GetDareSolution().iteration_count
          << " iterations.";
      // Cache converged covariance matrix to speed up initialization of
      // filter for subsequent joint.
      P_init = acceleration_filter.GetCovariance();
      joint_acceleration_estimators.push_back(std::move(acceleration_filter));
    }
    joint_acceleration = std::move(joint_acceleration_estimators);
  }

  if (config.estimate_post_sensor_dynamic_load() &&
      (!joint_acceleration.has_value() || !joint_velocity_handle.has_value())) {
    return absl::InvalidArgumentError(
        "Configured to with estimate_post_sensor_dynamic_load but "
        "at least one of the joint acceleration (estimator or hardware "
        "interface) or joint velocity hardware interface was not configured "
        "correctly.");
  }

  std::shared_ptr<const world::ObjectWorldClient> world_client =
      world_service->GetObjectWorldClient();

  // Check number of DoFs
  INTR_ASSIGN_OR_RETURN(const world::KinematicObject kinematic_object,
                        world_client->GetKinematicObject(
                            WorldObjectName(robot_world_object_name)));
  if (kinematic_object.JointPositions().size() < ndof) {
    return absl::FailedPreconditionError(
        absl::StrCat("DofKinematicView for robot '", robot_world_object_name,
                     "' does not have enough DoFs. Got ",
                     kinematic_object.JointPositions().size(),
                     ", expected at least ", ndof));
  }
  const intrinsic_proto::world::Object& robot_object_proto =
      kinematic_object.Proto();

  if (config.target_link_name().empty()) {
    return absl::InvalidArgumentError("Missing target link name.");
  }

  // Ensures the target link is part of the robot.
  auto target_link_entity_it = absl::c_find_if(
      robot_object_proto.entities(),
      [target_link_name =
           config.target_link_name()](const auto& id_and_entity) {
        const intrinsic_proto::world::Entity& entity = id_and_entity.second;
        return entity.name() == target_link_name;
      });
  if (target_link_entity_it == robot_object_proto.entities().end()) {
    return absl::FailedPreconditionError(
        absl::StrCat("Force/Torque target link '", config.target_link_name(),
                     "' is not a part of the robot '", robot_world_object_name,
                     "' in World."));
  }

  INTR_ASSIGN_OR_RETURN(
      SkeletonAndIdMap skeleton_and_id_map,
      GetSkeletonForObject(*world_client, kinematic_object.Name(),
                           /*include_children=*/true));

  if (config.ft_t_cog().size() != 3) {
    return absl::InvalidArgumentError(
        absl::StrCat("Force/Torque center of gravity must have size 3. Got ",
                     config.ft_t_cog().size(), "."));
  }
  ForceSensorVariables force_sensor_variables_initial;
  force_sensor_variables_initial.mounted_payload.mass_kg =
      config.support_mass();
  force_sensor_variables_initial.mounted_payload.ft_t_cog[0] =
      config.ft_t_cog().at(0);
  force_sensor_variables_initial.mounted_payload.ft_t_cog[1] =
      config.ft_t_cog().at(1);
  force_sensor_variables_initial.mounted_payload.ft_t_cog[2] =
      config.ft_t_cog().at(2);

  INTR_ASSIGN_OR_RETURN(kinematics::ElementId target_link_element_id,
                        FindElementIdForLinkName(
                            config.target_link_name(),
                            skeleton_and_id_map.kinematic_objects_in_skeleton,
                            skeleton_and_id_map.world_id_to_skeleton_id));

  // Some more checks on the skeleton:
  // 1. Ensures that there is a kinematic chain from the robot base to the
  //    target link.
  // 2. Ensures that this kinematic chain contains, at most, the first N
  // DoFs    between the robot base and tip, where N is the number of DoFs that
  // `JointPositionState` hardware interface reports. If the chain has
  // additional DoFs (for example, if the target link is at the end of a
  // moving gripper finger), then we cannot calculate the target link's
  // pose (because we only have the DoF positions for the robot itself).
  {
    // Assumes that the first `ndof` DoFs in the chain correspond to the
    // hardware joints. Any after that are "extra DoFs" that we cannot use,
    // because we don't have joint position information for them.
    absl::Span<const kinematics::ElementId> robot_dofs =
        skeleton_and_id_map.skeleton->GetDofIds().subspan(0, ndof);
    if (robot_dofs.size() != ndof) {
      return absl::FailedPreconditionError(absl::StrCat(
          "Number of DOFs mismatch between joint states from ",
          "hardware and skeleton, hardware layer has ", ndof, ", skeleton has ",
          robot_dofs.size(), " degrees of freedom."));
    }
    absl::Span<const kinematics::ElementId> extra_dofs =
        skeleton_and_id_map.skeleton->GetDofIds().subspan(ndof);
    // Walk up the kinematic tree until we hit the base (root), and make
    // sure that we do not encounter any joints other than the robot ones
    std::optional<const kinematics::Element*> current_element =
        skeleton_and_id_map.skeleton
            ->GetElement(target_link_element_id)
            // It's safe to call .value() because we've filled in
            // target_link_element_id after looking up the ID in `skeleton_`
            // in the first place.
            .value();
    while (current_element.has_value()) {
      INTRINSIC_RT_ASSIGN_OR_RETURN(
          kinematics::ElementId current_id,
          skeleton_and_id_map.skeleton->GetElementId(*current_element));
      // If the joint is part of `extra_dofs`, then it's not a joint we can
      // update for FK calculations!
      if (absl::c_find(extra_dofs, current_id) != extra_dofs.end()) {
        return absl::FailedPreconditionError(
            absl::StrCat("The kinematic chain from the base of robot ",
                         robot_world_object_name, " to target link ",
                         config.target_link_name(),
                         " contains a joint that is not part of the robot "
                         "itself (",
                         (*current_element)->GetName(),
                         "). Please check your World model."));
      }
      current_element = (*current_element)->GetParentElement();
    }
  }

  // Create a kinematics instance.
  auto kinematics_state = std::make_unique<intrinsic::kinematics::State>(
      skeleton_and_id_map.skeleton.get());

  if (config.ft_sensor_link_name().empty()) {
    return absl::InvalidArgumentError("Missing FT sensor link name.");
  }

  auto element_or = FindElementIdForLinkName(
      config.ft_sensor_link_name(),
      skeleton_and_id_map.kinematic_objects_in_skeleton,
      skeleton_and_id_map.world_id_to_skeleton_id);
  if (!element_or.ok()) {
    std::vector<std::string> names;
    for (const kinematics::ElementId& id :
         skeleton_and_id_map.skeleton->GetAllElementIds()) {
      names.push_back(skeleton_and_id_map.skeleton->GetElementName(id));
    }
    return absl::Status(
        element_or.status().code(),
        absl::StrCat(element_or.status().message(),
                     " Available links are: ", absl::StrJoin(names, ", ")));
  }
  kinematics::ElementId& ft_sensor_link_element_id = element_or.value();

  // Validate that FT sensor and target link are only connected by fixed
  // joints.
  INTR_RETURN_IF_ERROR(kinematics::AreElementsRigidlyAttached(
      *skeleton_and_id_map.skeleton, ft_sensor_link_element_id,
      target_link_element_id))
      << absl::StrCat(
             "The kinematic chain from the sensor link '",
             config.ft_sensor_link_name(), "' to target link '",
             config.target_link_name(),
             "' contains a movable joint. Please check your World model.");

  // Since we verified that the target link and FT-sensor link are located
  // on the same rigid body, we can compute the transformation between
  // FT-sensor and target at the default state values, without taking into
  // account sensed joint positions.
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      Pose3d target_t_ft,
      kinematics_state->GetTransform(target_link_element_id,
                                     ft_sensor_link_element_id));
  LOG(INFO) << "Force torque transform target_t_ft: "
            << target_t_ft.translation().transpose() << " "
            << target_t_ft.quaternion();
  int num_acceptable_constant_readings = kDefaultMaxNumberConstantFTReadings;
  if (config.has_num_acceptable_constant_readings()) {
    num_acceptable_constant_readings =
        config.num_acceptable_constant_readings();
  }

  ForceSensorConfig force_sensor_config{
      .robot_collection_name = std::string(robot_world_object_name),
      .target_link_name = config.target_link_name(),
      .target_link_element_id = target_link_element_id,
      .ft_sensor_link_name = config.ft_sensor_link_name(),
      .ft_sensor_link_element_id = ft_sensor_link_element_id,
      .target_t_ft = target_t_ft,
      .num_acceptable_constant_readings = num_acceptable_constant_readings,
      .estimate_post_sensor_dynamic_load =
          config.estimate_post_sensor_dynamic_load(),
  };

  // Set up the `WrenchStabilityMonitor` to detect control instability.
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<WrenchStabilityMonitor> wrench_stability_monitor,
      CreateWrenchStabilityMonitor(config.force_control_settings(),
                                   control_frequency_hz));

  return absl::WrapUnique(new ForceTorqueSensorFeature(
      std::move(force_sensor_config), std::move(force_sensor_variables_initial),
      std::move(skeleton_and_id_map.skeleton), std::move(kinematics_state),
      std::move(wrench_stability_monitor), std::move(input_status_handle),
      std::move(force_torque_command_handle), std::move(joint_position_handle),
      std::move(joint_velocity_handle), std::move(joint_acceleration)));
}

RealtimeStatusOr<JointStatePVA> ForceTorqueSensorFeature::ReadJointState() {
  JointStatePVA joint_state;
  void(joint_state.SetSize(ndof_));

  for (size_t dof = 0; dof < ndof_; ++dof) {
    joint_state.position[dof] = joint_position_handle_->position()->Get(dof);

    if (joint_velocity_handle_.has_value()) {
      joint_state.velocity[dof] =
          joint_velocity_handle_.value()->velocity()->Get(dof);
    }
    if (joint_acceleration_.has_value()) {
      if (std::holds_alternative<JointAccelerationStateHardwareInterface>(
              joint_acceleration_.value())) {
        joint_state.acceleration[dof] =
            std::get<JointAccelerationStateHardwareInterface>(
                joint_acceleration_.value())
                ->acceleration()
                ->Get(dof);
      } else if (std::holds_alternative<std::vector<
                     intrinsic::icon::LinearJointAccelerationFilter>>(
                     joint_acceleration_.value())) {
        icon::LinearJointAccelerationFilter::SingleJointState dof_state_in;
        icon::LinearJointAccelerationFilter::SingleJointState dof_joint_input;
        dof_state_in.position = joint_state.position[dof];
        dof_state_in.velocity = joint_state.velocity[dof];
        std::get<std::vector<intrinsic::icon::LinearJointAccelerationFilter>>(
            joint_acceleration_.value())[dof]
            .Predict(dof_joint_input);
        icon::LinearJointAccelerationFilter::SingleJointState dof_state_out =
            std::get<
                std::vector<intrinsic::icon::LinearJointAccelerationFilter>>(
                joint_acceleration_.value())[dof]
                .Correct(dof_state_in);
        joint_state.acceleration[dof] = dof_state_out.acceleration;
      }
    }
  }
  return std::move(joint_state);
}

RealtimeStatusOr<eigenmath::Vector6d>
ForceTorqueSensorFeature::ReadWrenchFromDevice(
    const JointStatePVA& current_full_joint_state) {
  if (std::holds_alternative<ForceTorqueStatusHardwareInterface>(
          input_status_handle_)) {
    return eigenmath::Vector6d(intrinsic_fbs::ViewAs<eigenmath::Vector6d>(
        std::get<ForceTorqueStatusHardwareInterface>(input_status_handle_)
            ->wrench()));
  } else if (std::holds_alternative<JointTorqueStateHardwareInterface>(
                 input_status_handle_)) {
    eigenmath::VectorNd external_joint_torque =
        intrinsic_fbs::ViewAs<eigenmath::VectorNd>(
            std::get<JointTorqueStateHardwareInterface>(input_status_handle_)
                ->torque());
    return ComputeWrenchFromExternalJointTorque(
        external_joint_torque, current_full_joint_state,
        config_.ft_sensor_link_element_id, *kinematics_state_,
        taring_data_joint_torque_interface_only_);
  }
  return InternalError(
      "No input provided and not caught in construction. "
      "This is an internal error.");
}

// Returns the total payload by combining the mounted payload and
// grasped payload.
PostSensorPayload ForceTorqueSensorFeature::GetTotalPayload() const {
  return force_sensor_variables_.mounted_payload +
         force_sensor_variables_.grasped_payload;
}

PostSensorPayload ForceTorqueSensorFeature::GetMountedPayload() const {
  return force_sensor_variables_.mounted_payload;
}

PostSensorPayload ForceTorqueSensorFeature::GetGraspedPayload() const {
  return force_sensor_variables_.grasped_payload;
}

void ForceTorqueSensorFeature::SetMountedPayload(
    const PostSensorPayload& payload) {
  force_sensor_variables_.mounted_payload = payload;
}

void ForceTorqueSensorFeature::SetGraspedPayload(
    const PostSensorPayload& payload) {
  force_sensor_variables_.grasped_payload = payload;
}

RealtimeStatus ForceTorqueSensorFeature::ReadStatus(
    RealtimePartInterface::ReadStatusParameters params) {
  // Fail in case the force-sensor-controller reports non-ok statuses. This
  // will allow ICON to fail cleanly in case of errors at device level.
  if (std::holds_alternative<ForceTorqueStatusHardwareInterface>(
          input_status_handle_)) {
    auto status_code =
        std::get<ForceTorqueStatusHardwareInterface>(input_status_handle_)
            ->status_code();
    auto raw_status_code =
        std::get<ForceTorqueStatusHardwareInterface>(input_status_handle_)
            ->raw_status_code();
    if (status_code != intrinsic_fbs::ForceSensorStatusCode::Ok) {
      auto status_message =
          RealtimeStatus::StrCat(intrinsic_fbs::ToFixedString(status_code),
                                 " (0x", absl::Hex(raw_status_code), ")");
      INTRINSIC_RT_LOG_THROTTLED(ERROR)
          << "Status of force/torque sensor is not OK. Status is: "
          << status_message;
      return icon::InternalError(status_message);
    }
  }

  // Get current robot joint state, to be used by gravity compensation and
  // dynamic load estimation.
  // Get joint angles in order used by the skeleton
  INTRINSIC_RT_ASSIGN_OR_RETURN(const JointStatePVA current_hw_joint_state,
                                ReadJointState());
  JointStatePVA current_full_joint_state;
  {
    // There's a bit of a dance here: the model might include joints that we
    // do not have readings for, but that we don't care about (see
    // `initialize()` for details).
    // To make sure we provide the right size of vector to SetStatePVA below,
    // we first read back the current state from `kinematics_state_`, and only
    // override the first N DoFs with new values from `joint_state_reader_`.
    JointStateP old_joint_positions = kinematics_state_->GetDofPositions();
    JointStateV old_joint_velocities = kinematics_state_->GetDofVelocities();
    JointStateA old_joint_accelerations =
        kinematics_state_->GetDofAccelerations();
    // If old_joint_positions has N members, then it *must* be possible to
    // resize current_full_joint_state to the same size!
    INTRINSIC_RT_RETURN_IF_ERROR(
        current_full_joint_state.SetSize(old_joint_positions.size()));
    current_full_joint_state.position = old_joint_positions.position;
    current_full_joint_state.velocity = old_joint_velocities.velocity;
    current_full_joint_state.acceleration =
        old_joint_accelerations.acceleration;
    if (!current_full_joint_state.IsSizeConsistent()) {
      return InternalError(
          "The current full joint state is not of consistent size.");
    }

    // Overwrite the first N joints
    for (int i = 0; i < current_hw_joint_state.size(); ++i) {
      current_full_joint_state.position[i] = current_hw_joint_state.position[i];
      current_full_joint_state.velocity[i] = current_hw_joint_state.velocity[i];
      current_full_joint_state.acceleration[i] =
          current_hw_joint_state.acceleration[i];
    }
  }
  INTRINSIC_RT_RETURN_IF_ERROR(
      kinematics_state_->SetStatePVA(current_full_joint_state));

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      Pose3d base_t_target,
      kinematics_state_->GetTransform(config_.target_link_element_id));

  const PostSensorPayload total_payload = GetTotalPayload();

  // Compute the static wrench currently exerted to the FT-sensor by the
  // support_mass.
  Pose3d robot_t_ft = base_t_target * config_.target_t_ft;
  force_sensor_variables_.wrench_at_ft_support_mass =
      ComputeWrenchFtSupportMass(total_payload.mass_kg, robot_t_ft,
                                 total_payload.ft_t_cog);

  // Run post-sensor dynamic load computation if requested.
  if (joint_velocity_handle_.has_value() && joint_acceleration_.has_value() &&
      config_.estimate_post_sensor_dynamic_load) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        const ComputePostSensorDynamicsResult post_sensor_dynamic_load_result,
        ComputePostSensorDynamicLoad(current_hw_joint_state.velocity,
                                     current_hw_joint_state.acceleration,
                                     config_.target_link_element_id,
                                     base_t_target, config_.target_t_ft,
                                     total_payload, kinematics_state_.get()));
    force_sensor_variables_.post_sensor_dynamic_load_at_ft =
        post_sensor_dynamic_load_result.dynamic_load_at_ft;
    force_sensor_variables_.post_sensor_dynamic_load_at_tip =
        post_sensor_dynamic_load_result.dynamic_load_at_target;
  }

  Wrench wrench_at_ft = Wrench::ZERO;

  // Do the read before setting the "taring completed" state.
  //
  // For in-part taring, this will make the taring state update within the same
  // cycle that taring stops.
  INTRINSIC_RT_ASSIGN_OR_RETURN(eigenmath::Vector6d new_reading,
                                ReadWrenchFromDevice(current_full_joint_state));

  if (!taring_state_.completed) {
    // Wrench values are unreliable during taring and often contain artificial
    // impulses that can corrupt the stability index, leading to false
    // positives. To prevent this, the stability monitor is reset until taring
    // completes. This reset must occur before setting the "taring completed"
    // state to catch taring requests that start and finish within the same
    // cycle.
    wrench_stability_monitor_->Reset();
    force_sensor_variables_.wrench_stability_index = 0.0;
  }

  // Store the "taring completed" state if completed.
  if (std::holds_alternative<ForceTorqueStatusHardwareInterface>(
          input_status_handle_)) {
    taring_state_.completed =
        std::get<ForceTorqueStatusHardwareInterface>(input_status_handle_)
            ->retare_completed();
  } else {
    taring_state_.completed =
        !taring_data_joint_torque_interface_only_.TaringInProgress();
  }

  // Check if we are still receiving data from the force sensor, but only if
  // no taring operation is ongoing. During taring all values are
  // meaningless and therefore need to be disregarded.
  if (taring_state_.completed) {
    if (std::holds_alternative<JointTorqueStateHardwareInterface>(
            input_status_handle_) ||
        (std::holds_alternative<ForceTorqueStatusHardwareInterface>(
             input_status_handle_) &&
         std::get<ForceTorqueStatusHardwareInterface>(input_status_handle_)
             ->enabled())) {
      constant_readings_counter_.AddReading(
          force_sensor_variables_
              .wrench_at_ft_unprocessed,  // still holds values from
                                          // previous read cycle!
          new_reading                     // new measurement.
      );

      if ((constant_readings_counter_.num_constant_readings() >
           config_.num_acceptable_constant_readings)) {
        return InternalError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
            "Force sensor reporting constant values: count= ",
            constant_readings_counter_.num_constant_readings(), ", force= [",
            eigenmath::ToFixedString(-new_reading),
            ". This indicates a possible FT-sensor failure, will raise an "
            "error in force_sensor_status."));
      }
    }
  }

  // This updates wrench_at_ft_unprocessed to the current reading.
  force_sensor_variables_.wrench_at_ft_unprocessed = new_reading;

  force_sensor_variables_.wrench_at_ft_uncompensated =
      -force_sensor_variables_.wrench_at_ft_unprocessed;

  // IMPORTANT! This is the key step in this controller. Since taring
  // happens before support mass compensation, the snapshot of the support
  // mass wrench taken at taring time needs to be subtracted here.
  wrench_at_ft = force_sensor_variables_.wrench_at_ft_uncompensated +
                 force_sensor_variables_.wrench_at_ft_support_mass -
                 force_sensor_variables_.wrench_at_ft_bias;

  // Wrench at the target point compensated for support_mass
  Wrench wrench_at_target = TransformWrench(config_.target_t_ft, wrench_at_ft);

  // Copy into variables made available in feature interfaces.
  force_sensor_variables_.wrench_at_ft = wrench_at_ft;
  force_sensor_variables_.wrench_at_target = wrench_at_target;

  // Update the stability index based on latest data (if no taring ongoing).
  if (taring_state_.completed) {
    INTRINSIC_RT_RETURN_IF_ERROR(
        wrench_stability_monitor_->Update(wrench_at_target));
    force_sensor_variables_.wrench_stability_index =
        wrench_stability_monitor_->GetStabilityIndex();
  }

  return OkStatus();
}

RealtimeStatus ForceTorqueSensorFeature::ApplyCommand(
    RealtimePartInterface::ApplyCommandParameters params) {
  const PostSensorPayload total_payload = GetTotalPayload();

  // Record the current F/T due to support_mass as bias when taring. Will
  // be subtracted later during load compensation.
  if (taring_state_.requested) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        Pose3d base_t_target,
        kinematics_state_->GetTransform(config_.target_link_element_id));
    Pose3d robot_t_ft = base_t_target * Pose3d(config_.target_t_ft);
    force_sensor_variables_.wrench_at_ft_bias = ComputeWrenchFtSupportMass(
        total_payload.mass_kg, robot_t_ft, total_payload.ft_t_cog);
    INTRINSIC_RT_LOG(INFO) << "Storing 'wrench_at_ft_bias' for sensor";

    if (force_torque_command_handle_) {
      // Tare at the device level if a FT-sensor hardware interface is
      // available.
      (*force_torque_command_handle_)->mutate_retare(true);
      (*force_torque_command_handle_)
          ->mutate_num_taring_cycles(taring_state_.requested_num_taring_cycles);
    } else {
      // Tare here in the part if FT-values are estimated from joint torque
      // data.
      taring_data_joint_torque_interface_only_.StartTare(
          taring_state_.requested_num_taring_cycles);
    }

    // Reset to avoid triggering taring multiple times.
    taring_state_.requested = false;
    taring_state_.completed = false;
  } else {
    if (force_torque_command_handle_) {
      (*force_torque_command_handle_)->mutate_retare(false);
    }
  }
  if (force_torque_command_handle_) {
    force_torque_command_handle_->UpdatedAt(Clock::now());
  }

  return OkStatus();
}

RealtimeStatus ForceTorqueSensorFeature::Tare(int num_taring_cycles) {
  // Only allow a new taring request when the ongoing taring cycle has been
  // completed.
  if (taring_state_.completed) {
    taring_state_.requested = true;
    taring_state_.completed = false;
    taring_state_.requested_num_taring_cycles = num_taring_cycles;
  }

  return OkStatus();
}

RealtimeStatusOr<bool> ForceTorqueSensorFeature::TareIsDone() const {
  return taring_state_.completed;
}

}  // namespace intrinsic::icon
