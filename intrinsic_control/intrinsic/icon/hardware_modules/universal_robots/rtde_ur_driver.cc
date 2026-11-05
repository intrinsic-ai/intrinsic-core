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

#include "intrinsic/icon/hardware_modules/universal_robots/rtde_ur_driver.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "include/ur_client_library/comm/control_mode.h"
#include "include/ur_client_library/primary/robot_message/error_code_message.h"
#include "include/ur_client_library/rtde/data_package.h"
#include "include/ur_client_library/types.h"
#include "include/ur_client_library/ur/dashboard_client.h"
#include "include/ur_client_library/ur/datatypes.h"
#include "include/ur_client_library/ur/robot_receive_timeout.h"
#include "include/ur_client_library/ur/ur_driver.h"

namespace intrinsic::icon {
namespace {

static constexpr size_t kMaxConnectionRetries = 1;
static constexpr std::chrono::milliseconds kConnectionRetryInterval =
    std::chrono::seconds(1);

}  // namespace

RtdeUrDriver::~RtdeUrDriver() {
  if (dashboard_client_) {
    dashboard_client_->disconnect();
  }
}

absl::Status RtdeUrDriver::ConnectDashboard(absl::string_view robot_ip) {
  auto dashboard_client =
      std::make_unique<urcl::DashboardClient>(std::string(robot_ip));
  if (!dashboard_client->connect(kMaxConnectionRetries,
                                 kConnectionRetryInterval)) {
    return absl::UnavailableError(
        absl::StrCat("Could not connect to dashboard in ",
                     kMaxConnectionRetries, " retries with an interval of ",
                     std::chrono::duration_cast<std::chrono::seconds>(
                         kConnectionRetryInterval)
                         .count(),
                     " seconds. Ensure the robot is powered on and the IP [",
                     robot_ip, "] is correct."));
  }
  dashboard_client_ = std::move(dashboard_client);
  return absl::OkStatus();
}

void RtdeUrDriver::DisconnectDashboard() {
  if (dashboard_client_) {
    dashboard_client_->disconnect();
  }
}

void RtdeUrDriver::ResetDashboard() { dashboard_client_.reset(); }

bool RtdeUrDriver::CommandAddToLog(absl::string_view message) {
  if (!dashboard_client_) return false;
  return dashboard_client_->commandAddToLog(std::string(message));
}

bool RtdeUrDriver::CommandIsInRemoteControl() {
  if (!dashboard_client_) return false;
  return dashboard_client_->commandIsInRemoteControl();
}

bool RtdeUrDriver::CommandStop() {
  if (!dashboard_client_) return false;
  return dashboard_client_->commandStop();
}

bool RtdeUrDriver::CommandGetRobotModel(std::string& robot_model) {
  if (!dashboard_client_) return false;
  return dashboard_client_->commandGetRobotModel(robot_model);
}

bool RtdeUrDriver::CommandGetSerialNumber(std::string& serial_number) {
  if (!dashboard_client_) return false;
  return dashboard_client_->commandGetSerialNumber(serial_number);
}

bool RtdeUrDriver::CommandPowerOn() {
  if (!dashboard_client_) return false;
  return dashboard_client_->commandPowerOn();
}

bool RtdeUrDriver::CommandBrakeRelease() {
  if (!dashboard_client_) return false;
  return dashboard_client_->commandBrakeRelease();
}

bool RtdeUrDriver::CommandPowerOff() {
  if (!dashboard_client_) return false;
  return dashboard_client_->commandPowerOff();
}

bool RtdeUrDriver::CommandSafetyMode(std::string& safety_mode_string) {
  if (!dashboard_client_) return false;
  return dashboard_client_->commandSafetyMode(safety_mode_string);
}

bool RtdeUrDriver::CommandUnlockProtectiveStop() {
  if (!dashboard_client_) return false;
  return dashboard_client_->commandUnlockProtectiveStop();
}

bool RtdeUrDriver::CommandRestartSafety() {
  if (!dashboard_client_) return false;
  return dashboard_client_->commandRestartSafety();
}

bool RtdeUrDriver::CommandCloseSafetyPopup() {
  if (!dashboard_client_) return false;
  return dashboard_client_->commandCloseSafetyPopup();
}

absl::Status RtdeUrDriver::InitializeDriver(const UrDriverConfig& config) {
  std::unique_ptr<urcl::ToolCommSetup> tool_comm_setup = nullptr;
  ur_driver_ = std::make_unique<urcl::UrDriver>(
      config.robot_ip, config.script_file, config.output_recipe,
      config.input_recipe, config.handle_program_state, config.headless_mode,
      std::move(tool_comm_setup), config.reverse_port,
      config.script_sender_port, config.servoj_gain,
      config.servoj_lookahead_time, config.non_blocking_read, config.reverse_ip,
      config.trajectory_port, config.script_command_port);
  return absl::OkStatus();
}

void RtdeUrDriver::ResetDriver() { ur_driver_.reset(); }

bool RtdeUrDriver::IsDriverValid() const { return ur_driver_ != nullptr; }

bool RtdeUrDriver::CheckCalibration(absl::string_view checksum) {
  if (!ur_driver_) return false;
  return ur_driver_->checkCalibration(std::string(checksum));
}

urcl::VersionInformation RtdeUrDriver::GetVersion() {
  if (!ur_driver_) return urcl::VersionInformation();
  return ur_driver_->getVersion();
}

double RtdeUrDriver::GetControlFrequency() {
  if (!ur_driver_) return 0.0;
  return ur_driver_->getControlFrequency();
}

std::unique_ptr<urcl::rtde_interface::DataPackage>
RtdeUrDriver::GetDataPackage() {
  DCHECK(ur_driver_ != nullptr);
  return ur_driver_->getDataPackage();
}

bool RtdeUrDriver::WriteJointCommand(const urcl::vector6d_t& values,
                                     urcl::comm::ControlMode mode,
                                     urcl::RobotReceiveTimeout timeout) {
  DCHECK(ur_driver_ != nullptr);
  return ur_driver_->writeJointCommand(values, mode, timeout);
}

void RtdeUrDriver::WriteKeepalive() {
  DCHECK(ur_driver_ != nullptr);
  ur_driver_->writeKeepalive();
}

bool RtdeUrDriver::StopControl() {
  DCHECK(ur_driver_ != nullptr);
  return ur_driver_->stopControl();
}

bool RtdeUrDriver::SendStandardDigitalOutput(uint8_t output_mask,
                                             uint8_t output_bits) {
  DCHECK(ur_driver_ != nullptr);
  return ur_driver_->getRTDEWriter().sendStandardDigitalOutput(output_mask,
                                                               output_bits);
}

bool RtdeUrDriver::SendConfigurableDigitalOutput(uint8_t output_mask,
                                                 uint8_t output_bits) {
  DCHECK(ur_driver_ != nullptr);
  return ur_driver_->getRTDEWriter().sendConfigurableDigitalOutput(output_mask,
                                                                   output_bits);
}

bool RtdeUrDriver::SendToolDigitalOutput(uint8_t output_mask,
                                         uint8_t output_bits) {
  DCHECK(ur_driver_ != nullptr);
  return ur_driver_->getRTDEWriter().sendToolDigitalOutput(output_mask,
                                                           output_bits);
}

bool RtdeUrDriver::SendExternalForceTorque(
    const urcl::vector6d_t& total_wrench) {
  DCHECK(ur_driver_ != nullptr);
  return ur_driver_->getRTDEWriter().sendExternalForceTorque(total_wrench);
}

void RtdeUrDriver::FtRtdeInputEnable(bool enable) {
  DCHECK(ur_driver_ != nullptr);
  ur_driver_->ftRtdeInputEnable(enable);
}

bool RtdeUrDriver::ZeroFTSensor() {
  DCHECK(ur_driver_ != nullptr);
  return ur_driver_->zeroFTSensor();
}

bool RtdeUrDriver::SendRobotProgram() {
  DCHECK(ur_driver_ != nullptr);
  return ur_driver_->sendRobotProgram();
}

bool RtdeUrDriver::SetPayload(double mass,
                              const urcl::vector3d_t& center_of_gravity) {
  DCHECK(ur_driver_ != nullptr);
  return ur_driver_->setPayload(mass, center_of_gravity);
}

bool RtdeUrDriver::SetFrictionScales(const urcl::vector6d_t& viscous,
                                     const urcl::vector6d_t& coulomb) {
  DCHECK(ur_driver_ != nullptr);
  return ur_driver_->setFrictionScales(viscous, coulomb);
}

void RtdeUrDriver::StartRTDECommunication() {
  DCHECK(ur_driver_ != nullptr);
  ur_driver_->startRTDECommunication();
}

std::deque<urcl::primary_interface::ErrorCode> RtdeUrDriver::GetErrorCodes() {
  DCHECK(ur_driver_ != nullptr);
  return ur_driver_->getErrorCodes();
}

}  // namespace intrinsic::icon
