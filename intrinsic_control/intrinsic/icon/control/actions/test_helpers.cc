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

#include "intrinsic/icon/control/actions/test_helpers.h"

#include <cstddef>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/parts/realtime_part_factory_common.h"
#include "intrinsic/icon/control/parts/realtime_part_interface.h"
#include "intrinsic/icon/proto/generic_part_config.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/math/signals/numerical_differentiation.h"

namespace intrinsic::icon {

intrinsic_proto::icon::v1::PartConfig PartConfigFromPart(
    absl::string_view name, const RealtimePartInterface& part) {
  intrinsic_proto::icon::v1::PartConfig part_config;
  part_config.set_name(name);
  part_config.set_part_type_name("fake_part_for_tests");
  part_config.set_hardware_resource_name("fake_hardware_resource_for_tests");
  for (const intrinsic_proto::icon::v1::FeatureInterfaceTypes&
           feature_interface :
       part.GetFeatureInterfaces().SupportedFeatureInterfaceTypes()) {
    part_config.add_feature_interfaces(feature_interface);
  }
  *part_config.mutable_generic_config() =
      ExtractGenericConfig(part.GetFeatureInterfaces());

  return part_config;
}

bool EvaluateTrajectoryWithinLimits(
    const std::vector<eigenmath::VectorNd>& joint_positions,
    const double lower_limit_value, const double upper_limit_value,
    const double tolerance) {
  for (const auto& joint_position : joint_positions) {
    for (size_t i = 0; i < joint_position.size(); ++i) {
      if ((joint_position[i] > upper_limit_value + tolerance) ||
          (joint_position[i] < lower_limit_value - tolerance))
        return false;
    }
  }
  return true;
}

bool EvaluateFirstDerivativesWithinLimits(
    const std::vector<eigenmath::VectorNd>& joint_positions,
    const double sampling_time_sec, int accuracy_order,
    const double limit_value, const double tolerance) {
  absl::StatusOr<std::vector<eigenmath::VectorNd>> first_derivatives =
      intrinsic::ComputeFirstDerivativesCentral(
          absl::MakeConstSpan(joint_positions), sampling_time_sec,
          accuracy_order);
  if (!first_derivatives.ok()) return false;
  for (const auto& first_derivative : first_derivatives.value()) {
    for (size_t i = 0; i < first_derivative.size(); ++i) {
      if ((first_derivative[i] > limit_value + tolerance) ||
          (first_derivative[i] < -limit_value - tolerance))
        return false;
    }
  }
  return true;
}

bool EvaluateSecondDerivativesWithinLimits(
    const std::vector<eigenmath::VectorNd>& joint_positions,
    const double sampling_time_sec, int accuracy_order,
    const double limit_value, const double tolerance) {
  absl::StatusOr<std::vector<eigenmath::VectorNd>> second_derivatives =
      intrinsic::ComputeSecondDerivativesCentral(
          absl::MakeConstSpan(joint_positions), sampling_time_sec,
          accuracy_order);
  if (!second_derivatives.ok()) return false;
  for (const auto& second_derivative : second_derivatives.value()) {
    for (size_t i = 0; i < second_derivative.size(); ++i) {
      if ((second_derivative[i] > limit_value + tolerance) ||
          (second_derivative[i] < -limit_value - tolerance))
        return false;
    }
  }
  return true;
}

}  // namespace intrinsic::icon
