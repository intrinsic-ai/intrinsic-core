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

#include "intrinsic/icon/control/parts/hal/arm_part/hal_arm_part.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/support/channel_arguments.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/shape_factory.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/icon/control/context.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/control/parts/hal/arm_part/hal_arm_part_config.pb.h"
#include "intrinsic/icon/control/parts/hal/v1/hal_part_config.pb.h"
#include "intrinsic/icon/control/parts/part_test_helper.h"
#include "intrinsic/icon/control/parts/proto/v1/mode_of_safe_operation_limits_config.pb.h"
#include "intrinsic/icon/control/parts/realtime_part_factory_common.h"
#include "intrinsic/icon/control/realtime_bridge_types.h"
#include "intrinsic/icon/control/safety/extern/safety_status.fbs.h"
#include "intrinsic/icon/control/services/assembly_from_world_service.h"
#include "intrinsic/icon/control/services/assembly_service.h"
#include "intrinsic/icon/control/services/dynamics_service.h"
#include "intrinsic/icon/control/services/geometry_from_geometry_library.h"
#include "intrinsic/icon/control/services/kinematics_from_world_service.h"
#include "intrinsic/icon/control/services/kinematics_service.h"
#include "intrinsic/icon/control/services/service.h"
#include "intrinsic/icon/control/services/service_collection.h"
#include "intrinsic/icon/control/services/world_from_grpc_service.h"
#include "intrinsic/icon/control/services/world_service.h"
#include "intrinsic/icon/dynamics/mock_rigid_body_interface.h"
#include "intrinsic/icon/dynamics/rigid_body_interface.h"
#include "intrinsic/icon/flatbuffers/transform_types.fbs.h"
#include "intrinsic/icon/hal/default_hardware_interfaces.h"  // IWYU pragma: keep
#include "intrinsic/icon/hal/hardware_interface_handle.h"
#include "intrinsic/icon/hal/hardware_interface_registry.h"
#include "intrinsic/icon/hal/hardware_module_init_context.h"
#include "intrinsic/icon/hal/hardware_module_interface.h"
#include "intrinsic/icon/hal/hardware_module_manager.h"
#include "intrinsic/icon/hal/hardware_module_proxy.h"
#include "intrinsic/icon/hal/hardware_module_registry.h"
#include "intrinsic/icon/hal/hardware_module_runtime.h"
#include "intrinsic/icon/hal/interfaces/control_mode.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_command.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_limits.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_state.fbs.h"
#include "intrinsic/icon/hal/interfaces/payload_command.fbs.h"
#include "intrinsic/icon/hal/interfaces/payload_state.fbs.h"
#include "intrinsic/icon/hal/interfaces/robot_payload_utils.h"
#include "intrinsic/icon/hal/module_config.h"
#include "intrinsic/icon/hal/proto/hardware_module_config.pb.h"
#include "intrinsic/icon/hal/test/test_hardware_module.h"
#include "intrinsic/icon/hal/test/test_hardware_module_config.h"
#include "intrinsic/icon/hardware_modules/fake/fake_module.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/shared_memory_manager.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/testing/unique_segment_name.h"
#include "intrinsic/icon/proto/joint_position_pid_torque_controller_config.pb.h"
#include "intrinsic/icon/proto/linear_joint_acceleration_filter_config.pb.h"
#include "intrinsic/icon/proto/safety_status.pb.h"
#include "intrinsic/icon/server/config/icon_main_config.pb.h"
#include "intrinsic/icon/utils/clock.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/ik/inverse_kinematics_interface.h"
#include "intrinsic/kinematics/ik/mock_inverse_kinematics.h"
#include "intrinsic/kinematics/model_interface.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/kinematics/test_utils.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/rt_thread.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/util/thread/thread_options.h"
#include "intrinsic/world/component/collision_component.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/dof_kinematic_view.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/robot_payload/robot_payload.h"
#include "intrinsic/world/robot_payload/robot_payload_base.h"
#include "intrinsic/world/service/test/utils.h"
#include "intrinsic/world/service/test/world_service_fake.h"
#include "intrinsic/world/test/world_test_utils.h"
#include "intrinsic/world/world.h"

using JointPositionCommandHardwareInterface =
    ::intrinsic_fbs::JointPositionCommand;
using JointAccelerationAndTorqueCommandHardwareInterface =
    ::intrinsic_fbs::JointAccelerationAndTorqueCommand;
using JointTorqueCommandHardwareInterface = ::intrinsic_fbs::JointTorqueCommand;
using JointPositionStateHardwareInterface = ::intrinsic_fbs::JointPositionState;
using JointVelocityStateHardwareInterface = ::intrinsic_fbs::JointVelocityState;
using ControlModeStatusHardwareInterface = ::intrinsic_fbs::ControlModeStatus;
using JointLimitsHardwareInterface = ::intrinsic_fbs::JointLimits;
using ::intrinsic::icon::MutableHardwareInterfaceHandle;
using ProcessWrenchCommandHardwareInterface = ::intrinsic_fbs::Wrench;
using PayloadCommandHardwareInterface = ::intrinsic_fbs::PayloadCommand;
using PayloadStateHardwareInterface = ::intrinsic_fbs::PayloadState;
using JointLimitsHardwareInterface = ::intrinsic_fbs::JointLimits;

using ::absl_testing::StatusIs;
using ::intrinsic::ParseTextProtoOrDie;
using ::intrinsic::eigenmath::testing::IsApprox;
using ::intrinsic::icon::test_hardware_module::TestHardwareModule;
using ::intrinsic_fbs::ControlMode;
using test_hardware_module::GetTestHardwareModuleConfig;
using ::testing::AllOf;
using ::testing::ByMove;
using ::testing::DoubleNear;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::Lt;
using ::testing::Return;

namespace intrinsic::icon {

namespace {

static constexpr char kHalArmPartName[] = "three_dof_hal_arm_part";
static constexpr char kSingleDofResourceId[] = "single_dof_resource_id";
constexpr double kControlFrequency = 10.0;
constexpr double kPVNoise = 0.001;
// Length and radius for the collision geometry capsule of each link in the
// simple robot that MakeFourLinkRobot builds.
constexpr double kLinkLength = 0.5;
constexpr double kLinkRadius = 0.1;

constexpr double kApplicationLimitFactor = 0.95;
constexpr double kSystemMaxPos = 10;
constexpr double kSystemMinPos = -10;
constexpr double kSystemMaxVel = 100;
constexpr double kSystemMaxAcc = 1000000;
constexpr double kSystemMaxJerk = 1000000;
constexpr double kSystemMaxTorque = 1;

constexpr double kCartesianMaxPos = 1;
constexpr double kCartesianTranslationMaxVel = 20;
constexpr double kCartesianTranslationMaxAcc = 10;
constexpr double kCartesianTranslationMaxJerk = 5;
constexpr double kCartesianRotationMaxVel = 15;
constexpr double kCartesianRotationMaxAcc = 10;
constexpr double kCartesianRotationMaxJerk = 5;

constexpr double kMountedPayloadMassKg = 5.0;

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

intrinsic_proto::icon::ArmPositionPidTorqueControllerConfig
CreateDefaultArmPositionPidTorqueControllerConfig(int num_dofs) {
  intrinsic_proto::icon::ArmPositionPidTorqueControllerConfig config;
  intrinsic_proto::icon::JointPositionPidTorqueControllerConfig joint_config;

  joint_config.set_kp(10);
  joint_config.set_ki(1);
  joint_config.set_kd(10);
  joint_config.set_max_torque(100);
  joint_config.set_alpha(1);
  joint_config.set_max_torque_delta_per_timestep(100);
  joint_config.set_max_integral_torque(100);
  joint_config.set_integration_window_seconds(100 / kControlFrequency);
  joint_config.set_integration_enable_range(0.1);
  joint_config.set_timestep(1 / kControlFrequency);

  // Add the same  config for each degree of freedom.
  for (int i = 0; i < num_dofs; ++i) {
    *config.add_joint_configs() = joint_config;
  }

  return config;
}

class SingleDofModule final : public HardwareModuleInterface {
 public:
  static constexpr char kPositionCommandInterfaceName[] =
      "joint_position_command";
  static constexpr char kAccelerationCommandInterfaceName[] =
      "joint_acceleration_command";
  static constexpr char kTorqueCommandInterfaceName[] = "joint_torque_command";
  static constexpr char kPositionStateInterfaceName[] = "joint_position_state";
  static constexpr char kVelocityStateInterfaceName[] = "joint_velocity_state";
  static constexpr char kControlModeStateInterfaceName[] = "control_mode_state";
  static constexpr char kProcessWrenchCommandInterfaceName[] =
      "process_wrench_command";
  static constexpr char kPayloadCommandInterfaceName[] = "payload_command";
  static constexpr char kPayloadStateInterfaceName[] = "payload_state";
  static constexpr char kJointSystemLimitsInterfaceName[] =
      "joint_system_limits";

  absl::Status Init(HardwareModuleInitContext& init_context) override {
    intrinsic::icon::HardwareInterfaceRegistry& interface_registry =
        init_context.GetInterfaceRegistry();
    INTR_ASSIGN_OR_RETURN(
        joint_position_command_,
        interface_registry
            .AdvertiseMutableInterface<JointPositionCommandHardwareInterface>(
                kPositionCommandInterfaceName, /*dof=*/1));

    INTR_ASSIGN_OR_RETURN(
        joint_acceleration_command_,
        interface_registry.AdvertiseMutableInterface<
            JointAccelerationAndTorqueCommandHardwareInterface>(
            kAccelerationCommandInterfaceName, /*dof=*/1));

    INTR_ASSIGN_OR_RETURN(
        joint_torque_command_,
        interface_registry
            .AdvertiseMutableInterface<JointTorqueCommandHardwareInterface>(
                kTorqueCommandInterfaceName, /*dof=*/1));

    INTR_ASSIGN_OR_RETURN(
        joint_position_state_,
        interface_registry
            .AdvertiseMutableInterface<JointPositionStateHardwareInterface>(
                kPositionStateInterfaceName, /*dof=*/1));

    INTR_ASSIGN_OR_RETURN(
        joint_velocity_state_,
        interface_registry
            .AdvertiseMutableInterface<JointVelocityStateHardwareInterface>(
                kVelocityStateInterfaceName, /*dof=*/1));

    INTR_ASSIGN_OR_RETURN(
        control_mode_state_,
        interface_registry
            .AdvertiseMutableInterface<ControlModeStatusHardwareInterface>(
                kControlModeStateInterfaceName,
                /*status=*/ControlMode::kCyclicPosition));

    INTR_ASSIGN_OR_RETURN(
        process_wrench_command_,
        interface_registry
            .AdvertiseMutableInterface<ProcessWrenchCommandHardwareInterface>(
                kProcessWrenchCommandInterfaceName));

    INTR_ASSIGN_OR_RETURN(
        payload_command_,
        interface_registry
            .AdvertiseMutableInterface<PayloadCommandHardwareInterface>(
                kPayloadCommandInterfaceName));

    INTR_ASSIGN_OR_RETURN(
        payload_state_,
        interface_registry
            .AdvertiseMutableInterface<PayloadStateHardwareInterface>(
                kPayloadStateInterfaceName));

    INTR_ASSIGN_OR_RETURN(
        joint_system_limits_,
        interface_registry.AdvertiseInterface<JointLimitsHardwareInterface>(
            kJointSystemLimitsInterfaceName, /*dof=*/1));

    return absl::OkStatus();
  }

  double GetJointPositionCommand() const {
    return joint_position_command_->position()->Get(0);
  }

  Time PositionCommandLastUpdatedTime() {
    return joint_position_command_.LastUpdatedTime();
  }

  double GetJointPositionState() const {
    return joint_position_state_->position()->Get(0);
  }

  // Sets the joint_position state.
  void SetJointPositionState(double position) {
    joint_position_state_->mutable_position()->Mutate(0, position);
  }

  // Sets the previous position command.
  void SetJointPositionCommand(double position) {
    joint_position_command_->mutable_position()->Mutate(0, position);
  }

  // Used to set velocity for tests states.
  void SetJointVelocityState(double velocity) {
    joint_velocity_state_->mutable_velocity()->Mutate(0, velocity);
  }

  // Used to retrieve the process wrench command for test states.
  Wrench GetProcessWrenchCommand() {
    Wrench wrench;
    wrench << process_wrench_command_->x(), process_wrench_command_->y(),
        process_wrench_command_->z(), process_wrench_command_->rx(),
        process_wrench_command_->ry(), process_wrench_command_->rz();
    return wrench;
  }

  absl::StatusOr<std::optional<RobotPayload>> GetCommandedPayload() {
    std::optional<RobotPayloadBase> payload;
    INTR_RETURN_IF_ERROR(CopyTo(*payload_command_->full_payload(), payload));
    return static_cast<std::optional<RobotPayload>>(payload);
  }

  absl::Status SetPayloadState(const std::optional<RobotPayloadBase>& payload) {
    return CopyTo(payload, *payload_state_->mutable_full_payload());
  }

  RealtimeStatus Activate() override { return OkStatus(); }

  RealtimeStatus Deactivate() override { return OkStatus(); }

  absl::Status EnableMotion() override {
    if (fail_transitions_) return absl::InternalError("EnableMotion failed");
    return absl::OkStatus();
  }

  absl::Status DisableMotion() override {
    if (fail_transitions_) return absl::InternalError("DisableMotion failed");
    return absl::OkStatus();
  }

  absl::Status ClearFaults() override {
    if (fail_transitions_) return absl::InternalError("ClearFaults failed");
    return absl::OkStatus();
  }

  absl::Status Shutdown() override { return absl::OkStatus(); }

  RealtimeStatus ApplyCommand() override {
    auto joint_pos_command = GetJointPositionCommand();
    joint_position_state_->mutable_position()->Mutate(0, joint_pos_command);
    return OkStatus();
  }

  RealtimeStatus ReadStatus() override {
    // NO-OP, Status is updated already through `ApplyCommand`.
    return OkStatus();
  }

  void SetFailingTransitions(bool fail_transitions) {
    fail_transitions_ = fail_transitions;
  }

