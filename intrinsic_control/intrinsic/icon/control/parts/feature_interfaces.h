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

#ifndef INTRINSIC_ICON_CONTROL_PARTS_FEATURE_INTERFACES_H_
#define INTRINSIC_ICON_CONTROL_PARTS_FEATURE_INTERFACES_H_

#include <stddef.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/joint_acceleration_command.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/control/parts/io_block.h"  
#include "intrinsic/icon/control/parts/realtime_robot_payload.h"  
#include "intrinsic/icon/dynamics/rigid_body_interface.h"  
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/chain.h"     
#include "intrinsic/kinematics/elements.h"  
#include "intrinsic/kinematics/ik/inverse_kinematics_interface.h"  
#include "intrinsic/kinematics/skeleton.h"  
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"
#include "intrinsic/util/fixed_vector.h"

namespace intrinsic::icon {

// These are the FeatureInterfaces that the RTCL supports.
// They are made available to Actions via a Part's FeatureInterfaceRegistry.

class JointPosition {
 public:
  virtual ~JointPosition() = default;
  // Sends the given position setpoints with velocity and torque feedforward
  // values.
  //
  // Returns an error if the setpoints are invalid, i.e. they contain the wrong
  // number of values or violate any limits. Returns an error if the part is
  // currently not in position mode.
  virtual RealtimeStatus SetPositionSetpoints(
      const JointPositionCommand& setpoints) = 0;

  // Returns the setpoints from the previous tick.
  virtual JointPositionCommand PreviousPositionSetpoints() const = 0;
};

class JointVelocity {
 public:
  virtual ~JointVelocity() = default;
  // Sends the given velocity setpoints.
  // Returns an error if the setpoints are invalid, i.e. they contain the wrong
  // number of values or violate any limits.
  // Returns an error if the part is currently not in velocity mode.
  virtual RealtimeStatus SetVelocitySetpoints(
      const eigenmath::VectorNd& setpoints) = 0;
};

class JointAcceleration {
 public:
  virtual ~JointAcceleration() = default;
  // Sends the given acceleration command.
  // Returns an error if the setpoints are invalid, i.e. they contain the wrong
  // number of values.
  // Returns an error if the part is currently not in acceleration mode.
  virtual RealtimeStatus SetAccelerationSetpoints(
      const JointAccelerationCommand& setpoints) = 0;

  // Returns the setpoints from the previous tick.
  virtual JointAccelerationCommand PreviousAccelerationSetpoints() const = 0;
};

class JointPositionSensor {
 public:
  virtual ~JointPositionSensor() = default;
  // Returns sensed position of all joints for this part.
  virtual JointStateP GetSensedPosition() const = 0;
};

class JointVelocityEstimator {
 public:
  virtual ~JointVelocityEstimator() = default;
  // Returns a velocity estimate of all joints for this part.
  virtual JointStateV GetVelocityEstimate() const = 0;
};

class JointAccelerationEstimator {
 public:
  virtual ~JointAccelerationEstimator() = default;
  // Returns a acceleration estimate of all joints for this part.
  virtual JointStateA GetAccelerationEstimate() const = 0;
};

class JointLimitsInterface {
 public:
  virtual ~JointLimitsInterface() = default;
  // Returns the application limits for the joints of this part for the
  // currently active ModeOfSafeOperation. These are configured, for instance
  // for a workcell. Actions should use these joint limits by default and reject
  // any user commands that exceed them.
  //
  // That said, an Action may exploit the margin between application limits and
  // system limits (see below) to better realize a user command. For example, a
  // small overshoot in jerk is acceptable, if it allows achieving velocity or
  // acceleration closer to what the user requested.
  //
  // Values of +/- std::numeric_limits<double>::infinity() designate
  // "unlimited".
  virtual JointLimits GetApplicationLimits() const = 0;

  // Returns the system limits for the joints of this part for the currently
  // active ModeOfSafeOperation.
  // These are hard limits as reported by the hardware itself, and ICON *must
  // not* exceed them. If any Action commands motion outside of the system
  // limits, ICON will terminate that Action immediately, and fault the robot.
  // Values of +/- std::numeric_limits<double>::infinity() designate
  // "unlimited".
  virtual JointLimits GetSystemLimits() const = 0;

