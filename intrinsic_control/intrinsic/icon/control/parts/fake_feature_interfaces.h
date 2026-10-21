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

#ifndef INTRINSIC_ICON_CONTROL_PARTS_FAKE_FEATURE_INTERFACES_H_
#define INTRINSIC_ICON_CONTROL_PARTS_FAKE_FEATURE_INTERFACES_H_

#include <stddef.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/manifolds.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/control/parts/io_block.h"
#include "intrinsic/icon/control/parts/realtime_robot_payload.h"
#include "intrinsic/icon/dynamics/mock_rigid_body_interface.h"
#include "intrinsic/icon/dynamics/rigid_body_interface.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/kinematics/elements.h"
#include "intrinsic/kinematics/ik/fake_inverse_kinematics.h"
#include "intrinsic/kinematics/ik/inverse_kinematics_interface.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/kinematics/test_utils.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {

// Fakes, rather than gmock Mocks, of the FeatureInterfaces. We can't use gmock
// Mocks, because those allocate memory, so they would make it impossible to use
// INTRINSIC_MALLOC_COUNTER in Action unit tests.

class FakeJointPosition final : public JointPosition {
 public:
  explicit FakeJointPosition(int ndof);

  // FeatureInterface method – saves `setpoints` for later inspection.
  RealtimeStatus SetPositionSetpoints(
      const JointPositionCommand& setpoints) override;

  JointPositionCommand PreviousPositionSetpoints() const override;

  // Sets the return value for all subsequent calls to SetPositionSetpoints().
  // Call this with a non-OK status to induce errors.
  void SetReturnStatus(RealtimeStatus s);

 protected:
  RealtimeStatus next_status_ = RealtimeStatus();
  JointPositionCommand setpoints_;
};

class FakeJointVelocity final : public JointVelocity {
 public:
  explicit FakeJointVelocity(int ndof);

  // FeatureInterface method – saves `setpoints` for later inspection.
  RealtimeStatus SetVelocitySetpoints(
      const eigenmath::VectorNd& setpoints) override;

  // Sets the return value for all subsequent calls to SetVelocitySetpoints().
  // Call this with a non-OK status to induce errors.
  void SetReturnStatus(RealtimeStatus s);

  // Returns the setpoints from the last call to SetVelocitySetpoints. Use this
  // for assertions.
  const eigenmath::VectorNd& GetVelocitySetpoints() const;

 protected:
  RealtimeStatus next_status_ = RealtimeStatus();
  eigenmath::VectorNd setpoints_;
};

class FakeJointAcceleration final : public JointAcceleration {
 public:
  explicit FakeJointAcceleration(int ndof);

  // FeatureInterface method – saves `setpoints` for later inspection.
  RealtimeStatus SetAccelerationSetpoints(
      const JointAccelerationCommand& setpoints) override;

  JointAccelerationCommand PreviousAccelerationSetpoints() const override;

  // Sets the return value for all subsequent calls to SetVelocitySetpoints().
  // Call this with a non-OK status to induce errors.
  void SetReturnStatus(RealtimeStatus s);

 protected:
  RealtimeStatus next_status_ = RealtimeStatus();
  JointAccelerationCommand setpoints_;
};

class FakeJointPositionSensor final : public JointPositionSensor {
 public:
  explicit FakeJointPositionSensor(int ndof);

  // Sets the return value for all subsequent calls to GetSensedPosition().
  void SetSensedPosition(const JointStateP& state);

  // FeatureInterface method – returns the value set by SetSensedPosition().
  JointStateP GetSensedPosition() const override;

 protected:
  JointStateP state_;
};

class FakeJointVelocityEstimator final : public JointVelocityEstimator {
 public:
  explicit FakeJointVelocityEstimator(int ndof);

  // Sets the return value for all subsequent calls to GetVelocityEstimate().
  void SetVelocityEstimate(const JointStateV& state);

  // FeatureInterface method – returns the value set by SetVelocityEstimate().
  JointStateV GetVelocityEstimate() const override;

 protected:
  JointStateV state_;
};

class FakeJointAccelerationEstimator final : public JointAccelerationEstimator {
 public:
  explicit FakeJointAccelerationEstimator(int ndof);

