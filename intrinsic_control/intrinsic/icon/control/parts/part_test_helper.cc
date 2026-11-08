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

#include "intrinsic/icon/control/parts/part_test_helper.h"

#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/fixed_array.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "intrinsic/icon/common/part_properties.h"
#include "intrinsic/icon/control/context.h"
#include "intrinsic/icon/control/parts/feature_interface_registry.h"
#include "intrinsic/icon/control/parts/part_property_registry.h"
#include "intrinsic/icon/control/parts/realtime_part.h"
#include "intrinsic/icon/control/parts/realtime_part_factory_common.h"
#include "intrinsic/icon/control/parts/realtime_part_from_proto_factory_registry.h"
#include "intrinsic/icon/control/realtime_bridge_types.h"
#include "intrinsic/icon/control/safety/extern/safety_status.fbs.h"
#include "intrinsic/icon/control/safety/safety_messages.fbs.h"
#include "intrinsic/icon/server/runtime_options.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {
namespace {
constexpr char kDefaultPartName[] = "default_part_name";
}

absl::StatusOr<bool> PartPropertyAccess::GetBoolProperty(
    absl::string_view name) const {
  auto id = property_name_to_index_.find(name);
  if (id == property_name_to_index_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Part property '", name, "' does not exist."));
  }
  if (id->second >= rt_to_non_rt_values_.size()) {
    return absl::InternalError(
        absl::StrCat("Invalid ID for part '", name, "' (", id->second, ")"));
  }
  const auto& property_value = rt_to_non_rt_values_.at(id->second);
  if (!std::holds_alternative<bool>(property_value)) {
    return absl::FailedPreconditionError(
        absl::StrCat("Type of part property '", name, "' is not boolean."));
  }
  return std::get<bool>(property_value);
}

absl::StatusOr<double> PartPropertyAccess::GetDoubleProperty(
    absl::string_view name) const {
  auto id = property_name_to_index_.find(name);
  if (id == property_name_to_index_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Part property '", name, "' does not exist."));
  }
  if (id->second >= rt_to_non_rt_values_.size()) {
    return absl::InternalError(
        absl::StrCat("Invalid ID for part '", name, "' (", id->second, ")"));
  }
  const auto& property_value = rt_to_non_rt_values_.at(id->second);
  if (!std::holds_alternative<double>(property_value)) {
    return absl::FailedPreconditionError(
        absl::StrCat("Type of part property '", name, "' is not double."));
  }
  return std::get<double>(property_value);
}

absl::Status PartPropertyAccess::SetBoolProperty(absl::string_view name,
                                                 bool value) {
  auto id = property_name_to_index_.find(name);
  if (id == property_name_to_index_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Part property '", name, "' does not exist."));
  }
  if (id->second >= non_rt_to_rt_values_.size()) {
    return absl::InternalError(
        absl::StrCat("Invalid ID for part '", name, "' (", id->second, ")"));
  }
  auto& property_value = non_rt_to_rt_values_.at(id->second);
  if (!std::holds_alternative<bool>(property_value)) {
    return absl::FailedPreconditionError(
        absl::StrCat("Type of part property '", name, "' is not boolean."));
  }
  property_value = value;
  return absl::OkStatus();
}

absl::Status PartPropertyAccess::SetDoubleProperty(absl::string_view name,
                                                   double value) {
  auto id = property_name_to_index_.find(name);
  if (id == property_name_to_index_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Part property '", name, "' does not exist."));
  }
  if (id->second >= non_rt_to_rt_values_.size()) {
    return absl::InternalError(
        absl::StrCat("Invalid ID for part '", name, "' (", id->second, ")"));
  }
  auto& property_value = non_rt_to_rt_values_.at(id->second);
  if (!std::holds_alternative<double>(property_value)) {
    return absl::FailedPreconditionError(
        absl::StrCat("Type of part property '", name, "' is not double."));
  }
  property_value = value;
  return absl::OkStatus();
}

// static
absl::StatusOr<std::unique_ptr<PartTestHelper>>
PartTestHelper::CreateFromProtoFactory(
    Parameters helper_parameters, absl::string_view part_type_name,
    const google::protobuf::Message& config) {
  auto factory =
      GetGlobalRealtimePartFromProtoFactoryRegistry().Get(part_type_name);
  if (factory == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("No factory for part type '", part_type_name,
                     "'. Did you link your part's register target into the "
                     "test and mark it alwayslink=True?"));
  }
  google::protobuf::Any any_config;
  if (!any_config.PackFrom(config)) {
    return absl::InternalError("Failed to pack part config into Any");
  }
  return PartTestHelper::CreateFromFactory(
      helper_parameters,
      [&](PartFactoryContext ctx) { return factory(ctx, any_config); });
}