  // Returns dynamics-based acceleration limits if joint position/velocity and
  // rigid body dynamics data are available, nullopt otherwise. These limits are
  // centrally computed to avoid its repeated computation in multiple places, as
  // it is computationally expensive to obtain them. It takes around 10 usec.
  struct JointAccelerationLimitsFromDynamics {
    eigenmath::VectorNd joint_acceleration_limits_at_min_torque;
    eigenmath::VectorNd joint_acceleration_limits_at_max_torque;
  };
  virtual icon::RealtimeStatusOr<
      std::optional<JointLimitsInterface::JointAccelerationLimitsFromDynamics>>
  GetJointAccelerationLimitsFromDynamics() const = 0;
};

class CartesianLimitsInterface {
 public:
  virtual ~CartesianLimitsInterface() = default;
  // Returns the default limits for the cartesian degrees of freedom for this
  // part for the currently active ModeOfSafeOperation. These are configured,
  // for instance for a workcell. Actions should use these cartesian limits by
  // default.
  virtual CartesianLimits GetDefaultCartesianLimits() const = 0;
};


// Provides access to analog and digital inputs and outputs.
class ADIO {
 public:
  // The maximum number of blocks that can be configured.
  static constexpr size_t kMaxBlocks = 64;
  struct ADIOState {
    // Realtime safe state of the io-blocks in the same order of the respective
    // block_names.
    FixedVector<AnalogBlock, kMaxBlocks> analog_inputs;
    FixedVector<AnalogBlock, kMaxBlocks> analog_outputs;
    FixedVector<DioBlock, kMaxBlocks> digital_inputs;
    FixedVector<DioBlock, kMaxBlocks> digital_outputs;
    // Using absl::Span works because the part's block names are runtime static.
    absl::Span<const std::string> analog_input_block_names;
    absl::Span<const std::string> analog_output_block_names;
    absl::Span<const std::string> digital_input_block_names;
    absl::Span<const std::string> digital_output_block_names;
  };
  // Lists the available names of the respective IO Blocks.
  virtual absl::Span<const std::string> DigitalInputBlockNames() const = 0;
  // Lists the names of signals in the block named `block_name`. Returns an
  // empty span if no block exists with the given name.
  virtual absl::Span<const std::string> DigitalInputSignalNames(
      absl::string_view block_name) const = 0;
  virtual absl::Span<const std::string> DigitalOutputBlockNames() const = 0;
  // Lists the names of signals in the block named `block_name`. Returns an
  // empty span if no block exists with the given name.
  virtual absl::Span<const std::string> DigitalOutputSignalNames(
      absl::string_view block_name) const = 0;
  virtual absl::Span<const std::string> AnalogInputBlockNames() const = 0;
  // Lists the names of signals in the block named `block_name`. Returns an
  // empty span if no block exists with the given name.
  virtual absl::Span<const std::string> AnalogInputSignalNames(
      absl::string_view block_name) const = 0;
  virtual absl::Span<const std::string> AnalogOutputBlockNames() const = 0;
  // Lists the names of signals in the block named `block_name`. Returns an
  // empty span if no block exists with the given name.
  virtual absl::Span<const std::string> AnalogOutputSignalNames(
      absl::string_view block_name) const = 0;

  // Provides a pointer to the respective IO Block.
  // Returns a nullptr if no block with that name exist.
  virtual const DioBlock* DigitalInputBlock(absl::string_view name) const = 0;
  virtual DioBlock* MutableDigitalOutputBlock(absl::string_view name) = 0;
  virtual const AnalogBlock* AnalogInputBlock(absl::string_view name) const = 0;
  virtual AnalogBlock* MutableAnalogOutputBlock(absl::string_view name) = 0;

  virtual ADIOState GetADIOState() const = 0;

  virtual ~ADIO() = default;
};

// A SimpleGripper can only grasp and release.
class SimpleGripper {
 public:
  enum class GripperCommand { kUnknown, kGrasp, kRelease };
  enum class GripperState { kUnknown, kGrasped, kReleased };
  virtual ~SimpleGripper() = default;

