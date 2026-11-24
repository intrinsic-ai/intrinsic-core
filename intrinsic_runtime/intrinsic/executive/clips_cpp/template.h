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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CPP_TEMPLATE_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CPP_TEMPLATE_H_

#include <ostream>
#include <string>
#include <vector>

#include "intrinsic/executive/clips_cpp/value.h"

namespace intrinsic {
namespace executive {
namespace clips {

class Environment;

class Template {
  friend std::ostream& operator<<(std::ostream& os, const Template& tpl);

 public:
  enum class DefaultType {
    kNoDefault = 0,
    kStaticDefault = 1,
    kDynamicDefault = 2
  };

  Template(Environment* env, void* c_template);

  bool operator==(const Template& other) const {
    return GetName() == other.GetName();
  }
  bool operator!=(const Template& other) const { return !(*this == other); }

  std::string GetName() const;
  std::string DebugString() const;

  // Access to raw C pointer, avoid usage if possible, but necessary internally.
  void* GetCPtr() const { return c_template_; }

  std::vector<std::string> GetSlotNames() const;
  Values GetSlotTypes(const std::string& slot_name) const;
  Values GetSlotAllowedValues(const std::string& slot_name) const;
  Values GetSlotCardinality(const std::string& slot_name) const;
  // Default type can be none, static, or dynamic, cf. CLIPS manual
  DefaultType GetSlotDefaultType(const std::string& slot_name) const;
  Values GetSlotDefaultValue(const std::string& slot_name) const;
  Values GetSlotRange(const std::string& slot_name) const;
  bool SlotExists(const std::string& slot_name) const;
  // A non-structured template is called an implied template. It is created
  // automatically on first use, e.g., "(some-fact A B)". Unlike templates which
  // are explicitly declared using, e.g., "(deftemplate foo (slot bar))".
  bool IsImplied() const;
  bool IsMultifieldSlot(const std::string& slot_name) const;
  bool IsSinglefieldSlot(const std::string& slot_name) const;

 private:
  // Associated environment, owned externally.
  Environment* env_;
  void* c_template_;
};

std::ostream& operator<<(std::ostream& os, const Template& tpl);

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CPP_TEMPLATE_H_