// static
absl::StatusOr<std::unique_ptr<PartTestHelper>>
PartTestHelper::CreateFromFactory(
    Parameters helper_parameters,
    absl::FunctionRef<
        absl::StatusOr<PartPtrAndPartConfig>(PartFactoryContext context)>
        factory) {
  Context context = helper_parameters.context.value_or(Context(
      /*service_collection=*/nullptr,
      /*hardware_module_manager=*/{},
      /*runtime_options=*/icon::ServerRuntimeOptions()));
  std::string part_name =
      helper_parameters.part_name.value_or(kDefaultPartName);

  std::vector<PartPropertyInitialData> property_initial_data;
  PartPropertyRegistry property_registry(property_initial_data);
  INTR_ASSIGN_OR_RETURN(
      PartPtrAndPartConfig part_and_config_proto,
      factory(PartFactoryContext{
          .part_name = part_name,
          .context = context,
          .property_registry = property_registry,
          .control_frequency_hz = helper_parameters.control_frequency_hz,
          .hardware_resource_name =
              helper_parameters.hardware_resource_name.value_or("")}));

  std::vector<PartPropertyValue> properties;
  properties.reserve(property_initial_data.size());
  absl::c_transform(property_initial_data, std::back_inserter(properties),
                    [](const PartPropertyInitialData& data) {
                      return PartPropertyValue{data.initial_value};
                    });

  absl::flat_hash_map<std::string, size_t> property_name_to_index;
  for (size_t property_index = 0; property_index < property_initial_data.size();
       ++property_index) {
    property_name_to_index[property_initial_data.at(property_index).name] =
        property_index;
  }
  // WrapUnique because this ctor is private.
  return absl::WrapUnique(new PartTestHelper(
      /*context=*/std::move(context),
      /*property_name_to_index=*/std::move(property_name_to_index),
      /*part_name=*/part_name,
      /*part=*/std::move(part_and_config_proto.part_ptr),
      /*part_config=*/std::move(part_and_config_proto.config),
      /*non_rt_to_rt_values=*/properties,
      /*rt_to_non_rt_values=*/properties));
}

FeatureInterfaceRegistry& PartTestHelper::GetPartFeatureInterfaces() {
  return part_.GetFeatureInterfaces();
}

const FeatureInterfaceRegistry& PartTestHelper::GetPartFeatureInterfaces()
    const {
  return part_.GetFeatureInterfaces();
}

PartPropertyAccess PartTestHelper::GetPartPropertyAccess() {
  absl::FixedArray<PartPropertyValue>* non_rt_to_rt_values_write =
      non_rt_to_rt_buffer_.GetFreeBuffer();
  absl::FixedArray<PartPropertyValue>* rt_to_non_rt_values_read;
  rt_to_non_rt_buffer_.GetActiveBuffer(&rt_to_non_rt_values_read);
  return PartPropertyAccess(
      /*property_name_to_index=*/property_name_to_index_,
      /*non_rt_to_rt_values=*/*non_rt_to_rt_values_write,
      /*rt_to_non_rt_values=*/*rt_to_non_rt_values_read);
}

SafetyStatus PartTestHelper::GetSafetyStatus() const { return safety_status_; }

void PartTestHelper::SetModeOfSafeOperation(
    ::intrinsic_fbs::ModeOfSafeOperation new_mode) {
  safety_status_.mode_of_safe_operation = new_mode;
}

void PartTestHelper::SetEnableButtonStatus(
    ::intrinsic_fbs::ButtonStatus new_button_status) {
  safety_status_.enable_button_status = new_button_status;
}

void PartTestHelper::SetEstopButtonStatus(
    ::intrinsic_fbs::ButtonStatus new_button_status) {
  safety_status_.estop_button_status = new_button_status;
}

void PartTestHelper::SetRequestedBehavior(
    ::intrinsic_fbs::RequestedBehavior new_behavior) {
  safety_status_.requested_behavior = new_behavior;
}

}  // namespace intrinsic::icon
