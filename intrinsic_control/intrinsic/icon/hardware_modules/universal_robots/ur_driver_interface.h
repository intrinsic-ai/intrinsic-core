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

#ifndef INTRINSIC_ICON_HARDWARE_MODULES_UNIVERSAL_ROBOTS_UR_DRIVER_INTERFACE_H_
#define INTRINSIC_ICON_HARDWARE_MODULES_UNIVERSAL_ROBOTS_UR_DRIVER_INTERFACE_H_

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "include/ur_client_library/comm/control_mode.h"
#include "include/ur_client_library/primary/robot_message/error_code_message.h"
#include "include/ur_client_library/rtde/data_package.h"
#include "include/ur_client_library/types.h"
#include "include/ur_client_library/ur/datatypes.h"
#include "include/ur_client_library/ur/robot_receive_timeout.h"

namespace urcl::primary_interface {
struct ErrorCode;
}  // namespace urcl::primary_interface

namespace intrinsic::icon {

// Configuration options for initializing the Universal Robots driver.
//
// This struct serves as an Intrinsic domain abstraction rather than using
// upstream `urcl::UrDriverConfiguration` directly:
// 1. Decoupling: Keeps `UrDriverInterface` and mock drivers (e.g.
// `FakeUrDriver`)
//    vendor-neutral without pulling in heavy URCL driver headers.
// 2. Compile-time safety: Uses `ABSL_REQUIRE_EXPLICIT_INIT` so callers must
//    explicitly supply required fields (e.g. `robot_ip`, recipes, callback).
// 3. Intrinsic invariants: Enforces Intrinsic-specific default values (such as
//    reverse communication ports and lookahead time tuned for
//    `DynamicPrefilter`).
// Concrete drivers (like `RtdeUrDriver`) map these fields to upstream
// `urcl::UrDriverConfiguration` internally during initialization.
struct UrDriverConfig {
  // IP address of the robot controller to connect to.
  std::string robot_ip ABSL_REQUIRE_EXPLICIT_INIT;

  // Path to the URScript file to be uploaded and executed on the robot
  // controller.
  std::string script_file ABSL_REQUIRE_EXPLICIT_INIT;

  // Path to the recipe file defining RTDE output fields exported by the robot.
  std::string output_recipe ABSL_REQUIRE_EXPLICIT_INIT;

  // Path to the recipe file defining RTDE input fields accepted by the robot.
  std::string input_recipe ABSL_REQUIRE_EXPLICIT_INIT;

  // Callback function invoked when the robot program running state changes.
  std::function<void(bool)> handle_program_state ABSL_REQUIRE_EXPLICIT_INIT;

  // If true, the driver executes in headless mode directly uploading the script
  // via the primary interface instead of relying on the PolyScope URCap.
  bool headless_mode = true;

  // Reverse port on the host used for low-latency bidirectional communication
  // (control commands and keepalive signals from the robot).
  uint32_t reverse_port = 7141;

  // Port on the host that serves the URScript program to the robot controller.
  uint32_t script_sender_port = 7142;

  // Port on the host used to stream trajectory points to the robot.
  uint32_t trajectory_port = 7143;

  // Reverse port on the host used to send arbitrary script commands to the
  // robot.
  uint32_t script_command_port = 7144;

  // Proportional servo gain [100, 2000] for position tracking in servoj calls.
  int servoj_gain = 2000;

  // Lookahead time [0.03, 0.2] seconds used by servoj to smooth trajectory
  // tracking.
  double servoj_lookahead_time = 0.1;

  // Whether RTDE reads should be non-blocking. If false, reads block until
  // a new RTDE synchronization package arrives from the robot.
  bool non_blocking_read = false;

  // Host IP address bound for reverse communication. If empty, the driver uses
  // the local interface address connected to the robot controller.
  std::string reverse_ip = "";

  // Bounded timeouts and retry policies to fail fast during driver
  // initialization if the robot is unreachable or powered off.

  // Maximum time to wait for initial TCP socket connection to the robot.
  std::chrono::milliseconds socket_connect_timeout =
      std::chrono::milliseconds(2000);

