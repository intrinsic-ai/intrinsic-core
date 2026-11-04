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

#include "intrinsic/hardware/gripper/service/generic_pinch_gripper_utils.h"

#include <algorithm>
#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "intrinsic/hardware/gripper/service/pinch_gripper_extended_status.h"

namespace intrinsic::gripper {

int64_t ConvertToHardwareValue(double in_value, double in_min, double in_max,
                               int64_t out_min, int64_t out_max) {
  if (in_min == in_max) {
    return (out_min + out_max) / 2;
  }
  double clamped_in_value = std::clamp(in_value, in_min, in_max);
  return ((clamped_in_value - in_min) * (out_max - out_min) /
          (in_max - in_min)) +
         out_min;
}

absl::StatusOr<int64_t> ConvertSiToHardwareValue(
    const double si_in_value,
    const intrinsic_proto::gripper::PhysicalQuantityFormatProperties& config) {
  // aliases for short names:
  const double si_in_minimum = config.si_properties().range().minimum();
  const double si_in_maximum = config.si_properties().range().maximum();
  // `hardware_out_minimum` is the hardware value that corresponds to the
  // `si_in_minimum`.
  const int64_t hardware_out_minimum =
      config.hardware_value_properties().range().minimum();
  // `hardware_out_maximum` is the hardware value that corresponds to the
  // `si_in_maximum`.
  const int64_t hardware_out_maximum =
      config.hardware_value_properties().range().maximum();
  int64_t hardware_value =
      ConvertToHardwareValue(si_in_value, si_in_minimum, si_in_maximum,
                             hardware_out_minimum, hardware_out_maximum);
  // It is possible that `hardware_out_minimum` is bigger than
  // `hardware_out_maximum`.
  if (hardware_value > std::max(hardware_out_minimum, hardware_out_maximum) ||
      hardware_value < std::min(hardware_out_minimum, hardware_out_maximum)) {
    return PinchGripperCommandOutOfRangeStatus(hardware_value);
  }
  return hardware_value;
}

double PositionFromHardwareValue(
    const intrinsic_proto::gripper::PinchGripperPhysicalConfig& config,
    uint8_t hardware_value) {
  const auto& position_config = config.position();
  if (!position_config.has_si_properties() ||
      !position_config.has_hardware_value_properties()) {
    return 0.0;
  }
  const double hw_min =
      position_config.hardware_value_properties().range().minimum();
  const double hw_max =
      position_config.hardware_value_properties().range().maximum();
  const double si_min = position_config.si_properties().range().minimum();
  const double si_max = position_config.si_properties().range().maximum();
  const double offset_position = hardware_value - hw_min;
  return (si_min + offset_position / (hw_max - hw_min) * (si_max - si_min));
}

absl::StatusOr<int64_t> ConvertPercentageToHardwareValue(
    const double percentage_in_value,
    const intrinsic_proto::gripper::PhysicalQuantityFormatProperties& config) {
  // aliases for short names:
  // `hardware_out_minimum` is the hardware value that corresponds to the 0%.
  const int64_t hardware_out_minimum =
      config.hardware_value_properties().range().minimum();
  // `hardware_out_maximum` is the hardware value that corresponds to the 100%.
  const int64_t hardware_out_maximum =
      config.hardware_value_properties().range().maximum();
  int64_t hardware_value =
      ConvertToHardwareValue(percentage_in_value, 0.0, 100.0,
                             hardware_out_minimum, hardware_out_maximum);
  // It is possible that `hardware_out_minimum` is bigger than
  // `hardware_out_maximum`.
  if (hardware_value > std::max(hardware_out_minimum, hardware_out_maximum) ||
      hardware_value < std::min(hardware_out_minimum, hardware_out_maximum)) {
    return PinchGripperCommandOutOfRangeStatus(hardware_value);
  }
  return hardware_value;
}

// Check the validity of the pinch gripper config.
// The flag `validate_si_position_config` is specifying whether to validate
// the config for position SI unit properties, or not; this is useful
// for example when the user only provides position commands in percentage,
// in which the SI unit properties are irrelevant and no need to be validated.
// Similarly for `validate_si_velocity_config` and `validate_si_effort_config`,
// for velocity and effort, respectively. These flags are separated to provide
// a better granularity, so that for example the user can specify position
// command in SI unit, and specify velocity and effort commands in percentages.
absl::Status CheckConfigValidity(
    const intrinsic_proto::gripper::PinchGripperPhysicalConfig& config,
    bool validate_si_position_config, bool validate_si_velocity_config,
    bool validate_si_effort_config) {
  if (!config.has_position()) {
    return absl::InvalidArgumentError("Missing gripper position config.");
  }
  if (validate_si_position_config) {
    if (!config.position().has_si_properties()) {
      return absl::InvalidArgumentError(
          "Missing gripper position SI properties.");
    }
    if (!config.position().si_properties().has_range()) {
      return absl::InvalidArgumentError(
          "Missing gripper position SI range properties.");
    }
    if (!config.position().si_properties().has_range()) {
      return absl::InvalidArgumentError(
          "Missing gripper position SI range specification.");
    }
    if (!config.position().si_properties().has_unit()) {
      return absl::InvalidArgumentError(
          "Missing gripper position SI unit specification.");
    }
  }
  if (!config.position().has_hardware_value_properties()) {
    return absl::InvalidArgumentError(
        "Missing gripper position hardware value properties.");
  }
  if (!config.position().hardware_value_properties().has_range()) {
    return absl::InvalidArgumentError(
        "Missing gripper position hardware value range properties.");
  }
  if (!config.has_velocity()) {
    return absl::InvalidArgumentError("Missing gripper velocity config.");
  }
  if (validate_si_velocity_config) {
    if (!config.velocity().has_si_properties()) {
      return absl::InvalidArgumentError(
          "Missing gripper velocity SI properties.");
    }
    if (!config.velocity().si_properties().has_range()) {
      return absl::InvalidArgumentError(
          "Missing gripper velocity SI range properties.");
    }
    if (!config.velocity().si_properties().has_unit()) {
      return absl::InvalidArgumentError(
          "Missing gripper velocity SI unit specification.");
    }
  }
  if (!config.velocity().has_hardware_value_properties()) {
    return absl::InvalidArgumentError(
        "Missing gripper velocity hardware value properties.");
  }
  if (!config.velocity().hardware_value_properties().has_range()) {
    return absl::InvalidArgumentError(
        "Missing gripper velocity hardware value range properties.");
  }
  if (!config.has_effort()) {
    return absl::InvalidArgumentError("Missing gripper effort config.");
  }
  if (validate_si_effort_config) {
    if (!config.effort().has_si_properties()) {
      return absl::InvalidArgumentError(
          "Missing gripper effort SI properties.");
    }
    if (!config.effort().si_properties().has_range()) {
      return absl::InvalidArgumentError(
          "Missing gripper effort SI range properties.");
    }
    if (!config.effort().si_properties().has_unit()) {
      return absl::InvalidArgumentError(
          "Missing gripper effort SI unit specification.");
    }
  }
  if (!config.effort().has_hardware_value_properties()) {
    return absl::InvalidArgumentError(
        "Missing gripper effort hardware value properties.");
  }
  if (!config.effort().hardware_value_properties().has_range()) {
    return absl::InvalidArgumentError(
        "Missing gripper effort hardware value range properties.");
  }
  return absl::OkStatus();
}

}  // namespace intrinsic::gripper
