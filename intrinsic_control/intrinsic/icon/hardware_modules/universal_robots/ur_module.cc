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

#include "intrinsic/icon/hardware_modules/universal_robots/ur_module.h"

#include <array>
#include <atomic>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "flatbuffers/vector.h"
#include "include/ur_client_library/comm/control_mode.h"
#include "include/ur_client_library/comm/pipeline.h"
#include "include/ur_client_library/comm/producer.h"
#include "include/ur_client_library/comm/stream.h"
#include "include/ur_client_library/log.h"
#include "include/ur_client_library/primary/package_header.h"
#include "include/ur_client_library/primary/primary_package.h"
#include "include/ur_client_library/primary/primary_parser.h"
#include "include/ur_client_library/primary/robot_message/error_code_message.h"
#include "include/ur_client_library/primary/robot_state/kinematics_info.h"
#include "include/ur_client_library/rtde/data_package.h"
#include "include/ur_client_library/types.h"
#include "include/ur_client_library/ur/dashboard_client.h"
#include "include/ur_client_library/ur/datatypes.h"
#include "include/ur_client_library/ur/robot_receive_timeout.h"
#include "include/ur_client_library/ur/tool_communication.h"
#include "include/ur_client_library/ur/ur_driver.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/control/realtime_bridge_types.h"
#include "intrinsic/icon/control/realtime_clock_interface.h"
#include "intrinsic/icon/control/safety/extern/safety_status.fbs.h"
#include "intrinsic/icon/control/safety/safety_messages.fbs.h"
#include "intrinsic/icon/control/safety/safety_messages_utils.h"
#include "intrinsic/icon/flatbuffers/transform_types.fbs.h"
#include "intrinsic/icon/flatbuffers/transform_types.h"
#include "intrinsic/icon/flatbuffers/transform_view.h"
#include "intrinsic/icon/hal/command_validator.h"
#include "intrinsic/icon/hal/hardware_interface_handle.h"
#include "intrinsic/icon/hal/hardware_interface_registry.h"
#include "intrinsic/icon/hal/hardware_interface_traits.h"
#include "intrinsic/icon/hal/hardware_module_init_context.h"
#include "intrinsic/icon/hal/hardware_module_registry.h"
#include "intrinsic/icon/hal/interfaces/force_sensor.fbs.h"
#include "intrinsic/icon/hal/interfaces/force_torque.fbs.h"
#include "intrinsic/icon/hal/interfaces/force_torque_utils.h"
#include "intrinsic/icon/hal/interfaces/io_controller.fbs.h"
#include "intrinsic/icon/hal/interfaces/io_controller_utils.h"
#include "intrinsic/icon/hal/interfaces/joint_command.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_command_utils.h"
#include "intrinsic/icon/hal/interfaces/joint_state.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_state_utils.h"
#include "intrinsic/icon/hal/interfaces/payload_command.fbs.h"
#include "intrinsic/icon/hal/interfaces/payload_command_utils.h"
#include "intrinsic/icon/hal/interfaces/payload_state.fbs.h"
#include "intrinsic/icon/hal/interfaces/payload_state_utils.h"
#include "intrinsic/icon/hal/interfaces/robot_controller.fbs.h"
#include "intrinsic/icon/hal/interfaces/robot_controller_utils.h"
#include "intrinsic/icon/hal/interfaces/robot_payload_utils.h"
#include "intrinsic/icon/hal/module_config.h"
#include "intrinsic/icon/hardware_modules/universal_robots/config.pb.h"
#include "intrinsic/icon/hardware_modules/universal_robots/log_handler.h"
#include "intrinsic/icon/hardware_modules/universal_robots/pass_through_filter.h"
#include "intrinsic/icon/hardware_modules/universal_robots/prefilter_interface.h"
#include "intrinsic/icon/hardware_modules/universal_robots/rtde_ur_driver.h"
#include "intrinsic/icon/hardware_modules/universal_robots/ur_calibration_utils.h"
#include "intrinsic/icon/hardware_modules/universal_robots/ur_error_codes.h"
#include "intrinsic/icon/hardware_modules/universal_robots/ur_kinematics_calibration_service.h"
#include "intrinsic/icon/hardware_modules/universal_robots/ur_prefilter_factory.h"
#include "intrinsic/icon/proto/safety_status.pb.h"
#include "intrinsic/icon/proto/safety_status_conversion.h"
#include "intrinsic/icon/utils/clock.h"
#include "intrinsic/icon/utils/duration.h"
#include "intrinsic/icon/utils/fixed_str_cat.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/realtime_guard.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"
#include "intrinsic/resources/proto/runtime_context.pb.h"
#include "intrinsic/util/invalid_until_set.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/rt_thread.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/world/robot_payload/robot_payload_base.h"

namespace intrinsic::icon {

using cartesian_impedance::RotateCartesianVector;
using ::intrinsic_fbs::ButtonStatus;
using ::intrinsic_fbs::ForceTorqueCommand;
using ::intrinsic_fbs::ForceTorqueStatus;
using ::intrinsic_fbs::JointAccelerationAndTorqueCommand;
using ::intrinsic_fbs::JointPositionState;
using ::intrinsic_fbs::JointTorqueCommand;
using ::intrinsic_fbs::JointVelocityState;
using ::intrinsic_fbs::ModeOfSafeOperation;
using ::intrinsic_fbs::RequestedBehavior;
using ::intrinsic_fbs::RobotControllerStatus;
using ::intrinsic_fbs::SafetyStatusMessage;
using ::urcl::primary_interface::KinematicsInfo;
using ::urcl::rtde_interface::RUNTIME_STATE;

namespace {

static constexpr int kNumJoints = 6;
// It takes two cycles for a Digital Output Command to be reflected as Status.
static constexpr int kCommandDigitalOutputEveryNCycles = 2;
static constexpr int kControlFrequencyHz = 500;

// Timeout for ticking the clock i.e. triggering ICON inside the realtime loop.
// Period is 500Hz --> cycle time is 2000us.
static constexpr absl::Duration kCycleTime = absl::Microseconds(2000);
// TODO(b/399595195): Consider reducing back to 2500ms once the bug is fixed.
static constexpr absl::Duration kCycleTimeTimeout =
    absl::Microseconds(3000);  // 1.5x the cycle time.
// Poll interval while waiting for RealtimeLoop() to observe a non-OK status
// or stop request.
static constexpr absl::Duration kCyclicLoopStatusPollInterval =
    absl::Milliseconds(1);
static constexpr absl::Duration kTickBlockingLogWarning =
    absl::Microseconds(1500);  // 0.75x the cycle time.
static constexpr absl::Duration kCyclicReadLogWarning =
    absl::Microseconds(2200);  // 1.1x the cycle time.
static constexpr absl::Duration kApplyCommandLogWarning =
    absl::Microseconds(1000);  // 0.5x the cycle time.
// A short timeout for retrying to connect to the robot.
// The integrated retry logic sometimes takes > 2 Minutes to return.
// See b/293589121. In testing a single retry was enough.
static constexpr absl::Duration kRetryConnectionTimeout = absl::Seconds(10);

// Read timeout for the reverse socket running in the
// external control script on the robot. The timeout cannot be higher than 1
// second for realtime commands. Explicitly passing the default value of 20ms
// as defined for writeJointCommand in
// google3/include/ur_client_library/ur/ur_driver.h
// for better visibility.
static const urcl::RobotReceiveTimeout kRobotReceiveTimeout =
    urcl::RobotReceiveTimeout::millisec(20);

// Maximum size of the event history. Old entries will be removed.
static constexpr size_t kEventHistoryMaxSize = 20;

// Timeout waiting for receiving calibration data.
static constexpr absl::Duration kCalibrationDataTimeout = absl::Seconds(10);
constexpr absl::Duration kClearSafetyTimeout = absl::Seconds(30);
constexpr absl::Duration kEnableTimeout = absl::Seconds(30);
constexpr absl::Duration kProgramTimeout = absl::Seconds(30);
constexpr absl::Duration kResetPointersTimeout = absl::Seconds(3);

// Polling interval (20 Hz = every 25 cycles at 500 Hz) for background resend
// thread.
static constexpr absl::Duration kResendThreadPollInterval =
    absl::Milliseconds(50);
// Settling guard time for UR controller script interpreter and RTDE stream
// stabilization after sending a program script.
static constexpr absl::Duration kResendProgramSettlingTime =
    absl::Milliseconds(200);
// Timeout waiting for robot program to reach PLAYING state after resending.
static constexpr absl::Duration kResendProgramSpinUpTimeout = absl::Seconds(1);

static constexpr absl::string_view kJointPositionCommandName =
    "joint_position_command";
static constexpr absl::string_view kJointTorqueCommandName =
    "joint_torque_command";
static constexpr absl::string_view kJointAccelerationCommandName =
    "joint_acceleration_command";
static constexpr absl::string_view kStandardDigitalOutputCommandName =
    "standard_digital_output_command";
static constexpr absl::string_view kConfigurableDigitalOutputCommandName =
    "configurable_digital_output_command";
static constexpr absl::string_view kToolDigitalOutputCommandName =
    "tool_digital_output_command";
static constexpr absl::string_view kForceTorqueCommandName =
    "force_torque_command";

}  // namespace

absl::string_view ToString(urcl::SafetyMode safety_mode) {
  switch (safety_mode) {
    case urcl::SafetyMode::NORMAL:
      return "NORMAL";
    case urcl::SafetyMode::REDUCED:
      return "REDUCED";
    case urcl::SafetyMode::PROTECTIVE_STOP:
      return "PROTECTIVE_STOP";
    case urcl::SafetyMode::RECOVERY:
      return "RECOVERY";
    case urcl::SafetyMode::SAFEGUARD_STOP:
      return "SAFEGUARD_STOP";
    case urcl::SafetyMode::SYSTEM_EMERGENCY_STOP:
      return "SAFEGUARD_STOP";
    case urcl::SafetyMode::ROBOT_EMERGENCY_STOP:
      return "ROBOT_EMERGENCY_STOP";
    case urcl::SafetyMode::VIOLATION:
      return "VIOLATION";
    case urcl::SafetyMode::FAULT:
      return "FAULT";
    case urcl::SafetyMode::VALIDATE_JOINT_ID:
      return "VALIDATE_JOINT_ID";
    case urcl::SafetyMode::UNDEFINED_SAFETY_MODE:
      return "UNDEFINED_SAFETY_MODE";
  }
}

absl::string_view ToString(urcl::rtde_interface::RUNTIME_STATE runtime_state) {
  switch (runtime_state) {
    case urcl::rtde_interface::RUNTIME_STATE::STOPPING:
      return "STOPPING";
    case urcl::rtde_interface::RUNTIME_STATE::STOPPED:
      return "STOPPED";
    case urcl::rtde_interface::RUNTIME_STATE::PLAYING:
      return "PLAYING";
    case urcl::rtde_interface::RUNTIME_STATE::PAUSING:
      return "PAUSING";
    case urcl::rtde_interface::RUNTIME_STATE::PAUSED:
      return "PAUSED";
    case urcl::rtde_interface::RUNTIME_STATE::RESUMING:
      return "RESUMING";
  }
}

namespace {

intrinsic_proto::icon::v1::Event::Severity ReportLevelToSeverity(
    urcl::primary_interface::ReportLevel reportLevel) {
  switch (reportLevel) {
    case urcl::primary_interface::ReportLevel::INFO:
      return intrinsic_proto::icon::v1::Event::INFO;
    case urcl::primary_interface::ReportLevel::WARNING:
      return intrinsic_proto::icon::v1::Event::WARNING;
    case urcl::primary_interface::ReportLevel::VIOLATION:
    case urcl::primary_interface::ReportLevel::FAULT:
      return intrinsic_proto::icon::v1::Event::ERROR;
    default:
      return intrinsic_proto::icon::v1::Event::WARNING;
  }
}

// Assigns a value to a flatbuffer vector.
template <typename T>
void CopyFbsVector(const urcl::vector6d_t& from, flatbuffers::Vector<T>& to) {
  QCHECK_EQ(to.size(), from.size());
  for (int idx = 0; idx < from.size(); ++idx) {
    to.Mutate(idx, from.at(idx));
  }
}

template <typename T>
inline RealtimeStatus ReadData(urcl::rtde_interface::DataPackage& data_pkg,
                               const std::string& var_name, T& data) {
  if (!data_pkg.getData(var_name, data)) {
    INTRINSIC_RT_LOG_THROTTLED(ERROR)
        << "RTDE data package missing variable: '" << var_name << "'.";
    return NotFoundError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "Did not find '", var_name, "'."));
  }
  return OkStatus();
}

template <typename T, size_t N>
inline RealtimeStatus ReadBitsetData(
    urcl::rtde_interface::DataPackage& data_pkg, const std::string& var_name,
    std::bitset<N>& data) {
  if (!data_pkg.getData<T, N>(var_name, data)) {
    INTRINSIC_RT_LOG_THROTTLED(ERROR)
        << "RTDE bitset data package missing variable: '" << var_name << "'.";
    return NotFoundError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "Did not find '", var_name, "'."));
  }
  return OkStatus();
}

// Populates data with the state read from `data_pkg`.
// The enum type of `data` can be smaller than the data in the package, but
// needs to be read as int32_t or uint32_t.
// The enums in urcl (SafetyMode, RobotMode) unfortunately are not the
// correct type.
template <typename S, typename T>
inline RealtimeStatus ReadStateData(urcl::rtde_interface::DataPackage& data_pkg,
                                    const std::string& var_name, T& data) {
  S state;
  if (!data_pkg.getData(var_name, state)) {
    return NotFoundError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "Did not find '", var_name, "'!"));
  }
  data = static_cast<T>(state);
  return OkStatus();
}