  // Returns an error if the command is not valid.
  virtual RealtimeStatus SetGripperCommand(const GripperCommand& command) = 0;
  virtual GripperState GetGripperState() const = 0;
};

// A LinearGripper can grasp to a specified width.
class LinearGripper {
 public:
  virtual ~LinearGripper() = default;

  // Sets the gripper command to the given width (in m), using the given force
  // (N) and speed (m/s).
  // Force and speed are optional; if not given, the default values from the
  // configuration will be used.
  // Some gripper devices may choose to ignore the force and/or speed parameters
  // if the underlying driver does not support it.
  // Returns an error if the command is not valid.
  virtual RealtimeStatus SetGripperCommand(double width,
                                           std::optional<double> force,
                                           std::optional<double> speed) = 0;
  virtual double GetGripperWidth() const = 0;
};

// A one-dimension range finder (laser distance sensor).
class RangeFinder {
 public:
  virtual ~RangeFinder() = default;
  virtual double GetSensedDistance() const = 0;
  virtual Pose3d GetPoseInTCPFrame() const = 0;
  virtual bool IsMeasurementValid() const = 0;
};


// An Inertial Measurement Unit (acceleration sensor).
class InertialMeasurementUnit {
 public:
  virtual ~InertialMeasurementUnit() = default;

  // The sensed linear acceleration.
  virtual eigenmath::Vector3d GetSensedLinearAcceleration() const = 0;

  // The sensed angular velocity.
  virtual eigenmath::Vector3d GetSensedAngularVelocity() const = 0;

  // The sensed orientation as quaternion.
  virtual eigenmath::Quaterniond GetSensedOrientation() const = 0;

  // The transform between robot flange and sensor.
  virtual Pose3d GetPoseInFlangeFrame() const = 0;
};

// Interface for rigid-body manipulator kinematics. The intended implementation
// of this interface is to also keep track of the kinematic chains inside this
// class to access them in real-time code. Assumed there is one ModelInterface
// with multiple tips, chain extraction is not real-time safe, thus all possible
// chains from the single base are built here and stored for later usage.
class ManipulatorKinematics {
 public:
  virtual ~ManipulatorKinematics() = default;

  virtual const kinematics::InverseKinematicsInterface&
  GetInverseKinematicsSolver() const = 0;
  // Returns the solver name / solver key of the inverse kinematics solver. The
  // return values' lifetime is bound to the liftime of the underlying kinematis
  // solver.
  virtual std::string_view GetInverseKinematicsSolverName() const = 0;
  virtual const kinematics::Skeleton& GetKinematicsModel() const = 0;
  virtual intrinsic::icon::RealtimeStatusOr<const kinematics::Chain*>
  GetKinematicsChain(kinematics::ElementId tip_id) const = 0;

  // Returns the base to tip transform for the given `dof_positions`. Assumes
  // the kinematic model is a chain and returns an error otherwise.
  virtual intrinsic::icon::RealtimeStatusOr<Pose3d> ComputeChainFK(
      const JointStateP& dof_positions) const = 0;
  // Returns the base to tip jacobian for the given `dof_positions`. Assumes
  // the kinematic model is a chain and returns an error otherwise.
  virtual intrinsic::icon::RealtimeStatusOr<eigenmath::Matrix6Nd>
  ComputeChainJacobian(const JointStateP& dof_positions) const = 0;
};

class JointTorque {
 public:
  virtual ~JointTorque() = default;
  // Sends the given torque setpoints.
  // Returns an error if the setpoints are invalid, i.e. they contain the wrong
  // number of values or violate any limits. Note that the Part may not be able
  // to predict position/velocity/acceleration limit violations that the new
  // torque setpoints might cause.
  // Returns an error if the part is currently not in torque mode.
  virtual RealtimeStatus SetTorqueSetpoints(
      const eigenmath::VectorNd& setpoints) = 0;

