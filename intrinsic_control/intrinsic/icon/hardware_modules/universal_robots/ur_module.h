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


#ifndef INTRINSIC_ICON_HARDWARE_MODULES_UNIVERSAL_ROBOTS_UR_MODULE_H_
#define INTRINSIC_ICON_HARDWARE_MODULES_UNIVERSAL_ROBOTS_UR_MODULE_H_
#include <array>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "include/ur_client_library/rtde/data_package.h"
#include "include/ur_client_library/types.h"
#include "include/ur_client_library/ur/datatypes.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/realtime_clock_interface.h"
#include "intrinsic/icon/control/safety/safety_messages.fbs.h"
#include "intrinsic/icon/hal/command_validator.h"
#include "intrinsic/icon/hal/hardware_interface_handle.h"
#include "intrinsic/icon/hal/hardware_module_init_context.h"
#include "intrinsic/icon/hal/hardware_module_interface.h"
#include "intrinsic/icon/hal/interfaces/force_torque.fbs.h"
#include "intrinsic/icon/hal/interfaces/io_controller.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_command.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_state.fbs.h"
#include "intrinsic/icon/hal/interfaces/payload_command.fbs.h"
#include "intrinsic/icon/hal/interfaces/payload_state.fbs.h"
#include "intrinsic/icon/hal/interfaces/robot_controller.fbs.h"
#include "intrinsic/icon/hardware_modules/universal_robots/config.pb.h"
#include "intrinsic/icon/hardware_modules/universal_robots/prefilter_interface.h"
#include "intrinsic/icon/hardware_modules/universal_robots/ur_driver_interface.h"
#include "intrinsic/icon/hardware_modules/universal_robots/ur_kinematics_calibration_service.h"
#include "intrinsic/icon/proto/safety_status.pb.h"
#include "intrinsic/icon/utils/async_buffer.h"
#include "intrinsic/icon/utils/clock.h"
#include "intrinsic/icon/utils/constants.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/math/twist.h"
#include "intrinsic/util/invalid_until_set.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/util/thread/thread_options.h"

namespace intrinsic::icon {

// A HAL module to control UniversalRobots E-Series robots.
// The implementation is an alternative to
// https://github.com/UniversalRobots/Universal_Robots_ROS2_Driver and uses
// https://github.com/UniversalRobots/Universal_Robots_Client_Library.
//
// The connection to the robot is established during Prepare and ClearFault.
// `RealtimeLoop` is responsible for the realtime communication with the robot
// (reading status and ticking the clock). Commands to the robot are added to a
// queue in ur_driver in `ApplyCommands`. Taring reports done after one cycle
// and feedforwards are not supported. The custom payload is set and activated
// in the `EnableMotion` state. The default payload on the teach pendant does
// not change, only the custom payload is changed. Do not change the payload on
// the teach pendant if it is set by the hardware module, use the set_payload
// skill instead.
class UniversalRobotsModule final : public HardwareModuleInterface {
 public:
  struct Config {
    // LINT.IfChange
    static constexpr int kDefaultServojGain = 2000;
    static constexpr double kDefaultServojLookaheadTime = 0.1;
    // LINT.ThenChange(//intrinsic_control/intrinsic/icon/hardware_modules/universal_robots/config.proto)

    struct FrictionCompensationScaleFactors {
      urcl::vector6d_t viscous_scale = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
      urcl::vector6d_t coulomb_scale = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    };

    template <typename Sink>
    friend void AbslStringify(Sink& sink, const Config& c) {
      absl::Format(
          &sink,
          "robot_ip[%s], script_file_path[%s], "
          "output_recipe_path[%s], input_recipe_path[%s], "
          "headless_mode[%v], calibration_checksum[%s], "
          "servoj_gain[%d], servoj_lookahead_seconds[%f], "
          "reverse_ip[%s], reverse_port[%d], script_sender_port[%d], "
          "trajectory_port[%d], script_command_port[%d],"
          "non_blocking_read[%v], "
          "module_thread_options[priority[%d], policy[%d], "
          "affinity[%s]], "
          "friction_compensation_scale_factors\n%s ",
          c.robot_ip, c.script_file_path, c.output_recipe_path,
          c.input_recipe_path, c.headless_mode, c.calibration_checksum,
          c.servoj_gain, c.servoj_lookahead_seconds, c.reverse_ip,
          c.reverse_port, c.script_sender_port, c.trajectory_port,
          c.script_command_port, c.non_blocking_read,
          c.module_thread_options.GetPriority().value_or(-1),
          c.module_thread_options.GetSchedulePolicy().value_or(-1),
          absl::StrJoin(c.module_thread_options.GetCpuSet(), ", "),
          c.joint_friction_compensation_scale_factors.has_value()
              ? absl::StrCat(
                    "Viscous scale factors: ",
                    absl::StrJoin(c.joint_friction_compensation_scale_factors
                                      ->viscous_scale,
                                  ", "),
                    "Coulomb scale factors: ",
                    absl::StrJoin(c.joint_friction_compensation_scale_factors
                                      ->coulomb_scale,
                                  ", "))
              : "[]");
    }

