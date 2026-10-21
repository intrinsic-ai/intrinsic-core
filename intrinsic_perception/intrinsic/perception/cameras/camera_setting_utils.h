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

#ifndef INTRINSIC_PERCEPTION_CAMERAS_CAMERA_SETTING_UTILS_H_
#define INTRINSIC_PERCEPTION_CAMERAS_CAMERA_SETTING_UTILS_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "absl/status/statusor.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_access.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/util/status/status_builder.h"

namespace intrinsic::perception {

// Creates and returns camera settings.
template <typename T>
CameraSetting CreateCameraSetting(std::string_view name, T value) {
  if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
    return {
        .name = std::string(name),
        .value = CameraSetting::Integer{.value = static_cast<int64_t>(value)}};
  } else if constexpr (std::is_floating_point_v<T>) {
    return {.name = std::string(name),
            .value = CameraSetting::Float{.value = static_cast<double>(value)}};
  } else if constexpr (std::is_same_v<T, bool>) {
    return {.name = std::string(name),
            .value = CameraSetting::Boolean{.value = value}};
  } else if constexpr (std::is_same_v<T, const char*> ||
                       std::is_same_v<T, std::string_view>) {
    return {.name = std::string(name),
            .value = CameraSetting::String{.value = value}};
  } else if constexpr (std::is_same_v<T, std::string>) {
    return {.name = std::string(name),
            .value = CameraSetting::String{.value = value}};
  } else {
    static_assert(false, "Unsupported value type.");
  }
}

// Creates and returns enumeration camera settings.
CameraSetting CreateEnumCameraSetting(std::string_view name,
                                      std::string_view enum_value);

// Creates and returns command camera settings.
CameraSetting CreateCommandCameraSetting(std::string_view name);

// Creates and returns camera setting access.
CameraSettingAccess CreateCameraSettingAccess(std::string_view name,
                                              CameraSettingAccess::Mode mode);

template <typename T>
absl::StatusOr<T> GetValue(const CameraSetting& camera_setting) {
  if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
    if (const CameraSetting::Integer* val =
            std::get_if<CameraSetting::Integer>(&camera_setting.value)) {
      return static_cast<T>(val->value);
    }
  } else if constexpr (std::is_floating_point_v<T>) {
    if (const CameraSetting::Float* val =
            std::get_if<CameraSetting::Float>(&camera_setting.value)) {
      return static_cast<T>(val->value);
    }
  } else if constexpr (std::is_same_v<T, bool>) {
    if (const CameraSetting::Boolean* val =
            std::get_if<CameraSetting::Boolean>(&camera_setting.value)) {
      return val->value;
    }
  } else if constexpr (std::is_same_v<T, std::string_view> ||
                       std::is_same_v<T, std::string>) {
    if (const CameraSetting::String* val =
            std::get_if<CameraSetting::String>(&camera_setting.value)) {
      return static_cast<T>(val->value);
    }
  } else {
    static_assert(false, "Unsupported value type.");
  }
  return intrinsic::InvalidArgumentErrorBuilder()
         << "`" << camera_setting.name
         << "` camera setting does not contain the requested value type.";
}

absl::StatusOr<std::string> GetEnumerationValue(
    const CameraSetting& camera_setting);

template <typename T>
concept HasFloatOrIntegerProperties =
    std::is_base_of_v<CameraSettingProperties::Float, T> ||
    std::is_base_of_v<CameraSettingProperties::Integer, T>;

template <HasFloatOrIntegerProperties P>
P::ValueType ClampAndRoundToIncrement(typename P::ValueType value,
                                      const P& properties) {
  if (!properties.range.has_value()) {
    return value;
  }

  using ValueType = typename P::ValueType;
  const ValueType min = properties.range->minimum;
  const ValueType max = properties.range->maximum;
  const ValueType inc = properties.increment;

  value = std::clamp(value, min, max);
  if (inc <= 0) {
    return value;
  }

  if constexpr (std::is_integral_v<ValueType>) {
    const ValueType rem = (value - min) % inc;
    ValueType result = value - rem;
    if (rem > inc / 2 && result <= max - inc) {
      result += inc;
    }
    return result;
  } else {
    const ValueType steps = std::ceil((value - min) / inc - 0.5);
    if (!(steps < 1.0 / std::numeric_limits<ValueType>::epsilon())) {
      return value;
    }
    const ValueType result = min + steps * inc;
    const ValueType tolerance = 4 * std::numeric_limits<ValueType>::epsilon() *
                                std::max(std::abs(min), std::abs(max));
    if (std::abs(result - max) <= tolerance) {
      return max;
    }
    if (result > max) {
      return min + (steps - 1) * inc;
    }
    return result;
  }
}

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_CAMERAS_CAMERA_SETTING_UTILS_H_
