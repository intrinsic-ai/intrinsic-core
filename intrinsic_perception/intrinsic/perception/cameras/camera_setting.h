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

#ifndef INTRINSIC_PERCEPTION_CAMERAS_CAMERA_SETTING_H_
#define INTRINSIC_PERCEPTION_CAMERAS_CAMERA_SETTING_H_

#include <cstdint>
#include <ostream>
#include <string>
#include <variant>

namespace intrinsic::perception {

struct CameraSetting {
  struct Integer {
    int64_t value = 0;
    auto operator<=>(const Integer&) const = default;
  };
  struct Float {
    double value = 0.0;
    auto operator<=>(const Float&) const = default;
  };
  struct Boolean {
    bool value = false;
    auto operator<=>(const Boolean&) const = default;
  };
  struct String {
    std::string value;
    auto operator<=>(const String&) const = default;
  };
  struct Enumeration {
    std::string value;
    auto operator<=>(const Enumeration&) const = default;
  };
  struct Command {
    auto operator<=>(const Command&) const = default;
  };
  using Value = std::variant<std::monostate, Integer, Float, Boolean, String,
                             Enumeration, Command>;

  std::string name;
  Value value = std::monostate();
  auto operator<=>(const CameraSetting&) const = default;

  // Returns true if `name` and `value` match `other`, comparing `Float` values
  // within tolerance (both absolute and relative) to account for 32-bit
  // `<FloatReg>` or `<Converter>` readback quantization on GenICam devices.
  [[nodiscard]] bool IsApprox(const CameraSetting& other) const;
};

std::ostream& operator<<(std::ostream& os, const CameraSetting& setting);

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_CAMERAS_CAMERA_SETTING_H_
