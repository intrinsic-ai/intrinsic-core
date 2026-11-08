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

#include "intrinsic/icon/control/parts/hal/force_torque_sensor_part/hal_force_torque_sensor_part_test_fixture.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "grpcpp/server_builder.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/context.h"
#include "intrinsic/icon/control/parts/hal/force_torque_sensor_part/hal_force_torque_sensor_part.h"
#include "intrinsic/icon/control/parts/hal/force_torque_sensor_part/hal_force_torque_sensor_part_config.pb.h"
#include "intrinsic/icon/control/parts/new_manipulator_kinematics.h"
#include "intrinsic/icon/control/services/kinematics_from_world_service.h"
#include "intrinsic/icon/control/services/kinematics_service.h"
#include "intrinsic/icon/control/services/service_collection.h"
#include "intrinsic/icon/control/services/world_from_grpc_service.h"
#include "intrinsic/icon/control/services/world_service.h"
#include "intrinsic/icon/hal/hardware_interface_registry.h"
#include "intrinsic/icon/hal/hardware_module_init_context.h"
#include "intrinsic/icon/hal/hardware_module_manager.h"
#include "intrinsic/icon/hal/hardware_module_proxy.h"
#include "intrinsic/icon/hal/hardware_module_runtime.h"
#include "intrinsic/icon/hal/interfaces/force_sensor.fbs.h"
#include "intrinsic/icon/hal/interfaces/force_torque.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_state.fbs.h"
#include "intrinsic/icon/hal/module_config.h"
#include "intrinsic/icon/hal/proto/hardware_module_config.pb.h"
#include "intrinsic/icon/hardware_modules/fake/fake_module_config.pb.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/shared_memory_manager.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/testing/unique_segment_name.h"
#include "intrinsic/icon/proto/linear_joint_acceleration_filter_config.pb.h"
#include "intrinsic/icon/utils/realtime_guard.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/kinematics/ik/inverse_kinematics_interface.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/ppr_component.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/service/test/world_service_fake.h"
#include "intrinsic/world/world.h"

