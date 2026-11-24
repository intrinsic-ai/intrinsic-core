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

#include "intrinsic/executive/clips_cpp/fact.h"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/slot_value.h"
#include "intrinsic/executive/clips_cpp/template.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/clips_cpp/value_util.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_macros.h"
// Keep CLIPS at the bottom to prevent macro pollution.
#include "clips/clips.h"
#include "clips/src/evaluatn.h"

namespace intrinsic {
namespace executive {
namespace clips {
namespace {

absl::Status AssignCFactSlotDefaults(Environment* env, void* c_fact)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  if (!EnvAssignFactSlotDefaults(env->GetCPtr(), c_fact)) {
    return absl::InternalError("Failed to assign default values");
  }
  return absl::OkStatus();
}

absl::Status SetCFactSlotValues(Environment* env, const Template& fact_template,
                                void* c_fact, const std::string& slot_name,
                                const Values& values)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  bool is_implied_template = fact_template.IsImplied();
  if (is_implied_template && slot_name != Fact::kImpliedSlotName) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Template '%s' is an implied template, can only set slot values "
        "for slot '%s'",
        fact_template.GetName(), Fact::kImpliedSlotName));
  }

  DATA_OBJECT dataobj;
  if (!internal::ValuesToDataObject(env->GetCPtr(), values, &dataobj).ok()) {
    return absl::InvalidArgumentError("Failed to convert value to data object");
  }
  if (!EnvPutFactSlot(env->GetCPtr(), c_fact,
                      is_implied_template ? nullptr : slot_name.c_str(),
                      &dataobj)) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Failed to set fact slot '%s' to [%s] (template %s)", slot_name,
        absl::StrJoin(values, ", ",
                      [](std::string* out, const Value& v) {
                        absl::StrAppend(out, v.ToString());
                      }),
        fact_template.GetName()));
  }
  return absl::OkStatus();
}

absl::Status SetCFactSlotValue(Environment* env, const Template& fact_template,
                               void* c_fact, const std::string& slot_name,
                               const Value& value)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  if (fact_template.IsImplied()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Template '%s' is an implied template, can only set multifield "
        "for slot '%s'",
        fact_template.GetName(), Fact::kImpliedSlotName));
  }

  DATA_OBJECT dataobj = DATA_OBJECT_INIT;
  if (!internal::ValueToDataObject(env->GetCPtr(), value, &dataobj).ok()) {
    return absl::InvalidArgumentError("Failed to convert value to data object");
  }

  if (!EnvPutFactSlot(env->GetCPtr(), c_fact, slot_name.c_str(), &dataobj)) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Failed to set fact slot '%s' to %s (template %s). "
                        "Does it have the correct type?",
                        slot_name,
                        value.GetValueType() == Value::Type::kString
                            ? absl::StrCat(R"(")", value.ToString(), R"(")")
                            : value.ToString(),
                        fact_template.GetName()));
  }
  return absl::OkStatus();
}

absl::Status SetSlots(Environment* env, const Template& fact_template,
                      void* c_fact, absl::Span<const SlotValue> slots)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  for (const SlotValue& slot_value : slots) {
    if (slot_value.IsNegated()) {
      return absl::InvalidArgumentError(
          "SlotValue for assert or modify cannot be negated");
    }
    if (!fact_template.SlotExists(slot_value.GetSlotName())) {
      return absl::InvalidArgumentError(
          absl::StrFormat("Template %s has no slot %s", fact_template.GetName(),
                          slot_value.GetSlotName()));
    }
    if (fact_template.IsSinglefieldSlot(slot_value.GetSlotName())) {
      if (!slot_value.IsValue()) {
        return absl::InvalidArgumentError(
            absl::StrFormat("Multifield for slot %s:%s",
                            fact_template.GetName(), slot_value.GetSlotName()));
      }
      INTR_ASSIGN_OR_RETURN(Value value, slot_value.GetSlotValue());
      INTR_RETURN_IF_ERROR(SetCFactSlotValue(env, fact_template, c_fact,
                                             slot_value.GetSlotName(), value));
    } else {
      if (!slot_value.IsValues()) {
        return absl::InvalidArgumentError(
            absl::StrFormat("Field for multislot %s:%s",
                            fact_template.GetName(), slot_value.GetSlotName()));
      }
      INTR_ASSIGN_OR_RETURN(Values values, slot_value.GetSlotValues());
      INTR_RETURN_IF_ERROR(SetCFactSlotValues(
          env, fact_template, c_fact, slot_value.GetSlotName(), values));
    }
  }
  return absl::OkStatus();
}

}  // namespace

// maximum length of debug string, 64k is a safe size, could make it smaller.
static const size_t DEBUG_STRING_BUFFER_LENGTH = 65536;

Fact::Fact(const Fact& fact) : env_(fact.env_), c_fact_(fact.c_fact_) {
  if (env_ != nullptr && c_fact_ != nullptr) {
    EnvIncrementFactCount(env_->GetCPtr(), c_fact_);
  }
}