// Prints the KinematicsInfo in a format that can be used by
// google3/third_party/ur_calibration.
std::string FormatKinematicsInfo(const KinematicsInfo& kin_info) {
  std::stringstream stream;
  stream << "------- Begin Kinematic Calibration Data -------\n";
  stream << "TODO(293841920): Using those parameters results in disconnected "
            "joints.\n";
  stream << "[mounting]\n";
  stream << "delta_theta = [" << absl::StrJoin(kin_info.dh_theta_, ", ")
         << "]\n";
  stream << "delta_a = [" << absl::StrJoin(kin_info.dh_a_, ", ") << "]\n";
  stream << "delta_d = [" << absl::StrJoin(kin_info.dh_d_, ", ") << "]\n";
  stream << "delta_alpha = [" << absl::StrJoin(kin_info.dh_alpha_, ", ")
         << "]\n";
  // TODO(b/293843859): Print as Hex.
  stream << "joint_checksum = [" << absl::StrJoin(kin_info.checksum_, ", ")
         << "]\n";
  stream << "calibration_status = " << kin_info.calibration_status_
         << " # 0 == notInitialized / 1 == notLinearised / 2 == Linearised \n";
  stream << "------- End Kinematic Calibration Data -------";

  return stream.str();
}

// Parses the calibration data received from the robot.
// Inspired by
// google3/examples/primary_pipeline_calibration.cpp
class CalibrationConsumer
    : public urcl::comm::IConsumer<urcl::primary_interface::PrimaryPackage> {
 public:
  explicit CalibrationConsumer(absl::Notification& got_calibration_data)
      : got_calibration_data_(got_calibration_data) {}
  ~CalibrationConsumer() override = default;

  bool consume(std::shared_ptr<urcl::primary_interface::PrimaryPackage> product)
      override {
    auto kin_info = std::dynamic_pointer_cast<KinematicsInfo>(product);
    if (kin_info == nullptr) {
      // Silently ignores non KinematicsInfo messages.
      return true;
    }
    kinematics_info_ = std::move(kin_info);
    got_calibration_data_.Notify();
    return true;
  }
  // Only valid after `got_calibration_data_` has been notified.
  absl::StatusOr<const KinematicsInfo*> GetKinematicsInfo() {
    if (!kinematics_info_) {
      return absl::FailedPreconditionError(
          "Did not receive calibration data. Did you call GetKinematicsInfo "
          "before receiving the notification?");
    }
    return kinematics_info_.get();
  }

 private:
  absl::Notification& got_calibration_data_;
  std::shared_ptr<KinematicsInfo> kinematics_info_;
};

absl::StatusOr<std::vector<EntityAndTransform>> ReadAndProcessCalibrationData(
    absl::string_view robot_ip, absl::string_view robot_type_name) {
  LOG(INFO) << "Reading calibration data from robot.";
  try {
    urcl::primary_interface::PrimaryParser parser;
    urcl::comm::URStream<urcl::primary_interface::PrimaryPackage>
        primary_stream(std::string(robot_ip),
                       urcl::primary_interface::UR_PRIMARY_PORT);
    urcl::comm::URProducer<urcl::primary_interface::PrimaryPackage> producer(
        primary_stream, parser);
    producer.setupProducer();

    absl::Notification got_calibration_data;
    CalibrationConsumer calibration_consumer(got_calibration_data);
    // Unused.
    urcl::comm::INotifier notifier;

    urcl::comm::Pipeline<urcl::primary_interface::PrimaryPackage>
        calib_pipeline(producer, &calibration_consumer, "Pipeline", notifier);
    calib_pipeline.run();

    if (!got_calibration_data.WaitForNotificationWithTimeout(
            kCalibrationDataTimeout)) {
      return absl::DeadlineExceededError(
          "Timed out waiting for calibration data.");
    }
    INTR_ASSIGN_OR_RETURN(const KinematicsInfo* kin_info,
                          calibration_consumer.GetKinematicsInfo());
    LOG(INFO) << FormatKinematicsInfo(*kin_info);

    return CalculateChain(*kin_info, robot_type_name);
  } catch (const std::exception& e) {
    return absl::InternalError(
        absl::StrCat("Error while reading calibration data: ", e.what()));
  }
}

// Creates a vector of size `num_bits` with a string representation of the
// indices e.g. ["0", "1", "2", ...].
std::vector<std::string> CreateDescriptions(size_t num_bits) {
  std::vector<std::string> descriptions{num_bits, ""};
  // Populates the description with the correct indices. This is not required,
  // but potentially nice.
  for (size_t i = 0; i < descriptions.size(); ++i) {
    descriptions[i] = std::to_string(i);
  }
  return descriptions;
}

// Ticks the clock. Waits the rest of the cycle time if an error returns
// early so there is no busy loop.
// Resets clock on error because it is more stable than only resetting in
// Activate.
RealtimeStatus TickIconOnErrorResetClock(
    RealtimeClockInterface* realtime_clock,
    const Clock::time_point& cycle_start_time) {
  const auto tick_blocking_status = realtime_clock->TickBlockingWithTimeout(
      /*current_timestamp=*/cycle_start_time,
      /*timeout=*/kCycleTimeTimeout);
  if (!tick_blocking_status.ok()) {
    INTRINSIC_RT_LOG_THROTTLED(ERROR)
        << "TickBlockingWithTimeout returned with error: "
        << tick_blocking_status.message()
        << ". This may indicate an issue with ICON.";

    if (const auto status = realtime_clock->Reset(kCycleTimeTimeout);
        !status.ok()) {
      INTRINSIC_RT_LOG_THROTTLED(ERROR)
          << "Failed to reset clock. With: " << status.message();
    }

    // TickBlockingWithTimeout can return immediately and lead to a busy loop.
    absl::SleepFor(kCycleTime - absl::Nanoseconds(intrinsic::ToInt64Nanoseconds(
                                    Clock::Now() - cycle_start_time)));
  }
  return tick_blocking_status;
}

Pose3d PoseFromUrVector(const urcl::vector6d_t& ur_pose) {
  const double tcp_angle =
      std::sqrt(std::pow(ur_pose[3], 2) + std::pow(ur_pose[4], 2) +
                std::pow(ur_pose[5], 2));
  eigenmath::Vector3d axis(ur_pose[3], ur_pose[4], ur_pose[5]);
  eigenmath::Vector3d position =
      eigenmath::Vector3d(ur_pose[0], ur_pose[1], ur_pose[2]);
  return CreateAngleAxisPose(tcp_angle, axis.normalized(), position);
}

// Transforms the force torque data from the robot (Base) to the correct frame
// (Tip/Sensor). Logic inspired by
// https://github.com/UniversalRobots/Universal_Robots_ROS2_Driver/blob/a8c3a4353b4fe32ede7c1c8f2eeae4b167da9e01/ur_robot_driver/src/hardware_interface.cpp#L760-L796
Wrench FTWrenchFromUrTcpForce(const urcl::vector6d_t& actual_tcp_pose,
                              const urcl::vector6d_t& actual_tcp_force) {
  Wrench sensed_wrench_base_aligned =
      Wrench(actual_tcp_force[0], actual_tcp_force[1], actual_tcp_force[2],
             actual_tcp_force[3], actual_tcp_force[4], actual_tcp_force[5]);
  const Pose3d base_t_tip_current = PoseFromUrVector(actual_tcp_pose);
  Wrench wrench_at_ft;
  wrench_at_ft.head<3>() =
      base_t_tip_current.quaternion().inverse().toRotationMatrix() *
      sensed_wrench_base_aligned.head<3>();
  wrench_at_ft.tail<3>() =
      base_t_tip_current.quaternion().inverse().toRotationMatrix() *
      sensed_wrench_base_aligned.tail<3>();
  return wrench_at_ft;
}

// Populates `safety_status` based on the information extracted from
// `robot_status`.
intrinsic::icon::SafetyStatus ExtractSafetyStatus(
    const UniversalRobotsModule::CyclicReadData& robot_status) {
  // Explicitly initialize in case the default is ever adjusted.
  intrinsic::icon::SafetyStatus safety_status{
      .mode_of_safe_operation = intrinsic_fbs::ModeOfSafeOperation::UNKNOWN,
      .estop_button_status = intrinsic_fbs::ButtonStatus::UNKNOWN,
      .enable_button_status = intrinsic_fbs::ButtonStatus::UNKNOWN,
      .requested_behavior = intrinsic_fbs::RequestedBehavior::UNKNOWN,
  };

  switch (robot_status.safety_mode) {
    case urcl::SafetyMode::NORMAL: {
      if (robot_status.runtime_state ==
          urcl::rtde_interface::RUNTIME_STATE::PLAYING) {
        safety_status.requested_behavior =
            intrinsic_fbs::RequestedBehavior::NORMAL_OPERATION;
      } else {
        safety_status.requested_behavior =
            intrinsic_fbs::RequestedBehavior::PAUSE;
      }
      safety_status.mode_of_safe_operation =
          intrinsic_fbs::ModeOfSafeOperation::AUTOMATIC;
      safety_status.estop_button_status =
          intrinsic_fbs::ButtonStatus::DISENGAGED;
      break;
    }
    case urcl::SafetyMode::REDUCED: {
      // TODO(b/492112467): Adjust the requested behavior once ICON supports
      // safety limited speed (SLS).
      safety_status.requested_behavior =
          intrinsic_fbs::RequestedBehavior::PAUSE;
      safety_status.mode_of_safe_operation =
          intrinsic_fbs::ModeOfSafeOperation::AUTOMATIC;
      safety_status.estop_button_status =
          intrinsic_fbs::ButtonStatus::DISENGAGED;
      break;
    }
    case urcl::SafetyMode::SAFEGUARD_STOP:
    case urcl::SafetyMode::RECOVERY: {
      safety_status.requested_behavior =
          intrinsic_fbs::RequestedBehavior::SAFE_STOP_2_TIME_MONITORED;
      safety_status.mode_of_safe_operation =
          intrinsic_fbs::ModeOfSafeOperation::AUTOMATIC;
      safety_status.estop_button_status =
          intrinsic_fbs::ButtonStatus::DISENGAGED;
      break;
    }
    case urcl::SafetyMode::SYSTEM_EMERGENCY_STOP:
    case urcl::SafetyMode::ROBOT_EMERGENCY_STOP:
      safety_status.estop_button_status = intrinsic_fbs::ButtonStatus::ENGAGED;
      [[fallthrough]];
    case urcl::SafetyMode::PROTECTIVE_STOP:
    case urcl::SafetyMode::VIOLATION:
    case urcl::SafetyMode::FAULT:
    case urcl::SafetyMode::VALIDATE_JOINT_ID:
    case urcl::SafetyMode::UNDEFINED_SAFETY_MODE: {
      safety_status.requested_behavior =
          intrinsic_fbs::RequestedBehavior::SAFE_STOP_0;
      safety_status.mode_of_safe_operation =
          intrinsic_fbs::ModeOfSafeOperation::UNKNOWN;
      break;
    }
  }
  return safety_status;
}

// Updates the mutable `safety_status` with the status extracted from
// `robot_status`.
void UpdateSafetyStatus(
    const UniversalRobotsModule::CyclicReadData& robot_status,
    MutableHardwareInterfaceHandle<intrinsic_fbs::SafetyStatusMessage>&
        safety_status) {
  const intrinsic::icon::SafetyStatus status =
      ExtractSafetyStatus(robot_status);
  safety_status->mutate_requested_behavior(status.requested_behavior);
  safety_status->mutate_mode_of_safe_operation(status.mode_of_safe_operation);
  safety_status->mutate_estop_button_status(status.estop_button_status);
  safety_status->mutate_enable_button_status(status.enable_button_status);
  safety_status.UpdatedAt(intrinsic::Clock::Now());
}

// Returns true if the robot is not hard-stopped and it can be externally
// controlled.
bool IsInControllableSafetyMode(std::string_view safety_mode_string) {
  return safety_mode_string ==
             urcl::safetyModeString(urcl::SafetyMode::NORMAL) ||
         safety_mode_string ==
             urcl::safetyModeString(urcl::SafetyMode::REDUCED) ||
         safety_mode_string ==
             urcl::safetyModeString(urcl::SafetyMode::SAFEGUARD_STOP) ||
         safety_mode_string ==
             urcl::safetyModeString(urcl::SafetyMode::RECOVERY);
}

// Stateless helper to translate driver error statuses
// into internal CyclicLoopState enumerations for atomic storage.
UniversalRobotsModule::CyclicLoopState ToCyclicLoopStatus(
    const RealtimeStatus& status) {
  if (status.ok()) {
    return UniversalRobotsModule::CyclicLoopState::kOk;
  }
  if (status.code() == absl::StatusCode::kUnavailable) {
    return UniversalRobotsModule::CyclicLoopState::kUninitialized;
  }
  if (status.message().find("connection") != absl::string_view::npos ||
      status.message().find("closed") != absl::string_view::npos) {
    return UniversalRobotsModule::CyclicLoopState::kRobotConnectionClosed;
  }
  if (status.message().find("not receive") != absl::string_view::npos) {
    return UniversalRobotsModule::CyclicLoopState::kNoDataReceived;
  }
  return UniversalRobotsModule::CyclicLoopState::kReadError;
}

}  // namespace

absl::string_view UniversalRobotsModule::ToString(ResendProgramState state) {
  switch (state) {
    case ResendProgramState::kIdle:
      return "kIdle";
    case ResendProgramState::kCloseConnectionRequested:
      return "kCloseConnectionRequested";
    case ResendProgramState::kSendProgramRequested:
      return "kSendProgramRequested";
    case ResendProgramState::kSendProgramInProgress:
      return "kSendProgramInProgress";
    case ResendProgramState::kFailed:
      return "kFailed";
  }
}

RealtimeStatus UniversalRobotsModule::GetCyclicLoopStatus() const {
  const CyclicLoopState status =
      cyclic_loop_status_.load(std::memory_order_acquire);
  switch (status) {
    case CyclicLoopState::kOk:
      return OkStatus();
    case CyclicLoopState::kUninitialized:
      return UnavailableError("Did not yet receive data from RTDE");
    case CyclicLoopState::kRobotConnectionClosed:
      return FailedPreconditionError(
          "Robot closed the connection. Was it switched to manual mode?");
    case CyclicLoopState::kNoDataReceived:
      return FailedPreconditionError("Did not receive data from robot.");
    case CyclicLoopState::kProgramNotActive:
      return FailedPreconditionError("Program not active on the robot");
    case CyclicLoopState::kStoppedSendingCommands:
      return FailedPreconditionError(
          "Cyclic loop was commanded to stop sending robot commands.");
    case CyclicLoopState::kReadError:
      return NotFoundError(
          "Error reading RTDE data package. See logs for missing variable "
          "name.");
  }
  return InternalError("Unknown cyclic loop status.");
}