  // Sets the return value for all subsequent calls to
  // GetAccelerationEstimate().
  void SetAccelerationEstimate(const JointStateA& state);

  // FeatureInterface method – returns the value set by
  // SetAccelerationEstimate().
  JointStateA GetAccelerationEstimate() const override;

 protected:
  JointStateA state_;
};

class FakeJointLimits final : public JointLimitsInterface {
 public:
  // All calls to GetApplication/SystemLimits() will return these.
  FakeJointLimits(JointLimits application_limits, JointLimits system_limits);
  explicit FakeJointLimits(const JointLimits& application_and_system_limits);

  // FeatureInterface methods – return the value set in the constructor().
  JointLimits GetApplicationLimits() const override;
  JointLimits GetSystemLimits() const override;
  icon::RealtimeStatusOr<
      std::optional<JointLimitsInterface::JointAccelerationLimitsFromDynamics>>
  GetJointAccelerationLimitsFromDynamics() const override;

  void SetApplicationAndSystemLimits(
      const JointLimits& application_and_system_limits);
  void SetApplicationLimits(const JointLimits& application_limits);
  void SetSystemLimits(const JointLimits& system_limits);
  void SetJointAccelerationLimitsFromDynamics(
      const std::optional<
          JointLimitsInterface::JointAccelerationLimitsFromDynamics>&
          joint_acceleration_limits_from_dynamics);

 protected:
  JointLimits application_limits_;
  JointLimits system_limits_;
  std::optional<JointLimitsInterface::JointAccelerationLimitsFromDynamics>
      joint_acceleration_limits_from_dynamics_ = std::nullopt;
};

class FakeCartesianLimits final : public CartesianLimitsInterface {
 public:
  // Calls to GetDefaultCartesianLimits() will return these.
  explicit FakeCartesianLimits(const CartesianLimits& default_limits);

  // FeatureInterface methods – return the value set in the constructor().
  CartesianLimits GetDefaultCartesianLimits() const override;

  void SetDefaultCartesianLimits(const CartesianLimits& default_limits);

 protected:
  CartesianLimits default_limits_;
};

class FakeGripper final : public SimpleGripper {
 public:
  // Saves the GripperCommand for later inspection
  RealtimeStatus SetGripperCommand(const GripperCommand& command) override;
  GripperState GetGripperState() const override;

  // Sets the return value for all subsequent calls to SetGripperCommand().
  // Call this with a non-OK status to induce errors.
  void SetReturnStatus(RealtimeStatus s);
  // Sets the GripperState for all subsequent calls to GetGripperState().
  void SetGripperState(const GripperState& state);
  // Allows the inspection of the command set by SetGripperCommand().
  GripperCommand GetGripperCommand();

 protected:
  RealtimeStatus next_status_ = RealtimeStatus();
  GripperCommand command_ = GripperCommand::kUnknown;
  GripperState state_ = GripperState::kUnknown;
};

class FakeLinearGripper final : public LinearGripper {
 public:
  RealtimeStatus SetGripperCommand(double width, std::optional<double> force,
                                   std::optional<double> speed) override;
  double GetGripperWidth() const override;

  // Sets the width returned by GetGripperWidth.
  void SetGripperWidth(double width);

  // Sets the return value for all subsequent calls to SetGripperCommand().
  // Call this with a non-OK status to induce errors.
  void SetReturnStatus(RealtimeStatus s);

  // Accessors to inspect values set by SetGripperCommand.
  std::optional<double> GetCommandedWidth() const;
  std::optional<double> GetCommandedForce() const;
  std::optional<double> GetCommandedSpeed() const;

 private:
  RealtimeStatus next_status_ = RealtimeStatus();
  std::optional<double> commanded_width_ = std::nullopt;
  std::optional<double> commanded_force_ = std::nullopt;
  std::optional<double> commanded_speed_ = std::nullopt;
  double sensed_width_ = 0.;
};

class FakeRangeFinder final : public RangeFinder {
 public:
  FakeRangeFinder(double fake_dist, Pose3d fake_pose);
  double GetSensedDistance() const override;
  bool IsMeasurementValid() const override;
  Pose3d GetPoseInTCPFrame() const override;
  void SetMeasurementValid(bool valid);
  void SetSensedDistance(double distance);