 private:
  MutableHardwareInterfaceHandle<JointPositionCommandHardwareInterface>
      joint_position_command_;
  MutableHardwareInterfaceHandle<
      JointAccelerationAndTorqueCommandHardwareInterface>
      joint_acceleration_command_;
  MutableHardwareInterfaceHandle<JointTorqueCommandHardwareInterface>
      joint_torque_command_;
  MutableHardwareInterfaceHandle<JointPositionStateHardwareInterface>
      joint_position_state_;
  MutableHardwareInterfaceHandle<JointVelocityStateHardwareInterface>
      joint_velocity_state_;
  MutableHardwareInterfaceHandle<ControlModeStatusHardwareInterface>
      control_mode_state_;
  MutableHardwareInterfaceHandle<ProcessWrenchCommandHardwareInterface>
      process_wrench_command_;
  MutableHardwareInterfaceHandle<PayloadCommandHardwareInterface>
      payload_command_;
  MutableHardwareInterfaceHandle<PayloadStateHardwareInterface> payload_state_;
  HardwareInterfaceHandle<JointLimitsHardwareInterface> joint_system_limits_;
  bool fail_transitions_ = false;
};

static absl::StatusOr<LinearChainRobotData> MakeTwoLinkRobot(World& world) {
  INTR_ASSIGN_OR_RETURN(
      LinearChainRobotData robot_data,
      CreateLinearChainRobot(
          &world, LinearChainRobotParams{
                      .robot_name = kSingleDofResourceId,
                      .robot_alias = kSingleDofResourceId,
                      .parent_id = kRootEntityId,
                      .link_params = {{.name = "base"}, {.name = "tip"}},
                      .joint_params =
                          {
                              {.name = "base_tip_joint",
                               .parent_t_inboard =
                                   Pose3d(eigenmath::Vector3d{0, 0, 0.5})},
                          },
                      .add_coordinate_frame = false,
                      .add_solver_key = false,
                      .resource_name = kSingleDofResourceId}));
  return robot_data;
}

class HalArmPartTest : public ::testing::Test {
 public:
  static constexpr char kModuleName[] = "single_dof_module";
  static constexpr char kHalArmPartName[] = "single_dof_hal_arm_part";

  void SetUp() override {
    grpc::ServerBuilder builder;
    intrinsic_proto::icon::HardwareModuleConfig single_dof_config;
    single_dof_config.set_name(kModuleName);

    WorldHashMap<std::string, World> worlds;

    World two_link_robot_world = World::CreateEmptyWorld();
    ASSERT_OK_AND_ASSIGN(LinearChainRobotData robot_data,
                         MakeTwoLinkRobot(two_link_robot_world));
    ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<DofKinematicView> dof_view,
        two_link_robot_world.GetDofKinematicView(robot_data.robot_id));
    JointLimitsXd system_limits;
    system_limits.SetSize(1);
    system_limits.min_position.setConstant(kSystemMinPos);
    system_limits.max_position.setConstant(kSystemMaxPos);
    system_limits.max_velocity.setConstant(kSystemMaxVel);
    system_limits.max_acceleration.setConstant(kSystemMaxAcc);
    system_limits.max_jerk.setConstant(kSystemMaxJerk);
    system_limits.max_torque.setConstant(kSystemMaxTorque);
    ASSERT_OK(
        dof_view->SetDofSystemLimits(system_limits, /*enforce_limits=*/false));
    JointLimitsXd application_limits = system_limits;
    application_limits.max_velocity *= kApplicationLimitFactor;
    application_limits.max_acceleration *= kApplicationLimitFactor;
    application_limits.max_jerk *= kApplicationLimitFactor;
    ASSERT_OK(dof_view->SetDofApplicationLimits(application_limits,
                                                /*enforce_limits=*/false));

    ASSERT_OK_AND_ASSIGN(
        auto object_world,
        intrinsic::object_world::ObjectWorld::CreateView(two_link_robot_world));
    ASSERT_OK_AND_ASSIGN(intrinsic::object_world::KinematicObject * kine_object,
                         object_world->GetKinematicObject(
                             intrinsic::WorldObjectName(kSingleDofResourceId)));
    CartesianLimits cart_limits = intrinsic::CreateSimpleCartesianLimits(
        kCartesianMaxPos, kCartesianTranslationMaxVel,
        kCartesianTranslationMaxAcc, kCartesianTranslationMaxJerk,
        kCartesianRotationMaxVel, kCartesianRotationMaxAcc,
        kCartesianRotationMaxJerk);
    ASSERT_OK(kine_object->SetCartesianLimits(cart_limits));

    // Set limits for the test robot.
    worlds.emplace("test_world", std::move(two_link_robot_world));

    ASSERT_OK_AND_ASSIGN(world_service_, FakeWorldService::Create(
                                             /*worlds=*/std::move(worlds)));
    world_client_ = std::make_shared<world::ObjectWorldClient>(
        "test_world", world_service_->NewObjectStub());
    ASSERT_OK_AND_ASSIGN(
        auto world_from_grpc_service,
        WorldFromGrpcService::Create({}, "test_world",
                                     world_service_->NewObjectStub()));
    ASSERT_OK_AND_ASSIGN(
        auto assembly_from_world_service,
        AssemblyFromWorldService::Create({}, world_from_grpc_service.get()));
    ASSERT_OK_AND_ASSIGN(
        auto kinematics_from_world_service,
        KinematicsFromWorldService::Create({}, world_from_grpc_service.get()));
    ASSERT_OK_AND_ASSIGN(service_collection_,
                         ServiceCollection::CreateFromInitializedServices(
                             std::move(world_from_grpc_service),
                             std::move(assembly_from_world_service),
                             std::move(kinematics_from_world_service)));
    auto single_dof_module = std::make_unique<SingleDofModule>();
    module_ptr_ = single_dof_module.get();
    {  // Scope ensures moved objects are not used after move.
      ASSERT_OK_AND_ASSIGN(auto shm_manager,
                           intrinsic::icon::SharedMemoryManager::Create(
                               memory_namespace_, kModuleName));
      ASSERT_OK_AND_ASSIGN(
          auto module_runtime,
          HardwareModuleRuntime::Create(
              std::move(shm_manager),
              {
                  /*realtime_clock=*/nullptr,
                  std::move(single_dof_module),
                  ModuleConfig(single_dof_config,
                               /*shared_memory_namespace=*/memory_namespace_,
                               /*realtime_clock=*/nullptr),
              }));

      module_runtime_ = std::move(module_runtime);
    }
    ASSERT_OK(module_runtime_->Run(builder));
  }

  ~HalArmPartTest() override { EXPECT_OK(module_runtime_->Stop()); }

  std::shared_ptr<world::ObjectWorldClient> GetObjectWorldClient() const {
    return world_client_;
  }
  const std::string memory_namespace_ =
      intrinsic::icon::UniqueMemoryNamespace();
  SingleDofModule* module_ptr_;
  std::unique_ptr<HardwareModuleRuntime> module_runtime_;
  std::unique_ptr<FakeWorldService> world_service_;
  std::shared_ptr<world::ObjectWorldClient> world_client_;
  ServiceCollection service_collection_;
};

TEST_F(HalArmPartTest, SingleDofModuleMapsCorrectlyToFeatureInterface) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_acceleration_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_acceleration_command()->set_interface_name(
      SingleDofModule::kAccelerationCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  hal_arm_part_config.mutable_joint_velocity_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_velocity_state()->set_interface_name(
      SingleDofModule::kVelocityStateInterfaceName);
  *hal_arm_part_config.mutable_linear_joint_acceleration_filter_config() =
      CreateDefaultLinearJointAccelerationFilterConfig();
  hal_arm_part_config.mutable_process_wrench_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_process_wrench_command()->set_interface_name(
      SingleDofModule::kProcessWrenchCommandInterfaceName);
  hal_arm_part_config.mutable_payload_command()->set_module_name(kModuleName);
  hal_arm_part_config.mutable_payload_command()->set_interface_name(
      SingleDofModule::kPayloadCommandInterfaceName);
  hal_arm_part_config.mutable_payload_state()->set_module_name(kModuleName);
  hal_arm_part_config.mutable_payload_state()->set_interface_name(
      SingleDofModule::kPayloadStateInterfaceName);
  hal_arm_part_config.mutable_joint_system_limits()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_system_limits()->set_interface_name(
      SingleDofModule::kJointSystemLimitsInterfaceName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{.resource_id = kSingleDofResourceId});

  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  EXPECT_OK(
      ValidateGenericConfig(helper_with_part->GetPartFeatureInterfaces()
                                .SupportedFeatureInterfaceTypes(),
                            helper_with_part->part_config().generic_config()));

  const auto& feature_interface_reg =
      helper_with_part->GetPartFeatureInterfaces();
  // Joint Position Command, State, ProcessWrenchCommand and Limits were set and
  // correctly converted.
  EXPECT_NE(feature_interface_reg.GetInterface<JointPosition>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointAcceleration>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointPositionSensor>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointLimitsInterface>(),
            nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointVelocityEstimator>(),
            nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointAccelerationEstimator>(),
            nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<ProcessWrenchAtEndeffector>(),
            nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<Payload>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<PayloadState>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointLimitsInterface>(),
            nullptr);
  // MoveOk is implicit if joint  limits and position are defined
  EXPECT_NE(feature_interface_reg.GetInterface<MoveOk>(), nullptr);
  // All other feature interfaces were not present in the hal arm part config.
  EXPECT_EQ(feature_interface_reg.GetInterface<JointVelocity>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointTorque>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointTorqueSensor>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<CartesianLimitsInterface>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<ManipulatorKinematics>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<Dynamics>(), nullptr);
}

TEST_F(HalArmPartTest, WorldRobotCollectionNameOverridesResourceId) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  hal_arm_part_config.mutable_joint_velocity_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_velocity_state()->set_interface_name(
      SingleDofModule::kVelocityStateInterfaceName);
  *hal_arm_part_config.mutable_linear_joint_acceleration_filter_config() =
      CreateDefaultLinearJointAccelerationFilterConfig();
  hal_arm_part_config.mutable_process_wrench_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_process_wrench_command()->set_interface_name(
      SingleDofModule::kProcessWrenchCommandInterfaceName);
  hal_arm_part_config.mutable_payload_command()->set_module_name(kModuleName);
  hal_arm_part_config.mutable_payload_command()->set_interface_name(
      SingleDofModule::kPayloadCommandInterfaceName);
  hal_arm_part_config.mutable_payload_state()->set_module_name(kModuleName);
  hal_arm_part_config.mutable_payload_state()->set_interface_name(
      SingleDofModule::kPayloadStateInterfaceName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{.resource_id = "SomeResourceId"});

  ASSERT_OK_AND_ASSIGN(
      auto helper_with_part,
      PartTestHelper::CreateFromProtoFactory(
          {.part_name = kHalArmPartName,
           .context = context,
           .control_frequency_hz = kControlFrequency,
           // We explicitly set the resource id here and let the arm part fetch
           // the kinematics from the world service through the hardware
           // resource name name rather than its own resource id.
           .hardware_resource_name = kSingleDofResourceId},
          HalArmPart::kPartTypeName, hal_arm_part_config));

  EXPECT_OK(
      ValidateGenericConfig(helper_with_part->GetPartFeatureInterfaces()
                                .SupportedFeatureInterfaceTypes(),
                            helper_with_part->part_config().generic_config()));

  const auto& feature_interface_reg =
      helper_with_part->GetPartFeatureInterfaces();
  // Joint Position Command, State, ProcessWrenchCommand and Limits were set and
  // correctly converted.
  EXPECT_NE(feature_interface_reg.GetInterface<JointPosition>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointPositionSensor>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointLimitsInterface>(),
            nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointVelocityEstimator>(),
            nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointAccelerationEstimator>(),
            nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<ProcessWrenchAtEndeffector>(),
            nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<Payload>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<PayloadState>(), nullptr);
  // MoveOk is implicit if joint  limits and position are defined
  EXPECT_NE(feature_interface_reg.GetInterface<MoveOk>(), nullptr);
  // All other feature interfaces were not present in the hal arm part config.
  EXPECT_EQ(feature_interface_reg.GetInterface<JointVelocity>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointTorque>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointTorqueSensor>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<CartesianLimitsInterface>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<ManipulatorKinematics>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<Dynamics>(), nullptr);
}

TEST_F(HalArmPartTest, ProcessWrenchSetCorrectly) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;

  hal_arm_part_config.mutable_process_wrench_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_process_wrench_command()->set_interface_name(
      SingleDofModule::kProcessWrenchCommandInterfaceName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});
  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  auto& feature_interface_reg = helper_with_part->GetPartFeatureInterfaces();

  EXPECT_NE(feature_interface_reg.GetInterface<ProcessWrenchAtEndeffector>(),
            nullptr);
  auto process_wrench_feature =
      feature_interface_reg.GetInterface<ProcessWrenchAtEndeffector>();
  // Call ReadStatus to reset the feature interface.
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());
  Wrench test_wrench;
  test_wrench << 1, 2, 3, 4, 5, 6;
  process_wrench_feature->SetProcessWrenchAtTip(test_wrench);

  INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ApplyCommand());

  EXPECT_THAT(module_ptr_->GetProcessWrenchCommand(),
              IsApprox(test_wrench, 1e-12));
}

