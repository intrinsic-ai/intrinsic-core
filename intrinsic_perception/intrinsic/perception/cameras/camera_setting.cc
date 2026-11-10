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

#include "intrinsic/perception/cameras/camera_setting.h"

#include <ostream>
#include <string_view>
#include <variant>

#include "absl/functional/overload.h"
#include "intrinsic/math/almost_equals.h"

namespace intrinsic::perception {
namespace {

// GenICam float features backed by 32-bit `<FloatReg>` registers or integer
// `<Converter>` formulas (e.g. `Gamma`, `ExposureTime`, `Gain`) may read back
// from hardware with 32-bit float or register-step quantization relative to the
// requested 64-bit `double` value.
constexpr double kFloatTolerance = 1e-6;

}  // namespace

bool CameraSetting::IsApprox(const CameraSetting& other) const {
  if (name != other.name) {
    return false;
  }
  if (const auto *lhs_float = std::get_if<Float>(&value),
      *rhs_float = std::get_if<Float>(&other.value);
      lhs_float != nullptr && rhs_float != nullptr) {
    return intrinsic::AlmostEquals(lhs_float->value, rhs_float->value,
                                   kFloatTolerance);
  }
  return value == other.value;
}

std::ostream& operator<<(std::ostream& os, const CameraSetting& setting) {
  const std::string_view name = setting.name;
  std::visit(absl::Overload{
                 [&](const std::monostate&) { os << "Not set: " << name; },
                 [&](const CameraSetting::Integer& value) {
                   os << "Integer: " << name << " = " << value.value;
                 },
                 [&](const CameraSetting::Float& value) {
                   os << "Float: " << name << " = " << value.value;
                 },
                 [&](const CameraSetting::Boolean& value) {
                   os << "Boolean: " << name << " = " << value.value;
                 },
                 [&](const CameraSetting::String& value) {
                   os << "String: " << name << " = " << value.value;
                 },
                 [&](const CameraSetting::Enumeration& value) {
                   os << "Enumeration: " << name << " = " << value.value;
                 },
                 [&](const CameraSetting::Command& value) {
                   os << "Command: " << name;
                 },
             },
             setting.value);
  return os;
}

}  // namespace intrinsic::perception