    // Creates a Config by parsing the proto. Populates defaults with the static
    // values above.
    static absl::StatusOr<Config> FromProto(
        const ::intrinsic_proto::icon::UniversalRobotsModuleConfig&
            proto_config);

    std::string robot_ip;
    // Path to the control script that is send to the robot. Not required when
    // using URCaps.
    std::string script_file_path = "/data/resources/external_control.urscript";
    // Defines the values exported by the robot.
    // Documentation:
    // https://www.universal-robots.com/articles/ur/interface-communication/real-time-data-exchange-rtde-guide/
    std::string output_recipe_path =
        "intrinsic_control/intrinsic/icon/hardware_modules/"
        "universal_robots/rtde_files/rtde_output_recipe.txt";
    // Defines the input values to the robot.
    std::string input_recipe_path =
        "intrinsic_control/intrinsic/icon/hardware_modules/"
        "universal_robots/rtde_files/rtde_input_recipe.txt";

    // Start robot in headless mode. This does not require the 'External
    // Control' URCap to be running on the robot, but this will send the
    // URScript to the robot directly. On e-Series robots this requires the
    // robot to run in 'remote-control' mode.
    // TODO(b/290575595): Implement and test non headless mode.
    bool headless_mode = true;
    // Checksum of the kinematics calibration as read from the robot.
    // Empty string skips the check.
    std::string calibration_checksum;
    // Proportional gain for arm joints following target position, range
    // [100,2000].
    int servoj_gain = kDefaultServojGain;
    // Range [0.03,0.2] smoothens the trajectory with this lookahead time.
    double servoj_lookahead_seconds = kDefaultServojLookaheadTime;
    // IP of the realtime NIC to connect to the robot. Used for reverse
    // connections (robot to RTPC).
    // Empty string uses the same IP as the outgoing connection to the robot.
    std::string reverse_ip = "";
    // Port that will be opened by the driver to allow direct communication
    // between the driver and the robot controller.
    uint32_t reverse_port = 7141;
    // The driver will offer an interface to receive the program's URScript on
    // this port. If the robot cannot connect to this port, `External Control`
    // will stop immediately
    uint32_t script_sender_port = 7142;
    // Reverse port used to send trajectory points to the robot.
    // Currently not used by the module. Port is still allocated by the UR
    // driver.  Required for using `writeTrajectoryPoint`.
    uint32_t trajectory_port = 7143;
    // Reverse port used to send script commands to the robot.
    // Currently not used by the module. Port is still allocated by the UR
    // driver. Required for using `sendScript`.
    uint32_t script_command_port = 7144;
    // Should only ever be enabled when the robot is not the clock source.
    // Untested. Can hide realtime issues.
    // TODO(b/290575856): Implement and test "non-blocking read".
    bool non_blocking_read = false;
    // If true the module reads and logs the kinematic calibration data on
    // startup. The kinematic calibration data is also served over gRPC as long
    // as a port is available.
    bool log_and_serve_calibration_data = true;
    // Options the realtime threads of this module should use. Contains the CPU
    // Affinity.
    ThreadOptions module_thread_options;

    std::optional<FrictionCompensationScaleFactors>
        joint_friction_compensation_scale_factors = std::nullopt;

    // This is necessary for correctly compensating for the payload force when
    // using the process wrench.
    //
    eigenmath::Vector3d gravity = {0.0, 0.0, kDefaultGravity};
  };

  // Copy of data read from the UR robot.
  struct CyclicReadData {
    urcl::vector6d_t joint_positions = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    urcl::vector6d_t joint_velocities = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    urcl::vector6d_t joint_accelerations = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    urcl::vector6d_t current = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    urcl::vector6d_t actual_tcp_force = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    urcl::vector6d_t actual_tcp_pose = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    urcl::vector3d_t payload_cog = {0.0, 0.0, 0.0};
    double payload_kg = 0.0;