TEST_F(HalArmPartTest, PayloadIsForwardedToHardwareModule) {
  // Initialize the world in the service_collection_ with a payload.
  std::shared_ptr<world::ObjectWorldClient> world_client =
      GetObjectWorldClient();
  ASSERT_OK_AND_ASSIGN(world::KinematicObject robot,
                       world_client->GetKinematicObject(
                           intrinsic::WorldObjectName(kSingleDofResourceId)));
  ASSERT_OK_AND_ASSIGN(
      RobotPayload payload,
      RobotPayload::Create(kMountedPayloadMassKg, Pose3d::Identity(),
                           eigenmath::Matrix3d::Identity()));
  ASSERT_OK(world_client->UpdateMountedPayload(robot, payload));

  ASSERT_OK_AND_ASSIGN(auto world_from_grpc_service,
                       WorldFromGrpcService::Create(
                           {}, "test_world", world_service_->NewObjectStub()));
  ASSERT_OK_AND_ASSIGN(
      auto assembly_from_world_service,
      AssemblyFromWorldService::Create({}, world_from_grpc_service.get()));
  ASSERT_OK_AND_ASSIGN(
      auto kinematics_from_world_service,
      KinematicsFromWorldService::Create({}, world_from_grpc_service.get()));
  ASSERT_OK_AND_ASSIGN(service_collection_,
                       ServiceCollection::CreateFromInitializedServices(
                           std::move(world_from_grpc_service),
                           std::move(assembly_from_world_service),
                           std::move(kinematics_from_world_service)));

  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;

  hal_arm_part_config.mutable_payload_command()->set_module_name(kModuleName);
  hal_arm_part_config.mutable_payload_command()->set_interface_name(
      SingleDofModule::kPayloadCommandInterfaceName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});
  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  auto& feature_interface_reg = helper_with_part->GetPartFeatureInterfaces();
  EXPECT_NE(feature_interface_reg.GetInterface<Payload>(), nullptr);

  // Check that the payload flatbuffer is initialized correctly.
  {
    ASSERT_OK_AND_ASSIGN(std::optional<RobotPayload> got_payload,
                         module_ptr_->GetCommandedPayload());
    EXPECT_EQ(got_payload->mass(), kMountedPayloadMassKg);
  }

  // Payload stays the same after a cycle.
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ApplyCommand());

  {
    ASSERT_OK_AND_ASSIGN(std::optional<RobotPayload> got_payload,
                         module_ptr_->GetCommandedPayload());
    EXPECT_EQ(got_payload->mass(), kMountedPayloadMassKg);
  }

  // Clear the payload.
  ASSERT_OK(helper_with_part->GetPartPropertyAccess().SetBoolProperty(
      "full_payload_has_value", false));

  INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ApplyCommand());

  {
    ASSERT_OK_AND_ASSIGN(std::optional<RobotPayload> got_payload,
                         module_ptr_->GetCommandedPayload());
    EXPECT_EQ(got_payload, std::nullopt);
  }

  // Set a new payload.
  ASSERT_OK(helper_with_part->GetPartPropertyAccess().SetDoubleProperty(
      "full_payload_mass", 3.0));
  ASSERT_OK(helper_with_part->GetPartPropertyAccess().SetBoolProperty(
      "full_payload_has_value", true));

  INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ApplyCommand());

  {
    ASSERT_OK_AND_ASSIGN(std::optional<RobotPayload> got_payload,
                         module_ptr_->GetCommandedPayload());
    EXPECT_EQ(got_payload->mass(), 3.0);
  }
}

TEST_F(HalArmPartTest, UnsetPayloadIsForwardedToHardwareModule) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;

  hal_arm_part_config.mutable_payload_command()->set_module_name(kModuleName);
  hal_arm_part_config.mutable_payload_command()->set_interface_name(
      SingleDofModule::kPayloadCommandInterfaceName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});
  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  auto& feature_interface_reg = helper_with_part->GetPartFeatureInterfaces();
  EXPECT_NE(feature_interface_reg.GetInterface<Payload>(), nullptr);

  // Check that the payload flatbuffer is initialized correctly.
  {
    ASSERT_OK_AND_ASSIGN(std::optional<RobotPayload> got_payload,
                         module_ptr_->GetCommandedPayload());
    EXPECT_EQ(got_payload, std::nullopt);
  }
  // Payload stays the same after a cycle.
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ApplyCommand());

  {
    ASSERT_OK_AND_ASSIGN(std::optional<RobotPayload> got_payload,
                         module_ptr_->GetCommandedPayload());
    EXPECT_EQ(got_payload, std::nullopt);
  }
}

TEST_F(HalArmPartTest, PayloadNotSetIfNoWorld) {
  // Create a service collection without a world.
  service_collection_ = ServiceCollection();

  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;

  hal_arm_part_config.mutable_payload_command()->set_module_name(kModuleName);
  hal_arm_part_config.mutable_payload_command()->set_interface_name(
      SingleDofModule::kPayloadCommandInterfaceName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});
  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  auto& feature_interface_reg = helper_with_part->GetPartFeatureInterfaces();
  EXPECT_NE(feature_interface_reg.GetInterface<Payload>(), nullptr);

  // Check that the payload flatbuffer is initialized correctly.
  {
    ASSERT_OK_AND_ASSIGN(std::optional<RobotPayload> got_payload,
                         module_ptr_->GetCommandedPayload());
    EXPECT_EQ(got_payload, std::nullopt);
  }
  // Payload stays the same after a cycle.
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ApplyCommand());

  {
    ASSERT_OK_AND_ASSIGN(std::optional<RobotPayload> got_payload,
                         module_ptr_->GetCommandedPayload());
    EXPECT_EQ(got_payload, std::nullopt);
  }
}

TEST_F(HalArmPartTest, PayloadStateIsReadFromHardwareModule) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;

  hal_arm_part_config.mutable_payload_command()->set_module_name(kModuleName);
  hal_arm_part_config.mutable_payload_command()->set_interface_name(
      SingleDofModule::kPayloadCommandInterfaceName);
  hal_arm_part_config.mutable_payload_state()->set_module_name(kModuleName);
  hal_arm_part_config.mutable_payload_state()->set_interface_name(
      SingleDofModule::kPayloadStateInterfaceName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});
  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  auto& feature_interface_reg = helper_with_part->GetPartFeatureInterfaces();
  PayloadState* payload_status =
      feature_interface_reg.GetInterface<PayloadState>();
  ASSERT_NE(payload_status, nullptr);

  EXPECT_EQ(payload_status->GetActivePayload(), std::nullopt);

  // Payload stays the same after a cycle.
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ApplyCommand());

  EXPECT_EQ(payload_status->GetActivePayload(), std::nullopt);

  ASSERT_OK_AND_ASSIGN(RobotPayload expected_payload,
                       RobotPayload::Create(1.5, Pose3d::Identity(),
                                            eigenmath::Matrix3d::Identity()));

  ASSERT_OK(module_ptr_->SetPayloadState(expected_payload));

  INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());

  EXPECT_EQ(payload_status->GetActivePayload(), expected_payload);
}

TEST_F(HalArmPartTest, PayloadStateWithoutPayloadCommandFails) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_payload_state()->set_module_name(kModuleName);
  hal_arm_part_config.mutable_payload_state()->set_interface_name(
      SingleDofModule::kPayloadStateInterfaceName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});
  EXPECT_THAT(PartTestHelper::CreateFromProtoFactory(
                  {.part_name = kHalArmPartName,
                   .context = context,
                   .control_frequency_hz = kControlFrequency,
                   .hardware_resource_name = kSingleDofResourceId},
                  HalArmPart::kPartTypeName, hal_arm_part_config),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Cannot register a `payload_state`")));
}

TEST_F(HalArmPartTest, SingleDofModuleVelocityEstimateCorrectWithoutFiltering) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_velocity_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_velocity_state()->set_interface_name(
      SingleDofModule::kVelocityStateInterfaceName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});
  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  const auto& feature_interface_reg =
      helper_with_part->GetPartFeatureInterfaces();

  EXPECT_NE(feature_interface_reg.GetInterface<JointVelocityEstimator>(),
            nullptr);

  const JointVelocityEstimator* velocity_state_feature =
      feature_interface_reg.GetInterface<JointVelocityEstimator>();

  // Step change in joint velocity state.
  module_ptr_->SetJointVelocityState(10);
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ApplyCommand());
  EXPECT_THAT(velocity_state_feature->GetVelocityEstimate().velocity[0],
              DoubleNear(10, 1e-6));
}

TEST_F(HalArmPartTest, SingleDofModuleVelocityEstimateCorrectWithFiltering) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_velocity_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_velocity_state()->set_interface_name(
      SingleDofModule::kVelocityStateInterfaceName);
  hal_arm_part_config.set_velocity_filter_cutoff_frequency(0.2 *
                                                           kControlFrequency);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});
  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  const auto& feature_interface_reg =
      helper_with_part->GetPartFeatureInterfaces();

  EXPECT_NE(feature_interface_reg.GetInterface<JointVelocityEstimator>(),
            nullptr);
  // All other feature interfaces were not present in the hal arm part config.
  EXPECT_EQ(feature_interface_reg.GetInterface<JointPosition>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointTorque>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointPositionSensor>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointLimitsInterface>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointVelocity>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointTorqueSensor>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<CartesianLimitsInterface>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<ManipulatorKinematics>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<Dynamics>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<MoveOk>(), nullptr);

  const JointVelocityEstimator* velocity_state_feature =
      feature_interface_reg.GetInterface<JointVelocityEstimator>();

  // Step change in joint velocity state.
  module_ptr_->SetJointVelocityState(10);
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ApplyCommand());
  // With estimation (low pass filter) after one timestep we expect that the
  // estimate would be between the original estimate and the new measurement.
  EXPECT_THAT(velocity_state_feature->GetVelocityEstimate().velocity[0],
              AllOf(Gt(0), Lt(10)));

  // After a series of iterations the estimate should converge
  for (int i = 0; i < 50; ++i) {
    INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());
    INTRINSIC_RT_EXPECT_OK(helper_with_part->ApplyCommand());
  }
  EXPECT_THAT(velocity_state_feature->GetVelocityEstimate().velocity[0],
              DoubleNear(10, 1e-6));
}

TEST_F(HalArmPartTest,
       SingleDofModuleCreatesVelocityEstimatorWithPositionState) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  hal_arm_part_config.set_calculate_velocity_state_from_position(true);
  hal_arm_part_config.set_velocity_filter_cutoff_frequency(0.2 *
                                                           kControlFrequency);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});
  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  const auto& feature_interface_reg =
      helper_with_part->GetPartFeatureInterfaces();

  EXPECT_NE(feature_interface_reg.GetInterface<JointVelocityEstimator>(),
            nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointPositionSensor>(), nullptr);

  // All other feature interfaces were not present in the hal arm part config.
  EXPECT_EQ(feature_interface_reg.GetInterface<JointPosition>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointTorque>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointLimitsInterface>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointVelocity>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointTorqueSensor>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<CartesianLimitsInterface>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<ManipulatorKinematics>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<Dynamics>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<MoveOk>(), nullptr);
}

TEST_F(HalArmPartTest, SingleDofModuleMapsCorrectlyToFeatureInterfaceWithPid) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_torque_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_torque_command()->set_interface_name(
      SingleDofModule::kTorqueCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  hal_arm_part_config.mutable_joint_velocity_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_velocity_state()->set_interface_name(
      SingleDofModule::kVelocityStateInterfaceName);
  *hal_arm_part_config.mutable_arm_position_pid_torque_controller_config() =
      CreateDefaultArmPositionPidTorqueControllerConfig(1);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});
  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  EXPECT_OK(
      ValidateGenericConfig(helper_with_part->GetPartFeatureInterfaces()
                                .SupportedFeatureInterfaceTypes(),
                            helper_with_part->part_config().generic_config()));

  const auto& feature_interface_reg =
      helper_with_part->GetPartFeatureInterfaces();
  // Joint Position Command, State and Limits were set and correctly converted.
  EXPECT_NE(feature_interface_reg.GetInterface<JointPosition>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointTorque>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointPositionSensor>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointLimitsInterface>(),
            nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointVelocityEstimator>(),
            nullptr);

  // MoveOk is implicit if joint limits and position are defined (even if via
  // PID controllers!)
  //
  // All other feature interfaces were not present in the hal arm part config.
  EXPECT_EQ(feature_interface_reg.GetInterface<JointVelocity>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointTorqueSensor>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<CartesianLimitsInterface>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<ManipulatorKinematics>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<Dynamics>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<MoveOk>(), nullptr);
}

TEST_F(HalArmPartTest,
       SingleDofModuleMapsCorrectlyToFeatureInterfacesWithLimits) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});

  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));
  EXPECT_OK(
      ValidateGenericConfig(helper_with_part->GetPartFeatureInterfaces()
                                .SupportedFeatureInterfaceTypes(),
                            helper_with_part->part_config().generic_config()));

  const auto& feature_interface_reg =
      helper_with_part->GetPartFeatureInterfaces();
  // Joint Position Command and State were set and correctly converted.
  EXPECT_NE(feature_interface_reg.GetInterface<JointPosition>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointPositionSensor>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointLimitsInterface>(),
            nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<MoveOk>(), nullptr);

  // All other feature interfaces were not present in the hal arm part config.
  EXPECT_EQ(feature_interface_reg.GetInterface<JointVelocity>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointTorque>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointTorqueSensor>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<CartesianLimitsInterface>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointVelocityEstimator>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointAccelerationEstimator>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<ManipulatorKinematics>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<Dynamics>(), nullptr);
}

TEST_F(HalArmPartTest, SingleDofModuleMapsCorrectlyCommunicates) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});
  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  EXPECT_OK(
      ValidateGenericConfig(helper_with_part->GetPartFeatureInterfaces()
                                .SupportedFeatureInterfaceTypes(),
                            helper_with_part->part_config().generic_config()));

  auto& feature_interface_reg = helper_with_part->GetPartFeatureInterfaces();
  auto* joint_position_feature =
      feature_interface_reg.GetInterface<JointPosition>();
  ASSERT_NE(joint_position_feature, nullptr);
  eigenmath::VectorNd pos(1);
  pos << 0.1;
  ASSERT_OK(hwm_manager.Prepare(/*deadline=*/absl::InfiniteFuture()));

  ASSERT_OK(hwm_manager.Activate(/*deadline=*/absl::InfiniteFuture()));
  absl::Notification enable_motion_notification;
  Thread thread;
  ASSERT_OK_AND_ASSIGN(thread, CreateRealtimeCapableThread(
                                   ThreadOptions().SetName("Enable"), [&]() {
                                     EXPECT_OK(hwm_manager.EnableMotionTestOnly(
                                         /*deadline=*/absl::InfiniteFuture()));
                                     enable_motion_notification.Notify();
                                   }));
  while (!enable_motion_notification.HasBeenNotified()) {
    EXPECT_OK(hwm_manager.Read(
        /*deadline=*/absl::InfiniteFuture()));  // Trigger processing of HWM
                                                // state.
    absl::SleepFor(absl::Milliseconds(100));
  }
  thread.join();
  ASSERT_OK(hwm_manager.UpdateState());
  ASSERT_OK(
      joint_position_feature->SetPositionSetpoints(JointPositionCommand(pos)));
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ApplyCommand());
  // Timeslicer triggers these calls after each controller update loop.
  EXPECT_OK(hwm_manager.Write(/*deadline=*/absl::InfiniteFuture()));
  EXPECT_OK(hwm_manager.Read(/*deadline=*/absl::InfiniteFuture()));

  EXPECT_THAT(module_ptr_->GetJointPositionCommand(), Eq(0.1));
  EXPECT_THAT(module_ptr_->GetJointPositionState(), Eq(0.1));

  // Check that the last updated time has been updated.
  EXPECT_GT(module_ptr_->PositionCommandLastUpdatedTime(), Clock::Zero());
}

