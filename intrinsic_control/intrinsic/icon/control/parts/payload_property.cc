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

#include "intrinsic/icon/control/parts/payload_property.h"

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "cppitertools/enumerate.hpp"
#include "intrinsic/eigenmath/so3.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/common/part_properties.h"
#include "intrinsic/icon/control/parts/part_property_registry.h"
#include "intrinsic/icon/control/parts/realtime_part_property_access.h"
#include "intrinsic/icon/control/parts/realtime_robot_payload.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/robot_payload/robot_payload.h"
#include "intrinsic/world/robot_payload/robot_payload_base.h"

namespace intrinsic::icon {

namespace {

RealtimeStatusOr<double> RobotPayloadById(const RobotPayloadBase& payload,
                                          int id) {
  switch (id) {
    case 0:
      return payload.mass();
    case 1:
      return payload.tip_t_cog().translation().x();
    case 2:
      return payload.tip_t_cog().translation().y();
    case 3:
      return payload.tip_t_cog().translation().z();
    case 4:
      return payload.tip_t_cog().quaternion().x();
    case 5:
      return payload.tip_t_cog().quaternion().y();
    case 6:
      return payload.tip_t_cog().quaternion().z();
    case 7:
      return payload.tip_t_cog().quaternion().w();
    case 8:
      return payload.inertia()(0, 0);
    case 9:
      return payload.inertia()(1, 1);
    case 10:
      return payload.inertia()(2, 2);
    case 11:
      return payload.inertia()(0, 1);
    case 12:
      return payload.inertia()(0, 2);
    case 13:
      return payload.inertia()(1, 2);
    default:
      return FailedPreconditionError(absl::StrCat("Invalid id: ", id));
  }
}

RealtimeStatusOr<RealtimeRobotPayload> FromArray(
    const std::array<double, kNumPayloadProperties>& array) {
  eigenmath::Quaterniond quaternion(array[7], array[4], array[5], array[6]);
  if (!quaternion.coeffs().allFinite()) {
    return FailedPreconditionError(
        "Payload quaternion must have finite coefficients.");
  }
  const double max_coeff = quaternion.coeffs().cwiseAbs().maxCoeff();
  if (!(max_coeff > 0.0)) {
    return FailedPreconditionError(
        "Payload quaternion must have non-zero norm.");
  }
  quaternion.coeffs() /= max_coeff;
  quaternion.normalize();
  if (!eigenmath::SO3d::IsNormalizedQuaternion(quaternion)) {
    return FailedPreconditionError(
        "Payload quaternion could not be normalized.");
  }

  eigenmath::Matrix3d inertia;
  inertia << array[8], array[11], array[12],  // line 1
      array[11], array[9], array[13],         // line 2
      array[12], array[13], array[10];        // line 3

  return RealtimeRobotPayload::Create(
      /*mass_kg=*/array[0],
      Pose3d(quaternion, eigenmath::Vector3d(array[1], array[2], array[3]),
             eigenmath::kDoNotNormalize),
      inertia);
}

}  // namespace

PayloadProperty::PayloadProperty(const Ids& ids, PartPropertyId has_value_id)
    : ids_(ids), has_value_id_(has_value_id) {}

absl::StatusOr<PayloadProperty> PayloadProperty::Create(
    absl::string_view payload_name,
    const std::optional<RobotPayloadBase>& initial_payload,
    PartPropertyRegistry& registry) {
  Ids ids;

  bool has_value = initial_payload.has_value();
  RobotPayloadBase payload = initial_payload.value_or(RobotPayloadBase());

  for (const auto& [i, suffix] : iter::enumerate(kNameSuffixes)) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(const double initial_value,
                                  RobotPayloadById(payload, i));
    INTR_ASSIGN_OR_RETURN(
        ids[i], registry.RegisterDoubleProperty(
                    absl::StrCat(payload_name, "_", suffix), initial_value));
  }
  PartPropertyId has_value_id;
  INTR_ASSIGN_OR_RETURN(
      has_value_id,
      registry.RegisterBoolProperty(
          absl::StrCat(payload_name, "_", kHasValueSuffix), has_value));
  return PayloadProperty(ids, has_value_id);
}