 protected:
  double fake_distance_m_;
  const Pose3d fake_pose_;
  bool fake_measurement_valid_ = true;
};

class FakeInertialMeasurementUnit final : public InertialMeasurementUnit {
 public:
  FakeInertialMeasurementUnit(eigenmath::Vector3d sensed_linear_acceleration,
                              eigenmath::Vector3d sensed_angular_velocity,
                              eigenmath::Quaterniond sensed_orientation,
                              Pose3d fake_pose);
  eigenmath::Vector3d GetSensedLinearAcceleration() const override;
  eigenmath::Vector3d GetSensedAngularVelocity() const override;
  eigenmath::Quaterniond GetSensedOrientation() const override;
  void SetSensedLinearAcceleration(
      const eigenmath::Vector3d& linear_acceleration);
  void SetSensedAngularVelocity(const eigenmath::Vector3d& angular_velocity);
  void SetSensedOrientation(const eigenmath::Quaterniond& orientation);
  Pose3d GetPoseInFlangeFrame() const override;

 protected:
  eigenmath::Vector3d sensed_linear_acceleration_;
  eigenmath::Vector3d sensed_angular_velocity_;
  eigenmath::Quaterniond sensed_orientation_;
  Pose3d fake_pose_;
};

class FakeProcessWrenchAtEndeffector final : public ProcessWrenchAtEndeffector {
 public:
  Wrench GetProcessWrenchAtTip() const override {
    return process_wrench_at_tip_;
  }
  void SetProcessWrenchAtTip(const Wrench& wrench_at_tip) override {
    process_wrench_at_tip_ = wrench_at_tip;
  }

 protected:
  Wrench process_wrench_at_tip_;
};

class FakeADIO final : public ADIO {
 public:
  struct FakeADIOState {
    absl::flat_hash_map<std::string, AnalogBlock> analog_inputs;
    absl::flat_hash_map<std::string, AnalogBlock> analog_outputs;
    absl::flat_hash_map<std::string, DioBlock> digital_inputs;
    absl::flat_hash_map<std::string, DioBlock> digital_outputs;

    absl::flat_hash_map<std::string, std::vector<std::string>>
        digital_input_signal_names;
    absl::flat_hash_map<std::string, std::vector<std::string>>
        digital_output_signal_names;
    absl::flat_hash_map<std::string, std::vector<std::string>>
        analog_input_signal_names;
    absl::flat_hash_map<std::string, std::vector<std::string>>
        analog_output_signal_names;
  };

  // Initializes the FakeADIO with the input and output blocks provided in
  // `initial_state`.
  explicit FakeADIO(FakeADIOState initial_state);

  // FeatureInterface methods
  // Lists the available names of the respective IO Blocks.
  absl::Span<const std::string> DigitalInputBlockNames() const override;
  absl::Span<const std::string> DigitalInputSignalNames(
      absl::string_view block_name) const override;
  absl::Span<const std::string> DigitalOutputBlockNames() const override;
  absl::Span<const std::string> DigitalOutputSignalNames(
      absl::string_view block_name) const override;
  absl::Span<const std::string> AnalogInputBlockNames() const override;
  absl::Span<const std::string> AnalogInputSignalNames(
      absl::string_view block_name) const override;
  absl::Span<const std::string> AnalogOutputBlockNames() const override;
  absl::Span<const std::string> AnalogOutputSignalNames(
      absl::string_view block_name) const override;

  // FeatureInterface methods
  // Provides a pointer to the respective IO Block.
  // Returns a nullptr if no block with that name exist.
  const AnalogBlock* AnalogInputBlock(absl::string_view name) const override;
  const DioBlock* DigitalInputBlock(absl::string_view name) const override;
  DioBlock* MutableDigitalOutputBlock(absl::string_view name) override;
  AnalogBlock* MutableAnalogOutputBlock(absl::string_view name) override;
  ADIO::ADIOState GetADIOState() const override;

  // Additional methods to mutate the available IO Blocks.
  AnalogBlock* MutableAnalogInputBlock(absl::string_view name);
  DioBlock* MutableDigitalInputBlock(absl::string_view name);