void UniversalRobotsModule::SetCyclicLoopRequest(CyclicLoopRequest request) {
  cyclic_loop_request_.store(request, std::memory_order_release);
}

absl::Status UniversalRobotsModule::LogToRobot(absl::string_view message) {
  try {
    if (!backend_->CommandAddToLog(std::string(message))) {
      LOG(WARNING) << "Failed to add to UR log: " << message;
    }
  } catch (const std::exception& e) {
    return absl::FailedPreconditionError(
        absl::StrCat("UR driver error during CommandAddToLog: ", e.what()));
  } catch (...) {
    return absl::FailedPreconditionError(
        "Unknown UR driver error during CommandAddToLog.");
  }
  return absl::OkStatus();
}

// static
absl::StatusOr<UniversalRobotsModule::Config>
UniversalRobotsModule::Config::FromProto(
    const intrinsic_proto::icon::UniversalRobotsModuleConfig& proto_config) {
  UniversalRobotsModule::Config config;

  if (proto_config.robot_ip().empty()) {
    return absl::InvalidArgumentError(
        "Robot IP needs to be defined. You can find the IP on the About "
        "page on the Teach Pendant.");
  }
  config.robot_ip = proto_config.robot_ip();

  config.calibration_checksum = proto_config.calibration_checksum();

  if (proto_config.has_log_and_serve_calibration_data()) {
    config.log_and_serve_calibration_data =
        proto_config.log_and_serve_calibration_data();
  }

  if (proto_config.has_reverse_interface()) {
    const auto& reverse_interface_config = proto_config.reverse_interface();
    config.reverse_ip = reverse_interface_config.reverse_ip();
    if (reverse_interface_config.has_reverse_port()) {
      config.reverse_port = reverse_interface_config.reverse_port();
    }
    if (reverse_interface_config.has_script_sender_port()) {
      config.script_sender_port = reverse_interface_config.script_sender_port();
    }
    if (reverse_interface_config.has_trajectory_port()) {
      config.trajectory_port = reverse_interface_config.trajectory_port();
    }
    if (reverse_interface_config.has_script_command_port()) {
      config.script_command_port =
          reverse_interface_config.script_command_port();
    }
  }

  if (proto_config.has_friction_compensation_scale_factors()) {
    if (proto_config.friction_compensation_scale_factors().coulomb().size() !=
            kNumJoints ||
        proto_config.friction_compensation_scale_factors().viscous().size() !=
            kNumJoints) {
      return absl::InvalidArgumentError(
          absl::StrCat("Exactly ", kNumJoints,
                       " Coulomb and Viscous scale factors must be provided."));
    }
    UniversalRobotsModule::Config::FrictionCompensationScaleFactors
        scale_factors;
    for (int i = 0; i < kNumJoints; i++) {
      const auto& val_viscous =
          proto_config.friction_compensation_scale_factors().viscous().at(i);
      const auto& val_coulomb =
          proto_config.friction_compensation_scale_factors().coulomb().at(i);
      if (val_viscous < 0.0 || val_viscous > 1.0) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Viscous friction scale factors must be between 0 and 1."));
      }
      if (val_coulomb < 0.0 || val_coulomb > 1.0) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Coulomb friction scale factors must be between 0 and 1."));
      }
      scale_factors.viscous_scale[i] = val_viscous;
      scale_factors.coulomb_scale[i] = val_coulomb;
    }
    config.joint_friction_compensation_scale_factors = scale_factors;
  }

  switch (proto_config.advanced_control_options_case()) {
    case intrinsic_proto::icon::UniversalRobotsModuleConfig::
        AdvancedControlOptionsCase::kNoAdvancedControl: {
      if (proto_config.no_advanced_control().has_servoj_gain()) {
        config.servoj_gain = proto_config.no_advanced_control().servoj_gain();
      }
      if (proto_config.no_advanced_control().has_servoj_lookahead_seconds()) {
        config.servoj_lookahead_seconds =
            proto_config.no_advanced_control().servoj_lookahead_seconds();
      }
      break;
    }
    case intrinsic_proto::icon::UniversalRobotsModuleConfig::
        AdvancedControlOptionsCase::kCustomAdvancedControl: {
      if (proto_config.custom_advanced_control().has_servoj_gain()) {
        config.servoj_gain =
            proto_config.custom_advanced_control().servoj_gain();
      }
      if (proto_config.custom_advanced_control()
              .has_servoj_lookahead_seconds()) {
        config.servoj_lookahead_seconds =
            proto_config.custom_advanced_control().servoj_lookahead_seconds();
      }
      break;
    }
    default:
      break;
  }

  if (config.servoj_gain < 100 || config.servoj_gain > 2000) {
    return absl::InvalidArgumentError(
        absl::StrCat("servoj_gain needs to be between 100 and 2000. Got: ",
                     config.servoj_gain));
  }

  if (config.servoj_lookahead_seconds < 0.03 ||
      config.servoj_lookahead_seconds > 0.2) {
    return absl::InvalidArgumentError(absl::StrCat(
        "servoj_lookahead_seconds needs to be between 0.03 and 0.2. Got: ",
        config.servoj_lookahead_seconds));
  }

  return config;
}

void UniversalRobotsModule::HandleRobotProgramState(bool program_running) {
  program_running_ = program_running;
  if (program_running) {
    INTRINSIC_RT_LOG(INFO) << "Program is running on the robot";
  } else {
    INTRINSIC_RT_LOG(INFO) << "Program stopped running on the robot";
  }
}

absl::Status UniversalRobotsModule::JoinFailedInitThread(
    absl::Duration timeout) {
  absl::MutexLock l(init_thread_mutex_);
  if (!failed_init_thread_disposal_) {
    LOG(INFO) << "There is no failed init thread to join.";
    return absl::OkStatus();
  }
  if (!failed_init_thread_disposal_->thread->joinable()) {
    LOG(INFO) << "Failed init thread is not joinable.";
    return absl::OkStatus();
  }

  if (!failed_init_thread_disposal_->has_returned
           .WaitForNotificationWithTimeout(timeout)) {
    return absl::AbortedError(
        "Timed out waiting for init_failed_thread to finish retrying. You can "
        "try again later (likely minutes), or restart the module");
  }

  LOG(INFO) << "Joining failed init thread.";
  if (failed_init_thread_disposal_->thread->joinable()) {
    failed_init_thread_disposal_->thread->join();
  }
  LOG(INFO) << "Successfully joined failed init thread.";
  failed_init_thread_disposal_ = nullptr;
  return absl::OkStatus();
}

absl::Status UniversalRobotsModule::ConnectToRobot(const Config& config) {
  try {
    if (!backend_) {
      backend_ = std::make_unique<RtdeUrDriver>();
    } else {
      if (backend_->IsDriverValid()) {
        LOG(INFO) << "Deleting old ur_driver_.";
        backend_->ResetDriver();
      }
      LOG(INFO) << "Deleting old dashboard_client_.";
      backend_->DisconnectDashboard();
      backend_->ResetDashboard();
    }

    LOG(INFO) << "Connecting to robot on " << config.robot_ip;
    INTR_RETURN_IF_ERROR(backend_->ConnectDashboard(config.robot_ip));

    if (config.headless_mode && !backend_->CommandIsInRemoteControl()) {
      return absl::FailedPreconditionError(
          "Module is configured for headless mode, but robot is not in "
          "RemoteControl mode. Please enable RemoteControl on the teach "
          "pendant.");
    }

    LOG(INFO) << "Stopping all currently running programs";
    // Stop program, if there is one running
    if (!backend_->CommandStop()) {
      return absl::FailedPreconditionError(
          "Could not send stop program command");
    }
    absl::SleepFor(absl::Seconds(1));

    robot_model_name_ = "";
    if (!backend_->CommandGetRobotModel(robot_model_name_)) {
      LOG(WARNING) << "Could not get robot model.";
    } else {
      LOG(INFO) << "Robot model: " << robot_model_name_;
    }

    program_running_ = false;
    const UrDriverConfig driver_config{
        .robot_ip = config.robot_ip,
        .script_file = config.script_file_path,
        .output_recipe =
            PathResolver::ResolveRunfilesPath(config.output_recipe_path),
        .input_recipe =
            PathResolver::ResolveRunfilesPath(config.input_recipe_path),
        .handle_program_state = absl::bind_front(
            &UniversalRobotsModule::HandleRobotProgramState, this),
        .headless_mode = config.headless_mode,
        .reverse_port = config.reverse_port,
        .script_sender_port = config.script_sender_port,
        .trajectory_port = config.trajectory_port,
        .script_command_port = config.script_command_port,
        .servoj_gain = config.servoj_gain,
        .servoj_lookahead_time = config.servoj_lookahead_seconds,
        .non_blocking_read = config.non_blocking_read,
        .reverse_ip = config.reverse_ip,
    };
    INTR_RETURN_IF_ERROR(backend_->InitializeDriver(driver_config));

    if (!config.calibration_checksum.empty()) {
      if (!backend_->CheckCalibration(config.calibration_checksum)) {
        return absl::FailedPreconditionError(absl::StrCat(
            "The provided checksum[", config.calibration_checksum,
            "] does not match the kinematics calibration of the robot."));
      } else {
        LOG(INFO) << "The kinematics calibration of the robot matches the "
                     "provided checksum["
                  << config.calibration_checksum << "].";
      }
    } else {
      LOG(INFO) << "Skipped checking the kinematics calibration of the robot.";
    }

    const auto version = backend_->GetVersion();
    // Logs the version to identify possible unstable versions.
    LOG(INFO) << "Version: " << version;
    // Checks if major version >= 5;
    if (!version.isESeries()) {
      return absl::FailedPreconditionError(
          "Non E-Series robots are not supported (major version needs to be >= "
          "5");
    }

    if (backend_->GetControlFrequency() != kControlFrequencyHz) {
      return absl::FailedPreconditionError(
          absl::StrCat("Module requires a Control Frequency of ",
                       kControlFrequencyHz, "Hz. The robot is running with ",
                       backend_->GetControlFrequency(), "Hz."));
    }
  } catch (const std::exception& e) {
    return absl::InternalError(absl::StrCat(
        "UR driver error while connecting to the robot: ", e.what()));
  }
  return absl::OkStatus();
}

RealtimeStatusOr<intrinsic::Clock::time_point>
UniversalRobotsModule::CyclicRead() {
  const absl::Time before_read = absl::Now();

  std::unique_ptr<urcl::rtde_interface::DataPackage> data_pkg;
  if (!backend_->IsDriverValid()) {
    return FailedPreconditionError(
        FixedStrCat<RealtimeStatus::kMaxMessageLength>(
            "Robot closed the connection. Was it switched to manual mode?"));
  }

  // UrDriver::getDataPackage timeout is 0 or 100ms - which is 10Hz as worst
  // case.
  // A Pipeline producer overflowed! Error will be raised if the buffer isn't
  // read before the next package arrives.
  data_pkg = backend_->GetDataPackage();
  if (data_pkg == nullptr) {
    return FailedPreconditionError("Did not receive data from robot.");
  }
  Clock::time_point cycle_start = Clock::Now();
  // Read current joint positions from robot data
  INTRINSIC_RT_RETURN_IF_ERROR(
      ReadData(*data_pkg, "actual_q", ur_status_.joint_positions));
  INTRINSIC_RT_RETURN_IF_ERROR(
      ReadData(*data_pkg, "actual_qd", ur_status_.joint_velocities));
  INTRINSIC_RT_RETURN_IF_ERROR(
      ReadData(*data_pkg, "actual_current", ur_status_.current));

  INTRINSIC_RT_RETURN_IF_ERROR(
      ReadData(*data_pkg, "actual_TCP_force", ur_status_.actual_tcp_force));
  INTRINSIC_RT_RETURN_IF_ERROR(
      ReadData(*data_pkg, "actual_TCP_pose", ur_status_.actual_tcp_pose));
  INTRINSIC_RT_RETURN_IF_ERROR(
      ReadData(*data_pkg, "speed_scaling", ur_status_.speed_scaling));

  INTRINSIC_RT_RETURN_IF_ERROR(
      ReadData(*data_pkg, "payload", ur_status_.payload_kg));
  INTRINSIC_RT_RETURN_IF_ERROR(
      ReadData(*data_pkg, "payload_cog", ur_status_.payload_cog));

  // Assumes that taring only takes one cycle as the ur_driver doesn't seem to
  // expose the taring status.
  // TODO(b/290925601): UR module: Understand/Improve taring behavior
  if (ur_status_.taring_in_progress) {
    if (--ur_status_.remaining_taring_cycles <= 0) {
      INTRINSIC_RT_LOG_THROTTLED(ERROR)
          << "Failed to tare within the specified number of cycles.";
      ur_status_.taring_in_progress = false;
    }
  }

  INTRINSIC_RT_RETURN_IF_ERROR(ReadBitsetData<uint64_t>(
      *data_pkg, "actual_digital_input_bits", ur_status_.digital_input_bits));
  INTRINSIC_RT_RETURN_IF_ERROR(ReadBitsetData<uint64_t>(
      *data_pkg, "actual_digital_output_bits", ur_status_.digital_output_bits));

  INTRINSIC_RT_RETURN_IF_ERROR(ReadData(*data_pkg, "standard_analog_input0",
                                        ur_status_.standard_analog_input[0]));
  INTRINSIC_RT_RETURN_IF_ERROR(ReadData(*data_pkg, "standard_analog_input1",
                                        ur_status_.standard_analog_input[1]));
  // Uses a local copy, as ur_status_.runtime_state is atomic which is not
  // supported by the template.
  RUNTIME_STATE runtime_state;
  INTRINSIC_RT_RETURN_IF_ERROR(
      ReadStateData<uint32_t>(*data_pkg, "runtime_state", runtime_state));
  ur_status_.runtime_state = runtime_state;
  runtime_state_.store(runtime_state, std::memory_order_relaxed);

  INTRINSIC_RT_RETURN_IF_ERROR(
      ReadStateData<int32_t>(*data_pkg, "robot_mode", ur_status_.robot_mode));
  INTRINSIC_RT_RETURN_IF_ERROR(
      ReadStateData<int32_t>(*data_pkg, "safety_mode", ur_status_.safety_mode));

  INTRINSIC_RT_RETURN_IF_ERROR(ReadBitsetData<uint32_t>(
      *data_pkg, "robot_status_bits", ur_status_.robot_status_bits));
  INTRINSIC_RT_RETURN_IF_ERROR(ReadBitsetData<uint32_t>(
      *data_pkg, "safety_status_bits", ur_status_.safety_status_bits));

  // Provide the data package for the inspection thread.
  *inspection_data_.last_data_package.GetFreeBuffer() = ur_status_;
  if (!inspection_data_.last_data_package.CommitFreeBuffer()) {
    LOG_EVERY_N_SEC(WARNING, 10)
        << "Failed to commit data package for inspection thread.";
  }

  const absl::Duration read_duration = absl::Now() - before_read;
  if (read_duration > kCyclicReadLogWarning) {
    INTRINSIC_RT_LOG_THROTTLED(WARNING)
        << "Long duration of CyclicRead, duration: "
        << absl::ToDoubleMicroseconds(read_duration) << " us.";
  }
  return cycle_start;
}