  // Returns the torque setpoints that were last commanded regardless of what
  // control mode the part is in. If no torque setpoints have been commanded
  // yet, it returns the initial values on the torque command hardware
  // interface.
  virtual eigenmath::VectorNd PreviousTorqueSetpoints() const = 0;
};

class JointTorqueSensor {
 public:
  virtual ~JointTorqueSensor() = default;
  // Returns sensed torque of all joints for this part.
  virtual JointStateT GetSensedTorque() const = 0;
};


// Rigid Body algorithms, including forward kinematics, forward and inverse
// dynamics calculations.
class Dynamics {
 public:
  virtual ~Dynamics() = default;
  // Provides access to a RigidBodyDynamicsInterface instance. Note that this
  // instance is shared between any Actions that use the Part, although only
  // one Action can use a Part at a time.
  virtual RigidBodyInterface& GetRigidBodyInterface() = 0;
};

// Interface to Force-Torque Sensor Readings.
// WARNING: This interface will change after the frame manager has been
// implemented in RTCL.
class ForceTorqueSensor {
 public:
  virtual ~ForceTorqueSensor() = default;
  // The current force-torque reading, as measured in the sensor's reference
  // frame.
  // TODO(b/174645393) add utils to post-process this and compensate payload
  // mass.
  // TODO(b/160309005): When the Frame Manager is done, change this to a stamped
  // type so we can transform the wrench to different frames.
  // Uncompensated force torque values from the sensor. The returned wrench is
  // the raw value from the hardware module, not compensating for quasi-static
  // gravitational loads, but matching sign convention.
  virtual Wrench WrenchAtSensorUncompensated() const = 0;
  // Filtered wrench at the sensor compensated for support mass and bias.
  virtual Wrench WrenchAtSensor() const = 0;
  // Filtered wrench at the tip compensated for support_mass and bias.
  virtual Wrench WrenchAtTip() const = 0;
  // Stability Index in [0, 1], indicating the presence of high-frequency
  // components in the sensed wrench. `0` indicates no frequency components,
  // whereas `1` indicates strong high-frequency activity.
  virtual double WrenchStabilityIndex() const = 0;
  // Estimated post-sensor dynamic load due to support_mass inertia, expressed
  // at the target or sensor frames.
  virtual Wrench PostSensorDynamicLoadAtTip() const = 0;
  virtual Wrench PostSensorDynamicLoadAtSensor() const = 0;
  // LINT.IfChange(ForceTorqueSensor)
  // Tare() and TareIsDone() override virtual methods in both ForceTorqueSensor
  // and StandaloneForceTorqueSensor
  //
  // Request a taring of the sensor with a desired number of averaging cycles,
  // until then TareIsDone() will report false.
  virtual RealtimeStatus Tare(int num_taring_cycles) = 0;
  // Returns true if the tare has completed.
  virtual RealtimeStatusOr<bool> TareIsDone() const = 0;
  // LINT.ThenChange(:StandaloneForceTorqueSensor)
};

class StandaloneForceTorqueSensor {
 public:
  virtual ~StandaloneForceTorqueSensor() = default;
  // Uncompensated force torque values from the sensor. The returned wrench is
  // the raw value from the hardware module, not compensating for quasi-static
  // gravitational loads, but matching sign convention.
  virtual Wrench WrenchAtSensorUncompensated() const = 0;
  // LINT.IfChange(StandaloneForceTorqueSensor)
  // Tare() and TareIsDone() override virtual methods in both ForceTorqueSensor
  // and StandaloneForceTorqueSensor
  //
  // Request a taring of the sensor with a desired number of averaging cycles,
  // until then TareIsDone() will report false.
  virtual RealtimeStatus Tare(int num_taring_cycles) = 0;
  // Returns true if the tare has completed.
  virtual RealtimeStatusOr<bool> TareIsDone() const = 0;
  // LINT.ThenChange(:ForceTorqueSensor)
};

// Interface to command the hand guiding control mode of a part or device.
// Hand guiding is a control mode where the robot renders low impedance
// and some damping but without a specific target.
class HandGuiding {
 public:
  virtual ~HandGuiding() = default;
  // Commands the part into hand guiding mode.
  //
  // Returns an error if hand guiding can't be enabled at the moment.
  virtual RealtimeStatus CommandHandGuiding() = 0;
};

// Interface to command the homing motion of a drive.
class Homing {
 public:
  Homing() = default;
  virtual ~Homing() = default;
  Homing(const Homing&) = default;
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
  virtual RealtimeStatus CommandHoming(absl::string_view drive_name,
                                       int8_t homing_method,
                                       double search_speed, double creep_speed,
                                       double acceleration, double offset) = 0;