 protected:
  std::vector<std::string> analog_input_names_;
  std::vector<std::string> analog_output_names_;
  std::vector<std::string> digital_input_names_;
  std::vector<std::string> digital_output_names_;

  absl::flat_hash_map<std::string, std::vector<std::string>>
      digital_input_signal_names_;
  absl::flat_hash_map<std::string, std::vector<std::string>>
      digital_output_signal_names_;
  absl::flat_hash_map<std::string, std::vector<std::string>>
      analog_input_signal_names_;
  absl::flat_hash_map<std::string, std::vector<std::string>>
      analog_output_signal_names_;

  FakeADIOState state_;
};

class FakeJointTorque final : public JointTorque {
 public:
  explicit FakeJointTorque(int ndof);

  // FeatureInterface method – saves `setpoints` for later inspection.
  RealtimeStatus SetTorqueSetpoints(
      const eigenmath::VectorNd& setpoints) override;

  // FeatureInterface method – returns the value set by SetTorqueSetpoints().
  eigenmath::VectorNd PreviousTorqueSetpoints() const override;

  // Sets the return value for all subsequent calls to SetPositionSetpoints().
  // Call this with a non-OK status to induce errors.
  void SetReturnStatus(RealtimeStatus s);

  // Returns the setpoints from the last call to SetTorqueSetpoints. Use this
  // for assertions.
  const eigenmath::VectorNd& GetTorqueSetpoints() const;

 protected:
  RealtimeStatus next_status_ = OkStatus();
  eigenmath::VectorNd setpoints_;
};

class FakeJointTorqueSensor final : public JointTorqueSensor {
 public:
  explicit FakeJointTorqueSensor(int ndof);

  // Sets the return value for all subsequent calls to GetSensedTorque().
  void SetSensedTorque(const JointStateT& state);

  // FeatureInterface method – returns the value set by SetSensedTorque().
  JointStateT GetSensedTorque() const override;

 protected:
  JointStateT state_;
};

class FakeManipulatorKinematics final : public ManipulatorKinematics {
 public:
  FakeManipulatorKinematics() = delete;

  // Initializes the FakeInverseKinematics with a given number of DoFs.
  explicit FakeManipulatorKinematics(size_t ndof)
      : fake_inverse_kinematics_(ndof),
        fake_skeleton_(
            kinematics::testing::CreateSerialChainSkeleton(ndof).value()) {
    auto chain_or = kinematics::CreateChainFromModel(*fake_skeleton_);
    CHECK_OK(chain_or);
    fake_chain_ = std::move(chain_or.value());
  }

  // Initializes the FakeInverseKinematics with a given skeleton.
  // TODO(b/189440093): this constructor should go away, and is only a temporary
  // solution for testing during transitioning to Kinematics3.
  explicit FakeManipulatorKinematics(
      std::unique_ptr<kinematics::Skeleton> skeleton)
      : fake_inverse_kinematics_(skeleton->GetNumberDegreesOfFreedom()),
        fake_skeleton_(std::move(skeleton)) {
    if (fake_skeleton_->HasOneTip()) {
      auto chain_or = kinematics::CreateChainFromModel(*fake_skeleton_);
      CHECK_OK(chain_or);
      fake_chain_ = std::move(chain_or.value());
    }
  }

  // Sets the return value for all subsequent calls to RealtimeComputeIK().
  icon::RealtimeStatus SetIKResult(
      absl::Span<const JointStateP> fake_ik_joint_solutions,
      const kinematics::InverseKinematicsInterface::IKResult& fake_ik_result) {
    return fake_inverse_kinematics_.SetIKResult(fake_ik_joint_solutions,
                                                fake_ik_result);
  }

  // ManipulatorKinematics method. Returns a FakeInverseKinematics initialized
  // with njoints.
  const kinematics::InverseKinematicsInterface& GetInverseKinematicsSolver()
      const override {
    return fake_inverse_kinematics_;
  }

  std::string_view GetInverseKinematicsSolverName() const override {
    return fake_inverse_kinematics_.GetName();
  }

  // Returns a kinematics model instance initialized with a test skeleton.
  const kinematics::Skeleton& GetKinematicsModel() const override {
    return *fake_skeleton_;
  }