TEST_F(HalArmPartTest,
       SingleDofModulePositionInterfaceProvidesCurrentStateWhenStale) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});
  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  EXPECT_OK(
      ValidateGenericConfig(helper_with_part->GetPartFeatureInterfaces()
                                .SupportedFeatureInterfaceTypes(),
                            helper_with_part->part_config().generic_config()));

  auto& feature_interface_reg = helper_with_part->GetPartFeatureInterfaces();
  auto* joint_position_feature =
      feature_interface_reg.GetInterface<JointPosition>();
  ASSERT_NE(joint_position_feature, nullptr);
  eigenmath::VectorNd pos(1);
  pos << 0.1;
  ASSERT_OK(hwm_manager.Prepare(/*deadline=*/absl::InfiniteFuture()));
  ASSERT_OK(hwm_manager.Activate(/*deadline=*/absl::InfiniteFuture()));
  absl::Notification enable_motion_notification;
  Thread thread;
  ASSERT_OK_AND_ASSIGN(thread, CreateRealtimeCapableThread(
                                   ThreadOptions().SetName("Enable"), [&]() {
                                     EXPECT_OK(hwm_manager.EnableMotionTestOnly(
                                         /*deadline=*/absl::InfiniteFuture()));
                                     enable_motion_notification.Notify();
                                   }));
  while (!enable_motion_notification.HasBeenNotified()) {
    EXPECT_OK(hwm_manager.Read(
        /*deadline=*/absl::InfiniteFuture()));  // Trigger processing of HWM
                                                // state.
    absl::SleepFor(absl::Milliseconds(100));
  }
  thread.join();
  ASSERT_OK(hwm_manager.UpdateState());

  // First pass, check that the arm part's state matches the setpoint.
  INTRINSIC_RT_ASSERT_OK(helper_with_part->ReadStatus());
  ASSERT_OK(
      joint_position_feature->SetPositionSetpoints(JointPositionCommand(pos)));
  INTRINSIC_RT_ASSERT_OK(helper_with_part->ApplyCommand());
  // Timeslicer triggers these calls after each controller update loop.
  ASSERT_OK(hwm_manager.Write(/*deadline=*/absl::InfiniteFuture()));
  ASSERT_OK(hwm_manager.Read(/*deadline=*/absl::InfiniteFuture()));
  ASSERT_THAT(module_ptr_->GetJointPositionCommand(), Eq(0.1));
  ASSERT_THAT(module_ptr_->GetJointPositionState(), Eq(0.1));

  // Now run some cycles without updates to the setpoint, it should go stale.
  constexpr int kNumCyclesTillStale = 5;
  for (int i = 0; i < kNumCyclesTillStale; i++) {
    INTRINSIC_RT_ASSERT_OK(helper_with_part->ReadStatus());
    INTRINSIC_RT_ASSERT_OK(helper_with_part->ApplyCommand());
    // Timeslicer triggers these calls after each controller update loop.
    ASSERT_OK(hwm_manager.Write(/*deadline=*/absl::InfiniteFuture()));
    ASSERT_OK(hwm_manager.Read(/*deadline=*/absl::InfiniteFuture()));
  }

  // Now set the current state to some other value. Querying the previous
  // setpoint should return this new value.
  double other_pos = 0.777;
  module_ptr_->SetJointPositionState(other_pos);
  INTRINSIC_RT_ASSERT_OK(helper_with_part->ReadStatus());
  EXPECT_THAT(joint_position_feature->PreviousPositionSetpoints().position()(0),
              Eq(other_pos));
}

TEST_F(
    HalArmPartTest,
    FeatureInterfacesPresentSensedValuesForPreviousSetpointsPriorToFirstReset) {
  // This test is important because it ensure that the user will not see a zero-
  // initialized previous setpoints value which can lead to strange out of limit
  // errors during initialization.

  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});
  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  EXPECT_OK(
      ValidateGenericConfig(helper_with_part->GetPartFeatureInterfaces()
                                .SupportedFeatureInterfaceTypes(),
                            helper_with_part->part_config().generic_config()));

  auto& feature_interface_reg = helper_with_part->GetPartFeatureInterfaces();
  auto* joint_position_feature =
      feature_interface_reg.GetInterface<JointPosition>();
  ASSERT_NE(joint_position_feature, nullptr);
  ASSERT_OK(hwm_manager.Prepare(/*deadline=*/absl::InfiniteFuture()));
  ASSERT_OK(hwm_manager.Activate(/*deadline=*/absl::InfiniteFuture()));

  double single_pos = 0.777;
  double single_cmd = 1.0;
  eigenmath::VectorNd cmd(1);
  cmd << single_cmd;

  module_ptr_->SetJointPositionState(single_pos);
  module_ptr_->SetJointPositionCommand(single_cmd);

  INTRINSIC_RT_ASSERT_OK(helper_with_part->ReadStatus());
  // The reading starts as stale, so we should see the sensed position here, not
  // the command.
  EXPECT_THAT(joint_position_feature->PreviousPositionSetpoints().position()(0),
              Eq(single_pos));
}

TEST_F(HalArmPartTest, FeatureInterfacesGetResetAfterEnabling) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});
  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  EXPECT_OK(
      ValidateGenericConfig(helper_with_part->GetPartFeatureInterfaces()
                                .SupportedFeatureInterfaceTypes(),
                            helper_with_part->part_config().generic_config()));

  auto& feature_interface_reg = helper_with_part->GetPartFeatureInterfaces();
  auto* joint_position_feature =
      feature_interface_reg.GetInterface<JointPosition>();
  ASSERT_NE(joint_position_feature, nullptr);
  ASSERT_OK(hwm_manager.Prepare(/*deadline=*/absl::InfiniteFuture()));
  ASSERT_OK(hwm_manager.Activate(/*deadline=*/absl::InfiniteFuture()));

  double single_pos = 0.777;
  double single_cmd = 1.0;
  eigenmath::VectorNd cmd(1);
  cmd << single_cmd;

  module_ptr_->SetJointPositionState(single_pos);
  module_ptr_->SetJointPositionCommand(single_cmd);

  // We explicitly call SetPositionSetpoints here to "unstale" the reading of
  // the previous setpoint. Otherwise we would fallback to reading the sensed
  // value immediately (as is intended).
  ASSERT_OK(
      joint_position_feature->SetPositionSetpoints(JointPositionCommand(cmd)));
  INTRINSIC_RT_ASSERT_OK(helper_with_part->ReadStatus());
  // The previous set points should be the command that we have that after
  // calling ReadStatus().
  EXPECT_THAT(joint_position_feature->PreviousPositionSetpoints().position()(0),
              Eq(single_cmd));

  absl::Notification enable_motion_notification;
  Thread thread;
  ASSERT_OK_AND_ASSIGN(thread, CreateRealtimeCapableThread(
                                   ThreadOptions().SetName("Enable"), [&]() {
                                     EXPECT_OK(hwm_manager.EnableMotionTestOnly(
                                         /*deadline=*/absl::InfiniteFuture()));
                                     enable_motion_notification.Notify();
                                   }));
  while (!enable_motion_notification.HasBeenNotified()) {
    EXPECT_OK(hwm_manager.Read(
        /*deadline=*/absl::InfiniteFuture()));  // Trigger processing of HWM
                                                // state.
    absl::SleepFor(absl::Milliseconds(100));
  }
  thread.join();
  ASSERT_OK(hwm_manager.UpdateState());

  // After enabling, the feature interfaces should be reset, which means the
  // previous set points should be the current position joint state.
  INTRINSIC_RT_ASSERT_OK(helper_with_part->ReadStatus());
  EXPECT_THAT(joint_position_feature->PreviousPositionSetpoints().position()(0),
              Eq(single_pos));
}

TEST_F(HalArmPartTest, SingleDofModuleMapsWontMatchUnexistingInterfaces) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  // JointPosition does not exist and thus later on fails.
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      "whatever_hardware_interface");

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});
  EXPECT_THAT(PartTestHelper::CreateFromProtoFactory(
                  {.part_name = kHalArmPartName,
                   .context = context,
                   .control_frequency_hz = kControlFrequency,
                   .hardware_resource_name = kSingleDofResourceId},
                  HalArmPart::kPartTypeName, hal_arm_part_config),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(HalArmPartTest, SingleDofModuleMapsWontMatchWrongInterfaces) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  // ControlModeStatus has a different type than JointPosition.
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kControlModeStateInterfaceName);

  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{});
  EXPECT_THAT(PartTestHelper::CreateFromProtoFactory(
                  {.part_name = kHalArmPartName,
                   .context = context,
                   .control_frequency_hz = kControlFrequency,
                   .hardware_resource_name = kSingleDofResourceId},
                  HalArmPart::kPartTypeName, hal_arm_part_config),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(HalArmPartTest,
       SingleDofModulePartFactoryFailsWithAccelerationStateAndFilter) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  hal_arm_part_config.mutable_joint_velocity_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_velocity_state()->set_interface_name(
      SingleDofModule::kVelocityStateInterfaceName);
  hal_arm_part_config.mutable_joint_acceleration_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_acceleration_state()->set_interface_name(
      "AccelerationStateInterface");
  *hal_arm_part_config.mutable_linear_joint_acceleration_filter_config() =
      CreateDefaultLinearJointAccelerationFilterConfig();

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/nullptr,
      /*hardware_module_manager=*/&hwm_manager, {});
  EXPECT_THAT(PartTestHelper::CreateFromProtoFactory(
                  {.part_name = kHalArmPartName,
                   .context = context,
                   .control_frequency_hz = kControlFrequency},
                  HalArmPart::kPartTypeName, hal_arm_part_config),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Cannot define both")));
}

TEST_F(
    HalArmPartTest,
    SingleDofModulePartFactoryFailsWithAccelerationFilterWithoutPositionState) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_velocity_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_velocity_state()->set_interface_name(
      SingleDofModule::kVelocityStateInterfaceName);
  *hal_arm_part_config.mutable_linear_joint_acceleration_filter_config() =
      CreateDefaultLinearJointAccelerationFilterConfig();

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/nullptr,
      /*hardware_module_manager=*/&hwm_manager, {});
  EXPECT_THAT(
      PartTestHelper::CreateFromProtoFactory(
          {.part_name = kHalArmPartName,
           .context = context,
           .control_frequency_hz = kControlFrequency},
          HalArmPart::kPartTypeName, hal_arm_part_config),
      StatusIs(absl::StatusCode::kFailedPrecondition, HasSubstr("without")));
}

TEST_F(
    HalArmPartTest,
    SingleDofModulePartFactoryFailsWithAccelerationFilterWithoutVelocityState) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  *hal_arm_part_config.mutable_linear_joint_acceleration_filter_config() =
      CreateDefaultLinearJointAccelerationFilterConfig();

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/nullptr,
      /*hardware_module_manager=*/&hwm_manager, {});
  EXPECT_THAT(
      PartTestHelper::CreateFromProtoFactory(
          {.part_name = kHalArmPartName,
           .context = context,
           .control_frequency_hz = kControlFrequency},
          HalArmPart::kPartTypeName, hal_arm_part_config),
      StatusIs(absl::StatusCode::kFailedPrecondition, HasSubstr("without")));
}

TEST_F(HalArmPartTest, StatusOnlyModulehasNoSafetyConfig) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager, {});
  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  EXPECT_OK(
      ValidateGenericConfig(helper_with_part->GetPartFeatureInterfaces()
                                .SupportedFeatureInterfaceTypes(),
                            helper_with_part->part_config().generic_config()));

  EXPECT_FALSE(helper_with_part->part_config()
                   .generic_config()
                   .has_safety_limits_config());
}

class HalArmPartMultiModuleTest : public ::testing::Test {
 public:
  static constexpr char kModuleName[] = "my_test_hardware_module";
  static constexpr char kModuleName2[] = "my_test_hardware_module2";
  static constexpr char kInterfacePrefix[] = "joint";

  ModuleConfig GetModuleConfig(std::string_view module_name) {
    return ModuleConfig(
        /*config=*/ParseTextProtoOrDie(
            GetTestHardwareModuleConfig(module_name, kInterfacePrefix)),
        /*shared_memory_namespace=*/"",
        /*realtime_clock=*/nullptr);
  }

  ModuleConfig GetModuleConfig(std::string_view shared_memory_namespace,
                               std::string_view module_name) {
    return ModuleConfig(
        /*config=*/ParseTextProtoOrDie(
            GetTestHardwareModuleConfig(module_name, kInterfacePrefix)),
        /*shared_memory_namespace=*/shared_memory_namespace,
        /*realtime_clock=*/nullptr);
  }

  HardwareModule GetModuleInstance(const ModuleConfig& config) {
    return hardware_module_registry::CreateInstance(config,
                                                    /*realtime_clock=*/nullptr);
  }

  void SetUp() override {
    WorldHashMap<std::string, World> worlds;

    World two_link_robot_world = World::CreateEmptyWorld();
    ASSERT_OK_AND_ASSIGN(LinearChainRobotData robot_data,
                         MakeTwoLinkRobot(two_link_robot_world));
    ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<DofKinematicView> dof_view,
        two_link_robot_world.GetDofKinematicView(robot_data.robot_id));
    JointLimitsXd system_limits;
    system_limits.SetSize(1);
    system_limits.min_position.setConstant(kSystemMinPos);
    system_limits.max_position.setConstant(kSystemMaxPos);
    system_limits.max_velocity.setConstant(kSystemMaxVel);
    system_limits.max_acceleration.setConstant(kSystemMaxAcc);
    system_limits.max_jerk.setConstant(kSystemMaxJerk);
    system_limits.max_torque.setConstant(kSystemMaxTorque);
    ASSERT_OK(dof_view->SetDofSystemLimits(system_limits,
                                           /*enforce_limits=*/false));
    JointLimitsXd application_limits = system_limits;
    application_limits.max_velocity *= kApplicationLimitFactor;
    application_limits.max_acceleration *= kApplicationLimitFactor;
    application_limits.max_jerk *= kApplicationLimitFactor;
    ASSERT_OK(dof_view->SetDofApplicationLimits(application_limits,
                                                /*enforce_limits=*/false));

    ASSERT_OK_AND_ASSIGN(world_servers_, InitializeWorldService(
                                             std::move(two_link_robot_world)));
    world_service_ = world_servers_.world_service.get();