// Called before `Activate()` is called.
absl::Status UniversalRobotsModule::Prepare() {
  // Realtime loop can be stopped because ICON does not expect the clock to be
  // ticked yet.
  INTR_RETURN_IF_ERROR(StopRealtimeLoop());

  INTR_RETURN_IF_ERROR(CloseRobotConnection("Prepare"));

  LOG(INFO) << "Prepare finished closing the previous robot connection.";

  INTR_RETURN_IF_ERROR(InitializeRobotConnection());

  // This needs to happen after `InitializeRobotConnection()`, which
  // initializes `robot_model_name_`.
  if (config_.log_and_serve_calibration_data) {
    INTR_ASSIGN_OR_RETURN(
        auto robot_chain_update,
        ReadAndProcessCalibrationData(config_.robot_ip, robot_model_name_));
    INTR_ASSIGN_OR_RETURN(auto rt_calibrated_solver_key,
                          GetCalibratedRobotIkSolverKey(robot_model_name_));
    calibration_service_.SetCalibratedKinematicsResponse(
        robot_chain_update, rt_calibrated_solver_key,
        GetCalibratedRobotIkSolverTip());
  }

  // Initialize dynamic prefilters. This will initialize the prefilter
  // config based on the previously set config, which can be either a default
  // or a custom provided filter.
  INTR_ASSIGN_OR_RETURN(
      prefilter_,
      CreateUrPrefilter(robot_model_name_, advanced_control_options_));

  INTR_RETURN_IF_ERROR(StartRealtimeLoop());

  INTR_RETURN_IF_ERROR(LogToRobot("Intrinsic: Prepare finished."));
  return absl::OkStatus();
}

RealtimeStatus UniversalRobotsModule::Activate() {
  tick_clock_ = true;
  return OkStatus();
}

RealtimeStatus UniversalRobotsModule::Deactivate() {
  // TODO(b/383701675): The connection to the robot can be closed here, but it
  // needs to be done in an non realtime thread.
  tick_clock_ = false;
  return OkStatus();
}

RealtimeStatus UniversalRobotsModule::SendProcessWrench(
    const Wrench& process_wrench) {
  const Pose3d base_t_tip_current =
      PoseFromUrVector(ur_status_.actual_tcp_pose);

  // UR Subtracts the effect of the configured payload from the FT values so we
  // must add these here to cancel.
  const eigenmath::Vector3d p_payload{ur_status_.payload_cog[0],
                                      ur_status_.payload_cog[1],
                                      ur_status_.payload_cog[2]};
  const eigenmath::Vector3d g_in_tip_frame =
      -base_t_tip_current.rotationMatrix().inverse() * config_.gravity;
  const eigenmath::Vector3d gravity_torque_in_tip_frame =
      ur_status_.payload_kg * p_payload.cross(g_in_tip_frame);
  const eigenmath::Vector3d gravity_force_in_tip_frame =
      ur_status_.payload_kg * g_in_tip_frame;

  // N.B.: ICON uses an opposite sign convention for FT measurements so here the
  // process wrench is negated to transform into the UR convention.
  const urcl::vector6d_t total_wrench{
      -process_wrench.x() + gravity_force_in_tip_frame[0],
      -process_wrench.y() + gravity_force_in_tip_frame[1],
      -process_wrench.z() + gravity_force_in_tip_frame[2],
      -process_wrench.RX() + gravity_torque_in_tip_frame[0],
      -process_wrench.RY() + gravity_torque_in_tip_frame[1],
      -process_wrench.RZ() + gravity_torque_in_tip_frame[2],
  };

  if (!backend_->SendExternalForceTorque(total_wrench)) {
    return InternalError("Failed to send external force torque.");
  }

  return OkStatus();
}

// Only called after activate in ICON
RealtimeStatus UniversalRobotsModule::ApplyCommand() {
  const absl::Time before_write = absl::Now();

  if (!backend_->IsDriverValid()) {
    return FailedPreconditionError("Robot driver transport is inactive.");
  }

  // Don't apply a command if reading failed.
  // Surface the error to ICON and the user.
  if (const RealtimeStatus status = GetCyclicLoopStatus(); !status.ok()) {
    INTRINSIC_RT_LOG_THROTTLED(WARNING)
        << "Cyclic loop reported error: " << status.message();
    return status;
  }

  // During safeguard stop (SS2), recovery, or intermediate runtime state
  // transitions (pausing, paused, resuming), do not send active motion/IO
  // commands or RTDE keep-alives to the robot. RTDE packets accumulate in the
  // UR controller buffer while URScript is paused, causing error C271A1
  // ("Runtime is too much behind") and a Protective Stop upon resume.
  const RUNTIME_STATE current_runtime_state =
      runtime_state_.load(std::memory_order_relaxed);
  if (ur_status_.safety_mode == urcl::SafetyMode::SAFEGUARD_STOP ||
      ur_status_.safety_mode == urcl::SafetyMode::RECOVERY ||
      current_runtime_state == RUNTIME_STATE::PAUSED ||
      current_runtime_state == RUNTIME_STATE::PAUSING ||
      current_runtime_state == RUNTIME_STATE::RESUMING) {
    return OkStatus();
  }

  if (ur_status_.safety_mode != urcl::SafetyMode::NORMAL &&
      ur_status_.safety_mode != urcl::SafetyMode::REDUCED) {
    return FailedPreconditionError(
        FixedStrCat<RealtimeStatus::kMaxMessageLength>(
            "Robot is safety stopped. SafetyMode: ",
            ::intrinsic::icon::ToString(ur_status_.safety_mode)));
  }

  if (!program_running_ || current_runtime_state != RUNTIME_STATE::PLAYING) {
    ResendProgramState expected = ResendProgramState::kIdle;
    if (!resend_robot_program_state_.compare_exchange_strong(
            expected, ResendProgramState::kCloseConnectionRequested)) {
      if (expected == ResendProgramState::kSendProgramRequested ||
          expected == ResendProgramState::kSendProgramInProgress) {
        // Program resend is already requested or in progress. Allow resend to
        // complete without requesting connection closure.
        return OkStatus();
      }
    } else {
      // The state was kIdle and has now been set to kCloseConnectionRequested.
      INTRINSIC_RT_LOG_THROTTLED(ERROR)
          << "Motion is enabled, but the program is not running on the robot, "
             "or runtime state not playing. RuntimeState["
          << ::intrinsic::icon::ToString(current_runtime_state)
          << "] program_running[" << (program_running_ ? "yes" : "no") << "]";
    }
    const RealtimeStatus program_error =
        FailedPreconditionError("Program not active on the robot");
    SetCyclicLoopRequest(CyclicLoopRequest::kProgramNotActive);
    return program_error;
  }
  urcl::vector6d_t jcommand = {0};

  // // Do not command a position or torque if the command was not updated this
  // cycle.
  urcl::comm::ControlMode control_mode;
  if (command_validator_.WasUpdatedThisCycle(joint_position_command_).ok()) {
    eigenmath::VectorNd setpoints = intrinsic_fbs::ViewAs<eigenmath::VectorNd>(
        joint_position_command_->position());
    INTRINSIC_RT_ASSIGN_OR_RETURN(eigenmath::VectorNd result,
                                  prefilter_->ComputeControl(setpoints));
    for (size_t i = 0; i < jcommand.size(); ++i) {
      jcommand[i] = result[i];
    }
    control_mode = urcl::comm::ControlMode::MODE_SERVOJ;
  } else if (command_validator_.WasUpdatedThisCycle(joint_acceleration_command_)
                 .ok()) {
    for (size_t i = 0; i < jcommand.size(); ++i) {
      jcommand[i] = joint_acceleration_command_->acceleration()->Get(i);
    }
    control_mode = urcl::comm::ControlMode::MODE_RESOLVED_ACCELERATION;
    // Reset the prefilter if the acceleration command is updated so that on the
    // next position command the prefilter is reinitialized. This ensures that
    // there are no transients.
    prefilter_->Reset();
  } else if (command_validator_.WasUpdatedThisCycle(joint_torque_command_)
                 .ok()) {
    for (size_t i = 0; i < jcommand.size(); ++i) {
      jcommand[i] = joint_torque_command_->torque()->Get(i);
    }
    control_mode = urcl::comm::ControlMode::MODE_TORQUE;
    // Reset the prefilter if the torque command is updated so that on the next
    // position command the prefilter is reinitialized. This ensures that there
    // are no transients.
    prefilter_->Reset();
  } else {
    return FailedPreconditionError(
        "No joint command was updated this cycle. Please make sure that at "
        "least one joint command is sent in every cycle.");
  }

  try {
    // Feedforwards and acceleration status are not supported because we use the
    // official client that is also used for ROS2. It is possible to adjust the
    // RTDE interface with acceleration as well as feedforwards.
    // https://www.universal-robots.com/articles/ur/interface-communication/real-time-data-exchange-rtde-guide/
    // This also requires adjusting `external_control.urscript` and the `URCaps`
    // used by ROS2.
    // Sending a joint command also pets the watchdog.
    if (!backend_->WriteJointCommand(jcommand, control_mode,
                                     kRobotReceiveTimeout)) {
      return InternalError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
          "Failed to send joint command to ur_robot with control mode ",
          control_mode));
    }

    // Only sending digital outputs on changes, because sending every cycle
    // seems to overwhelm RTDE/the controller and results in "Long Duration of
    // Cyclic Read" messages until the connection dies after some time.
    // The ROS2 implementation uses a non-realtime thread with a sleep of 20ms.
    //
    // It would be nicer, if the RTDEWrite supported sending a full
    // DigitalOutput state.
    //
    // It takes two cycles for a Digital Output Command to be reflected as
    // Status.
    if (current_cycle_ % kCommandDigitalOutputEveryNCycles == 0) {
      auto process_dio_command =
          []<typename CommandT, typename StatusT, typename SendF>(
              const CommandT& command_handle, const StatusT& status_handle,
              absl::string_view command_name, SendF send_fn) -> RealtimeStatus {
        if (const auto digital_output_command = command_handle.Value();
            digital_output_command.ok()) {
          for (size_t i = 0;
               i < digital_output_command.value()->signals()->size(); ++i) {
            const bool commanded_value =
                digital_output_command.value()->signals()->Get(i)->value();
            const bool current_value =
                status_handle->signals()->Get(i)->value();
            if (current_value == commanded_value) {
              continue;
            }
            if (!send_fn(i, commanded_value)) {
              return FailedPreconditionError(
                  FixedStrCat<RealtimeStatus::kMaxMessageLength>(
                      "Failed to send ", command_name, " to ur_robot."));
            }
          }
        } else {
          INTRINSIC_RT_LOG_FIRST(WARNING)
              << "Ignoring '" << command_name
              << "' because its cycle is not valid. Is the block configured in "
                 "ICON?";
        }
        return OkStatus();
      };

      INTRINSIC_RT_RETURN_IF_ERROR(process_dio_command(
          standard_digital_output_command_, standard_digital_output_status_,
          kStandardDigitalOutputCommandName, [this](size_t i, bool val) {
            return backend_->SendStandardDigitalOutput(i, val);
          }));
      INTRINSIC_RT_RETURN_IF_ERROR(process_dio_command(
          configurable_digital_output_command_,
          configurable_digital_output_status_,
          kConfigurableDigitalOutputCommandName, [this](size_t i, bool val) {
            return backend_->SendConfigurableDigitalOutput(i, val);
          }));
      INTRINSIC_RT_RETURN_IF_ERROR(process_dio_command(
          tool_digital_output_command_, tool_digital_output_status_,
          kToolDigitalOutputCommandName, [this](size_t i, bool val) {
            return backend_->SendToolDigitalOutput(i, val);
          }));
    }

    // We assume that the process wrench should be set on the controller when it
    // is updated on the hardware interface.
    if (command_validator_.WasUpdatedThisCycle(process_wrench_command_).ok()) {
      if (!sending_external_ft_ ||
          (sending_external_ft_ && !external_ft_enabled_)) {
        backend_->FtRtdeInputEnable(true);
        sending_external_ft_ = true;
        external_ft_enabled_ = true;
      }
      // Send the real process wrench.
      INTRINSIC_RT_RETURN_IF_ERROR(SendProcessWrench(
          {process_wrench_command_->x(), process_wrench_command_->y(),
           process_wrench_command_->z(), process_wrench_command_->rx(),
           process_wrench_command_->ry(), process_wrench_command_->rz()}));

    } else if (sending_external_ft_) {
      if (external_ft_enabled_) {
        backend_->FtRtdeInputEnable(false);
        external_ft_enabled_ = false;
      }
      // Once you start using the input force torque values you need to
      // continually write even if it is disabled from use otherwise  a "C207A0:
      // Fieldbus input disconnected" error occurs. As a work around we send in
      // zero FT after disabling.
      INTRINSIC_RT_RETURN_IF_ERROR(
          SendProcessWrench({0.0, 0.0, 0.0, 0.0, 0.0, 0.0}));
    }

    if (command_validator_.WasUpdatedThisCycle(ft_command_).ok()) {
      if (ft_command_->retare()) {
        if (!backend_->ZeroFTSensor()) {
          return FailedPreconditionError(
              "Failed to send 'retare' command to ur_robot.");
        } else {
          ur_status_.taring_in_progress = true;
          ur_status_.remaining_taring_cycles = ft_command_->num_taring_cycles();
          INTRINSIC_RT_LOG_THROTTLED(INFO)
              << "Taring requested with [" << ur_status_.remaining_taring_cycles
              << "] cycles.";
        }
      }
    } else {
      INTRINSIC_RT_LOG_FIRST(WARNING)
          << "Ignoring '" << kForceTorqueCommandName
          << "' because its cycle is not valid. Is the part configured in "
             "ICON?";
    }
  } catch (const std::exception& e) {
    return InternalError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "UR driver error during ApplyCommand: ", e.what()));
  }

  const absl::Duration write_duration = absl::Now() - before_write;
  if (write_duration > kApplyCommandLogWarning) {
    INTRINSIC_RT_LOG_THROTTLED(WARNING)
        << "Long duration of ApplyCommand, duration: "
        << absl::ToDoubleMicroseconds(write_duration) << " us.";
  }
  return OkStatus();
}

