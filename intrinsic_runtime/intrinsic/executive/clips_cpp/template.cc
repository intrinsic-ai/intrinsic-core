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

#include "intrinsic/executive/clips_cpp/template.h"

#include <ostream>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/clips_cpp/value_util.h"
// Keep CLIPS at the bottom to prevent macro pollution.
#include "clips/clips.h"

namespace intrinsic {
namespace executive {
namespace clips {

Template::Template(Environment* env, void* c_template)
    : env_(env), c_template_(c_template) {}

std::string Template::GetName() const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  return EnvGetDeftemplateName(env_->GetCPtr(), c_template_);
}

std::string Template::DebugString() const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  return EnvGetDeftemplatePPForm(env_->GetCPtr(), c_template_);
}

std::vector<std::string> Template::GetSlotNames() const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  DATA_OBJECT dataobj;
  EnvDeftemplateSlotNames(env_->GetCPtr(), c_template_, &dataobj);
  return internal::FilterDataObjectStrings(&dataobj);
}

Values Template::GetSlotTypes(const std::string& slot_name) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  DATA_OBJECT dataobj;
  EnvDeftemplateSlotTypes(env_->GetCPtr(), c_template_, slot_name.c_str(),
                          &dataobj);
  return internal::DataObjectToValues(&dataobj);
}

Values Template::GetSlotAllowedValues(const std::string& slot_name) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  DATA_OBJECT dataobj;
  EnvDeftemplateSlotAllowedValues(env_->GetCPtr(), c_template_,
                                  slot_name.c_str(), &dataobj);
  return internal::DataObjectToValues(&dataobj);
}

Values Template::GetSlotCardinality(const std::string& slot_name) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  DATA_OBJECT dataobj;
  EnvDeftemplateSlotCardinality(env_->GetCPtr(), c_template_, slot_name.c_str(),
                                &dataobj);
  return internal::DataObjectToValues(&dataobj);
}

Template::DefaultType Template::GetSlotDefaultType(const std::string& slot_name)
    const ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  int default_type = EnvDeftemplateSlotDefaultP(env_->GetCPtr(), c_template_,
                                                slot_name.c_str());
  switch (default_type) {
    case STATIC_DEFAULT:
      return DefaultType::kStaticDefault;
    case DYNAMIC_DEFAULT:
      return DefaultType::kDynamicDefault;
    default:
      return DefaultType::kNoDefault;
  }
}

Values Template::GetSlotDefaultValue(const std::string& slot_name) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  DATA_OBJECT dataobj;
  EnvDeftemplateSlotDefaultValue(env_->GetCPtr(), c_template_,
                                 slot_name.c_str(), &dataobj);
  return internal::DataObjectToValues(&dataobj);
}

Values Template::GetSlotRange(const std::string& slot_name) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  DATA_OBJECT dataobj;
  EnvDeftemplateSlotRange(env_->GetCPtr(), c_template_, slot_name.c_str(),
                          &dataobj);
  return internal::DataObjectToValues(&dataobj);
}

bool Template::SlotExists(const std::string& slot_name) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  return EnvDeftemplateSlotExistP(env_->GetCPtr(), c_template_,
                                  slot_name.c_str()) != 0;
}

bool Template::IsImplied() const {
  const auto* typed_c_template = static_cast<struct deftemplate*>(c_template_);
  return typed_c_template->implied;
}

bool Template::IsMultifieldSlot(const std::string& slot_name) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  return EnvDeftemplateSlotMultiP(env_->GetCPtr(), c_template_,
                                  slot_name.c_str()) != 0;
}

bool Template::IsSinglefieldSlot(const std::string& slot_name) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  return EnvDeftemplateSlotSingleP(env_->GetCPtr(), c_template_,
                                   slot_name.c_str()) != 0;
}

std::ostream& operator<<(std::ostream& os, const Template& tpl)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(tpl.env_->mutex()) {
  return os << tpl.DebugString();
}

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic
