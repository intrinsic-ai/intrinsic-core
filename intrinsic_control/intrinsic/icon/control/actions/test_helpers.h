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

#ifndef INTRINSIC_ICON_CONTROL_ACTIONS_TEST_HELPERS_H_
#define INTRINSIC_ICON_CONTROL_ACTIONS_TEST_HELPERS_H_

#include <vector>

#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/parts/realtime_part_interface.h"
#include "intrinsic/icon/proto/v1/types.pb.h"

namespace intrinsic::icon {

// Populates a PartConfig with automatically-generated config protos, based on
// `part`'s feature interfaces.
//
// Note that this function cannot generate fully valid PartConfig protos for all
// feature interfaces. For example, the config for the JointVelocity feature
// interface contains the number of joints, but the feature interface itself has
// no way of getting that information.
intrinsic_proto::icon::v1::PartConfig PartConfigFromPart(
    absl::string_view name, const RealtimePartInterface& part,
    absl::string_view part_type_name = "fake_part_for_tests");

// Checks whether the joint position trajectories are withtin the expected
// limits up to a desired tolerance [`lower_limit_value`-`tolerance`,
// `upper_limit_value`+`tolerance`]. Returns true on success.
bool EvaluateTrajectoryWithinLimits(
    const std::vector<eigenmath::VectorNd>& joint_positions,
    double lower_limit_value, double upper_limit_value, double tolerance);

// Approximates the first derivatives of a time series via central finite
// differences and checks whether they are withtin the expected bounds up to a
// desired tolerance [-`limit_value`-`tolerance`, `limit_value`+`tolerance`].
// Returns true on success.
bool EvaluateFirstDerivativesWithinLimits(
    const std::vector<eigenmath::VectorNd>& joint_positions,
    double sampling_time_sec, int accuracy_order, double limit_value,
    double tolerance);

// Approximates the second derivatives of a time series via central finite
// differences and checks whether they are withtin the expected bounds up to a
// desired tolerance [-`limit_value`-`tolerance`, `limit_value`+`tolerance`]
// Returns true on success.
bool EvaluateSecondDerivativesWithinLimits(
    const std::vector<eigenmath::VectorNd>& joint_positions,
    double sampling_time_sec, int accuracy_order, double limit_value,
    double tolerance);

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ACTIONS_TEST_HELPERS_H_