absl::Status UniversalRobotsModule::EnableMotion() {
  LOG(INFO) << "EnableMotion request on " << kHwModuleTypeName;

  if (!backend_->IsDriverValid()) {
    return absl::FailedPreconditionError("Robot driver transport is inactive.");
  }

  if (const RealtimeStatus cyclic_loop_status = GetCyclicLoopStatus();
      !cyclic_loop_status.ok()) {
    LOG(ERROR) << "Cyclic loop reported error: "
               << cyclic_loop_status.message();
    return cyclic_loop_status;
  }

  INTR_RETURN_IF_ERROR(LogToRobot("Intrinsic: EnableMotion called."));

  try {
    if (!backend_->CommandIsInRemoteControl()) {
      return absl::FailedPreconditionError(
          "Robot is not in remote control mode.");
    }
  } catch (const std::exception& e) {
    return absl::FailedPreconditionError(absl::StrCat(
        "UR driver error during commandIsInRemoteControl: ", e.what()));
  }

  if (ur_status_.safety_mode != urcl::SafetyMode::NORMAL &&
      ur_status_.safety_mode != urcl::SafetyMode::REDUCED &&
      ur_status_.safety_mode != urcl::SafetyMode::SAFEGUARD_STOP &&
      ur_status_.safety_mode != urcl::SafetyMode::RECOVERY) {
    return absl::FailedPreconditionError(
        "Can't enable. Please resolve the safety violation first.");
  }

  const absl::Time enable_deadline = absl::Now() + kEnableTimeout;
  try {
    while (ur_status_.robot_mode != urcl::RobotMode::RUNNING) {
      if (absl::Now() >= enable_deadline) {
        return absl::DeadlineExceededError(
            absl::StrCat("Robot failed to enable within: ",
                         absl::FormatDuration(kEnableTimeout)));
      }
      const auto robot_mode = ur_status_.robot_mode;
      switch (robot_mode) {
        case urcl::RobotMode::UNKNOWN:
        case urcl::RobotMode::NO_CONTROLLER:
        case urcl::RobotMode::DISCONNECTED:
          return absl::FailedPreconditionError(
              "Can't enable. Not connected to controller.");
        case urcl::RobotMode::CONFIRM_SAFETY:
          return absl::FailedPreconditionError(
              "Can't enable. Please resolve the safety violation first.");
        case urcl::RobotMode::BOOTING: {
          LOG(INFO) << "Robot is booting";
          break;
        }
        case urcl::RobotMode::POWER_OFF: {
          LOG(INFO) << "Sending PowerOn Command";
          if (!backend_->CommandPowerOn()) {
            return absl::FailedPreconditionError(
                "Could not send power on command");
          }
          break;
        }
        case urcl::RobotMode::POWER_ON: {
          LOG(INFO) << "Sending BrakeRelease Command";
          if (!backend_->CommandBrakeRelease()) {
            return absl::FailedPreconditionError(
                "Could not send BrakeRelease command");
          }
          break;
        }
        case urcl::RobotMode::IDLE: {
          LOG(INFO) << "Robot is idle: Sending BrakeRelease Command";
          if (!backend_->CommandBrakeRelease()) {
            return absl::FailedPreconditionError(
                "Could not send BrakeRelease command");
          }
          break;
        }
        case urcl::RobotMode::BACKDRIVE: {
          LOG(INFO) << "Sending PowerOff Command";
          if (!backend_->CommandPowerOff()) {
            return absl::FailedPreconditionError(
                "Could not send power off command");
          }
          break;
        }
        case urcl::RobotMode::RUNNING: {
          break;
        }
        case urcl::RobotMode::UPDATING_FIRMWARE:
          return absl::FailedPreconditionError(
              "The robot is updating its firmware. Try again later.");
      }
      absl::SleepFor(absl::Seconds(1));
    }

    absl::SleepFor(absl::Seconds(1));

    // Robot is in mode urcl::RobotMode::RUNNING
    absl::Time program_running_deadline = absl::Now() + kProgramTimeout;
    while (true) {
      if (absl::Now() >= program_running_deadline) {
        return absl::DeadlineExceededError(
            absl::StrCat("Program failed to run within: ",
                         absl::FormatDuration(kProgramTimeout)));
      }
      INTRINSIC_RT_RETURN_IF_ERROR(GetCyclicLoopStatus());
      const RUNTIME_STATE runtime_state =
          runtime_state_.load(std::memory_order_relaxed);
      if (program_running_ && runtime_state == RUNTIME_STATE::PLAYING) {
        LOG(INFO) << "Program is running and runtime_state is PLAYING.";
        break;
      }

      switch (runtime_state) {
        case RUNTIME_STATE::STOPPING:
          break;
        case RUNTIME_STATE::PAUSED:
        case RUNTIME_STATE::STOPPED: {
          ResendProgramState expected = ResendProgramState::kIdle;
          const bool cas_succeeded =
              resend_robot_program_state_.compare_exchange_strong(
                  expected, ResendProgramState::kSendProgramRequested);

          bool resend_program_succeeded = false;
          if (expected == ResendProgramState::kFailed ||
              expected == ResendProgramState::kCloseConnectionRequested) {
            LOG(WARNING) << "Enable motion: Cannot request program resend, "
                            "current state is ["
                         << ToString(expected) << "]. Triggering recovery.";
          } else {
            if (cas_succeeded) {
              LOG(INFO)
                  << "Enable motion: Requesting robot program resend/resume "
                     "via background thread";
            } else {
              LOG(INFO)
                  << "Enable motion: Program resend already requested or in "
                     "progress (state: ["
                  << ToString(expected) << "]); waiting for state resolution.";
            }

            // 1. Wait for background program resend thread to finish (state ->
            // kIdle, kFailed, or kCloseConnectionRequested).
            const absl::Time wait_deadline = absl::Now() + kProgramTimeout;
            while (absl::Now() < wait_deadline) {
              const ResendProgramState state =
                  resend_robot_program_state_.load();
              if (state == ResendProgramState::kIdle ||
                  state == ResendProgramState::kFailed ||
                  state == ResendProgramState::kCloseConnectionRequested) {
                break;
              }
              absl::SleepFor(absl::Milliseconds(100));
            }

            // 2. Verify program resend completed and robot transitioned to
            // PLAYING state.
            if (resend_robot_program_state_.load() ==
                    ResendProgramState::kIdle &&
                program_running_ &&
                runtime_state_.load(std::memory_order_relaxed) ==
                    RUNTIME_STATE::PLAYING) {
              resend_program_succeeded = true;
            }
          }
          if (!resend_program_succeeded) {
            const auto error = absl::FailedPreconditionError(
                "Failed to send robot program. Setting CriticalError, then "
                "ClearingFaults");
            LOG(WARNING) << error;
            INTR_RETURN_IF_ERROR(CloseRobotConnection(error.message()));
            absl::SleepFor(absl::Seconds(1));
            INTR_RETURN_IF_ERROR(ClearFaults());
            continue;
          }
          LOG(INFO) << "Robot program send successfully.";
          break;
        }
        case RUNTIME_STATE::PLAYING:
          break;
        case RUNTIME_STATE::PAUSING:
          break;
        case RUNTIME_STATE::RESUMING:
          break;
      }
      absl::SleepFor(absl::Seconds(1));
    }
  } catch (const std::exception& e) {
    // Ideally the critical error would always surface before enable motion.
    const auto error = absl::InternalError(
        absl::StrCat("UR driver error while enabling motion: ", e.what()));
    LOG(ERROR) << error;
    INTR_RETURN_IF_ERROR(CloseRobotConnection(error.message()));
    return error;
  }

  INTR_RETURN_IF_ERROR(LogToRobot("Intrinsic: EnableMotion finished."));

  prefilter_->Reset();

  std::optional<RobotPayloadBase> full_payload;
  INTRINSIC_RT_RETURN_IF_ERROR(
      CopyTo(*payload_command_->full_payload(), full_payload));

  if (full_payload.has_value()) {
    double mass = full_payload->mass();
    urcl::vector3d_t center_of_gravity = {
        full_payload->tip_t_cog().translation().x(),
        full_payload->tip_t_cog().translation().y(),
        full_payload->tip_t_cog().translation().z()};

    const std::string msg =
        absl::StrCat("Setting payload to: mass=", mass, " center_of_gravity=[",
                     absl::StrJoin(center_of_gravity, ","), "]");

    LOG(INFO) << "Setting payload to " << msg;
    INTR_RETURN_IF_ERROR(LogToRobot(msg));

    auto set_payload_error = [this](absl::string_view reason = "") {
      std::string msg = "Failed to set payload.";
      if (!reason.empty()) {
        absl::StrAppend(&msg, ": ", reason);
      }
      absl::Status error = absl::FailedPreconditionError(msg);
      if (const auto status = CloseRobotConnection(error.message());
          !status.ok()) {
        error = absl::Status(status.code(), absl::StrCat(error.message(), "; ",
                                                         status.message()));
      }
      return error;
    };
    try {
      if (!backend_->SetPayload(mass, center_of_gravity)) {
        return set_payload_error();
      }
    } catch (const std::exception& e) {
      return set_payload_error(e.what());
    }
  } else {
    LOG(INFO) << "Not setting custom payload. Using payload configured on "
                 "robot controller.";
  }

  // Copy the commanded payload to the state. If no payload was commanded, it is
  // nullopt.
  INTRINSIC_RT_RETURN_IF_ERROR(
      CopyTo(full_payload, *payload_state_->mutable_full_payload()));

  if (config_.joint_friction_compensation_scale_factors.has_value()) {
    const bool supports_friction_scales = backend_->SetFrictionScales(
        config_.joint_friction_compensation_scale_factors.value().viscous_scale,
        config_.joint_friction_compensation_scale_factors.value()
            .coulomb_scale);
    if (!supports_friction_scales) {
      const auto error = absl::FailedPreconditionError(
          "Friction scale parameters were configured but this robot does not "
          "support them. Please ensure the controller Polyscope version is "
          "greater than 5.25.1.");
      LOG(ERROR) << error;
      INTR_RETURN_IF_ERROR(CloseRobotConnection(error.message()));
      return error;
    }
  }
  LOG(INFO) << "Motion enabled";
  return OkStatus();
}

absl::Status UniversalRobotsModule::DisableMotion() {
  LOG(INFO) << "DisableMotion request on " << kHwModuleTypeName;
  LogToRobot("Intrinsic: DisableMotion called.").IgnoreError();
  return OkStatus();
}

absl::Status UniversalRobotsModule::ClearFaults() {
  // Relevant documentation:
  // https://www.universal-robots.com/articles/ur/dashboard-server-e-series-port-29999/
  // https://www.universal-robots.com/articles/ur/application-installation/connecting-an-emergency-stop-device-vs-a-safeguard-protective-device/
  LOG(INFO) << "ClearFaults requested on " << kHwModuleTypeName;

  // Reset any prior kFailed state to kIdle so ReadStatus does not continuously
  // fail while ClearFaults executes. Not checking the return of
  // compare_exchange_strong, because if the state is not kFailed, it is already
  // in a valid state (e.g. kIdle) and no reset is needed. This is safe because:
  // 1) An explicit ClearFaults call gives recovery a clean slate.
  // 2) If the driver connection is invalid, ResendProgramLoop drops pending
  //    resend requests without setting kFailed.
  // 3) If connection re-initialization is required below, the resend lifecycle
  //    will be cleanly re-evaluated once RTDE communication starts.
  ResendProgramState expected_resend = ResendProgramState::kFailed;
  resend_robot_program_state_.compare_exchange_strong(
      expected_resend, ResendProgramState::kIdle);

  bool connection_alive = backend_->IsDriverValid();
  if (connection_alive) {
    try {
      connection_alive = backend_->CommandIsInRemoteControl();
    } catch (const std::exception& e) {
      LOG(WARNING) << "Error occurred while checking remote control state in "
                      "ClearFaults: "
                   << e.what();
      connection_alive = false;
    } catch (...) {
      LOG(ERROR) << "Unknown exception occurred while checking remote control "
                    "state in ClearFaults.";
      connection_alive = false;
    }
  }

  if (const RealtimeStatus cyclic_loop_status = GetCyclicLoopStatus();
      !cyclic_loop_status.ok() || !connection_alive) {
    LOG(INFO) << "Module connection is not alive or in a critical error state. "
              << "Message: " << cyclic_loop_status.message()
              << ". Re-initializing robot connection.";

    INTR_RETURN_IF_ERROR(CloseRobotConnection("ClearFaults"));

    if (const auto status = InitializeRobotConnection(); !status.ok()) {
      // ICON can't recover if the clock is not reset.
      Shutdown().IgnoreError();
      // This is a critical error.
      return absl::AbortedError("Failed to re-initialize robot connection.");
    }

    // Reset resend state to kIdle before starting RTDE communication.
    // This is safe and necessary because:
    // 1) The driver and dashboard connections were just re-initialized, so any
    //    stale resend requests or close-connection flags from before recovery
    //    are invalid and must be cleared.
    // 2) Resetting before StartRTDECommunication() ensures that subsequent
    //    program resend requests triggered by ReadStatus run without being
    //    clobbered mid-flight by ClearFaults.
    resend_robot_program_state_.store(ResendProgramState::kIdle);

    backend_->StartRTDECommunication();
    absl::SleepFor(absl::Seconds(1));
  }

  std::string safety_mode_string = "";
  const absl::Time clear_safety_deadline = absl::Now() + kClearSafetyTimeout;

  if (const auto status = LogToRobot("Intrinsic: ClearFaults called.");
      !status.ok()) {
    INTR_RETURN_IF_ERROR(CloseRobotConnection(status.message()));
    return status;
  }

  try {
    if (!backend_->CommandIsInRemoteControl()) {
      return absl::FailedPreconditionError(
          "Robot is not in remote control mode.");
    }
  } catch (const std::exception& e) {
    return absl::FailedPreconditionError(absl::StrCat(
        "UR driver error during commandIsInRemoteControl: ", e.what()));
  }

  try {
    do {
      if (absl::Now() >= clear_safety_deadline) {
        const auto error = absl::FailedPreconditionError(
            absl::StrCat("Failed to clear safety within ",
                         absl::FormatDuration(kClearSafetyTimeout)));
        LOG(ERROR) << error.message();
        return error;
      }
      LOG(INFO) << "Reading SafetyMode";
      // Reads the safety mode using the dashboard client so we don't rely on
      // potentially old state.
      if (!backend_->CommandSafetyMode(safety_mode_string)) {
        return absl::InternalError("Failed to read SafetyMode.");
      }
      // React to safety status as read from rtde. Sometimes the dashboard
      // returns NORMAL, while the realtime status is Protective Stop.
      if (GetCyclicLoopStatus().ok() &&
          (safety_status_->mode_of_safe_operation() !=
           intrinsic_fbs::ModeOfSafeOperation::AUTOMATIC)) {
        if (!backend_->CommandUnlockProtectiveStop()) {
          return absl::FailedPreconditionError(
              "Failed to unlock protective stop.");
        }
      }
      // React to safety mode as read from the dashboard client. Sometimes
      // the dashboard returns NORMAL, while the realtime status is Protective
      // Stop.
      if (IsInControllableSafetyMode(safety_mode_string)) {
        LOG(INFO) << "Safety mode is " << safety_mode_string
                  << ". Nothing to do";
        break;
      }

      if (safety_mode_string ==
          urcl::safetyModeString(urcl::SafetyMode::PROTECTIVE_STOP)) {
        LOG(INFO) << "Waiting 5s, then Unlocking protective stop";
        // Ensures there are 5s between fault and unlock.
        absl::SleepFor(absl::Seconds(5));
        if (!backend_->CommandUnlockProtectiveStop()) {
          return absl::FailedPreconditionError(
              "Failed to unlock protective stop.");
        }
      } else if (safety_mode_string ==
                     urcl::safetyModeString(
                         urcl::SafetyMode::ROBOT_EMERGENCY_STOP) ||
                 safety_mode_string ==
                     urcl::safetyModeString(
                         urcl::SafetyMode::SYSTEM_EMERGENCY_STOP)) {
        const auto error = absl::FailedPreconditionError(
            "Please clear the EMERGENCY_STOP signal by releasing the E-Stop "
            "button.");

        LOG(WARNING) << error.message();
        return error;
      } else if (safety_mode_string ==
                     urcl::safetyModeString(urcl::SafetyMode::FAULT) ||
                 safety_mode_string ==
                     urcl::safetyModeString(urcl::SafetyMode::VIOLATION)) {
        LOG(INFO) << "Restarting safety";
        if (!backend_->CommandRestartSafety()) {
          return absl::FailedPreconditionError(
              "Failed to commandRestartSafety.");
        }
      } else {
        LOG_EVERY_N_SEC(WARNING, 5)
            << "Unsupported safety mode: " << safety_mode_string;
      }
      absl::SleepFor(absl::Seconds(1));
    } while (!IsInControllableSafetyMode(safety_mode_string));
  } catch (const std::exception& e) {
    const auto error = absl::FailedPreconditionError(
        absl::StrCat("UR driver error during ClearFaults: ", e.what()));
    INTR_RETURN_IF_ERROR(CloseRobotConnection(error.message()));
    return error;
  }

  // Trying to close the safety popup.
  try {
    if (!backend_->CommandCloseSafetyPopup()) {
      LOG(INFO) << "Failed to close safety popup";
    }
  } catch (const std::exception& e) {
    return absl::FailedPreconditionError(absl::StrCat(
        "UR driver error during commandCloseSafetyPopup: ", e.what()));
  }

  INTR_RETURN_IF_ERROR(LogToRobot("Intrinsic: ClearFaults finished."));

  LOG(INFO) << "Faults cleared.";
  return OkStatus();
}

absl::Status UniversalRobotsModule::Shutdown() {
  LOG(INFO) << "Shutdown on " << kHwModuleTypeName;
  LogToRobot("Intrinsic: Shutting down.").IgnoreError();

  if (const auto status = StopRealtimeLoop(); !status.ok()) {
    LOG(WARNING) << "Failed to stop realtime loop during Shutdown: " << status;
  }
  if (const auto status = CloseRobotConnection("Shutdown"); !status.ok()) {
    LOG(WARNING) << "Failed to closing robot connection during shutdown. "
                    "Expect an unclean shutdown.";
  }

  if (const auto status = JoinFailedInitThread(kRetryConnectionTimeout);
      !status.ok()) {
    LOG(WARNING) << "Failed to join failed_init thread during shutdown. "
                    "Expect an unclean shutdown.";
  }

  if (const auto status = realtime_clock_->Reset(kCycleTimeTimeout);
      !status.ok()) {
    LOG(ERROR) << "Failed to reset clock: " << status.message();
  }

  return absl::OkStatus();
}

UniversalRobotsModule::UniversalRobotsModule()
    : backend_(std::make_unique<RtdeUrDriver>()),
      prefilter_(std::make_unique<PassThroughFilter>()) {}

UniversalRobotsModule::UniversalRobotsModule(
    std::unique_ptr<UrDriverInterface> backend)
    : backend_(std::move(backend)),
      prefilter_(std::make_unique<PassThroughFilter>()) {}

UniversalRobotsModule::~UniversalRobotsModule() {
  try {
    urcl::unregisterLogHandler();
  } catch (const std::exception& e) {
    LOG(ERROR) << e.what();
  }
}

RealtimeStatus UniversalRobotsModule::Enabled() {
  pet_watchdog_ = false;
  return OkStatus();
}

RealtimeStatus UniversalRobotsModule::Disabled() {
  pet_watchdog_ = true;
  return OkStatus();
}

// Reads new messages from the robot by looping through:
// 1. Blocks on new message from robot in CyclicRead();
// 2. Ticks timeslicer using TickBlockingWithTimeout
// 3. Pets Watchdog if motion is not enabled.
void UniversalRobotsModule::RealtimeLoop() {
  while (!cancel_runtime_loop_thread_) {
    current_cycle_++;
    // Populated with the real cycle start time below in CyclicRead() if there
    // is a connection to the robot.
    Clock::time_point cycle_start_time = Clock::Now();

    if (reset_clock_) {
      if (const auto status = realtime_clock_->Reset(kCycleTimeTimeout);
          !status.ok()) {
        INTRINSIC_RT_LOG_THROTTLED(ERROR)
            << "Failed to reset clock. With: " << status.message();
      } else {
        INTRINSIC_RT_LOG_THROTTLED(INFO) << "Reset clock.";
      }
      reset_clock_ = false;
      // Sleep to ensure that the clock is reset before the next cycle.
      absl::SleepFor(kCycleTime -
                     absl::Nanoseconds(intrinsic::ToInt64Nanoseconds(
                         Clock::Now() - cycle_start_time)));
      continue;
    }

    if (backend_->IsDriverValid()) {
      try {
        // startRTDECommunication() is a no-op if the communication is already
        // active.
        backend_->StartRTDECommunication();
      } catch (const std::exception& e) {
        INTRINSIC_RT_LOG_THROTTLED(WARNING)
            << "UR driver error during startRTDECommunication in RealtimeLoop: "
            << e.what();
      }
    }

    // The correct start of the cycle is returned by CyclicRead().
    cycle_start_time = Clock::Invalid();
    try {
      const auto cycle_start = CyclicRead();
      if (!cycle_start.status().ok()) {
        cyclic_loop_status_.store(ToCyclicLoopStatus(cycle_start.status()),
                                  std::memory_order_release);
        INTRINSIC_RT_LOG_THROTTLED(WARNING)
            << "UR driver error during CyclicRead: "
            << cycle_start.status().message();
        // TODO(b/383701675): The connection to the robot can be closed here,
        // but it needs to be done in non-real time to keep ticking the clock.
        cycle_start_time = Clock::Now();
      } else {
        cycle_start_time = cycle_start.value();
        // If an external thread requests a stop (e.g. CloseRobotConnection)
        // immediately after this load, RealtimeLoop will complete the current
        // in-flight cycle as OK and latch the error state on the subsequent
        // tick (<= 2ms), well within kCycleTimeTimeout.
        const CyclicLoopRequest req =
            cyclic_loop_request_.load(std::memory_order_acquire);
        CyclicLoopState state = CyclicLoopState::kOk;
        switch (req) {
          case CyclicLoopRequest::kNone:
            state = CyclicLoopState::kOk;
            break;
          case CyclicLoopRequest::kStopSendingCommands:
            state = CyclicLoopState::kStoppedSendingCommands;
            break;
          case CyclicLoopRequest::kProgramNotActive:
            state = CyclicLoopState::kProgramNotActive;
            break;
        }
        cyclic_loop_status_.store(state, std::memory_order_release);
      }
    } catch (const std::exception& e) {
      INTRINSIC_RT_LOG_THROTTLED(WARNING)
          << "UR driver error during CyclicRead: " << e.what();
    }

    // Continues the loop early if the connection to the robot is lost.
    // Ticks ICON if required, sleeps until the next cycle.
    if (cyclic_loop_status_.load(std::memory_order_acquire) !=
        CyclicLoopState::kOk) {
      if (tick_clock_) {
        // Ignoring return status because we lost connection to the robot and
        // ICON may or may not be happy.
        std::ignore =
            TickIconOnErrorResetClock(realtime_clock_, cycle_start_time);
        // TickIconOnErrorResetClock can return immediately and lead to a busy
        // loop.
      }
      absl::SleepFor(kCycleTime -
                     absl::Nanoseconds(intrinsic::ToInt64Nanoseconds(
                         Clock::Now() - cycle_start_time)));
      continue;
    }

    // Only calls TickBlocking after "activate" was called (i.e. ICON is
    // running and connected).
    // cyclic_loop_status_ is ok() here.
    if (tick_clock_) {
      const absl::Time before_tick_blocking = absl::Now();
      // Ignoring errors because the realtime loop can only fault at this point.
      std::ignore =
          TickIconOnErrorResetClock(realtime_clock_, cycle_start_time);

      const absl::Duration tick_blocking = absl::Now() - before_tick_blocking;
      if (tick_blocking > kTickBlockingLogWarning) {
        INTRINSIC_RT_LOG_THROTTLED(WARNING)
            << "Long duration of TickBlocking (ICON control), duration: "
            << absl::ToDoubleMicroseconds(tick_blocking) << " us.";
      }
    }

    // Only sends data if the program is running on the robot.
    // Both RUNTIME_STATE and program_running_are required to understand if
    // the robot can receive commands.
    // cyclic_loop_status_ is ok() here.
    if (program_running_ &&
        ur_status_.runtime_state == RUNTIME_STATE::PLAYING) {
      // `pet_watchdog_` adjusted in Enabled() and Disabled().
      // If ApplyCommand was not called, we need to send a keepalive message.
      if (pet_watchdog_) {
        // Not checking return value, as the RTDE connection may not be
        // active.
        backend_->WriteKeepalive();
      }
    }
  }  // end of realtime loop

  LOG(INFO) << kHwModuleTypeName << " ended realtime loop.";
}

void UniversalRobotsModule::ResendProgramLoop(intrinsic::StopToken stop_token) {
  while (!stop_token.stop_requested()) {
    const ResendProgramState current_state = resend_robot_program_state_.load();

    // Handle connection closure request if program stopped on robot.
    if (current_state == ResendProgramState::kCloseConnectionRequested) {
      ResendProgramState expected =
          ResendProgramState::kCloseConnectionRequested;
      if (resend_robot_program_state_.compare_exchange_strong(
              expected, ResendProgramState::kIdle)) {
        LOG(INFO) << "Background thread: Program stopped on robot. "
                  << "Closing robot connection and resetting ur_driver...";
        CloseRobotConnection("Program stopped on robot").IgnoreError();
        absl::SleepFor(kResendThreadPollInterval);
        continue;
      }
    }

    // Clear pending resend requests if driver connection is invalid.
    if (!backend_->IsDriverValid()) {
      ResendProgramState expected = ResendProgramState::kSendProgramRequested;
      resend_robot_program_state_.compare_exchange_strong(
          expected, ResendProgramState::kIdle);
      absl::SleepFor(kResendThreadPollInterval);
      continue;
    }

    // Atomically claim and process program resend request.
    ResendProgramState expected = ResendProgramState::kSendProgramRequested;
    if (resend_robot_program_state_.compare_exchange_strong(
            expected, ResendProgramState::kSendProgramInProgress)) {
      LOG(INFO) << "Background thread: Resending robot program...";
      // TODO(b/290575595): sendRobotProgram is only supported in headless mode.
      // Optimally we would use a resume call to recover from SS2 stops,
      // but that's not exposed by the driver.

      // Attempt uploading robot script to UR controller.
      bool send_succeeded = false;
      try {
        send_succeeded = backend_->SendRobotProgram();
      } catch (const std::exception& e) {
        LOG(WARNING)
            << "Exception while resending robot program in background thread: "
            << e.what();
        send_succeeded = false;
      }
      if (!send_succeeded) {
        LOG(WARNING) << "Failed to resend robot program in background thread.";
        resend_robot_program_state_.store(ResendProgramState::kFailed);
      } else {
        // Wait for robot controller to transition program to PLAYING state.
        const absl::Time wait_deadline =
            absl::Now() + kResendProgramSpinUpTimeout;
        while (!stop_token.stop_requested() && absl::Now() < wait_deadline) {
          if (program_running_ &&
              runtime_state_.load(std::memory_order_relaxed) ==
                  urcl::rtde_interface::RUNTIME_STATE::PLAYING) {
            break;
          }
          absl::SleepFor(kResendThreadPollInterval);
        }

        // Settle and reset state to kIdle if PLAYING reached; otherwise set
        // kFailed.
        if (program_running_ &&
            runtime_state_.load(std::memory_order_relaxed) ==
                urcl::rtde_interface::RUNTIME_STATE::PLAYING) {
          absl::SleepFor(kResendProgramSettlingTime);
          expected = ResendProgramState::kSendProgramInProgress;
          resend_robot_program_state_.compare_exchange_strong(
              expected, ResendProgramState::kIdle);
        } else {
          LOG(WARNING) << "Robot program failed to reach PLAYING state within "
                       << kResendProgramSpinUpTimeout
                       << " deadline after resending.";
          // Only set kFailed if state is still kSendProgramInProgress. Not
          // checking the return of compare_exchange_strong, because if it is
          // false, ClearFaults or connection closure already reset the state to
          // kIdle and overwriting it with kFailed is intentionally avoided.
          expected = ResendProgramState::kSendProgramInProgress;
          resend_robot_program_state_.compare_exchange_strong(
              expected, ResendProgramState::kFailed);
        }
      }
    }
    absl::SleepFor(kResendThreadPollInterval);
  }
}