RealtimeStatusOr<std::optional<RealtimeRobotPayload>> PayloadProperty::Read(
    const RealtimePartPropertyAccess& access) const {
  INTRINSIC_RT_ASSIGN_OR_RETURN(const bool has_value,
                                access.GetBoolProperty(has_value_id_));
  if (!has_value) {
    return std::optional<RealtimeRobotPayload>();
  }
  std::array<double, kNumPayloadProperties> array;
  for (const auto& [i, id] : iter::enumerate(ids_)) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(array[i], access.GetDoubleProperty(id));
  }
  INTRINSIC_RT_ASSIGN_OR_RETURN(RealtimeRobotPayload payload, FromArray(array));
  return std::optional<RealtimeRobotPayload>(std::move(payload));
}

RealtimeStatus PayloadProperty::Write(
    RealtimePartPropertyAccess& access,
    const std::optional<RobotPayloadBase>& payload) const {
  if (!payload.has_value()) {
    return access.SetBoolProperty(has_value_id_, false);
  }

  INTRINSIC_RT_RETURN_IF_ERROR(access.SetBoolProperty(has_value_id_, true));
  for (const auto& [i, id] : iter::enumerate(ids_)) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(const double value,
                                  RobotPayloadById(payload.value(), i));
    INTRINSIC_RT_RETURN_IF_ERROR(access.SetDoubleProperty(ids_[i], value));
  }
  return OkStatus();
}

absl::StatusOr<absl::flat_hash_map<std::string, PartPropertyValue>>
ToPartPropertyMap(const std::optional<RobotPayload>& payload,
                  absl::string_view name) {
  absl::flat_hash_map<std::string, PartPropertyValue> map;
  for (const auto& [i, suffix] : iter::enumerate(kNameSuffixes)) {
    INTR_ASSIGN_OR_RETURN(
        const double value,
        RobotPayloadById(payload.value_or(RobotPayload()), i));
    map[absl::StrCat(name, "_", suffix)] = value;
  }
  map[absl::StrCat(name, "_", kHasValueSuffix)] = payload.has_value();
  return map;
}

struct VariantToDouble {
  absl::StatusOr<double> operator()(double value) { return value; }
  absl::StatusOr<double> operator()(bool value) {
    return absl::FailedPreconditionError("Expected double property, got bool.");
  }
};
struct VariantToBool {
  absl::StatusOr<bool> operator()(double value) {
    return absl::FailedPreconditionError("Expected bool property, got double.");
  }
  absl::StatusOr<bool> operator()(bool value) { return value; }
};

absl::StatusOr<std::optional<RobotPayload>> FromPartPropertyMap(
    const absl::flat_hash_map<std::string, PartPropertyValue>& map,
    absl::string_view name) {
  const auto it = map.find(absl::StrCat(name, "_", kHasValueSuffix));
  if (it == map.end()) {
    return absl::NotFoundError(
        absl::StrCat("Property not found: ", name, "_", kHasValueSuffix));
  }
  INTR_ASSIGN_OR_RETURN(const bool has_value,
                        std::visit(VariantToBool(), it->second));
  if (!has_value) {
    return std::nullopt;
  }

  std::array<double, kNumPayloadProperties> array;
  for (const auto& [i, suffix] : iter::enumerate(kNameSuffixes)) {
    const auto it = map.find(absl::StrCat(name, "_", suffix));
    if (it == map.end()) {
      return absl::NotFoundError(
          absl::StrCat("Property not found: ", name, "_", suffix));
    }
    INTR_ASSIGN_OR_RETURN(array[i], std::visit(VariantToDouble(), it->second));
  }
  INTRINSIC_RT_ASSIGN_OR_RETURN(RealtimeRobotPayload payload, FromArray(array));
  return RobotPayload::Create(payload.mass(), payload.tip_t_cog(),
                              payload.inertia());
}

}  // namespace intrinsic::icon