    ASSERT_OK_AND_ASSIGN(auto world_from_grpc_service,
                         WorldFromGrpcService::Create(
                             {}, "world", world_service_->NewObjectStub()));
    ASSERT_OK_AND_ASSIGN(
        auto assembly_from_world_service,
        AssemblyFromWorldService::Create({}, world_from_grpc_service.get()));
    ASSERT_OK_AND_ASSIGN(
        auto kinematics_from_world_service,
        KinematicsFromWorldService::Create({}, world_from_grpc_service.get()));
    ASSERT_OK_AND_ASSIGN(
        auto geometry_from_geometry_library_service,
        GeometryFromGeometryLibrary::Create(
            intrinsic_proto::geometry::GeometryService::NewStub(
                world_servers_.geometry_server->InProcessChannel(
                    grpc::ChannelArguments()))));
    ASSERT_OK_AND_ASSIGN(
        service_collection_,
        ServiceCollection::CreateFromInitializedServices(
            std::move(world_from_grpc_service),
            std::move(assembly_from_world_service),
            std::move(kinematics_from_world_service),
            std::move(geometry_from_geometry_library_service)));
    std::string shared_memory_namespace =
        intrinsic::icon::UniqueMemoryNamespace();

    auto module_config = GetModuleConfig(shared_memory_namespace, kModuleName);
    auto module_config2 =
        GetModuleConfig(shared_memory_namespace, kModuleName2);
    {
      ASSERT_OK_AND_ASSIGN(auto shm_manager1,
                           intrinsic::icon::SharedMemoryManager::Create(
                               shared_memory_namespace, kModuleName));
      ASSERT_OK_AND_ASSIGN(
          auto module_runtime,
          HardwareModuleRuntime::Create(std::move(shm_manager1),
                                        GetModuleInstance(module_config)));

      module_runtime_ = std::move(module_runtime);
    }
    {
      ASSERT_OK_AND_ASSIGN(auto shm_manager2,
                           intrinsic::icon::SharedMemoryManager::Create(
                               shared_memory_namespace, kModuleName2));
      ASSERT_OK_AND_ASSIGN(
          auto module_runtime2,
          HardwareModuleRuntime::Create(std::move(shm_manager2),
                                        GetModuleInstance(module_config2)));
      module_runtime2_ = std::move(module_runtime2);
    }
    module_ptr_ = dynamic_cast<TestHardwareModule*>(
        module_runtime_->GetHardwareModule().instance.get());
    module_ptr2_ = dynamic_cast<TestHardwareModule*>(
        module_runtime2_->GetHardwareModule().instance.get());

    grpc::ServerBuilder builder1;
    ASSERT_OK(module_runtime_->Run(builder1));
    grpc::ServerBuilder builder2;
    ASSERT_OK(module_runtime2_->Run(builder2));
    ASSERT_OK_AND_ASSIGN(
        auto hardware_module_proxy,
        HardwareModuleProxy::Attach(module_config, absl::ZeroDuration()));
    ASSERT_OK_AND_ASSIGN(
        auto hardware_module_proxy2,
        HardwareModuleProxy::Attach(module_config2, absl::ZeroDuration()));
    EXPECT_TRUE(hardware_module_proxy.IsConnected());
    EXPECT_TRUE(hardware_module_proxy2.IsConnected());
    EXPECT_OK(manager_.Add(std::move(hardware_module_proxy),
                           /*proxy_drives_clock=*/false,
                           intrinsic_proto::icon::HardwareConfig{}));
    EXPECT_OK(manager_.Add(std::move(hardware_module_proxy2),
                           /*proxy_drives_clock=*/false,
                           intrinsic_proto::icon::HardwareConfig{}));
    module_proxy_ = manager_.GetHardwareModuleProxy(kModuleName);
    module_proxy2_ = manager_.GetHardwareModuleProxy(kModuleName2);
  }

  ~HalArmPartMultiModuleTest() override {
    EXPECT_OK(module_runtime_->Stop());
    EXPECT_OK(module_runtime2_->Stop());
  }

  TestHardwareModule *module_ptr_, *module_ptr2_;
  std::unique_ptr<HardwareModuleRuntime> module_runtime_, module_runtime2_;
  const HardwareModuleProxy *module_proxy_, *module_proxy2_;
  HardwareModuleManager manager_;
  WorldServers world_servers_;
  FakeWorldService* world_service_;
  ServiceCollection service_collection_;
};

class MockKinematicsService
    : public ServiceImplBase<MockKinematicsService, KinematicsService> {
 public:
  MOCK_METHOD(std::unique_ptr<kinematics::InverseKinematicsInterface>,
              CreateInverseKinematicsSolverForPart,
              (absl::string_view part_name), (const, override));
  MOCK_METHOD(std::unique_ptr<kinematics::Skeleton>,
              CreateKinematicsModelForPart, (absl::string_view part_name),
              (const, override));
  MOCK_METHOD(const kinematics::ModelInterface*, GetKinematicsModelForPart,
              (absl::string_view part_name), (const, override));
  MOCK_METHOD(std::vector<std::string>, ListPartNames, (), (const, override));
};

TEST_F(HalArmPartTest, FactoryFailsIfKinematicsModelNotFound) {
  static constexpr absl::string_view kKinematicsModelName = "kinematics_model";

  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  hal_arm_part_config.set_kinematics_model_name(kKinematicsModelName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  // For C++ reasons (?), I can't inline this into the
  // CreateFromServicesNoInitialize() call below.
  std::vector<std::unique_ptr<Service>> services;
  // This will return Unimplemented or similar when asked for a model.
  services.emplace_back(std::make_unique<MockKinematicsService>());
  ASSERT_OK_AND_ASSIGN(ServiceCollection service_collection,
                       ServiceCollection::CreateFromServiceListNoInitialize(
                           std::move(services)));

  Context context(
      /*service_collection=*/&service_collection,
      /*hardware_module_manager=*/&hwm_manager, {});

  EXPECT_THAT(PartTestHelper::CreateFromProtoFactory(
                  {.part_name = kHalArmPartName,
                   .context = context,
                   .control_frequency_hz = kControlFrequency},
                  HalArmPart::kPartTypeName, hal_arm_part_config),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(HalArmPartTest, LoadsKinematicsModel) {
  static constexpr absl::string_view kKinematicsModelName =
      kSingleDofResourceId;
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  hal_arm_part_config.set_kinematics_model_name(kKinematicsModelName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  // For C++ reasons (?), I can't inline this into the
  // CreateFromServicesNoInitialize() call below.
  std::vector<std::unique_ptr<Service>> services;
  ASSERT_OK_AND_ASSIGN(auto world_service,
                       WorldFromGrpcService::Create(
                           {}, "test_world", world_service_->NewObjectStub()));
  services.emplace_back(std::move(world_service));
  //  Using FakePubSubServiceImpl simply because it's a fake service that
  //  already exists.
  auto mock_kinematics = std::make_unique<MockKinematicsService>();
  EXPECT_CALL(*mock_kinematics,
              CreateInverseKinematicsSolverForPart(kKinematicsModelName))
      .WillOnce(Return(
          ByMove(std::make_unique<kinematics::MockInverseKinematics>(1))));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<kinematics::Skeleton> kinematics_model,
                       kinematics::testing::CreateSerialChainSkeleton(1));
  EXPECT_CALL(*mock_kinematics,
              CreateKinematicsModelForPart(kKinematicsModelName))
      .WillOnce(Return(ByMove(std::move(kinematics_model))));
  services.emplace_back(std::move(mock_kinematics));
  ASSERT_OK_AND_ASSIGN(ServiceCollection service_collection,
                       ServiceCollection::CreateFromServiceListNoInitialize(
                           std::move(services)));

  Context context(
      /*service_collection=*/&service_collection,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{.resource_id = kSingleDofResourceId});

  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  const auto& feature_interface_reg =
      helper_with_part->GetPartFeatureInterfaces();

  EXPECT_NE(feature_interface_reg.GetInterface<ManipulatorKinematics>(),
            nullptr);
}

class MockDynamicsService
    : public ServiceImplBase<MockDynamicsService, DynamicsService> {
 public:
  MOCK_METHOD(absl::StatusOr<std::unique_ptr<RigidBodyInterface>>,
              CreateDynamicsSolverForPart, (absl::string_view part_name),
              (const, override));
};

TEST_F(HalArmPartTest, DynamicsModelUsesPayloadFromWorld) {
  static constexpr absl::string_view kDynamicsModelName = kSingleDofResourceId;
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  hal_arm_part_config.mutable_payload_command()->set_module_name(kModuleName);
  hal_arm_part_config.mutable_payload_command()->set_interface_name(
      SingleDofModule::kPayloadCommandInterfaceName);
  hal_arm_part_config.set_dynamics_model_name(kDynamicsModelName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  // Initialize the world in the service_collection with a payload.
  std::shared_ptr<world::ObjectWorldClient> world_client =
      GetObjectWorldClient();
  ASSERT_OK_AND_ASSIGN(world::KinematicObject robot,
                       world_client->GetKinematicObject(
                           intrinsic::WorldObjectName(kSingleDofResourceId)));
  Pose3d tip_t_cog = Pose3d(eigenmath::Vector3d(1, 2, 3));
  eigenmath::Matrix3d inertia = eigenmath::Matrix3d::Identity();
  inertia(0, 1) = -0.1;
  inertia(1, 0) = -0.1;
  inertia(0, 2) = -0.1;
  inertia(2, 0) = -0.1;
  inertia(2, 1) = -0.1;
  inertia(1, 2) = -0.1;
  ASSERT_OK_AND_ASSIGN(
      RobotPayload payload,
      RobotPayload::Create(kMountedPayloadMassKg, tip_t_cog, inertia));
  ASSERT_OK(world_client->UpdateMountedPayload(robot, payload));

  std::vector<std::unique_ptr<Service>> services;
  ASSERT_OK_AND_ASSIGN(auto world_service,
                       WorldFromGrpcService::Create(
                           {}, "test_world", world_service_->NewObjectStub()));
  services.emplace_back(std::move(world_service));

  // We also need a MockKinematicsService because the Dynamics service
  // needs to query it.
  auto mock_kinematics = std::make_unique<MockKinematicsService>();
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<kinematics::Skeleton> kinematics_model,
                       kinematics::testing::CreateSerialChainSkeleton(1));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      auto tip_id, kinematics_model->FindNonBranchingKinematicChainTip());
  EXPECT_CALL(*mock_kinematics,
              CreateKinematicsModelForPart(kDynamicsModelName))
      .WillOnce(Return(ByMove(std::move(kinematics_model))));
  EXPECT_CALL(*mock_kinematics,
              CreateInverseKinematicsSolverForPart(kDynamicsModelName))
      .WillOnce(Return(
          ByMove(std::make_unique<kinematics::MockInverseKinematics>(1))));
  services.emplace_back(std::move(mock_kinematics));

  auto mock_dynamics_for_limits = std::make_unique<MockRigidBodyInterface>();
  auto mock_dynamics_for_payload = std::make_unique<MockRigidBodyInterface>();
  // Check that we call SetEndEffectorDynamicsParameters() with the payload
  // parameters.
  EXPECT_CALL(*mock_dynamics_for_payload,
              SetEndEffectorDynamicsParameters(IsApprox(tip_t_cog),
                                               Eq(kMountedPayloadMassKg),
                                               IsApprox(inertia), Eq(tip_id)))
      .Times(1);
  auto mock_dynamics_service = std::make_unique<MockDynamicsService>();
  EXPECT_CALL(*mock_dynamics_service, CreateDynamicsSolverForPart)
      .WillOnce(Return(ByMove(std::move(mock_dynamics_for_limits))))
      .WillOnce(Return(ByMove(std::move(mock_dynamics_for_payload))));
  services.emplace_back(std::move(mock_dynamics_service));

  ASSERT_OK_AND_ASSIGN(ServiceCollection service_collection,
                       ServiceCollection::CreateFromServiceListNoInitialize(
                           std::move(services)));

  Context context(
      /*service_collection=*/&service_collection,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{.resource_id = kSingleDofResourceId});

  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));
}

TEST_F(HalArmPartTest,
       DynamicsModelPayloadSettingFailsWithoutManipulatorKinematics) {
  static constexpr absl::string_view kDynamicsModelName = kSingleDofResourceId;
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  hal_arm_part_config.mutable_payload_command()->set_module_name(kModuleName);
  hal_arm_part_config.mutable_payload_command()->set_interface_name(
      SingleDofModule::kPayloadCommandInterfaceName);
  hal_arm_part_config.set_dynamics_model_name(kDynamicsModelName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  // Initialize the world in the service_collection with a payload.
  std::shared_ptr<world::ObjectWorldClient> world_client =
      GetObjectWorldClient();
  ASSERT_OK_AND_ASSIGN(world::KinematicObject robot,
                       world_client->GetKinematicObject(
                           intrinsic::WorldObjectName(kSingleDofResourceId)));
  Pose3d tip_t_cog = Pose3d(eigenmath::Vector3d(1, 2, 3));
  eigenmath::Matrix3d inertia = eigenmath::Matrix3d::Identity();
  ASSERT_OK_AND_ASSIGN(
      RobotPayload payload,
      RobotPayload::Create(kMountedPayloadMassKg, tip_t_cog, inertia));
  ASSERT_OK(world_client->UpdateMountedPayload(robot, payload));

  std::vector<std::unique_ptr<Service>> services;
  ASSERT_OK_AND_ASSIGN(auto world_service,
                       WorldFromGrpcService::Create(
                           {}, "test_world", world_service_->NewObjectStub()));
  services.emplace_back(std::move(world_service));

  // No mock kinematics service is added, so we expect an error when creating
  // the part.

  auto mock_dynamics_for_limits = std::make_unique<MockRigidBodyInterface>();
  auto mock_dynamics_for_payload = std::make_unique<MockRigidBodyInterface>();

  auto mock_dynamics_service = std::make_unique<MockDynamicsService>();
  EXPECT_CALL(*mock_dynamics_service, CreateDynamicsSolverForPart)
      .WillOnce(Return(ByMove(std::move(mock_dynamics_for_limits))))
      .WillOnce(Return(ByMove(std::move(mock_dynamics_for_payload))));
  services.emplace_back(std::move(mock_dynamics_service));

  ASSERT_OK_AND_ASSIGN(ServiceCollection service_collection,
                       ServiceCollection::CreateFromServiceListNoInitialize(
                           std::move(services)));

  Context context(
      /*service_collection=*/&service_collection,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{.resource_id = kSingleDofResourceId});

  EXPECT_THAT(
      PartTestHelper::CreateFromProtoFactory(
          {.part_name = kHalArmPartName,
           .context = context,
           .control_frequency_hz = kControlFrequency,
           .hardware_resource_name = kSingleDofResourceId},
          HalArmPart::kPartTypeName, hal_arm_part_config),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("ManipulatorKinematics interface is not available")));
}