absl::Status UniversalRobotsModule::StartRealtimeLoop() {
  LOG(INFO) << "Starting Realtime thread";
  cyclic_loop_status_.store(CyclicLoopState::kUninitialized,
                            std::memory_order_release);
  SetCyclicLoopRequest(CyclicLoopRequest::kNone);
  cancel_runtime_loop_thread_ = false;
  // The provided thread options can be real, or non-realtime.
  intrinsic::ThreadOptions thread_options = config_.module_thread_options;
  thread_options.SetName("UR Realtime");
  runtime_loop_thread_ = std::make_unique<intrinsic::Thread>();

  {
    absl::Notification thread_running;
    INTR_ASSIGN_OR_RETURN(
        *runtime_loop_thread_,
        CreateRealtimeCapableThread(thread_options, [this, &thread_running]() {
          thread_running.Notify();
          RealtimeLoop();
        }));

    thread_running.WaitForNotification();
  }

  {
    absl::Notification thread_running;
    resend_robot_program_state_ = ResendProgramState::kIdle;
    resend_robot_program_thread_ = std::make_unique<intrinsic::Thread>();
    intrinsic::ThreadOptions resend_options;
    resend_options.SetName("UR ResendProg");
    INTR_ASSIGN_OR_RETURN(
        *resend_robot_program_thread_,
        intrinsic::CreateThread(
            resend_options,
            [this, &thread_running](intrinsic::StopToken stop_token) {
              thread_running.Notify();
              ResendProgramLoop(stop_token);
            }));
    thread_running.WaitForNotification();
  }

  reset_clock_ = true;
  absl::Time reset_clock_deadline = absl::Now() + kResetPointersTimeout;

  // Clock is reset by the realtime thread in RealtimeLoop ->
  // TickIconOnErrorResetClock.
  while (reset_clock_) {
    if (absl::Now() > reset_clock_deadline) {
      return DeadlineExceededError("Failed to reset clock within timeout.");
    }
    // Some sleep is required to not burn the CPU.
    absl::SleepFor(kCycleTime / 4);
  }

  return absl::OkStatus();
}

absl::Status UniversalRobotsModule::CloseRobotConnection(
    absl::string_view message) {
  // Not stopping RT loop, as ICON needs to be ticked.
  INTRINSIC_RT_LOG_THROTTLED(WARNING)
      << "Closing robot connection because: " << message;

  program_running_ = false;
  SetCyclicLoopRequest(CyclicLoopRequest::kStopSendingCommands);

  // Allow the realtime loop thread to finish its current cycle and see
  // cyclic_loop_status_ != ok before we reset the driver pointers.
  if (runtime_loop_thread_ && runtime_loop_thread_->joinable() &&
      !cancel_runtime_loop_thread_) {
    const absl::Time deadline = absl::Now() + kCycleTimeTimeout;
    bool status_ok = GetCyclicLoopStatus().ok();
    while (status_ok && !cancel_runtime_loop_thread_ &&
           absl::Now() < deadline) {
      absl::SleepFor(kCyclicLoopStatusPollInterval);
      status_ok = GetCyclicLoopStatus().ok();
    }
    if (status_ok && !cancel_runtime_loop_thread_) {
      INTRINSIC_RT_LOG_THROTTLED(ERROR)
          << "Realtime loop failed to acknowledge error state within "
          << kCycleTimeTimeout << " before resetting driver pointers.";
    }
  }

  try {
    if (backend_->IsDriverValid() && !backend_->StopControl()) {
      LOG(WARNING) << "Failed to stop control.";
    }

    backend_->DisconnectDashboard();
  } catch (const std::exception& e) {
    LOG(ERROR) << "Error when closing robot connection: " << e.what();
  }

  try {
    backend_->ResetDriver();
    backend_->ResetDashboard();
  } catch (const std::exception& e) {
    return absl::AbortedError(
        absl::StrCat("Error when resetting robot pointers: ", e.what()));
  }

  LOG(INFO) << "Robot pointers have been reset.";
  return absl::OkStatus();
}

absl::Status UniversalRobotsModule::StopRealtimeLoop() {
  tick_clock_ = false;
  cancel_runtime_loop_thread_ = true;
  if (runtime_loop_thread_ && runtime_loop_thread_->joinable()) {
    runtime_loop_thread_->join();
  }
  if (resend_robot_program_thread_ &&
      resend_robot_program_thread_->joinable()) {
    resend_robot_program_thread_->request_stop();
    resend_robot_program_thread_->join();
  }
  return absl::OkStatus();
}

absl::Status UniversalRobotsModule::InitializeRobotConnection() {
  INTR_RETURN_IF_ERROR(JoinFailedInitThread(kRetryConnectionTimeout));
  const std::string error_message =
      absl::StrCat("Failed to connect to ", config_.robot_ip, " in ",
                   absl::FormatDuration(kRetryConnectionTimeout),
                   ". Ensure the robot is powered on and the IP is correct.");
  // Stores the init_thread in case it gets stuck because returning without
  // joining crashes the module.
  // Can't immediately join the thread because
  // google3/src/comm/tcp_socket.cpp
  // is stuck for minutes in the worst case. Releasing the thread, correctly
  // leads to leak checker failures.
  auto init_context = std::make_unique<ThreadAndStatus>();
  init_context->status = absl::DeadlineExceededError(error_message);
  // Pointer copy remains stable in the lambda capture after moving the
  // init_context.
  ThreadAndStatus* context_ptr = init_context.get();
  LOG(INFO) << "Initializing robot connection";
  // Starts the UR driver in a separate thread to set the CPU affinity without
  // influencing the non realtime module thread.
  // Not setting realtime priority because that leads to all UR threads
  // running with realtime priority.
  // Not setting a thread name to use the ones set by UR driver.
  INTR_ASSIGN_OR_RETURN(
      *init_context->thread,
      intrinsic::CreateThread(ThreadOptions().SetAffinity(
                                  config_.module_thread_options.GetCpuSet()),
                              [this, context_ptr]() {
                                context_ptr->status = ConnectToRobot(config_);
                                if (!context_ptr->status.ok()) {
                                  LOG(ERROR) << "Failed to connect: "
                                             << context_ptr->status;
                                }
                                context_ptr->has_returned.Notify();
                              }));
  if (!init_context->has_returned.WaitForNotificationWithTimeout(
          kRetryConnectionTimeout)) {
    absl::MutexLock l(init_thread_mutex_);
    failed_init_thread_disposal_ = std::move(init_context);
    // More specific message takes precedence.
    INTR_RETURN_IF_ERROR(failed_init_thread_disposal_->status);
    return absl::DeadlineExceededError(error_message);
  }
  init_context->thread->join();
  INTR_RETURN_IF_ERROR(init_context->status);
  SetCyclicLoopRequest(CyclicLoopRequest::kNone);

  return absl::OkStatus();
}

absl::Status UniversalRobotsModule::Init(
    HardwareModuleInitContext& init_context) {
  INTRINSIC_ASSERT_NON_REALTIME();
  icon::HardwareInterfaceRegistry& interface_registry =
      init_context.GetInterfaceRegistry();
  const icon::ModuleConfig& config = init_context.GetModuleConfig();

  // Validate that, if the ModuleConfig has a control period, it's equivalent to
  // kCycleTime.
  INTR_ASSIGN_OR_RETURN(auto control_period, config.GetControlPeriod());
  if ((absl::FDivDuration(control_period, absl::Seconds(1)) -
       kControlFrequencyHz) > 1e-6) {
    return absl::InvalidArgumentError(absl::StrCat(
        "HardwareModuleConfig specifies a control period (", control_period,
        ") that is different from what Universal Robots support (", kCycleTime,
        ", i.e. ", kControlFrequencyHz, "Hz)"));
  }

  // RTDE lib allocates memory on the heap.
  init_context.DisableMallocGuard();

  // Register service in any case so that the gRPC service exists and returns a
  // nicer error message if init fails.
  init_context.RegisterGrpcService(calibration_service_);

  INTR_ASSIGN_OR_RETURN(
      joint_position_command_,
      interface_registry
          .AdvertiseInterface<intrinsic_fbs::JointPositionCommand>(
              kJointPositionCommandName, kNumJoints));
  INTR_ASSIGN_OR_RETURN(
      joint_torque_command_,
      interface_registry.AdvertiseInterface<JointTorqueCommand>(
          kJointTorqueCommandName, kNumJoints));
  INTR_ASSIGN_OR_RETURN(
      joint_acceleration_command_,
      interface_registry.AdvertiseInterface<JointAccelerationAndTorqueCommand>(
          kJointAccelerationCommandName, kNumJoints));
  INTR_ASSIGN_OR_RETURN(
      joint_position_state_,
      interface_registry.AdvertiseMutableInterface<JointPositionState>(
          "joint_position_state", kNumJoints));
  INTR_ASSIGN_OR_RETURN(
      joint_velocity_state_,
      interface_registry.AdvertiseMutableInterface<JointVelocityState>(
          "joint_velocity_state", kNumJoints));

  // Only the size is required.
  std::vector<std::string> eight_bit_descriptions = CreateDescriptions(8);
  INTR_ASSIGN_OR_RETURN(
      standard_digital_output_command_,
      interface_registry.AdvertiseStrictInterface<intrinsic_fbs::DIOCommand>(
          kStandardDigitalOutputCommandName, eight_bit_descriptions));
  INTR_ASSIGN_OR_RETURN(
      standard_digital_output_status_,
      interface_registry.AdvertiseMutableInterface<intrinsic_fbs::DIOStatus>(
          "standard_digital_output_status", eight_bit_descriptions));
  INTR_ASSIGN_OR_RETURN(
      standard_digital_input_,
      interface_registry.AdvertiseMutableInterface<intrinsic_fbs::DIOStatus>(
          "standard_digital_input_status", eight_bit_descriptions));
  INTR_ASSIGN_OR_RETURN(
      configurable_digital_output_command_,
      interface_registry.AdvertiseStrictInterface<intrinsic_fbs::DIOCommand>(
          kConfigurableDigitalOutputCommandName, eight_bit_descriptions));
  INTR_ASSIGN_OR_RETURN(
      configurable_digital_output_status_,
      interface_registry.AdvertiseMutableInterface<intrinsic_fbs::DIOStatus>(
          "configurable_digital_output_status", eight_bit_descriptions));
  INTR_ASSIGN_OR_RETURN(
      configurable_digital_input_,
      interface_registry.AdvertiseMutableInterface<intrinsic_fbs::DIOStatus>(
          "configurable_digital_input_status", eight_bit_descriptions));

  // Only the size is required.
  std::vector<std::string> two_bit_descriptions = CreateDescriptions(2);
  INTR_ASSIGN_OR_RETURN(
      tool_digital_output_command_,
      interface_registry.AdvertiseStrictInterface<intrinsic_fbs::DIOCommand>(
          kToolDigitalOutputCommandName, two_bit_descriptions));
  INTR_ASSIGN_OR_RETURN(
      tool_digital_output_status_,
      interface_registry.AdvertiseMutableInterface<intrinsic_fbs::DIOStatus>(
          "tool_digital_output_status", two_bit_descriptions));
  INTR_ASSIGN_OR_RETURN(
      tool_digital_input_,
      interface_registry.AdvertiseMutableInterface<intrinsic_fbs::DIOStatus>(
          "tool_digital_input_status", two_bit_descriptions));

  INTR_ASSIGN_OR_RETURN(
      analog_input_status_,
      interface_registry.AdvertiseMutableInterface<intrinsic_fbs::AIOStatus>(
          "analog_input_status", two_bit_descriptions));

  INTR_ASSIGN_OR_RETURN(
      ft_command_, interface_registry.AdvertiseInterface<ForceTorqueCommand>(
                       kForceTorqueCommandName));
  INTR_ASSIGN_OR_RETURN(
      ft_status_,
      interface_registry.AdvertiseMutableInterface<ForceTorqueStatus>(
          "force_torque_status"));

  INTR_ASSIGN_OR_RETURN(
      safety_status_,
      interface_registry.AdvertiseMutableInterface<SafetyStatusMessage>(
          "safety_status",
          /*mode_of_safe_operation=*/ModeOfSafeOperation::AUTOMATIC,
          /*estop_button_status=*/ButtonStatus::UNKNOWN,
          /*enable_button_status=*/ButtonStatus::UNKNOWN,
          /*requested_behavior=*/RequestedBehavior::UNKNOWN));

  INTR_ASSIGN_OR_RETURN(
      robot_controller_status_,
      interface_registry
          .AdvertiseMutableInterface<intrinsic_fbs::RobotControllerStatus>(
              "robot_controller_status"));

  INTR_ASSIGN_OR_RETURN(
      payload_command_,
      interface_registry.AdvertiseInterface<intrinsic_fbs::PayloadCommand>(
          "payload_command"));

  INTR_ASSIGN_OR_RETURN(
      payload_state_,
      interface_registry.AdvertiseMutableInterface<intrinsic_fbs::PayloadState>(
          "payload_state"));

  INTR_ASSIGN_OR_RETURN(
      process_wrench_command_,
      interface_registry.AdvertiseInterface<intrinsic_fbs::Wrench>(
          "process_wrench_command"));

  INTR_ASSIGN_OR_RETURN(command_validator_,
                        Validator::Create(interface_registry));

  realtime_clock_ = config.GetRealtimeClock();
  QCHECK(realtime_clock_ != nullptr)
      << kHwModuleTypeName
      << " is expecting to drive the clock but the "
         "shared clock is not available.";

  INTR_ASSIGN_OR_RETURN(
      const auto proto_config,
      config.GetConfig<intrinsic_proto::icon::UniversalRobotsModuleConfig>());

  INTR_ASSIGN_OR_RETURN(config_,
                        UniversalRobotsModule::Config::FromProto(proto_config));
  advanced_control_options_ = proto_config;

  config_.module_thread_options = config.GetIconThreadOptions();
  // The UR module cannot be used with malloc guard, since it uses the rtde lib
  // which allocates memory.
  config_.module_thread_options.SetMallocGuarded(false);
  LOG(INFO) << "UR Module Config: " << config_;

  init_context.EnableCycleTimeMetrics(kCycleTime,
                                      /*log_cycle_time_warnings=*/true);

  LOG(INFO) << kHwModuleTypeName << " initialized.";
  return absl::OkStatus();
}

