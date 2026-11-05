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

#ifndef INTRINSIC_ICON_HARDWARE_MODULES_UNIVERSAL_ROBOTS_RTDE_UR_DRIVER_H_
#define INTRINSIC_ICON_HARDWARE_MODULES_UNIVERSAL_ROBOTS_RTDE_UR_DRIVER_H_

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "include/ur_client_library/comm/control_mode.h"
#include "include/ur_client_library/primary/robot_message/error_code_message.h"
#include "include/ur_client_library/rtde/data_package.h"
#include "include/ur_client_library/types.h"
#include "include/ur_client_library/ur/dashboard_client.h"
#include "include/ur_client_library/ur/datatypes.h"
#include "include/ur_client_library/ur/robot_receive_timeout.h"
#include "include/ur_client_library/ur/ur_driver.h"
#include "intrinsic/icon/hardware_modules/universal_robots/ur_driver_interface.h"

namespace intrinsic::icon {

// Concrete implementation of the Universal Robots hardware driver backend
// interface.
class RtdeUrDriver : public UrDriverInterface {
 public:
  RtdeUrDriver() = default;
  ~RtdeUrDriver() override;

  // Dashboard Client Control Interface
  absl::Status ConnectDashboard(absl::string_view robot_ip) override;
  void DisconnectDashboard() override;
  void ResetDashboard() override;
  bool CommandAddToLog(absl::string_view message) override;
  bool CommandIsInRemoteControl() override;
  bool CommandStop() override;
  bool CommandGetRobotModel(std::string& robot_model) override;
  bool CommandGetSerialNumber(std::string& serial_number) override;
  bool CommandPowerOn() override;
  bool CommandBrakeRelease() override;
  bool CommandPowerOff() override;
  bool CommandSafetyMode(std::string& safety_mode_string) override;
  bool CommandUnlockProtectiveStop() override;
  bool CommandRestartSafety() override;
  bool CommandCloseSafetyPopup() override;

  // Primary & RTDE Driver Interface
  absl::Status InitializeDriver(const UrDriverConfig& config) override;

  void ResetDriver() override;
  bool IsDriverValid() const override;
  bool CheckCalibration(absl::string_view checksum) override;
  urcl::VersionInformation GetVersion() override;
  double GetControlFrequency() override;

  // High-Frequency Control Loops
  std::unique_ptr<urcl::rtde_interface::DataPackage> GetDataPackage() override;
  bool WriteJointCommand(const urcl::vector6d_t& values,
                         urcl::comm::ControlMode mode,
                         urcl::RobotReceiveTimeout timeout) override;
  void WriteKeepalive() override;
  bool StopControl() override;

  // Digital I/O Streams
  bool SendStandardDigitalOutput(uint8_t output_mask,
                                 uint8_t output_bits) override;
  bool SendConfigurableDigitalOutput(uint8_t output_mask,
                                     uint8_t output_bits) override;
  bool SendToolDigitalOutput(uint8_t output_mask, uint8_t output_bits) override;

  // External Sensor Streams
  bool SendExternalForceTorque(const urcl::vector6d_t& total_wrench) override;
  void FtRtdeInputEnable(bool enable) override;
  bool ZeroFTSensor() override;

  // Payload & Scales
  bool SendRobotProgram() override;
  bool SetPayload(double mass,
                  const urcl::vector3d_t& center_of_gravity) override;
  bool SetFrictionScales(const urcl::vector6d_t& viscous,
                         const urcl::vector6d_t& coulomb) override;

  // Comm Management
  void StartRTDECommunication() override;
  std::deque<urcl::primary_interface::ErrorCode> GetErrorCodes() override;

 private:
  std::unique_ptr<urcl::UrDriver> ur_driver_;
  std::unique_ptr<urcl::DashboardClient> dashboard_client_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_HARDWARE_MODULES_UNIVERSAL_ROBOTS_RTDE_UR_DRIVER_H_