TEST_F(HalArmPartTest, FactoryFailsIfWithoutDynamicsService) {
  static constexpr absl::string_view kDynamicsModelName = "dynamics_model";
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  hal_arm_part_config.set_dynamics_model_name(kDynamicsModelName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  std::vector<std::unique_ptr<Service>> services;
  ASSERT_OK_AND_ASSIGN(ServiceCollection service_collection,
                       ServiceCollection::CreateFromServiceListNoInitialize(
                           std::move(services)));

  Context context(
      /*service_collection=*/&service_collection,
      /*hardware_module_manager=*/&hwm_manager, {});

  EXPECT_THAT(PartTestHelper::CreateFromProtoFactory(
                  {.part_name = kHalArmPartName,
                   .context = context,
                   .control_frequency_hz = kControlFrequency},
                  HalArmPart::kPartTypeName, hal_arm_part_config),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(HalArmPartTest, LoadsDynamicsModel) {
  static constexpr absl::string_view kDynamicsModelName = kSingleDofResourceId;
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  hal_arm_part_config.set_dynamics_model_name(kDynamicsModelName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  std::vector<std::unique_ptr<Service>> services;
  ASSERT_OK_AND_ASSIGN(auto world_service,
                       WorldFromGrpcService::Create(
                           {}, "test_world", world_service_->NewObjectStub()));
  services.emplace_back(std::move(world_service));

  const eigenmath::VectorNd max_acc_limits = eigenmath::VectorNd::Ones(1);
  const eigenmath::VectorNd min_acc_limits = -eigenmath::VectorNd::Ones(1);
  auto mock_dynamics = std::make_unique<MockRigidBodyInterface>();
  EXPECT_CALL(*mock_dynamics, ComputeForwardDynamics)
      .WillOnce(Return(ByMove(min_acc_limits)))
      .WillOnce(Return(ByMove(max_acc_limits)));

  auto mock_dynamics_service = std::make_unique<MockDynamicsService>();
  EXPECT_CALL(*mock_dynamics_service, CreateDynamicsSolverForPart)
      .WillOnce(Return(ByMove(std::move(mock_dynamics))))
      .WillRepeatedly(
          []() { return std::make_unique<MockRigidBodyInterface>(); });

  services.emplace_back(std::move(mock_dynamics_service));
  ASSERT_OK_AND_ASSIGN(ServiceCollection service_collection,
                       ServiceCollection::CreateFromServiceListNoInitialize(
                           std::move(services)));

  Context context(
      /*service_collection=*/&service_collection,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{.resource_id = kSingleDofResourceId});

  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  const auto& feature_interface_reg =
      helper_with_part->GetPartFeatureInterfaces();

  EXPECT_NE(feature_interface_reg.GetInterface<Dynamics>(), nullptr);

  // Verify that mock joint acceleration limits are properly set within the
  // joint limits feature interface.
  const auto& joint_limits_feature_interface =
      feature_interface_reg.GetInterface<JointLimitsInterface>();
  EXPECT_NE(joint_limits_feature_interface, nullptr);
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      const auto& joint_acceleration_limits_from_dynamics,
      joint_limits_feature_interface->GetJointAccelerationLimitsFromDynamics());
  EXPECT_NE(joint_acceleration_limits_from_dynamics, std::nullopt);
  EXPECT_EQ(joint_acceleration_limits_from_dynamics
                ->joint_acceleration_limits_at_min_torque,
            min_acc_limits);
  EXPECT_EQ(joint_acceleration_limits_from_dynamics
                ->joint_acceleration_limits_at_max_torque,
            max_acc_limits);
}

TEST_F(HalArmPartTest, T1LimitsConfiguredCorrectly) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);

  // Add the additional limit configuration for the modes of safe operation.
  intrinsic_proto::icon::v1::ModeOfSafeOperationLimitsConfig
      mode_of_safe_operation_limits_config;
  mode_of_safe_operation_limits_config.mutable_t1_limits()
      ->set_joint_max_velocity(kSystemMaxVel * 0.1);
  mode_of_safe_operation_limits_config.mutable_t1_limits()
      ->set_tcp_max_rotational_velocity(kCartesianRotationMaxVel * 0.1);
  mode_of_safe_operation_limits_config.mutable_t1_limits()
      ->set_tcp_max_translational_velocity(kCartesianTranslationMaxVel * 0.1);
  *hal_arm_part_config.mutable_mode_of_safe_operation_limits_config() =
      mode_of_safe_operation_limits_config;

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{.resource_id = kSingleDofResourceId});

  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  // Check that the Generic config contains the safety limits.
  EXPECT_OK(
      ValidateGenericConfig(helper_with_part->GetPartFeatureInterfaces()
                                .SupportedFeatureInterfaceTypes(),
                            helper_with_part->part_config().generic_config()));
  EXPECT_TRUE(helper_with_part->part_config()
                  .generic_config()
                  .has_safety_limits_config());

  // Check that a value matches correctly for unknown (default) safety mode.
  EXPECT_THAT(helper_with_part->part_config()
                  .generic_config()
                  .safety_limits_config()
                  .joint_limits_map()
                  .at(intrinsic_proto::icon::ModeOfSafeOperation_Name(
                      intrinsic_proto::icon::MODE_OF_SAFE_OPERATION_UNKNOWN))
                  .system_limits()
                  .max_velocity()
                  .values()
                  .at(0),
              Eq(kSystemMaxVel));

  // Check that some values matches correctly for unknown (default) safety mode.
  EXPECT_THAT(helper_with_part->part_config()
                  .generic_config()
                  .safety_limits_config()
                  .joint_limits_map()
                  .at(intrinsic_proto::icon::ModeOfSafeOperation_Name(
                      intrinsic_proto::icon::MODE_OF_SAFE_OPERATION_TEACHING_1))
                  .system_limits()
                  .max_velocity()
                  .values()
                  .at(0),
              Eq(kSystemMaxVel * 0.1));
  EXPECT_THAT(helper_with_part->part_config()
                  .generic_config()
                  .safety_limits_config()
                  .cartesian_limits_map()
                  .at(intrinsic_proto::icon::ModeOfSafeOperation_Name(
                      intrinsic_proto::icon::MODE_OF_SAFE_OPERATION_TEACHING_1))
                  .default_cartesian_limits()
                  .max_translational_velocity()
                  .at(0),
              Eq(kCartesianTranslationMaxVel * 0.1));

  auto& feature_interface_reg = helper_with_part->GetPartFeatureInterfaces();

  // Check that the Cartesian velocity limits match the limits for the default
  // safety mode.
  auto* cartesian_limits_interface =
      feature_interface_reg.GetInterface<CartesianLimitsInterface>();
  ASSERT_NE(cartesian_limits_interface, nullptr);
  EXPECT_THAT(cartesian_limits_interface->GetDefaultCartesianLimits()
                  .max_translational_velocity,
              Eq(eigenmath::Vector3d({kCartesianTranslationMaxVel,
                                      kCartesianTranslationMaxVel,
                                      kCartesianTranslationMaxVel})));
  EXPECT_THAT(cartesian_limits_interface->GetDefaultCartesianLimits()
                  .max_rotational_velocity,
              Eq(kCartesianRotationMaxVel));

  // Change the safety status to T1
  helper_with_part->SetModeOfSafeOperation(
      ::intrinsic_fbs::ModeOfSafeOperation::TEACH_PENDANT_1);
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());

  EXPECT_THAT(cartesian_limits_interface->GetDefaultCartesianLimits()
                  .max_translational_velocity,
              Eq(0.1 * eigenmath::Vector3d({kCartesianTranslationMaxVel,
                                            kCartesianTranslationMaxVel,
                                            kCartesianTranslationMaxVel})));
  EXPECT_THAT(cartesian_limits_interface->GetDefaultCartesianLimits()
                  .max_rotational_velocity,
              Eq(0.1 * kCartesianRotationMaxVel));
}

TEST_F(HalArmPartTest,
       SingleDofModuleFailsVelocityLimitBrokenDueToChangeInSafetyStatusToT1) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);

  // Add the additional limit configuration for the modes of safe operation.
  intrinsic_proto::icon::v1::ModeOfSafeOperationLimitsConfig
      mode_of_safe_operation_limits_config;
  mode_of_safe_operation_limits_config.mutable_t1_limits()
      ->set_joint_max_velocity(kSystemMaxVel * 0.1);
  mode_of_safe_operation_limits_config.mutable_t1_limits()
      ->set_tcp_max_rotational_velocity(kCartesianRotationMaxVel * 0.1);
  mode_of_safe_operation_limits_config.mutable_t1_limits()
      ->set_tcp_max_translational_velocity(kCartesianTranslationMaxVel * 0.1);
  *hal_arm_part_config.mutable_mode_of_safe_operation_limits_config() =
      mode_of_safe_operation_limits_config;

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{.resource_id = kSingleDofResourceId});

  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  auto& feature_interface_reg = helper_with_part->GetPartFeatureInterfaces();

  auto* joint_position_feature =
      feature_interface_reg.GetInterface<JointPosition>();
  ASSERT_NE(joint_position_feature, nullptr);

  // Set a command that is moving slower than the application velocity limit but
  // higher than the T1 joint velocity limit.
  auto prev_command = joint_position_feature->PreviousPositionSetpoints();
  eigenmath::VectorNd pos(1);
  pos << prev_command.position()[0];
  const double delta_pos =
      0.2 * (kSystemMaxVel * kApplicationLimitFactor / kControlFrequency);
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());
  pos[0] += delta_pos;
  INTRINSIC_RT_EXPECT_OK(
      joint_position_feature->SetPositionSetpoints(JointPositionCommand(pos)));
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ApplyCommand());

  // Change the safety status to T1
  helper_with_part->SetModeOfSafeOperation(
      ::intrinsic_fbs::ModeOfSafeOperation::TEACH_PENDANT_1);

  INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());
  pos[0] += delta_pos;
  EXPECT_THAT(
      joint_position_feature->SetPositionSetpoints(JointPositionCommand(pos)),
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("velocity joint limits")));
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ApplyCommand());
}

TEST_F(HalArmPartTest, DisablesWhenLeavingCartesianPositionLimits) {
  static constexpr absl::string_view kKinematicsModelName =
      kSingleDofResourceId;
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);
  hal_arm_part_config.set_kinematics_model_name(kKinematicsModelName);

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  // For C++ reasons (?), I can't inline this into the
  // CreateFromServicesNoInitialize() call below.
  std::vector<std::unique_ptr<Service>> services;
  ASSERT_OK_AND_ASSIGN(auto world_service,
                       WorldFromGrpcService::Create(
                           {}, "test_world", world_service_->NewObjectStub()));
  services.emplace_back(std::move(world_service));
  auto mock_kinematics = std::make_unique<MockKinematicsService>();
  EXPECT_CALL(*mock_kinematics,
              CreateInverseKinematicsSolverForPart(kKinematicsModelName))
      .WillOnce(Return(
          ByMove(std::make_unique<kinematics::MockInverseKinematics>(1))));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<kinematics::Skeleton> kinematics_model,
                       kinematics::testing::CreateSerialChainSkeleton(1));
  EXPECT_CALL(*mock_kinematics,
              CreateKinematicsModelForPart(kKinematicsModelName))
      .WillOnce(Return(ByMove(std::move(kinematics_model))));
  services.emplace_back(std::move(mock_kinematics));
  ASSERT_OK_AND_ASSIGN(ServiceCollection service_collection,
                       ServiceCollection::CreateFromServiceListNoInitialize(
                           std::move(services)));

  Context context(
      /*service_collection=*/&service_collection,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{.resource_id = kSingleDofResourceId});

  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  auto& feature_interface_reg = helper_with_part->GetPartFeatureInterfaces();

  EXPECT_NE(feature_interface_reg.GetInterface<ManipulatorKinematics>(),
            nullptr);

  auto* joint_position_feature =
      feature_interface_reg.GetInterface<JointPosition>();
  ASSERT_NE(joint_position_feature, nullptr);
  auto prev_command = joint_position_feature->PreviousPositionSetpoints();
  LOG(INFO) << prev_command.position()[0];

  // The robot has started outside the cartesian limits. Move back into limits.
  // The kinematics are composed by a single revolute joint.
  eigenmath::VectorNd pos(1);
  pos << 1.5;
  ASSERT_OK(
      joint_position_feature->SetPositionSetpoints(JointPositionCommand(pos)));
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ApplyCommand());
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());

  // Try to set setpoint outside of cartesian limits.
  pos(0) = 1.3;
  EXPECT_THAT(
      joint_position_feature->SetPositionSetpoints(JointPositionCommand(pos)),
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("plane violated")));
}

TEST_F(HalArmPartTest, UsesT1CartesianLimits) {
  intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
  hal_arm_part_config.mutable_joint_position_command()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
      SingleDofModule::kPositionCommandInterfaceName);
  hal_arm_part_config.mutable_joint_position_state()->set_module_name(
      kModuleName);
  hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
      SingleDofModule::kPositionStateInterfaceName);

  // Add the additional limit configuration for the modes of safe operation.
  intrinsic_proto::icon::v1::ModeOfSafeOperationLimitsConfig
      mode_of_safe_operation_limits_config;
  mode_of_safe_operation_limits_config.mutable_t1_limits()
      ->set_joint_max_velocity(kSystemMaxVel * 0.1);
  mode_of_safe_operation_limits_config.mutable_t1_limits()
      ->set_tcp_max_rotational_velocity(kCartesianRotationMaxVel * 0.1);
  mode_of_safe_operation_limits_config.mutable_t1_limits()
      ->set_tcp_max_translational_velocity(kCartesianTranslationMaxVel * 0.1);
  *hal_arm_part_config.mutable_mode_of_safe_operation_limits_config() =
      mode_of_safe_operation_limits_config;

  ASSERT_OK_AND_ASSIGN(
      auto single_dof_module_proxy,
      HardwareModuleProxy::Attach(memory_namespace_, kModuleName,
                                  absl::ZeroDuration()));

  HardwareModuleManager hwm_manager;
  ASSERT_OK(hwm_manager.Add(std::move(single_dof_module_proxy),
                            /*proxy_drives_clock=*/false, {}));

  Context context(
      /*service_collection=*/&service_collection_,
      /*hardware_module_manager=*/&hwm_manager,
      /*runtime_options=*/{.resource_id = kSingleDofResourceId});

  ASSERT_OK_AND_ASSIGN(auto helper_with_part,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalArmPartName,
                            .context = context,
                            .control_frequency_hz = kControlFrequency,
                            .hardware_resource_name = kSingleDofResourceId},
                           HalArmPart::kPartTypeName, hal_arm_part_config));

  auto& feature_interface_reg = helper_with_part->GetPartFeatureInterfaces();

  // Check that the Cartesian velocity limits match the limits for the default
  // safety mode.
  auto* cartesian_limits_interface =
      feature_interface_reg.GetInterface<CartesianLimitsInterface>();
  ASSERT_NE(cartesian_limits_interface, nullptr);
  EXPECT_THAT(cartesian_limits_interface->GetDefaultCartesianLimits()
                  .max_translational_velocity,
              Eq(eigenmath::Vector3d({kCartesianTranslationMaxVel,
                                      kCartesianTranslationMaxVel,
                                      kCartesianTranslationMaxVel})));
  EXPECT_THAT(cartesian_limits_interface->GetDefaultCartesianLimits()
                  .max_rotational_velocity,
              Eq(kCartesianRotationMaxVel));

  // Change the safety status to T1
  helper_with_part->SetModeOfSafeOperation(
      ::intrinsic_fbs::ModeOfSafeOperation::TEACH_PENDANT_1);
  INTRINSIC_RT_EXPECT_OK(helper_with_part->ReadStatus());

  EXPECT_THAT(cartesian_limits_interface->GetDefaultCartesianLimits()
                  .max_translational_velocity,
              Eq(0.1 * eigenmath::Vector3d({kCartesianTranslationMaxVel,
                                            kCartesianTranslationMaxVel,
                                            kCartesianTranslationMaxVel})));
  EXPECT_THAT(cartesian_limits_interface->GetDefaultCartesianLimits()
                  .max_rotational_velocity,
              Eq(0.1 * kCartesianRotationMaxVel));
}