Fact::Fact(Fact&& fact)
    : env_(std::exchange(fact.env_, nullptr)),
      c_fact_(std::exchange(fact.c_fact_, nullptr)) {}

Fact::Fact(Environment* env, void* c_fact) : env_(env), c_fact_(c_fact) {
  if (env_ != nullptr && c_fact_ != nullptr) {
    EnvIncrementFactCount(env->GetCPtr(), c_fact_);
  }
}

Fact::~Fact() { ConditionalRelease(); }

absl::StatusOr<Fact> Fact::AssertFact(Environment* env,
                                      const Template& fact_template,
                                      absl::Span<const SlotValue> slots,
                                      bool assign_defaults)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  if (env == nullptr) {
    return absl::InvalidArgumentError("Environment pointer cannot be null");
  }
  void* c_fact = EnvCreateFact(env->GetCPtr(), fact_template.GetCPtr());
  if (c_fact == nullptr) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Cannot create fact for template '%s'", fact_template.GetName()));
  }

  if (assign_defaults) {
    absl::Status status = AssignCFactSlotDefaults(env, c_fact);
    if (!status.ok()) {
      ReturnFact(env->GetCPtr(), static_cast<struct fact*>(c_fact));
      return status;
    }
  }

  absl::Status status = SetSlots(env, fact_template, c_fact, slots);
  if (!status.ok()) {
    ReturnFact(env->GetCPtr(), static_cast<struct fact*>(c_fact));
    return status;
  }

  if (EnvAssert(env->GetCPtr(), c_fact) == nullptr) {
    // CLIPS already called ReturnFact in this case, no cleanup needed. See
    // HandleFactDuplication from facthsh.c
    return absl::AlreadyExistsError("Fact already exists");
  }

  return Fact(env, c_fact);
}

Fact& Fact::operator=(const Fact& fact)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  ConditionalRelease();
  env_ = fact.env_;
  c_fact_ = fact.c_fact_;
  if (env_ != nullptr && c_fact_ != nullptr) {
    EnvIncrementFactCount(env_->GetCPtr(), c_fact_);
  }
  return *this;
}

Fact& Fact::operator=(Fact&& fact)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  if (this == &fact) {
    return *this;
  }
  ConditionalRelease();
  env_ = std::exchange(fact.env_, nullptr);
  c_fact_ = std::exchange(fact.c_fact_, nullptr);
  return *this;
}

void Fact::ConditionalRelease() ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  if (env_ != nullptr && c_fact_ != nullptr) {
    EnvDecrementFactCount(env_->GetCPtr(), c_fact_);
  }
}

bool Fact::Exists() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  if (env_ == nullptr || c_fact_ == nullptr) {
    return false;
  }
  return EnvFactExistp(env_->GetCPtr(), c_fact_) != 0;
}

bool Fact::Matches(absl::Span<const SlotValue> slots) const {
  if (env_ == nullptr || c_fact_ == nullptr) {
    return false;
  }
  for (const SlotValue& slot : slots) {
    if (slot.IsValue()) {
      INTR_ASSIGN_OR_RETURN(Value expected_value, slot.GetSlotValue(),
                            _.LogError().With(Return(false)));
      INTR_ASSIGN_OR_RETURN(Value observed_value,
                            GetSlotValue(slot.GetSlotName()),
                            _.LogError().With(Return(false)));
      if (slot.IsNegated()) {
        if (observed_value == expected_value) return false;
      } else {
        if (observed_value != expected_value) return false;
      }
    } else if (slot.IsValues()) {
      INTR_ASSIGN_OR_RETURN(Values expected_values, slot.GetSlotValues(),
                            _.LogError().With(Return(false)));
      INTR_ASSIGN_OR_RETURN(Values observed_values,
                            GetSlotValues(slot.GetSlotName()),
                            _.LogError().With(Return(false)));
      if (slot.IsNegated()) {
        if (observed_values == expected_values) return false;
      } else {
        if (observed_values != expected_values) return false;
      }
    } else {
      LOG(WARNING) << "Invalid SlotValue, neither Value nor Values";
      return false;
    }
  }
  return true;
}

int64_t Fact::GetIndex() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  if (env_ == nullptr || c_fact_ == nullptr) {
    return -1;
  }
  return EnvFactIndex(env_->GetCPtr(), c_fact_);
}

Template Fact::GetTemplate() const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  if (env_ == nullptr || c_fact_ == nullptr) {
    return Template(env_, nullptr);
  }
  return Template(env_, EnvFactDeftemplate(env_->GetCPtr(), c_fact_));
}

std::string Fact::GetTemplateName() const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  if (env_ == nullptr || c_fact_ == nullptr) {
    return "";
  }
  void* c_template = EnvFactDeftemplate(env_->GetCPtr(), c_fact_);
  if (c_template == nullptr) return "";
  return EnvGetDeftemplateName(env_->GetCPtr(), c_template);
}