    // Speed scaling of the trajectory limiter
    // https://www.universal-robots.com/articles/ur/interface-communication/real-time-data-exchange-rtde-guide/
    double speed_scaling = 1.0;
    bool taring_done = false;
    bool taring_in_progress = false;
    int32_t remaining_taring_cycles = 0;
    // https://www.universal-robots.com/articles/ur/interface-communication/connecting-internal-inputs-and-outputs-io-on-the-robots-controller/
    // Current state of the digital outputs.
    // 0-7: Standard,
    // 8-15: Configurable,
    // 16-17: Tool
    std::bitset<18> digital_output_bits = {0};
    // Current state of the digital inputs.
    // 0-7: Standard,
    // 8-15: Configurable,
    // 16-17: Tool
    std::bitset<18> digital_input_bits = {0};
    std::array<double, 2> standard_analog_input = {0.0, 0.0};
    urcl::rtde_interface::RUNTIME_STATE runtime_state =
        urcl::rtde_interface::RUNTIME_STATE::STOPPED;
    urcl::RobotMode robot_mode = urcl::RobotMode::UNKNOWN;
    urcl::SafetyMode safety_mode = urcl::SafetyMode::UNDEFINED_SAFETY_MODE;
    // Bits 0-10: Is normal mode | Is reduced mode | Is protective stopped | Is
    // recovery mode | Is safeguard stopped | Is system emergency stopped | Is
    // robot emergency stopped | Is emergency stopped | Is violation | Is fault
    // | Is stopped due to safety
    std::bitset<32> safety_status_bits = {0};
    // Bits 0-3:  Is power on | Is program running | Is teach button pressed |
    // Is power button pressed
    std::bitset<32> robot_status_bits = {0};
  };
  static constexpr char kHwModuleTypeName[] = "UniversalRobotsModule";

  UniversalRobotsModule();
  explicit UniversalRobotsModule(std::unique_ptr<UrDriverInterface> backend);
  ~UniversalRobotsModule() override;

  absl::Status Init(HardwareModuleInitContext& init_context) override;

  // Disconnects from the robot, stops the realtime loop, then
  // connects to robot and starts the realtime loop.
  absl::Status Prepare() override;

  RealtimeStatus Activate() override;

  RealtimeStatus Deactivate() override;

  absl::Status EnableMotion() override;

  // Signals that the hardware module will now receive commands from ICON via
  // `ApplyCommand()`.
  // Disables automatically feeding the watchdog.
  RealtimeStatus Enabled() override;

  // Signals that the hardware module should take over control over the
  // hardware.
  // Enables automatically feeding the watchdog.
  RealtimeStatus Disabled() override;

  absl::Status DisableMotion() override;

  absl::Status ClearFaults() override;

  absl::Status Shutdown() override;

  // Closes the connection to the robot while leaving the realtime loop running
  // so that the clock keeps getting ticked.
  //
  // Returns kAborted error if the robot connection could not be closed.
  absl::Status CloseRobotConnection(absl::string_view message);

  // Writes the commands into a queue to be send to the robot.
  // Checks that the value was updated in the same cycle as reported by the
  // IconState interface.
  // Returns FailedPreconditionError if not.
  // Silently ignores if a DIO command was not updated, because it may not be
  // configured in ICON.
  // Silently ignores if the FT command was not updated,  because it may not be
  // configured in ICON.
  RealtimeStatus ApplyCommand() override;

  // Copies data from internal buffer read from the robot into the shared memory
  // segment.
  // The data from the robot is read in CyclicRead.
  RealtimeStatus ReadStatus() override;

  absl::Status ProvideInspectionData(
      intrinsic_proto::icon::v1::HardwareModuleInspectionData& data) override;

  // Represents external instructions sent to RealtimeLoop() from other
  // threads to demand a transition to a halted or faulted state.
  enum class CyclicLoopRequest : int32_t {
    // No action requested; permit normal cyclic loop operation.
    kNone = 0,
    // Request to immediately halt robot communication and stop sending commands
    // before resetting underlying connection transport or driver state.
    kStopSendingCommands,
    // Request to transition to an error status when motion is attempted
    // without an active control program running on the robot.
    kProgramNotActive,
  };

  // Represents the operating state reported exclusively by RealtimeLoop() (and
  // initialized during startup).
  enum class CyclicLoopState : int32_t {
    // Normal operating status (no errors observed in loop, no stop requests
    // active).
    kOk = 0,
    // Status during initialization before RTDE packets begin arriving.
    kUninitialized,
    // Status when RTDE socket read fails (e.g. disconnection or driver reset).
    kRobotConnectionClosed,
    // Status when RTDE GetDataPackage() returns null within the timeout
    // interval.
    kNoDataReceived,
    // Status when acknowledging a kProgramNotActive request.
    kProgramNotActive,
    // Status when acknowledging an external kStopSendingCommands request.
    kStoppedSendingCommands,
    // Status when reading required variables from the RTDE package fails.
    kReadError,
  };

 private:
  enum class ResendProgramState : int32_t {
    kIdle,
    kCloseConnectionRequested,
    // Required to resume program execution after a SS2 stop.
    kSendProgramRequested,
    kSendProgramInProgress,
    kFailed,
  };
  static absl::string_view ToString(ResendProgramState state);

  // Gets notified once the RTDE script is running on the robot.
  // Updates the atomic `program_running_`.
  void HandleRobotProgramState(bool program_running);
  // (Re-)initializes the robot connection.
  absl::Status InitializeRobotConnection();
  // Connects to the robot and performs some initial sanity checks.
  absl::Status ConnectToRobot(const Config& config);
  // Starts the realtime loop thread. Forwards the errors of starting the
  // thread.
  absl::Status StartRealtimeLoop();

  // Stops the realtime loop and waits for the realtime_thread to finish.
  absl::Status StopRealtimeLoop();
  // The loop of the realtime thread that reads from the robot.
  // It cycles through the following steps:
  // 1. Waits for and parses cyclic data from the robot.
  // 2. Tells ICON to step its control cycle and waits for it to finish.
  // 3. Sends the new command to the robot, or pets the watchdog.
  // If 2. fails, the module will `Fault`.
  void RealtimeLoop();
  // The loop of the background thread that handles program resend and
  // connection closure requests.
  void ResendProgramLoop(intrinsic::StopToken stop_token);
  // Waits for the next packet from the robot and parses the joint data into
  // local storage.
  // Returns the time_point of the cycle start (when the read from the
  // robot returns).
  RealtimeStatusOr<intrinsic::Clock::time_point> CyclicRead();

  // Logs a message to the Tech Pendants log.
  absl::Status LogToRobot(absl::string_view message);

  RealtimeStatus SendProcessWrench(const Wrench& process_wrench);

  // Thread-safe, lock-free accessors for cyclic loop status.
  RealtimeStatus GetCyclicLoopStatus() const;
  // Thread safe interface to command the RealtimeLoop to e.g. stop sending
  // robot commands.
  void SetCyclicLoopRequest(CyclicLoopRequest request);

  // The RealtimeLoop resets the clock when true.
  // This ensures that there is no race on resetting the clock.
  std::atomic<bool> reset_clock_ = false;
  static_assert(decltype(reset_clock_)::is_always_lock_free);

  Config config_;
  intrinsic_proto::icon::UniversalRobotsModuleConfig advanced_control_options_;
  std::string robot_model_name_;
  std::unique_ptr<UrDriverInterface> backend_;

  RealtimeClockInterface* realtime_clock_ = nullptr;

  std::unique_ptr<intrinsic::Thread> runtime_loop_thread_;
  std::atomic<bool> cancel_runtime_loop_thread_ = false;
  static_assert(decltype(cancel_runtime_loop_thread_)::is_always_lock_free);
  // More or less matches the data from the rtde_output_recipe/the data exposed
  // by the UR Cap.
  // Doesn't need to be atomic, as it is written and read from the main
  // realtime thread.
  CyclicReadData ur_status_;
  // The state of external program that's running on the robot as set by
  // `handleRobotProgramState`. True if the custom program is running on the
  // robot.
  // If no program is running, we need to resend/restart it and stop sending
  // commands in the meantime so that the send buffer doesn't overflow.
  std::atomic<bool> program_running_ = false;
  static_assert(decltype(program_running_)::is_always_lock_free);

  std::atomic<urcl::rtde_interface::RUNTIME_STATE> runtime_state_{
      urcl::rtde_interface::RUNTIME_STATE::STOPPED};
  static_assert(decltype(runtime_state_)::is_always_lock_free);

  // When true, ICON is connected and expects to be ticked.
  std::atomic<bool> tick_clock_ = false;
  static_assert(decltype(tick_clock_)::is_always_lock_free);

  // Indicates if the realtime loop needs to pet the Watchdog. This is the case
  // if RTDE is active, but no command was send to the robot e.g. if ICON is not
  // connected.
  std::atomic<bool> pet_watchdog_ = false;
  static_assert(decltype(pet_watchdog_)::is_always_lock_free);

  // Indicates if motion is currently enabled in ICON (between Enabled() and
  // Disabled()). Program resend during ReadStatus is only allowed while
  // motion is enabled; in kActivated, ICON only monitors robot state.
  std::atomic<bool> motion_enabled_ = false;
  static_assert(decltype(motion_enabled_)::is_always_lock_free);
  // Stores errors of the realtime loop to export to ICON on
  // ReadStatus/ApplyCommand.
  std::atomic<CyclicLoopState> cyclic_loop_status_{
      CyclicLoopState::kUninitialized};
  static_assert(decltype(cyclic_loop_status_)::is_always_lock_free);
  std::atomic<CyclicLoopRequest> cyclic_loop_request_{CyclicLoopRequest::kNone};
  static_assert(decltype(cyclic_loop_request_)::is_always_lock_free);

  size_t current_cycle_ = 0;

  // Variables used to track the state of the process wrench streaming.
  bool sending_external_ft_ = false;
  bool external_ft_enabled_ = false;
  // The HardwareInterfaceHandles are declared last so they are destructed and
  // cleaned up first.
  //
  // When motion is enabled a new command must be set to either the
  // JointPositionCommand or the JointTorqueCommand on every cycle. This is
  // checked in ApplyCommand.
  HardwareInterfaceHandle<::intrinsic_fbs::JointPositionCommand>
      joint_position_command_;
  HardwareInterfaceHandle<::intrinsic_fbs::JointTorqueCommand>
      joint_torque_command_;
  MutableHardwareInterfaceHandle<::intrinsic_fbs::JointPositionState>
      joint_position_state_;
  MutableHardwareInterfaceHandle<::intrinsic_fbs::JointVelocityState>
      joint_velocity_state_;

  // 8bit
  StrictHardwareInterfaceHandle<intrinsic_fbs::DIOCommand>
      standard_digital_output_command_;
  // The status of the output as reported by the robot.
  MutableHardwareInterfaceHandle<intrinsic_fbs::DIOStatus>
      standard_digital_output_status_;
  // 8bit
  MutableHardwareInterfaceHandle<intrinsic_fbs::DIOStatus>
      standard_digital_input_;

  // 8bit
  StrictHardwareInterfaceHandle<intrinsic_fbs::DIOCommand>
      configurable_digital_output_command_;
  // The status of the output as reported by the robot.
  MutableHardwareInterfaceHandle<intrinsic_fbs::DIOStatus>
      configurable_digital_output_status_;
  // 8bit
  MutableHardwareInterfaceHandle<intrinsic_fbs::DIOStatus>
      configurable_digital_input_;

  // 2bit
  // TI0, TI1 - sendToolDigitalOutput
  StrictHardwareInterfaceHandle<intrinsic_fbs::DIOCommand>
      tool_digital_output_command_;
  // The status of the output as reported by the robot.
  MutableHardwareInterfaceHandle<intrinsic_fbs::DIOStatus>
      tool_digital_output_status_;
  // 2bit
  MutableHardwareInterfaceHandle<intrinsic_fbs::DIOStatus> tool_digital_input_;

  // Analog inputs (AI0 to AI1)
  // Analog outputs are not supported by ICON.
  MutableHardwareInterfaceHandle<intrinsic_fbs::AIOStatus> analog_input_status_;

  HardwareInterfaceHandle<intrinsic_fbs::ForceTorqueCommand> ft_command_;
  MutableHardwareInterfaceHandle<intrinsic_fbs::ForceTorqueStatus> ft_status_;

  MutableHardwareInterfaceHandle<intrinsic_fbs::SafetyStatusMessage>
      safety_status_;

  MutableHardwareInterfaceHandle<intrinsic_fbs::RobotControllerStatus>
      robot_controller_status_;

  HardwareInterfaceHandle<intrinsic_fbs::PayloadCommand> payload_command_;

  MutableHardwareInterfaceHandle<intrinsic_fbs::PayloadState> payload_state_;

  HardwareInterfaceHandle<intrinsic_fbs::Wrench> process_wrench_command_;

  Validator command_validator_;

  std::unique_ptr<PrefilterInterface> prefilter_;

  UrKinematicsCalibrationDataService calibration_service_;

  struct InspectionData {
    // Written and read from the inspection thread. Do not use from other
    // threads!
    std::deque<intrinsic_proto::icon::v1::Event> event_history;
    // Written from the cyclic read thread and read from the inspection thread.
    // Do not use from other threads!
    intrinsic::AsyncBuffer<intrinsic::InvalidUntilSet<CyclicReadData>>
        last_data_package;
  };
  InspectionData inspection_data_;

  std::atomic<ResendProgramState> resend_robot_program_state_{
      ResendProgramState::kIdle};
  static_assert(decltype(resend_robot_program_state_)::is_always_lock_free);
  std::unique_ptr<intrinsic::Thread> resend_robot_program_thread_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_HARDWARE_MODULES_UNIVERSAL_ROBOTS_UR_MODULE_H_