class HalArmPartJointLimitsTest : public ::testing::Test {
 public:
  static constexpr char kModuleName[] = "single_dof_module";
  static constexpr char kHalArmPartName[] = "single_dof_hal_arm_part";

  void SetUp() override {
    intrinsic_proto::icon::HardwareModuleConfig single_dof_config;
    single_dof_config.set_name(kModuleName);

    WorldHashMap<std::string, World> worlds;

    World two_link_robot_world = World::CreateEmptyWorld();
    ASSERT_OK_AND_ASSIGN(LinearChainRobotData robot_data,
                         MakeTwoLinkRobot(two_link_robot_world));
    ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<DofKinematicView> dof_view,
        two_link_robot_world.GetDofKinematicView(robot_data.robot_id));
    JointLimitsXd system_limits;
    system_limits.SetSize(1);
    system_limits.min_position.setConstant(kSystemMinPos);
    system_limits.max_position.setConstant(kSystemMaxPos);
    system_limits.max_velocity.setConstant(kSystemMaxVel);
    system_limits.max_acceleration.setConstant(kSystemMaxAcc);
    system_limits.max_jerk.setConstant(kSystemMaxJerk);
    system_limits.max_torque.setConstant(kSystemMaxTorque);
    ASSERT_OK(
        dof_view->SetDofSystemLimits(system_limits, /*enforce_limits=*/false));
    JointLimitsXd application_limits = system_limits;
    application_limits.max_velocity *= kApplicationLimitFactor;
    application_limits.max_acceleration *= kApplicationLimitFactor;
    application_limits.max_jerk *= kApplicationLimitFactor;
    ASSERT_OK(dof_view->SetDofApplicationLimits(application_limits,
                                                /*enforce_limits=*/false));

    // Set limits for the test robot.
    worlds.emplace("test_world", std::move(two_link_robot_world));

    ASSERT_OK_AND_ASSIGN(world_service_, FakeWorldService::Create(
                                             /*worlds=*/std::move(worlds)));
    ASSERT_OK_AND_ASSIGN(
        auto world_from_grpc_service,
        WorldFromGrpcService::Create({}, "test_world",
                                     world_service_->NewObjectStub()));
    ASSERT_OK_AND_ASSIGN(
        auto assembly_from_world_service,
        AssemblyFromWorldService::Create({}, world_from_grpc_service.get()));
    ASSERT_OK_AND_ASSIGN(
        auto kinematics_from_world_service,
        KinematicsFromWorldService::Create({}, world_from_grpc_service.get()));
    ASSERT_OK_AND_ASSIGN(service_collection_,
                         ServiceCollection::CreateFromInitializedServices(
                             std::move(world_from_grpc_service),
                             std::move(assembly_from_world_service),
                             std::move(kinematics_from_world_service)));
    auto single_dof_module = std::make_unique<SingleDofModule>();
    module_ptr_ = single_dof_module.get();

    std::string shared_memory_namespace =
        intrinsic::icon::UniqueMemoryNamespace();

    ASSERT_OK_AND_ASSIGN(auto shm_manager,
                         intrinsic::icon::SharedMemoryManager::Create(
                             shared_memory_namespace, kModuleName));
    {
      ASSERT_OK_AND_ASSIGN(auto module_runtime,
                           HardwareModuleRuntime::Create(
                               std::move(shm_manager),
                               {
                                   /*realtime_clock=*/nullptr,
                                   std::move(single_dof_module),
                                   ModuleConfig(single_dof_config,
                                                /*shared_memory_namespace=*/"",
                                                /*realtime_clock=*/nullptr),
                               }));
      module_runtime_ = std::move(module_runtime);
    }
    grpc::ServerBuilder builder1;
    ASSERT_OK(module_runtime_->Run(builder1));

    intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
    hal_arm_part_config.mutable_joint_position_command()->set_module_name(
        kModuleName);
    hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
        SingleDofModule::kPositionCommandInterfaceName);
    hal_arm_part_config.mutable_joint_position_state()->set_module_name(
        kModuleName);
    hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
        SingleDofModule::kPositionStateInterfaceName);

    ASSERT_OK_AND_ASSIGN(
        auto single_dof_module_proxy,
        HardwareModuleProxy::Attach(shared_memory_namespace, kModuleName,
                                    absl::ZeroDuration()));

    ASSERT_OK(hwm_manager_.Add(std::move(single_dof_module_proxy),
                               /*proxy_drives_clock=*/false, {}));
    Context context(
        /*service_collection=*/&service_collection_,
        /*hardware_module_manager=*/&hwm_manager_,
        {.resource_id = kSingleDofResourceId});

    ASSERT_OK_AND_ASSIGN(helper_with_part_,
                         PartTestHelper::CreateFromProtoFactory(
                             {.part_name = kHalArmPartName,
                              .context = context,
                              .control_frequency_hz = kControlFrequency,
                              .hardware_resource_name = kSingleDofResourceId},
                             HalArmPart::kPartTypeName, hal_arm_part_config));
  }

  ~HalArmPartJointLimitsTest() override { EXPECT_OK(module_runtime_->Stop()); }

  SingleDofModule* module_ptr_;
  std::unique_ptr<HardwareModuleRuntime> module_runtime_;
  std::unique_ptr<PartTestHelper> helper_with_part_ = nullptr;
  HardwareModuleManager hwm_manager_;
  std::unique_ptr<FakeWorldService> world_service_;
  ServiceCollection service_collection_;
};

TEST_F(HalArmPartJointLimitsTest,
       SingleDofModuleFailsWhenCommandedOutsideLimits) {
  auto& feature_interface_reg = helper_with_part_->GetPartFeatureInterfaces();

  auto* joint_position_feature =
      feature_interface_reg.GetInterface<JointPosition>();
  ASSERT_NE(joint_position_feature, nullptr);
  auto prev_command = joint_position_feature->PreviousPositionSetpoints();
  eigenmath::VectorNd pos(1);
  pos << prev_command.position()[0];
  const double delta_pos =
      kSystemMaxVel * kApplicationLimitFactor / kControlFrequency;
  // Make sure max number of iterations is below a certain value to prevent
  // timeouts.
  ASSERT_THAT((kSystemMaxPos / delta_pos), Lt(1e5));
  // Keep iterating till we reach just before the limits.
  for (int t = 0; t < (kSystemMaxPos / delta_pos) - 1; ++t) {
    INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
    pos[0] += delta_pos;
    ASSERT_OK(joint_position_feature->SetPositionSetpoints(
        JointPositionCommand(pos)));
    INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());
  }

  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
  //  Check that the next setpoint leads to a position limit violation and is
  //  not applied.
  pos[0] += delta_pos;
  EXPECT_THAT(
      joint_position_feature->SetPositionSetpoints(JointPositionCommand(pos)),
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("position limits")));
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());
  EXPECT_THAT(module_ptr_->GetJointPositionState(), Lt(pos[0]));
}

TEST_F(HalArmPartJointLimitsTest,
       SingleDofModuleFailsWhenDifferentiatedVelocityBreaksLimit) {
  auto& feature_interface_reg = helper_with_part_->GetPartFeatureInterfaces();

  auto* joint_position_feature =
      feature_interface_reg.GetInterface<JointPosition>();
  ASSERT_NE(joint_position_feature, nullptr);
  auto prev_command = joint_position_feature->PreviousPositionSetpoints();
  eigenmath::VectorNd pos(1);
  pos << prev_command.position()[0];
  const double delta_pos_bad =
      1.1 * (kSystemMaxVel * kApplicationLimitFactor / kControlFrequency);
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
  pos[0] += delta_pos_bad;
  EXPECT_THAT(
      joint_position_feature->SetPositionSetpoints(JointPositionCommand(pos)),
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("velocity joint limits")));
}

TEST_F(HalArmPartJointLimitsTest, CanRecoverFromBeyondJointPositionLimits) {
  auto& feature_interface_reg = helper_with_part_->GetPartFeatureInterfaces();

  auto* joint_position_feature =
      feature_interface_reg.GetInterface<JointPosition>();
  ASSERT_NE(joint_position_feature, nullptr);

  // Set the initial joint position outside the joint position limit.
  module_ptr_->SetJointPositionState(1.1 * kSystemMaxPos);
  module_ptr_->SetJointPositionCommand(1.1 * kSystemMaxPos);

  eigenmath::VectorNd pos(1);
  pos << module_ptr_->GetJointPositionState();
  // Increment position moving *away* from safety
  pos[0] += 0.01;
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
  EXPECT_THAT(
      joint_position_feature->SetPositionSetpoints(JointPositionCommand(pos)),
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("position limits")));

  // Now increment position in the opposite direction: moving towards safety.
  pos[0] = module_ptr_->GetJointPositionState() - 0.01;
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(
      joint_position_feature->SetPositionSetpoints(JointPositionCommand(pos)));
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());
}

// Three degree of freedom hardware config used to test halarmpart with
// collision checking.
constexpr absl::string_view k3DofRobotNoClockConfig = R"pb(
  name: "fake_module"
  control_frequency_hz: 2500
  drives_realtime_clock: false
  module_config {
    [type.googleapis.com/intrinsic_proto.icon.FakeModuleConfig] {
      arm_interfaces {
        control_mode_state_name: "control_mode_state"
        joint_position_command_name: "joint_position_command"
        joint_velocity_command_name: "joint_velocity_command"
        joint_torque_command_name: "joint_torque_command"
        joint_position_state_name: "joint_position_state"
        joint_velocity_state_name: "joint_velocity_state"
        joint_acceleration_state_name: "joint_acceleration_state"
        joint_torque_state_name: "joint_torque_state"
        initial_joint_state {
          position_sensed: 0.0
          velocity_sensed: 0.0
          acceleration_sensed: 0.0
        }
        initial_joint_state {
          position_sensed: 0.0
          velocity_sensed: 0.0
          acceleration_sensed: 0.0
        }
        initial_joint_state {
          position_sensed: 0.0
          velocity_sensed: 0.0
          acceleration_sensed: 0.0
        }
      }
    }
  }
)pb";

// Adds a simple, four-link robot to `world`:
//
//            │
//            │ <- tip (link)
//            o <- l2_tip_joint (joint)
//            │
//            │ <- l2 (link)
//            o <- l1_l2_joint (joint)
//            │
//            │ <- l1 (link)
//       Z    o <- base_l1_joint (joint)
//    X  │    │
//     ╲ │    │ <- base (link)
//  Y────┘    ┴
//
// All joints rotate about the X axis. That is, given the view above, positive
// joint angles rotate each link clockwise.
// Each link has a capsule geometry with kLinkLength and kLinkRadius.
// TODO(b/281047415) Move this to an external util.
absl::StatusOr<LinearChainRobotData> MakeFourLinkRobot(World& world) {
  INTR_ASSIGN_OR_RETURN(
      LinearChainRobotData robot_data,
      CreateLinearChainRobot(
          &world,
          LinearChainRobotParams{
              .robot_name = absl::StrCat(kHalArmPartName, "_robot"),
              .parent_id = kRootEntityId,
              .link_params = {{.name = "base"},
                              {.name = "l1"},
                              {.name = "l2"},
                              {.name = "tip"}},
              .joint_params =
                  {{.name = "base_l1_joint",
                    .parent_t_inboard = Pose3d(eigenmath::Vector3d{0, 0, 0.5})},
                   {.name = "l1_l2_joint",
                    .parent_t_inboard = Pose3d(eigenmath::Vector3d{0, 0, 0.5})},
                   {.name = "l2_tip_joint",
                    .parent_t_inboard =
                        Pose3d(eigenmath::Vector3d{0, 0, 0.5})}},
              .add_coordinate_frame = false,
              .add_solver_key = true}));
  for (const LinkEntityId link_id : robot_data.link_ids) {
    // We want to ignore collisions of l1 and l2 with its immediate neighbors.
    // Rather than setting individual exclusions, just disable collision
    // altogether, since the only collision objects in World are our four
    // links.
    if (world.GetLocalNameForEntityById(link_id) == "l1" ||
        world.GetLocalNameForEntityById(link_id) == "l2") {
      INTR_ASSIGN_OR_RETURN(
          CollisionComponent * collision_component,
          world.GetComponentByEntityId<CollisionComponent>(link_id));
      collision_component->SetCollisionResponse(false);
    }
    INTR_ASSIGN_OR_RETURN(
        GeometryComponent * geometry_component,
        world.GetComponentByEntityId<GeometryComponent>(link_id));

    geometry_component->SetGeometry(
        kKindCollisionGeometry,
        // Capsules default to being centered around the origin, so translate up
        // by half the length.
        {{"0", MakeTransformedCapsule(
                   /*length=*/kLinkLength, /*radius=*/kLinkRadius,
                   /*ref_t_shape=*/
                   Pose3d(eigenmath::Vector3d(0, 0, 0.5 * kLinkLength)))}});
  }
  return robot_data;
}

struct TestParams {
  std::string resource_id = "";
  std::string hardware_resource_name = "";
};

