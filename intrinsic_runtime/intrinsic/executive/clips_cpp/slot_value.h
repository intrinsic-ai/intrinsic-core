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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CPP_SLOT_VALUE_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CPP_SLOT_VALUE_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/executive/clips_cpp/value.h"

namespace intrinsic {
namespace executive {
namespace clips {

// Class representing a CLIPS Fact Slot to Value assignment
class SlotValue {
 public:
  SlotValue(std::string_view slot_name, const Value& value,
            bool negated = false)
      : slot_name_(slot_name), valuev_(value), negated_(negated) {}
  SlotValue(std::string_view slot_name, const Symbol& symbol,
            bool negated = false)
      : slot_name_(slot_name), valuev_(Value(symbol)), negated_(negated) {}
  SlotValue(std::string_view slot_name, const Values& values,
            bool negated = false)
      : slot_name_(slot_name), valuev_(values), negated_(negated) {}
  SlotValue(std::string_view slot_name, float value, bool negated = false)
      : slot_name_(slot_name), valuev_(Value(value)), negated_(negated) {}
  SlotValue(std::string_view slot_name, double value, bool negated = false)
      : slot_name_(slot_name), valuev_(Value(value)), negated_(negated) {}
  SlotValue(std::string_view slot_name, int value, bool negated = false)
      : slot_name_(slot_name), valuev_(Value(value)), negated_(negated) {}
  SlotValue(std::string_view slot_name, unsigned int value,
            bool negated = false)
      : slot_name_(slot_name), valuev_(Value(value)), negated_(negated) {}
  SlotValue(std::string_view slot_name, int64_t value, bool negated = false)
      : slot_name_(slot_name), valuev_(Value(value)), negated_(negated) {}
  SlotValue(std::string_view slot_name, std::string_view value,
            bool negated = false)
      : slot_name_(slot_name), valuev_(Value(value)), negated_(negated) {}
  SlotValue(std::string_view slot_name, void* value, bool negated = false)
      : slot_name_(slot_name), valuev_(Value(value)), negated_(negated) {}

  bool IsValue() const { return std::holds_alternative<Value>(valuev_); }
  bool IsValues() const { return std::holds_alternative<Values>(valuev_); }
  bool IsNegated() const { return negated_; }

  std::string GetSlotName() const { return slot_name_; }

  absl::StatusOr<Value> GetSlotValue() const {
    if (!IsValue()) return absl::InvalidArgumentError("Not singlefield");
    return std::get<Value>(valuev_);
  }

  absl::StatusOr<Values> GetSlotValues() const {
    if (!IsValues()) return absl::InvalidArgumentError("Not multifield");
    return std::get<Values>(valuev_);
  }

 private:
  std::string slot_name_;
  std::variant<Value, Values> valuev_;
  bool negated_ = false;
};

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CPP_SLOT_VALUE_H_