namespace intrinsic::icon {
namespace {

constexpr int kDefaultNDof = 3;
constexpr double kPVNoise = 0.001;

}  // namespace

intrinsic_proto::icon::LinearJointAccelerationFilterConfig
CreateDefaultLinearJointAccelerationFilterConfig() {
  intrinsic_proto::icon::LinearJointAccelerationFilterConfig
      linear_joint_acceleration_filter_config;
  linear_joint_acceleration_filter_config.set_joint_position_process_noise(
      kPVNoise);
  linear_joint_acceleration_filter_config.set_joint_velocity_process_noise(
      kPVNoise);
  linear_joint_acceleration_filter_config.set_joint_acceleration_process_noise(
      0.5);
  linear_joint_acceleration_filter_config.set_joint_jerk_process_noise(1250);
  linear_joint_acceleration_filter_config.set_joint_position_measurement_noise(
      kPVNoise);
  linear_joint_acceleration_filter_config.set_joint_velocity_measurement_noise(
      kPVNoise);
  intrinsic_proto::icon::DiscreteAlgebraicRicattiEquationSolutionConfig
      dare_config;
  dare_config.set_max_iterations(5000);
  dare_config.set_accuracy(1e-7);
  dare_config.set_regularizer_epsilon(1e-7);
  dare_config.set_dare_iteration_strategy(
      intrinsic_proto::icon::DiscreteAlgebraicRicattiEquationSolutionConfig::
          DARE_ITERATION_STRATEGY_ROBUST);
  *linear_joint_acceleration_filter_config.mutable_dare_settings() =
      dare_config;
  return linear_joint_acceleration_filter_config;
}

absl::StatusOr<World> MakeDefaultRobotWorld(bool moveable_sensor_link,
                                            const Pose3d& tip_t_ft) {
  World test_world = World::CreateEmptyWorld();
  INTR_ASSIGN_OR_RETURN(
      auto robot_data,
      CreateLinearChainRobot(
          &test_world,
          LinearChainRobotParams{
              .robot_name = kRobotCollectionName,
              .robot_alias = kRobotCollectionName,
              .link_params =
                  {
                      {.name = "base_link"},
                      {.name = "link_mast_pan"},
                      {.name = "link_mast_tilt"},
                      {.name = kTargetLinkName},
                      {.name = kForceTorqueSensorLinkName},
                  },
              .joint_params =
                  {
                      {
                          .name = "jaco_joint_mast_pan",
                          .parent_t_inboard =
                              Pose3d(eigenmath::Vector3d{0, 0, 1.0}),
                          .axis = eigenmath::Vector3d::UnitZ(),
                          .lower_value_limit = -3.7,
                          .upper_value_limit = 2.15,
                      },
                      {
                          .name = "jaco_joint_mast_tilt",
                          .parent_t_inboard =
                              Pose3d(eigenmath::Vector3d{0, 0, 1.0}),
                          .axis = eigenmath::Vector3d::UnitY(),
                          .lower_value_limit = -1.55,
                          .upper_value_limit = 1.55,
                      },
                      {
                          .name = "jaco_joint_mast_wrist",
                          .parent_t_inboard =
                              Pose3d(eigenmath::Vector3d{1.0, 0, 0}),
                          .axis = eigenmath::Vector3d::UnitX(),
                      },
                      {
                          .name = "ft_sensor_fixed_joint",
                          .parent_t_inboard = tip_t_ft,
                          .motion_type =
                              moveable_sensor_link
                                  ? intrinsic_proto::world::
                                        KinematicsComponent::
                                            MOTION_TYPE_REVOLUTE
                                  : intrinsic_proto::world::
                                        KinematicsComponent::MOTION_TYPE_FIXED,
                      },
                  },
              .add_solver_key = true,
              .resource_name = kRobotCollectionName,
          }));
  INTR_ASSIGN_OR_RETURN(WorldEntity * robot_entity,
                        test_world.GetEntityById(robot_data.robot_id));
  INTR_ASSIGN_OR_RETURN(PPRComponent * ppr_component,
                        robot_entity->GetOrCreateComponent<PPRComponent>());
  ppr_component->SetResourceName(kRobotCollectionName);
  return std::move(test_world);
}

absl::Status ArmModule::Init(
    intrinsic::icon::HardwareModuleInitContext& init_context) {
  intrinsic::icon::HardwareInterfaceRegistry& interface_registry =
      init_context.GetInterfaceRegistry();
  INTR_ASSIGN_OR_RETURN(
      joint_position_state_,
      interface_registry
          .AdvertiseMutableInterface<JointPositionStateHardwareInterface>(
              kPositionStateInterfaceName, kDefaultNDof));
  INTR_ASSIGN_OR_RETURN(
      joint_velocity_state_,
      interface_registry
          .AdvertiseMutableInterface<JointVelocityStateHardwareInterface>(
              kVelocityStateInterfaceName, kDefaultNDof));
  INTR_ASSIGN_OR_RETURN(
      joint_acceleration_state_,
      interface_registry
          .AdvertiseMutableInterface<JointAccelerationStateHardwareInterface>(
              kAccelerationStateInterfaceName, kDefaultNDof));
  INTR_ASSIGN_OR_RETURN(
      external_joint_torque_state_,
      interface_registry
          .AdvertiseMutableInterface<intrinsic_fbs::JointTorqueState>(
              kExternalJointTorqueInterfaceName, kDefaultNDof));

  return absl::OkStatus();
}

RealtimeStatus ArmModule::SetJointPositionState(eigenmath::VectorNd position) {
  if (position.size() < kDefaultNDof) {
    return InvalidArgumentError(RealtimeStatus::StrCat(
        "Position vector size is smaller than expected: ", position.size(),
        " vs ", kDefaultNDof, "."));
  }
  for (int i = 0; i < kDefaultNDof; ++i) {
    joint_position_state_->mutable_position()->Mutate(i, position[i]);
  }
  return OkStatus();
}

RealtimeStatus ArmModule::SetJointVelocityState(eigenmath::VectorNd velocity) {
  if (velocity.size() < kDefaultNDof) {
    return InvalidArgumentError(RealtimeStatus::StrCat(
        "Velocity vector size is smaller than expected: ", velocity.size(),
        " vs ", kDefaultNDof, "."));
  }
  for (int i = 0; i < kDefaultNDof; ++i) {
    joint_velocity_state_->mutable_velocity()->Mutate(i, velocity[i]);
  }
  return OkStatus();
}

RealtimeStatus ArmModule::SetJointAccelerationState(
    eigenmath::VectorNd acceleration) {
  if (acceleration.size() < kDefaultNDof) {
    return InvalidArgumentError(RealtimeStatus::StrCat(
        "Acceleration vector size is smaller than expected: ",
        acceleration.size(), " vs ", kDefaultNDof, "."));
  }
  for (int i = 0; i < kDefaultNDof; ++i) {
    joint_acceleration_state_->mutable_acceleration()->Mutate(i,
                                                              acceleration[i]);
  }
  return OkStatus();
}

RealtimeStatus ArmModule::SetExternalJointTorque(eigenmath::VectorNd torque) {
  if (torque.size() < kDefaultNDof) {
    return InvalidArgumentError(RealtimeStatus::StrCat(
        "Torque vector size is smaller than expected: ", torque.size(), " vs ",
        kDefaultNDof, "."));
  }
  for (int i = 0; i < kDefaultNDof; ++i) {
    external_joint_torque_state_->mutable_torque()->Mutate(i, torque[i]);
  }
  return OkStatus();
}

absl::Status ForceTorqueModule::Init(
    intrinsic::icon::HardwareModuleInitContext& init_context) {
  INTRINSIC_ASSERT_NON_REALTIME();
  intrinsic::icon::HardwareInterfaceRegistry& interface_registry =
      init_context.GetInterfaceRegistry();
  INTR_ASSIGN_OR_RETURN(
      force_torque_command_handle_,
      interface_registry
          .AdvertiseMutableInterface<ForceTorqueCommandHardwareInterface>(
              kForceTorqueCommand));
  INTR_ASSIGN_OR_RETURN(
      force_torque_status_handle_,
      interface_registry
          .AdvertiseMutableInterface<ForceTorqueStatusHardwareInterface>(
              kForceTorqueStatus));

  force_torque_status_handle_->mutate_retare_completed(true);
  force_torque_status_handle_->mutate_raw_status_code(0);
  force_torque_status_handle_->mutate_status_code(
      intrinsic_fbs::ForceSensorStatusCode::Ok);

  return absl::OkStatus();
}

RealtimeStatus ForceTorqueModule::ApplyCommand() {
  if (force_torque_command_handle_->retare()) {
    taring_ = true;
    tare_cycle_counter_ = 0;
    tare_for_ = force_torque_command_handle_->num_taring_cycles();
    force_torque_status_handle_->mutate_retare_completed(false);
  }
  return OkStatus();
}

RealtimeStatus ForceTorqueModule::ReadStatus() {
  if (taring_) {
    if (tare_cycle_counter_ < tare_for_) {
      tare_cycle_counter_++;
    } else {
      force_torque_status_handle_->mutate_retare_completed(true);
      taring_ = false;
      tare_cycle_counter_ = 0;
    }
  }
  return OkStatus();
}

void ForceTorqueModule::SetForceTorque(const Wrench& wrench) {
  force_torque_status_handle_->mutable_wrench()->mutate_x(wrench[0]);
  force_torque_status_handle_->mutable_wrench()->mutate_y(wrench[1]);
  force_torque_status_handle_->mutable_wrench()->mutate_z(wrench[2]);
  force_torque_status_handle_->mutable_wrench()->mutate_rx(wrench[3]);
  force_torque_status_handle_->mutable_wrench()->mutate_ry(wrench[4]);
  force_torque_status_handle_->mutable_wrench()->mutate_rz(wrench[5]);
}

void ForceTorqueModule::SetEnabled(const bool enabled) {
  force_torque_status_handle_->mutate_enabled(enabled);
}

void ForceTorqueModule::SetError(
    intrinsic_fbs::ForceSensorStatusCode status_code) {
  if (status_code == intrinsic_fbs::ForceSensorStatusCode::Ok) {
    force_torque_status_handle_->mutate_raw_status_code(0);
  } else {
    force_torque_status_handle_->mutate_raw_status_code(1);
  }
  force_torque_status_handle_->mutate_status_code(status_code);
}

intrinsic_proto::icon::HalForceTorqueSensorPartConfig
CreateDefaultHalFTSensorPartConfig() {
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_sensor_part_config;

  ft_sensor_part_config.mutable_force_torque_state()->set_interface_name(
      ForceTorqueModule::kForceTorqueStatus);
  ft_sensor_part_config.mutable_force_torque_state()->set_module_name(
      kFTModuleName);
  ft_sensor_part_config.mutable_force_torque_command()->set_interface_name(
      ForceTorqueModule::kForceTorqueCommand);
  ft_sensor_part_config.mutable_force_torque_command()->set_module_name(
      kFTModuleName);
  ft_sensor_part_config.mutable_joint_position_state()->set_interface_name(
      ArmModule::kPositionStateInterfaceName);
  ft_sensor_part_config.mutable_joint_position_state()->set_module_name(
      kArmModuleName);
  ft_sensor_part_config.mutable_joint_velocity_state()->set_interface_name(
      ArmModule::kVelocityStateInterfaceName);
  ft_sensor_part_config.mutable_joint_velocity_state()->set_module_name(
      kArmModuleName);
  ft_sensor_part_config.mutable_joint_acceleration_state()->set_interface_name(
      ArmModule::kAccelerationStateInterfaceName);
  ft_sensor_part_config.mutable_joint_acceleration_state()->set_module_name(
      kArmModuleName);
  ft_sensor_part_config.set_world_robot_collection_name(kRobotCollectionName);
  ft_sensor_part_config.set_target_link_name(kTargetLinkName);
  ft_sensor_part_config.set_ft_sensor_link_name(kForceTorqueSensorLinkName);

  ft_sensor_part_config.set_support_mass(kDefaultMountedPayloadMass);
  ft_sensor_part_config.mutable_ft_t_cog()->Add(kDefaultMountedPayloadCoG[0]);
  ft_sensor_part_config.mutable_ft_t_cog()->Add(kDefaultMountedPayloadCoG[1]);
  ft_sensor_part_config.mutable_ft_t_cog()->Add(kDefaultMountedPayloadCoG[2]);
  ft_sensor_part_config.set_num_acceptable_constant_readings(100000);

  return ft_sensor_part_config;
}

void HalForceTorqueSensorPartTest::Init(World world,
                                        absl::string_view arm_hwm_config) {
  WorldHashMap<std::string, World> worlds;
  worlds.emplace("test_world", std::move(world));

  ASSERT_OK_AND_ASSIGN(world_service_,
                       FakeWorldService::Create(std::move(worlds)));
  ASSERT_OK_AND_ASSIGN(auto world_from_grpc_service,
                       WorldFromGrpcService::Create(
                           {}, "test_world", world_service_->NewObjectStub()));
  ASSERT_OK_AND_ASSIGN(
      auto kinematics_from_world_service,
      KinematicsFromWorldService::Create({}, world_from_grpc_service.get()));
  ASSERT_OK_AND_ASSIGN(service_collection_,
                       ServiceCollection::CreateFromInitializedServices(
                           std::move(world_from_grpc_service),
                           std::move(kinematics_from_world_service)));
  WorldService* icon_world_service =
      service_collection_.GetServiceOrNull<WorldService>();
  ASSERT_NE(icon_world_service, nullptr);
  ASSERT_NE(icon_world_service->GetObjectWorldClient(), nullptr);

  KinematicsService* kinematics_service =
      service_collection_.GetServiceOrNull<KinematicsService>();
  std::unique_ptr<kinematics::InverseKinematicsInterface>
      inverse_kinematics_interface;
  std::unique_ptr<kinematics::Skeleton> kinematics_model;
  kinematics_model =
      kinematics_service->CreateKinematicsModelForPart(kRobotCollectionName);
  inverse_kinematics_interface =
      kinematics_service->CreateInverseKinematicsSolverForPart(
          kRobotCollectionName);

  kinematics_ = std::make_unique<NewManipulatorKinematicsImpl>(
      std::move(inverse_kinematics_interface), std::move(kinematics_model));

  intrinsic_proto::icon::HardwareModuleConfig ft_hw_module_config;
  ft_hw_module_config.set_name(kFTModuleName);
  auto ft_module = std::make_unique<ForceTorqueModule>();
  ft_module_ptr_ = ft_module.get();

  std::string shared_memory_namespace =
      intrinsic::icon::UniqueMemoryNamespace();

  {
    ASSERT_OK_AND_ASSIGN(auto shm_manager,
                         intrinsic::icon::SharedMemoryManager::Create(
                             shared_memory_namespace, kFTModuleName));
    ASSERT_OK_AND_ASSIGN(auto ft_module_runtime,
                         HardwareModuleRuntime::Create(
                             std::move(shm_manager),
                             {
                                 nullptr,
                                 std::move(ft_module),
                                 ModuleConfig(ft_hw_module_config,
                                              shared_memory_namespace, nullptr),
                             }));
    ft_module_runtime_ = std::move(ft_module_runtime);
  }

  grpc::ServerBuilder builder;
  ASSERT_OK(ft_module_runtime_->Run(builder));

  intrinsic_proto::icon::HardwareModuleConfig arm_module_config =
      ParseTextProtoOrDie(arm_hwm_config);
  arm_module_config.set_name(kArmModuleName);
  {
    intrinsic_proto::icon::FakeModuleConfig config;
    ASSERT_TRUE(arm_module_config.module_config().UnpackTo(&config));
  }

  auto loopback_module = std::make_unique<ArmModule>();
  arm_module_ptr_ = loopback_module.get();

  {
    ASSERT_OK_AND_ASSIGN(auto shm_manager,
                         intrinsic::icon::SharedMemoryManager::Create(
                             shared_memory_namespace, kArmModuleName));
    ASSERT_OK_AND_ASSIGN(auto arm_module_runtime,
                         HardwareModuleRuntime::Create(
                             std::move(shm_manager),
                             {
                                 nullptr,
                                 std::move(loopback_module),
                                 ModuleConfig(arm_module_config,
                                              shared_memory_namespace, nullptr),
                             }));
    arm_module_runtime_ = std::move(arm_module_runtime);
  }

  ASSERT_OK(arm_module_runtime_->Run(builder));

  ASSERT_OK_AND_ASSIGN(
      auto ft_loopback_module_proxy,
      HardwareModuleProxy::Attach(shared_memory_namespace, kFTModuleName,
                                  absl::ZeroDuration()));
  ASSERT_OK_AND_ASSIGN(
      auto arm_loopback_module_proxy,
      HardwareModuleProxy::Attach(shared_memory_namespace, kArmModuleName,
                                  absl::ZeroDuration()));

  ASSERT_OK(hwm_manager_.Add(std::move(ft_loopback_module_proxy), false, {}));
  ASSERT_OK(hwm_manager_.Add(std::move(arm_loopback_module_proxy), false, {}));

  ft_module_ptr_->SetEnabled(true);
}

Context HalForceTorqueSensorPartTest::GetContext() {
  return Context(&service_collection_, &hwm_manager_, {});
}

}  // namespace intrinsic::icon