class HalArmPartTestWithServices : public ::testing::TestWithParam<TestParams> {
 public:
  static constexpr char kModuleName[] = "fake_module";
  static constexpr int kNDof = 3;

  static constexpr double kMaxAbsPos = 1000.0;
  static constexpr double kMaxVel = 900.0;
  static constexpr double kMaxAcc = 800.0;
  static constexpr double kMaxJerk = 700.0;
  static constexpr double kMaxTorque = 600.0;

  static constexpr double kExpectedApplicationLimitRatio = 0.95;

  void SetUp() override {
    intrinsic_proto::icon::HardwareModuleConfig loopback_module_config =
        ParseTextProtoOrDie(k3DofRobotNoClockConfig);
    loopback_module_config.set_name(kModuleName);

    auto loopback_module =
        std::make_unique<intrinsic::timeslicer::fake_module::FakeModule>();

    std::string shared_memory_namespace =
        intrinsic::icon::UniqueMemoryNamespace();
    {
      ASSERT_OK_AND_ASSIGN(auto shm_manager,
                           intrinsic::icon::SharedMemoryManager::Create(
                               shared_memory_namespace, kModuleName));
      ASSERT_OK_AND_ASSIGN(auto module_runtime,
                           HardwareModuleRuntime::Create(
                               std::move(shm_manager),
                               {
                                   /*realtime_clock=*/nullptr,
                                   std::move(loopback_module),
                                   ModuleConfig(loopback_module_config,
                                                /*shared_memory_namespace=*/"",
                                                /*realtime_clock=*/nullptr),
                               }));
      module_runtime_ = std::move(module_runtime);
    }

    grpc::ServerBuilder builder;
    ASSERT_OK(module_runtime_->Run(builder));

    WorldHashMap<std::string, World> worlds;

    World four_link_robot_world = World::CreateEmptyWorld();
    ASSERT_OK_AND_ASSIGN(LinearChainRobotData robot_data,
                         MakeFourLinkRobot(four_link_robot_world));
    ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<DofKinematicView> dof_view,
        four_link_robot_world.GetDofKinematicView(robot_data.robot_id));
    JointLimitsXd system_limits;
    system_limits.SetSize(kNDof);
    system_limits.min_position.setConstant(-kMaxAbsPos);
    system_limits.max_position.setConstant(kMaxAbsPos);
    system_limits.max_velocity.setConstant(kMaxVel);
    system_limits.max_acceleration.setConstant(kMaxAcc);
    system_limits.max_jerk.setConstant(kMaxJerk);
    system_limits.max_torque.setConstant(kMaxTorque);
    ASSERT_OK(
        dof_view->SetDofSystemLimits(system_limits, /*enforce_limits=*/false));
    JointLimitsXd application_limits = system_limits;
    application_limits.max_velocity *= kExpectedApplicationLimitRatio;
    application_limits.max_acceleration *= kExpectedApplicationLimitRatio;
    application_limits.max_jerk *= kExpectedApplicationLimitRatio;
    ASSERT_OK(dof_view->SetDofApplicationLimits(application_limits,
                                                /*enforce_limits=*/false));

    // Set limits for the test robot.
    ASSERT_OK_AND_ASSIGN(
        WorldServers world_servers,
        InitializeWorldService(std::move(four_link_robot_world)));

    ASSERT_OK_AND_ASSIGN(
        auto world_from_grpc_service,
        WorldFromGrpcService::Create(
            {}, "world", world_servers.world_service->NewObjectStub()));
    ASSERT_OK_AND_ASSIGN(
        auto assembly_from_world_service,
        AssemblyFromWorldService::Create({}, world_from_grpc_service.get()));
    ASSERT_OK_AND_ASSIGN(
        auto kinematics_from_world_service,
        KinematicsFromWorldService::Create({}, world_from_grpc_service.get()));
    ASSERT_OK_AND_ASSIGN(
        auto geometry_from_geometry_library_service,
        GeometryFromGeometryLibrary::Create(
            intrinsic_proto::geometry::GeometryService::NewStub(
                world_servers.geometry_server->InProcessChannel(
                    grpc::ChannelArguments()))));
    ASSERT_OK_AND_ASSIGN(
        service_collection_,
        ServiceCollection::CreateFromInitializedServices(
            std::move(world_from_grpc_service),
            std::move(assembly_from_world_service),
            std::move(kinematics_from_world_service),
            std::move(geometry_from_geometry_library_service)));
    WorldService* icon_world_service =
        service_collection_.GetServiceOrNull<WorldService>();
    AssemblyService* assembly_service =
        service_collection_.GetServiceOrNull<AssemblyService>();
    ASSERT_NE(icon_world_service, nullptr);
    ASSERT_NE(assembly_service, nullptr);

    intrinsic_proto::icon::HalArmPartConfig hal_arm_part_config;
    hal_arm_part_config.mutable_joint_position_command()->set_module_name(
        kModuleName);
    hal_arm_part_config.mutable_joint_position_command()->set_interface_name(
        SingleDofModule::kPositionCommandInterfaceName);
    // We do not set a hardware interface for joint limits, but specify
    // world_robot_collection name. HalArmPart should read values from the World
    // (see above).
    std::string hardware_resource_name = GetParam().resource_id;
    if (!GetParam().hardware_resource_name.empty()) {
      hardware_resource_name = GetParam().hardware_resource_name;
    }
    hal_arm_part_config.mutable_joint_position_state()->set_module_name(
        kModuleName);
    hal_arm_part_config.mutable_joint_position_state()->set_interface_name(
        SingleDofModule::kPositionStateInterfaceName);
    hal_arm_part_config.set_check_collisions(true);

    ASSERT_OK_AND_ASSIGN(
        auto loopback_module_proxy,
        HardwareModuleProxy::Attach(shared_memory_namespace, kModuleName,
                                    absl::ZeroDuration()));

    ASSERT_OK(hwm_manager_.Add(std::move(loopback_module_proxy),
                               /*proxy_drives_clock=*/false, {}));

    Context context(
        /*service_collection=*/&service_collection_,
        /*hardware_module_manager=*/&hwm_manager_,
        /*runtime_options=*/{.resource_id = GetParam().resource_id});

    ASSERT_OK_AND_ASSIGN(
        helper_with_part_,
        PartTestHelper::CreateFromProtoFactory(
            {
                .part_name = kHalArmPartName,
                .context = context,
                .control_frequency_hz = kControlFrequency,
                .hardware_resource_name = hardware_resource_name,
            },
            HalArmPart::kPartTypeName, hal_arm_part_config));
  }

  ~HalArmPartTestWithServices() override { EXPECT_OK(module_runtime_->Stop()); }

  SingleDofModule* module_ptr_;
  std::unique_ptr<HardwareModuleRuntime> module_runtime_;
  ServiceCollection service_collection_;
  std::unique_ptr<PartTestHelper> helper_with_part_ = nullptr;
  HardwareModuleManager hwm_manager_;
};

TEST_P(HalArmPartTestWithServices,
       PartInitializesCorrectlyWithCollisionChecking) {
  auto& feature_interface_reg = helper_with_part_->GetPartFeatureInterfaces();
  // Joint Position Command, State and Limits were set and correctly converted.
  EXPECT_NE(feature_interface_reg.GetInterface<JointPosition>(), nullptr);
  EXPECT_NE(feature_interface_reg.GetInterface<JointPositionSensor>(), nullptr);
  const JointLimitsInterface* joint_limits_interface =
      feature_interface_reg.GetInterface<JointLimitsInterface>();
  EXPECT_NE(joint_limits_interface, nullptr);
  EXPECT_EQ(joint_limits_interface->GetSystemLimits().size(), kNDof);
  EXPECT_EQ(joint_limits_interface->GetSystemLimits().min_position(0),
            -kMaxAbsPos);
  EXPECT_EQ(joint_limits_interface->GetSystemLimits().max_position(0),
            kMaxAbsPos);
  EXPECT_EQ(joint_limits_interface->GetSystemLimits().max_velocity(0), kMaxVel);
  EXPECT_EQ(joint_limits_interface->GetSystemLimits().max_acceleration(0),
            kMaxAcc);
  EXPECT_EQ(joint_limits_interface->GetSystemLimits().max_jerk(0), kMaxJerk);
  EXPECT_EQ(joint_limits_interface->GetSystemLimits().max_torque(0),
            kMaxTorque);

  EXPECT_EQ(joint_limits_interface->GetApplicationLimits().size(), kNDof);
  EXPECT_EQ(joint_limits_interface->GetApplicationLimits().min_position(0),
            -kMaxAbsPos);
  EXPECT_EQ(joint_limits_interface->GetApplicationLimits().max_position(0),
            kMaxAbsPos);
  EXPECT_EQ(joint_limits_interface->GetApplicationLimits().max_velocity(0),
            kExpectedApplicationLimitRatio * kMaxVel);
  EXPECT_EQ(joint_limits_interface->GetApplicationLimits().max_acceleration(0),
            kExpectedApplicationLimitRatio * kMaxAcc);
  EXPECT_EQ(joint_limits_interface->GetApplicationLimits().max_jerk(0),
            kExpectedApplicationLimitRatio * kMaxJerk);
  EXPECT_EQ(joint_limits_interface->GetApplicationLimits().max_torque(0),
            kMaxTorque);

  // MoveOk is implicit if joint  limits and position are defined
  EXPECT_NE(feature_interface_reg.GetInterface<MoveOk>(), nullptr);
  // Cartesian limits are present on all kinematic robots in the object world.
  EXPECT_NE(feature_interface_reg.GetInterface<CartesianLimitsInterface>(),
            nullptr);
  // All other feature interfaces were not present in the hal arm part config.
  EXPECT_EQ(feature_interface_reg.GetInterface<JointVelocityEstimator>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointAccelerationEstimator>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointVelocity>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointTorque>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<JointTorqueSensor>(), nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<ManipulatorKinematics>(),
            nullptr);
  EXPECT_EQ(feature_interface_reg.GetInterface<Dynamics>(), nullptr);
}

TEST_P(HalArmPartTestWithServices, FaultsEnteringSelfCollision) {
  auto& feature_interface_reg = helper_with_part_->GetPartFeatureInterfaces();
  auto* joint_position_feature =
      feature_interface_reg.GetInterface<JointPosition>();

  ASSERT_NE(joint_position_feature, nullptr);
  // Start the robot in a position that is not in collision.
  eigenmath::VectorNd pos;
  pos.setConstant(kNDof, M_PI * 0.4);
  ASSERT_OK(
      joint_position_feature->SetPositionSetpoints(JointPositionCommand(pos)));
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());

  // Setpoint that would result in collision.
  //    o──────o
  //    │      │
  //    │      │
  //   [!]─────o
  //
  pos.setConstant(M_PI * 0.5);
  EXPECT_THAT(
      joint_position_feature->SetPositionSetpoints(JointPositionCommand(pos)),
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("collision")));
}

INSTANTIATE_TEST_SUITE_P(
    HalArmPartTestWithServices, HalArmPartTestWithServices,
    ::testing::Values(
        // Just a resource ID only should work
        TestParams{
            .resource_id = absl::StrCat(kHalArmPartName, "_robot"),
            .hardware_resource_name = "",
        },
        // Just a hardware_resource_name should work
        TestParams{
            .resource_id = "",
            .hardware_resource_name = absl::StrCat(kHalArmPartName, "_robot"),
        },
        // Both should work.
        TestParams{
            .resource_id = absl::StrCat(kHalArmPartName, "_robot"),
            .hardware_resource_name = absl::StrCat(kHalArmPartName, "_robot"),
        },
        // hardware_resource_name should override resource ID if different.
        TestParams{
            .resource_id = "Something invalid",
            .hardware_resource_name = absl::StrCat(kHalArmPartName, "_robot"),
        }));

TEST_F(HalArmPartJointLimitsTest, OperationalStateFromHwmState) {
  EXPECT_THAT(
      helper_with_part_->part().GetOperationalStatus(),
      RealtimeIsOkAndHolds(Field("state", &RealtimeOperationalStatus::state,
                                 RealtimeOperationalState::kDisabled)));

  auto deadline = absl::Now() + absl::Seconds(15);
  ASSERT_OK(hwm_manager_.Prepare(deadline));
  EXPECT_OK(hwm_manager_.Activate(deadline));
  EXPECT_THAT(
      helper_with_part_->part().GetOperationalStatus(),
      RealtimeIsOkAndHolds(Field("state", &RealtimeOperationalStatus::state,
                                 RealtimeOperationalState::kDisabled)));
  {
    module_ptr_->SetFailingTransitions(true);
    absl::Notification enabled_notification;
    Thread thread;
    ASSERT_OK_AND_ASSIGN(
        thread,
        CreateRealtimeCapableThread(ThreadOptions().SetName("HwmLoop"), [&] {
          while (!enabled_notification.HasBeenNotified()) {
            EXPECT_OK(hwm_manager_.Read(deadline));
            EXPECT_OK(hwm_manager_.UpdateState());
          }
        }));
    // EnableMotion should fail because we inserted a failing transition.
    EXPECT_THAT(hwm_manager_.EnableMotionTestOnly(deadline),
                StatusIs(absl::StatusCode::kUnavailable));
    enabled_notification.Notify();
    thread.join();
    EXPECT_OK(hwm_manager_.UpdateState());
    EXPECT_THAT(helper_with_part_->part().GetOperationalStatus(),
                RealtimeIsOkAndHolds(
                    Field("state", &RealtimeOperationalStatus::state,
                          RealtimeOperationalState::kFaultedConnected)));
  }
  module_ptr_->SetFailingTransitions(false);
  absl::Notification enabled_notification;
  Thread thread;
  ASSERT_OK_AND_ASSIGN(
      thread,
      CreateRealtimeCapableThread(ThreadOptions().SetName("HwmLoop"), [&] {
        while (!enabled_notification.HasBeenNotified()) {
          EXPECT_OK(hwm_manager_.Read(deadline));
          EXPECT_OK(hwm_manager_.UpdateState());
        }
      }));
  EXPECT_OK(hwm_manager_.ClearFaultsTestOnly(deadline));
  EXPECT_OK(hwm_manager_.EnableMotionTestOnly(deadline));
  enabled_notification.Notify();
  thread.join();
  EXPECT_OK(hwm_manager_.UpdateState());

  EXPECT_THAT(
      helper_with_part_->part().GetOperationalStatus(),
      RealtimeIsOkAndHolds(Field("state", &RealtimeOperationalStatus::state,
                                 RealtimeOperationalState::kEnabled)));
}

}  // namespace
}  // namespace intrinsic::icon