  intrinsic::icon::RealtimeStatusOr<const kinematics::Chain*>
  GetKinematicsChain(kinematics::ElementId tip_id) const override {
    if (tip_id != fake_chain_.GetTipId()) {
      return icon::InvalidArgumentError(RealtimeStatus::StrCat(
          "Wrong tip_id specified. Wanted ", fake_chain_.GetTipId().value(),
          ", got ", tip_id.value()));
    }
    return &fake_chain_;
  }

  // Sets the return value for all subsequent calls to ComputeChainFK().
  // Call this with a non-OK status to induce errors, or with OkStatus to stop
  // returning errors.
  // N.B.: Injected errors *do not* "auto-reset" after calls to
  // ComputeChainFK().
  void SetFKReturnStatus(intrinsic::icon::RealtimeStatus status) {
    fk_status_ = status;
  }
  void SetJacobianReturnStatus(intrinsic::icon::RealtimeStatus status) {
    jacobian_status_ = status;
  }

  // This is a fake method for testing that just returns a pose in which the
  // translation is given as the first three joint input values and the rotation
  // is the expSO3 of the last 3 joint input values.
  intrinsic::icon::RealtimeStatusOr<Pose3d> ComputeChainFK(
      const JointStateP& dof_positions) const override {
    CHECK(dof_positions.IsSizeConsistent())
        << "Joint state size is inconsistent.";
    const size_t njoints = fake_inverse_kinematics_.GetNumDof();
    CHECK_EQ(dof_positions.size(), njoints) << absl::StrFormat(
        "Joint state size (%zu) should be %zu.", dof_positions.size(), njoints);

    // Early return if a user has injected an error.
    INTRINSIC_RT_RETURN_IF_ERROR(fk_status_);

    // Initialize resulting pose with identity, required for the case njoints
    // < 6.
    Pose3d base_t_tip;
    base_t_tip.translation() = eigenmath::Vector3d::Zero();
    base_t_tip.setQuaternion(eigenmath::Quaterniond::Identity());
    eigenmath::Vector6d joint_position_variable = eigenmath::Vector6d::Zero();
    // Forward the values of up to the first 6 joints directly to cartesian
    // variables.
    for (size_t i = 0; i < 6 && i < njoints; ++i) {
      joint_position_variable(i) = dof_positions.position[i];
    }

    base_t_tip.translation() = joint_position_variable.head<3>();
    base_t_tip.so3() = intrinsic::eigenmath::expSO3(
        eigenmath::Vector3d(joint_position_variable.tail<3>()));

    return base_t_tip;
  }

  // Fake Jacobian. Simply returns the identity matrix, for testing.
  intrinsic::icon::RealtimeStatusOr<eigenmath::Matrix6Nd> ComputeChainJacobian(
      const JointStateP& dof_positions) const override {
    // Early return if a user has injected an error.
    INTRINSIC_RT_RETURN_IF_ERROR(jacobian_status_);
    const int njoints = fake_inverse_kinematics_.GetNumDof();
    CHECK_EQ(dof_positions.size(), njoints);
    eigenmath::Matrix6Nd jacobian = eigenmath::Matrix6Nd::Identity(6, njoints);
    return jacobian;
  }

 protected:
  kinematics::FakeInverseKinematics fake_inverse_kinematics_;
  std::unique_ptr<kinematics::Skeleton> fake_skeleton_;
  kinematics::Chain fake_chain_;
  intrinsic::icon::RealtimeStatus fk_status_ = OkStatus();
  intrinsic::icon::RealtimeStatus jacobian_status_ = OkStatus();
};

// While the override of GetRigidBodyInterface() itself is realtime safe, any
// actions on the returned mock are not, so this does not follow the same naming
// convention as the other classes in this file.
class MockDynamics final : public Dynamics {
 public:
  MockDynamics() : mock_dynamics_(std::make_unique<MockRigidBodyInterface>()) {}

  RigidBodyInterface& GetRigidBodyInterface() override {
    return *mock_dynamics_;
  }

  // Returns a reference to the mock objects so users can set expectations.
  MockRigidBodyInterface& DynamicsMock() { return *mock_dynamics_; }