icon::RealtimeStatus UniversalRobotsModule::ReadStatus() {
  // Returns an error if reading from the robot in RealtimeLoop failed.
  if (const RealtimeStatus cyclic_loop_status = GetCyclicLoopStatus();
      !cyclic_loop_status.ok()) {
    INTRINSIC_RT_LOG_THROTTLED(WARNING)
        << "Cyclic loop reported error: " << cyclic_loop_status.message();
    return cyclic_loop_status;
  }

  if (resend_robot_program_state_.load() == ResendProgramState::kFailed) {
    return FailedPreconditionError("Failed to resend robot program");
  }

  UpdateSafetyStatus(ur_status_, safety_status_);

  const RUNTIME_STATE current_runtime_state =
      runtime_state_.load(std::memory_order_relaxed);
  if ((ur_status_.safety_mode == urcl::SafetyMode::NORMAL ||
       ur_status_.safety_mode == urcl::SafetyMode::REDUCED) &&
      (current_runtime_state == RUNTIME_STATE::PAUSED ||
       current_runtime_state == RUNTIME_STATE::STOPPED || !program_running_)) {
    // Resuming program execution (e.g., after an SS2 stop) may already have
    // requested resending the robot program.
    ResendProgramState expected = ResendProgramState::kIdle;
    if (!resend_robot_program_state_.compare_exchange_strong(
            expected, ResendProgramState::kSendProgramRequested)) {
      INTRINSIC_RT_LOG_THROTTLED(INFO)
          << "ReadStatus: Program resend already requested or in state ["
          << ToString(expected) << "]";
    }
  }

  robot_controller_status_->mutate_speed_scaling(ur_status_.speed_scaling);
  robot_controller_status_.UpdatedAt(intrinsic::Clock::Now());
  // TODO(b/467225110): Export correctly.
  if (ur_status_.speed_scaling != 1) {
    INTRINSIC_RT_LOG_THROTTLED(INFO)
        << "'speed_scaling': value [" << ur_status_.speed_scaling << "]";
  }

  CopyFbsVector(ur_status_.joint_positions,
                *joint_position_state_->mutable_position());
  joint_position_state_.UpdatedAt(intrinsic::Clock::Now());
  CopyFbsVector(ur_status_.joint_velocities,
                *joint_velocity_state_->mutable_velocity());
  joint_velocity_state_.UpdatedAt(intrinsic::Clock::Now());

  const Wrench wrench_at_ft = FTWrenchFromUrTcpForce(
      /*actual_tcp_pose=*/ur_status_.actual_tcp_pose,
      /*actual_tcp_force=*/ur_status_.actual_tcp_force);

  ft_status_->mutable_wrench()->mutate_x(wrench_at_ft[0]);
  ft_status_->mutable_wrench()->mutate_y(wrench_at_ft[1]);
  ft_status_->mutable_wrench()->mutate_z(wrench_at_ft[2]);
  ft_status_->mutable_wrench()->mutate_rx(wrench_at_ft[3]);
  ft_status_->mutable_wrench()->mutate_ry(wrench_at_ft[4]);
  ft_status_->mutable_wrench()->mutate_rz(wrench_at_ft[5]);
  ft_status_->mutate_status_code(intrinsic_fbs::ForceSensorStatusCode::Ok);
  ft_status_->mutate_raw_status_code(0);
  ft_status_->mutate_enabled(true);
  ft_status_.UpdatedAt(intrinsic::Clock::Now());

  // Assumes that taring only takes one cycle as the ur_driver doesn't seem to
  // expose the taring status.
  // TODO(b/290925601): UR module: Understand/Improve taring behavior
  ur_status_.taring_done = true;
  ur_status_.taring_in_progress = false;
  ft_status_->mutate_retare_completed(ur_status_.taring_done);
  ft_status_.UpdatedAt(intrinsic::Clock::Now());

  // 0-7: Standard,
  // 8-15: Configurable,
  for (size_t i = 0; i < 8; ++i) {
    standard_digital_output_status_->mutable_signals()
        ->GetMutableObject(i)
        ->mutate_value(ur_status_.digital_output_bits[i]);
    standard_digital_input_->mutable_signals()
        ->GetMutableObject(i)
        ->mutate_value(ur_status_.digital_input_bits[i]);

    configurable_digital_output_status_->mutable_signals()
        ->GetMutableObject(i)
        ->mutate_value(ur_status_.digital_output_bits[i + 8]);
    configurable_digital_input_->mutable_signals()
        ->GetMutableObject(i)
        ->mutate_value(ur_status_.digital_input_bits[i + 8]);
  }
  standard_digital_output_status_.UpdatedAt(intrinsic::Clock::Now());
  standard_digital_input_.UpdatedAt(intrinsic::Clock::Now());
  configurable_digital_output_status_.UpdatedAt(intrinsic::Clock::Now());
  configurable_digital_input_.UpdatedAt(intrinsic::Clock::Now());

  // 16-17: Tool
  for (size_t i = 0; i < 2; ++i) {
    tool_digital_output_status_->mutable_signals()
        ->GetMutableObject(i)
        ->mutate_value(ur_status_.digital_output_bits[i + 16]);
    tool_digital_input_->mutable_signals()->GetMutableObject(i)->mutate_value(
        ur_status_.digital_input_bits[i + 16]);
  }
  tool_digital_output_status_.UpdatedAt(intrinsic::Clock::Now());
  tool_digital_input_.UpdatedAt(intrinsic::Clock::Now());

  for (size_t i = 0; i < 2; ++i) {
    analog_input_status_->mutable_signals()->GetMutableObject(i)->mutate_value(
        ur_status_.standard_analog_input[i]);
  }
  analog_input_status_.UpdatedAt(intrinsic::Clock::Now());

  if (safety_status_->mode_of_safe_operation() !=
          intrinsic_fbs::ModeOfSafeOperation::AUTOMATIC &&
      safety_status_->mode_of_safe_operation() !=
          intrinsic_fbs::ModeOfSafeOperation::UNKNOWN) {
    return FailedPreconditionError(
        FixedStrCat<RealtimeStatus::kMaxMessageLength>(
            "Robot is safety stopped. ModeOfSafeOperation: ",
            intrinsic_fbs::EnumNameModeOfSafeOperation(
                safety_status_->mode_of_safe_operation())));
  }

  return OkStatus();
}

absl::Status UniversalRobotsModule::ProvideInspectionData(
    intrinsic_proto::icon::v1::HardwareModuleInspectionData& data) {
  if (!backend_->IsDriverValid()) {
    return absl::OkStatus();
  }

  intrinsic::InvalidUntilSet<CyclicReadData>* data_package;
  inspection_data_.last_data_package.GetActiveBuffer(&data_package);
  if (data_package && data_package->has_value()) {
    *data.mutable_safety_status() =
        ToProto(ExtractSafetyStatus(data_package->value()));
  }

  const auto errors = backend_->GetErrorCodes();
  for (auto& error : errors) {
    const auto& error_code_map = UrErrorCodeToStringMapSingleton();
    const auto error_code_it = error_code_map.find(error.message_code);
    absl::string_view error_message = "Unknown error code";
    if (error_code_it != error_code_map.end()) {
      error_message = error_code_it->second;
    }

    // Concatenate error code and human readable string representation.
    // UR error codes are all prefixed with a C.
    const std::string composed_error_message =
        absl::StrCat("C", error.message_code, ": ", error_message);

    // Do not repeat the same message. The robot reports the exact same error
    // code for each joint in many cases, which does not really provide more
    // information to the user and clutters the event history.
    // UR does not provide information on which joint the error occurred.
    if (!inspection_data_.event_history.empty() &&
        inspection_data_.event_history.front().message() ==
            composed_error_message) {
      continue;
    }

    intrinsic_proto::icon::v1::Event event;
    event.set_message(composed_error_message);
    if (const auto status =
            intrinsic::FromAbslTime(absl::Now(), event.mutable_timestamp());
        !status.ok()) {
      LOG_EVERY_N_SEC(ERROR, 10)
          << "Failed to set inspection timestamp: " << status;
    }
    event.set_severity(ReportLevelToSeverity(error.report_level));

    inspection_data_.event_history.push_front(event);
  }

  // Keep the event history to a defined size.
  if (inspection_data_.event_history.size() > kEventHistoryMaxSize) {
    inspection_data_.event_history.resize(kEventHistoryMaxSize);
  }

  // Copy the history to the protobuf repeated container.
  for (const auto& event : inspection_data_.event_history) {
    *data.mutable_event_history()->add_events() = event;
  }

  return absl::OkStatus();
}

}  // namespace intrinsic::icon

// Register the interfaces used by the module
namespace intrinsic::icon::hardware_interface_traits {
INTRINSIC_ADD_HARDWARE_INTERFACE(intrinsic_fbs::JointPositionCommand,
                                 intrinsic_fbs::BuildJointPositionCommand,
                                 "intrinsic_fbs.JointPositionCommand")

INTRINSIC_ADD_HARDWARE_INTERFACE(intrinsic_fbs::JointTorqueCommand,
                                 intrinsic_fbs::BuildJointTorqueCommand,
                                 "intrinsic_fbs.JointTorqueCommand")

INTRINSIC_ADD_HARDWARE_INTERFACE(
    intrinsic_fbs::JointAccelerationAndTorqueCommand,
    intrinsic_fbs::BuildJointAccelerationAndTorqueCommand,
    "intrinsic_fbs.JointAccelerationAndTorqueCommand")

INTRINSIC_ADD_HARDWARE_INTERFACE(intrinsic_fbs::JointPositionState,
                                 intrinsic_fbs::BuildJointPositionState,
                                 "intrinsic_fbs.JointPositionState")

INTRINSIC_ADD_HARDWARE_INTERFACE(intrinsic_fbs::JointVelocityState,
                                 intrinsic_fbs::BuildJointVelocityState,
                                 "intrinsic_fbs.JointVelocityState")

INTRINSIC_ADD_HARDWARE_INTERFACE(intrinsic_fbs::SafetyStatusMessage,
                                 intrinsic_fbs::BuildSafetyStatusMessage,
                                 "intrinsic_fbs.SafetyStatus")

INTRINSIC_ADD_HARDWARE_INTERFACE(intrinsic_fbs::ForceTorqueCommand,
                                 intrinsic_fbs::CreateFbsForceTorqueCommand,
                                 "intrinsic_fbs.ForceTorqueCommand")

INTRINSIC_ADD_HARDWARE_INTERFACE(intrinsic_fbs::ForceTorqueStatus,
                                 intrinsic_fbs::CreateFbsForceTorqueStatus,
                                 "intrinsic_fbs.ForceTorqueStatus")

INTRINSIC_ADD_HARDWARE_INTERFACE(intrinsic_fbs::Wrench,
                                 intrinsic_fbs::CreateWrenchBuffer,
                                 "intrinsic_fbs.Wrench")

INTRINSIC_ADD_HARDWARE_INTERFACE(intrinsic_fbs::DIOStatus,
                                 intrinsic_fbs::BuildDIOStatus,
                                 "intrinsic_fbs.DigitalInputStatus")

INTRINSIC_ADD_HARDWARE_INTERFACE(intrinsic_fbs::AIOStatus,
                                 intrinsic_fbs::BuildAIOStatus,
                                 "intrinsic_fbs.AnalogInputStatus")

INTRINSIC_ADD_HARDWARE_INTERFACE(intrinsic_fbs::DIOCommand,
                                 intrinsic_fbs::BuildDIOCommand,
                                 "intrinsic_fbs.DigitalOutputCommand")

INTRINSIC_ADD_HARDWARE_INTERFACE(intrinsic_fbs::RobotControllerStatus,
                                 intrinsic_fbs::BuildRobotControllerStatus,
                                 "intrinsic_fbs.RobotControllerStatus")

INTRINSIC_ADD_HARDWARE_INTERFACE(intrinsic_fbs::PayloadCommand,
                                 intrinsic_fbs::BuildPayloadCommand,
                                 "intrinsic_fbs.PayloadCommand")

INTRINSIC_ADD_HARDWARE_INTERFACE(intrinsic_fbs::PayloadState,
                                 intrinsic_fbs::BuildPayloadState,
                                 "intrinsic_fbs.PayloadState")

}  // namespace intrinsic::icon::hardware_interface_traits

REGISTER_HARDWARE_MODULE(icon::UniversalRobotsModule);