bool Fact::IsTemplate(absl::string_view template_name) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  if (env_ == nullptr || c_fact_ == nullptr) {
    return false;
  }
  void* c_template = EnvFactDeftemplate(env_->GetCPtr(), c_fact_);
  if (c_template == nullptr) return false;
  return (template_name == EnvGetDeftemplateName(env_->GetCPtr(), c_template));
}

std::vector<std::string> Fact::GetSlotNames() const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  if (env_ == nullptr || c_fact_ == nullptr) {
    return {};
  }
  DATA_OBJECT dataobj;
  EnvFactSlotNames(env_->GetCPtr(), c_fact_, &dataobj);
  return internal::FilterDataObjectStrings(&dataobj);
}

absl::StatusOr<Value> Fact::GetSlotValue(const std::string& name) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  if (env_ == nullptr || c_fact_ == nullptr) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Cannot get slot '%s': fact or environment pointer is null", name));
  }
  if (GetTemplate().IsImplied()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Template '%s' is an implied template, can only get multifield "
        "for slot '%s'",
        GetTemplateName(), kImpliedSlotName));
  }

  DATA_OBJECT dataobj;
  if (!EnvGetFactSlot(env_->GetCPtr(), c_fact_, name.c_str(), &dataobj)) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Slot '%s' does not exist for '%s' fact or the fact "
                        "does not exist anymore.",
                        name, GetTemplateName()));
  }
  auto values = internal::DataObjectToValues(&dataobj);
  if (values.size() != 1) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Slot '%s' has %zu entries (expected 1)", name, values.size()));
  }
  return values[0];
}

absl::StatusOr<Values> Fact::GetSlotValues(const std::string& name) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  if (env_ == nullptr || c_fact_ == nullptr) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Cannot get multislot '%s': fact pointer or environment is null",
        name));
  }
  bool is_implied_template = GetTemplate().IsImplied();
  if (is_implied_template && (name != kImpliedSlotName) && !name.empty()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Template '%s' is an implied template, can only get slot values "
        "for slot '%s'",
        GetTemplateName(), kImpliedSlotName));
  }

  DATA_OBJECT dataobj;
  if (!EnvGetFactSlot(env_->GetCPtr(), c_fact_,
                      is_implied_template ? nullptr : name.c_str(), &dataobj)) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Multislot '%s' does not exist for '%s' fact", name,
                        GetTemplateName()));
  }
  return internal::DataObjectToValues(&dataobj);
}

std::string Fact::DebugString(bool include_identifier) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(GetEnvironment()->mutex()) {
  if (env_ == nullptr || c_fact_ == nullptr) {
    return "";
  }
  env_->mutex()->AssertHeld();
  std::vector<char> buffer(DEBUG_STRING_BUFFER_LENGTH, 0);
  const char kFactDbgStr[] = "FactDbgStr";
  OpenStringDestination(env_->GetCPtr(), kFactDbgStr, buffer.data(),
                        DEBUG_STRING_BUFFER_LENGTH - 1);
  if (include_identifier) {
    PrintFactWithIdentifier(env_->GetCPtr(), kFactDbgStr,
                            static_cast<struct fact*>(c_fact_));
  } else {
    PrintFact(env_->GetCPtr(), kFactDbgStr, static_cast<struct fact*>(c_fact_),
              FALSE, FALSE);
  }
  CloseStringDestination(env_->GetCPtr(), kFactDbgStr);
  return buffer.data();
}

bool Fact::operator==(const Fact& other) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex(), other.env_->mutex()) {
  if (this == &other) return true;
  bool is_null = (env_ == nullptr || c_fact_ == nullptr);
  bool other_is_null = (other.env_ == nullptr || other.c_fact_ == nullptr);
  if (is_null && other_is_null) return true;
  if (is_null != other_is_null) return false;

  auto templ = GetTemplate();
  if (templ != other.GetTemplate()) return false;

  const auto slot_names = GetSlotNames();
  if (slot_names != other.GetSlotNames()) return false;

  for (const auto& slot_name : slot_names) {
    if (templ.IsSinglefieldSlot(slot_name)) {
      auto status_or_this_value = GetSlotValue(slot_name);
      if (!status_or_this_value.ok()) return false;
      auto status_or_other_value = other.GetSlotValue(slot_name);
      if (!status_or_other_value.ok()) return false;
      if (status_or_this_value.value() != status_or_other_value.value()) {
        return false;
      }
    } else {
      auto status_or_this_values = GetSlotValues(slot_name);
      if (!status_or_this_values.ok()) return false;
      auto status_or_other_values = other.GetSlotValues(slot_name);
      if (!status_or_other_values.ok()) return false;
      if (status_or_this_values.value() != status_or_other_values.value()) {
        return false;
      }
    }
  }

  return true;
}

std::ostream& operator<<(std::ostream& os, const Fact& fact)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(fact.GetEnvironment()->mutex()) {
  return os << fact.DebugString();
}

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic
