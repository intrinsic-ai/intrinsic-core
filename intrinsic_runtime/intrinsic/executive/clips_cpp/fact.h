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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CPP_FACT_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CPP_FACT_H_

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/executive/clips_cpp/slot_value.h"
#include "intrinsic/executive/clips_cpp/template.h"
#include "intrinsic/executive/clips_cpp/value.h"

namespace intrinsic {
namespace executive {
namespace clips {

class Environment;

// Fact has a shared lifetime with Environment. If the associated
// Environment is destroyed the Fact is no longer valid.
// Facts cannot be created directly, but only through interaction with the
// environment.
// Facts represented by this class had to be asserted at creation and may have
// been retracted later (you can check with Exists). Memory of the C structs is
// managed solely through the CLIPS garbage collector. This class ensures that
// CLIPS is informed about externally held references using
// EnvIncrementFactCount and EnvDecrementFactCount.
class Fact {
  friend class Environment;

 public:
  static constexpr char kImpliedSlotName[] = "implied";

  Fact() = delete;
  Fact(Environment* env, void* c_fact);
  Fact(const Fact& fact);
  Fact(Fact&& fact);
  ~Fact();

  // Asserts a fact with the specified `slots` for `fact_template` in `env`.
  // Ensures that the memory of the resulting fact struct is managed by CLIPS or
  // cleaned up in case of an error.
  static absl::StatusOr<Fact> AssertFact(Environment* env,
                                         const Template& fact_template,
                                         absl::Span<const SlotValue> slots,
                                         bool assign_defaults = true);

  Fact& operator=(const Fact& fact);
  Fact& operator=(Fact&& fact);
  bool operator==(const Fact& other) const;

  // Access to raw C pointer, avoid usage if possible, but necessary internally.
  void* GetCPtr() const { return c_fact_; }
  Environment* GetEnvironment() const { return env_; }

  // Check if the fact still exists in the environment, or whether it
  // has been retracted.
  bool Exists() const;

  // Checks if this fact matches all given slot values.
  // Returns true if all slots match, false if at least one doesn't.
  bool Matches(absl::Span<const SlotValue> slots) const;

  // Get the index of the fact in the working memory.
  int64_t GetIndex() const;

  // Get template information of the fact
  Template GetTemplate() const;
  std::string GetTemplateName() const;
  bool IsTemplate(absl::string_view template_name) const;

  // Get the names of all slots associated with this fact
  // (respectively of its associated template).
  std::vector<std::string> GetSlotNames() const;

  // Get value of a specific slot. Use this if you expect the value not
  // to be a multifield value.
  absl::StatusOr<Value> GetSlotValue(const std::string& name) const;

  // Get value of a specific slot. Use this if you *do* expect the value
  // to be a multifield value.
  // You may use this function with an empty name to get all values of
  // an ordered fact (cf. CLIPS Basic Programming Manual).
  absl::StatusOr<Values> GetSlotValues(const std::string& name) const;

  // Including identifiers prefixes fact strings with "f-<index>" fact indexes.
  std::string DebugString(bool include_identifier = false) const;

 private:
  // Decreases the reference counter in Clips for the c fact pointer.
  void ConditionalRelease();

  // Associated environment, owned externally.
  Environment* env_;
  void* c_fact_;
};

std::ostream& operator<<(std::ostream& os, const Fact& fact);

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CPP_FACT_H_