 private:
  std::unique_ptr<MockRigidBodyInterface> mock_dynamics_;
};

class FakeForceTorqueSensor final : public ForceTorqueSensor {
 public:
  Wrench WrenchAtSensorUncompensated() const override {
    return wrench_at_ft_uncompensated_;
  }
  Wrench WrenchAtSensor() const override { return wrench_at_ft_; }
  Wrench WrenchAtTip() const override { return wrench_at_tip_; }
  double WrenchStabilityIndex() const override {
    return wrench_stability_index_;
  }
  Wrench PostSensorDynamicLoadAtSensor() const override;
  Wrench PostSensorDynamicLoadAtTip() const override;
  RealtimeStatus Tare(int num_taring_cycles) override;
  RealtimeStatusOr<bool> TareIsDone() const override { return tare_is_done_; }

  void SetWrenchAtSensorUncompensated(const Wrench& w) {
    wrench_at_ft_uncompensated_ = w;
  }
  void SetWrenchAtSensor(const Wrench& w) { wrench_at_ft_ = w; }
  void SetWrenchAtTip(const Wrench& w) { wrench_at_tip_ = w; }
  void SetWrenchStabilityIndex(const double wrench_stability_index) {
    wrench_stability_index_ = wrench_stability_index;
  }
  void SetPostSensorDynamicLoadAtSensor(const Wrench& w);
  void SetPostSensorDynamicLoadAtTip(const Wrench& w);
  bool GetTareRequestedFlag() const { return tare_requested_; }
  void SetTareIsDone(bool tare_is_done) { tare_is_done_ = tare_is_done; }
  // Sets the return value for all subsequent calls to Tare().
  // Call this with a non-OK status to induce errors, or with an OkStatus to
  // stop returning errors.
  void SetReturnStatus(RealtimeStatus s);

 private:
  Wrench wrench_at_ft_uncompensated_;
  Wrench wrench_at_ft_;
  Wrench wrench_at_tip_;
  Wrench post_sensor_dynamic_load_at_ft_;
  Wrench post_sensor_dynamic_load_at_tip_;
  double wrench_stability_index_;
  bool tare_requested_ = false;
  bool tare_is_done_ = false;
  RealtimeStatus next_status_ = OkStatus();
};

class FakeStandaloneForceTorqueSensor final
    : public StandaloneForceTorqueSensor {
 public:
  Wrench WrenchAtSensorUncompensated() const override { return wrench_at_ft_; }
  RealtimeStatus Tare(int num_taring_cycles) override;
  RealtimeStatusOr<bool> TareIsDone() const override { return tare_is_done_; }

  void SetWrenchAtSensorUncompensated(const Wrench& w) { wrench_at_ft_ = w; }
  bool GetTareRequestedFlag() const { return tare_requested_; }
  void SetTareIsDone(bool tare_is_done) { tare_is_done_ = tare_is_done; }
  // Sets the return value for all subsequent calls to Tare().
  // Call this with a non-OK status to induce errors, or with an OkStatus to
  // stop returning errors.
  void SetReturnStatus(RealtimeStatus s);

 private:
  Wrench wrench_at_ft_;
  bool tare_requested_ = false;
  bool tare_is_done_ = false;
  RealtimeStatus next_status_ = OkStatus();
};

class FakeHandGuiding final : public HandGuiding {
 public:
  RealtimeStatus CommandHandGuiding() override {
    hand_guiding_ = true;
    return next_status_;
  }

  // Returns true after CommandHandGuiding was called.
  bool IsHandGuidingRunning() { return hand_guiding_; }
  // Resets the hand-guiding state back to false.
  void ClearHandGuiding() { hand_guiding_ = false; }

  // Sets the return value for all subsequent calls to CommandHandGuiding().
  // Call this with a non-OK status to induce errors.
  void SetReturnStatus(RealtimeStatus s) { next_status_ = s; }

 private:
  bool hand_guiding_ = false;
  RealtimeStatus next_status_ = RealtimeStatus();
};