  // Returns true if the drive with `drive_name` is currently homing.
  //
  // `drive_name`: The name of the drive to check.
  virtual bool IsHoming(absl::string_view drive_name) const = 0;

  // Returns true if the drive with `drive_name` is done homing.
  //
  // `drive_name`: The name of the drive to check.
  //
  // Returns:
  //   - `true` if the homing is done.
  //   - `false` if the homing is not done.
  //   - `InternalError` if the status of the homing could not be determined.
  virtual RealtimeStatusOr<bool> IsHomingDone(
      absl::string_view drive_name) const = 0;
};

// Interface to read the current control mode of the part or device.
class ControlModeExporter {
 public:
  enum class ControlMode {
    kUnknown = 0,
    kCyclicPosition,
    kCyclicVelocity,
    kCyclicTorque,
    kHandGuiding
  };
  virtual ~ControlModeExporter() = default;
  // Returns the currently active control mode.
  virtual ControlMode GetCurrentControlMode() const = 0;
};

// Interface to check if a JointPositionCommand should be commanded. Designed to
// check if command moves towards safety with respect to position limits. When
// using IsMoveOkWithPartLimits this checks whether the command would be
// permitted by the l1_controller. When using IsMoveOkWithUserLimits a more
// restrictive test (thus still allowed by the l1_controller) is evaluated.
class MoveOk {
 public:
  virtual ~MoveOk() = default;
  // Returns whether the part would be able to safely move using the
  // JointPositionCommand setpoint passed as an argument. A true value signifies
  // a move is permissible while false that either the move is not permissible
  // or an error prevented the determination. This compares to the default
  // limits set in the part.
  virtual bool IsMoveOkWithPartLimits(const JointPositionCommand& setpoint) = 0;

  // This method implements the same logic as IsMoveOkWithPartLimits but
  // checks against a user defined set of limits provided as the argument
  // `joint_limits`. `joint_limits` must be `WithinLimits()` of the current
  // default limits defined within the part (taking into account the safe mode
  // of operation).
  virtual bool IsMoveOkWithUserLimits(const JointPositionCommand& setpoint,
                                      const JointLimits& joint_limits) = 0;
};

// Interface to forward sensed process forces (and torques) at the end-effector
// to the lower-level robot controller for inverse dynamics. Informally
// speaking, this is the interaction force with the environment, compensated for
// quasi-static and dynamic payload effects - only interaction forces are
// supposed to be considered.
class ProcessWrenchAtEndeffector {
 public:
  virtual ~ProcessWrenchAtEndeffector() = default;
  // Sends the given process (interaction) wrench to robot controller for
  // inverse dynamics.
  virtual void SetProcessWrenchAtTip(const Wrench& wrench_at_tip) = 0;
  // Returns the currently stored setpoint. This will be overwritten during
  // "SetProcessWrenchAtTip".
  virtual Wrench GetProcessWrenchAtTip() const = 0;
};

// Interface to set the payload of the robot in the part and on the hardware
// module. The payload is read from the part properties and only set when
// enabling motion.
// This interface is only needed to register the feature interface and does not
// provide any functionality.
class Payload {
 public:
  virtual ~Payload() = default;
};

// Interface to read the active payload of the robot. The payload is set on the
// hardware module and can be read in the part.
class PayloadState {
 public:
  virtual ~PayloadState() = default;
  // Read the active payload of the robot. The active payload does only change
  // when the robot is transitioning from disabled to enabled. It contains the
  // full payload after the robot's flange.
  virtual std::optional<RealtimeRobotPayload> GetActivePayload() const = 0;
};

// Interface to read a sensed Cartesian position.
class CartesianPositionState {
 public:
  virtual ~CartesianPositionState() = default;
  // Returns the sensed Cartesian position.
  virtual Pose3d GetSensedPose() const = 0;
};


}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_PARTS_FEATURE_INTERFACES_H_