  // Number of socket reconnection attempts before aborting.
  size_t socket_reconnect_attempts = 1;

  // Delay between socket reconnection attempts.
  std::chrono::milliseconds socket_reconnection_timeout =
      std::chrono::milliseconds(500);

  // Number of attempts to negotiate and initialize the RTDE protocol handshake.
  size_t rtde_initialization_attempts = 2;

  // Delay between RTDE protocol initialization attempts.
  std::chrono::milliseconds rtde_initialization_timeout =
      std::chrono::milliseconds(500);
};

// Interface abstraction for the Universal Robots hardware driver backend.
class UrDriverInterface {
 public:
  virtual ~UrDriverInterface() = default;

  // Dashboard Client Control Interface

  // Connects to the robot dashboard server on port 29999.
  //
  // `timeout`: Maximum duration to wait for the TCP socket connection.
  // The default 2000 ms bound ensures initialization fails fast if the robot
  // is powered off or unreachable, avoiding Linux TCP SYN retransmission hangs
  // (typically 20-127 seconds).
  virtual absl::Status ConnectDashboard(
      absl::string_view robot_ip,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) = 0;
  virtual void DisconnectDashboard() = 0;
  virtual void ResetDashboard() = 0;
  virtual bool CommandAddToLog(absl::string_view message) = 0;
  virtual bool CommandIsInRemoteControl() = 0;
  virtual bool CommandStop() = 0;
  virtual bool CommandGetRobotModel(std::string& robot_model) = 0;
  virtual bool CommandGetSerialNumber(std::string& serial_number) = 0;
  virtual bool CommandPowerOn() = 0;
  virtual bool CommandBrakeRelease() = 0;
  virtual bool CommandPowerOff() = 0;
  virtual bool CommandSafetyMode(std::string& safety_mode_string) = 0;
  virtual bool CommandUnlockProtectiveStop() = 0;
  virtual bool CommandRestartSafety() = 0;
  virtual bool CommandCloseSafetyPopup() = 0;

  // Primary & RTDE Driver Interface
  virtual absl::Status InitializeDriver(const UrDriverConfig& config) = 0;

  virtual void ResetDriver() = 0;
  virtual bool IsDriverValid() const = 0;
  virtual bool CheckCalibration(absl::string_view checksum) = 0;
  virtual urcl::VersionInformation GetVersion() = 0;
  virtual double GetControlFrequency() = 0;

  // High-Frequency Control Loops
  virtual std::unique_ptr<urcl::rtde_interface::DataPackage>
  GetDataPackage() = 0;
  virtual bool WriteJointCommand(const urcl::vector6d_t& values,
                                 urcl::comm::ControlMode mode,
                                 urcl::RobotReceiveTimeout timeout) = 0;
  virtual void WriteKeepalive() = 0;
  virtual bool StopControl() = 0;

  // Digital I/O Streams
  virtual bool SendStandardDigitalOutput(uint8_t output_mask,
                                         uint8_t output_bits) = 0;
  virtual bool SendConfigurableDigitalOutput(uint8_t output_mask,
                                             uint8_t output_bits) = 0;
  virtual bool SendToolDigitalOutput(uint8_t output_mask,
                                     uint8_t output_bits) = 0;

  // External Sensor Streams
  virtual bool SendExternalForceTorque(
      const urcl::vector6d_t& total_wrench) = 0;
  virtual void FtRtdeInputEnable(bool enable) = 0;
  virtual bool ZeroFTSensor() = 0;

  // Payload & Scales
  virtual bool SendRobotProgram() = 0;
  virtual bool SetPayload(double mass,
                          const urcl::vector3d_t& center_of_gravity) = 0;
  virtual bool SetFrictionScales(const urcl::vector6d_t& viscous,
                                 const urcl::vector6d_t& coulomb) = 0;

  // Comm Management
  virtual void StartRTDECommunication() = 0;
  virtual std::deque<urcl::primary_interface::ErrorCode> GetErrorCodes() = 0;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_HARDWARE_MODULES_UNIVERSAL_ROBOTS_UR_DRIVER_INTERFACE_H_