class FakeHoming final : public Homing {
 public:
  // Commands the drive with `drive_name` to start homing. The parameters
  // `homing_method`, `search_speed`, `creep_speed`, `acceleration` and `offset`
  // are used to configure the homing motion.
  //
  // `drive_name`: The name of the drive to command for homing.
  // `homing_method`: The homing method to use. There are a few standard
  //                  methods, but the drive manufacturer can define custom
  //                  methods as well (then often using negative values).
  // `search_speed`: The speed at which the drive will search a switch point.
  //                 For the unit please see the manufacturer's documentation.
  // `creep_speed`: The speed at which the drive will approach the zero point.
  //                For the unit please see the manufacturer's documentation.
  // `acceleration`: The acceleration at which the drive will approach the zero
  //                 point. For the unit please see the manufacturer's
  //                 documentation.
  // `offset`: The offset that'll be applied to the new home position after
  //           homing. For the unit please see the manufacturer's documentation.
  //
  // Returns:
  //   - `OkStatus` if the homing command was successfully sent.
  //   - `InternalError` if the homing command could not be sent.
  RealtimeStatus CommandHoming(absl::string_view drive_name,
                               int8_t homing_method, double search_speed,
                               double creep_speed, double acceleration,
                               double offset) override {
    ++command_homing_call_count_;
    homing_ = true;
    homing_done_ = false;
    return next_status_;
  }

  // Returns true if the drive with `drive_name` is currently homing.
  //
  // `drive_name`: The name of the drive to check.
  bool IsHoming(absl::string_view drive_name) const override { return homing_; }

  // Returns true if the drive with `drive_name` is done homing.
  //
  // `drive_name`: The name of the drive to check.
  //
  // Returns:
  //   - `true` if the homing is done.
  //   - `false` if the homing is not done.
  //   - `InternalError` if the status of the homing could not be determined.
  RealtimeStatusOr<bool> IsHomingDone(
      absl::string_view drive_name) const override {
    return homing_done_;
  }

  // Test helper function to set the homing state to false.
  void StopHoming() { homing_ = false; }

  // Test helper function to set the return value for all subsequent calls to
  // CommandHoming(). Call this with a non-OK status to induce errors.
  void SetReturnStatus(RealtimeStatus s) { next_status_ = s; }

  // Test helper function to set the homing done state.
  void SetHomingDone(bool done) {
    homing_done_ = done;
    if (homing_done_) {
      homing_ = false;
    }
  }

  // Test helper function to get the number of times CommandHoming was called.
  // Returns the number of times CommandHoming was called.
  int GetCommandHomingCallCount() const { return command_homing_call_count_; }

 private:
  bool homing_ = false;
  bool homing_done_ = false;
  RealtimeStatus next_status_ = RealtimeStatus();
  int command_homing_call_count_ = 0;
};

class FakeControlModeExporter final : public ControlModeExporter {
 public:
  ControlMode GetCurrentControlMode() const override {
    return current_control_mode_;
  }
  // Sets the return value for all subsequent calls to GetCurrentControlMode().
  void SetCurrentControlMode(ControlMode mode) { current_control_mode_ = mode; }

 private:
  ControlMode current_control_mode_ = ControlMode::kUnknown;
};

class FakeMoveOk final : public MoveOk {
 public:
  explicit FakeMoveOk();
  bool IsMoveOkWithPartLimits(const JointPositionCommand& setpoints) override;

  bool IsMoveOkWithUserLimits(const JointPositionCommand& setpoints,
                              const JointLimits& joint_limits) override;

  // Sets the return value for all subsequent calls to IsMoveOk() and
  // IsMoveOkWithLimits().
  void SetIsMoveOk(bool is_ok);

 private:
  bool current_is_move_ok_;
};

class FakePayloadState final : public PayloadState {
 public:
  std::optional<RealtimeRobotPayload> GetActivePayload() const override;
  void SetActivePayload(const RealtimeRobotPayload& payload);

 private:
  std::optional<RealtimeRobotPayload> active_payload_;
};

class FakeCartesianPositionState final : public CartesianPositionState {
 public:
  Pose3d GetSensedPose() const override { return sensed_pose_; }
  void SetSensedPose(const Pose3d& pose) { sensed_pose_ = pose; }

 private:
  Pose3d sensed_pose_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_PARTS_FAKE_FEATURE_INTERFACES_H_
