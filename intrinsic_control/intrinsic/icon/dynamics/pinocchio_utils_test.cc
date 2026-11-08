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

#include "intrinsic/icon/dynamics/pinocchio_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "pinocchio/parsers/sample-models.hpp"

namespace intrinsic::icon {
namespace {

using ::intrinsic::eigenmath::testing::IsApprox;
using ::testing::HasSubstr;

// Test the configuration to tangent vector conversion for a floating base
// humanoid, where the number of DoF in tangent and configuration space actually
// differs.
TEST(PinocchioUtilsTest, ConfigurationVectorConversionWorks) {
  pinocchio::Model model;
  pinocchio::buildModels::humanoid(model);

  // Initialize a known minimal coordinate vector as Ones.
  eigenmath::VectorXd tangent_vector = eigenmath::VectorXd::Ones(model.nv);

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      eigenmath::VectorXd config_vector,
      ToPinocchioConfigVector(model, tangent_vector));

  // In configuration (=manifold) formulation, base orientation should be equal
  // to the following known quaternion:
  const eigenmath::Vector4d base_orientation_quat_coeffs(0.439802, 0.439802,
                                                         0.439802, 0.647859);
  EXPECT_THAT(config_vector.segment(3, 4),
              IsApprox(base_orientation_quat_coeffs, /*tolerance*/ 1e-3));

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      eigenmath::VectorXd tangent_vector_roundtrip,
      FromPinocchioConfigVector(model, config_vector));

  EXPECT_THAT(tangent_vector_roundtrip, IsApprox(tangent_vector));
}

TEST(PinocchioUtilsTest, ConfigurationVectorConversionFailsForSizeMismatch) {
  pinocchio::Model model;
  pinocchio::buildModels::humanoid(model);

  eigenmath::VectorXd tangent_vector_wrong_size =
      eigenmath::VectorXd::Zero(model.nv - 1);

  EXPECT_THAT(ToPinocchioConfigVector(model, tangent_vector_wrong_size),
              icon::RealtimeStatusIs(absl::StatusCode::kFailedPrecondition,
                                     HasSubstr("dimensions mismatch")));
}

TEST(PinocchioUtilsTest, TangentVectorConversionFailsForSizeMismatch) {
  pinocchio::Model model;
  pinocchio::buildModels::humanoid(model);

  eigenmath::VectorXd config_vector_wrong_size =
      eigenmath::VectorXd::Zero(model.nq - 1);

  EXPECT_THAT(FromPinocchioConfigVector(model, config_vector_wrong_size),
              icon::RealtimeStatusIs(absl::StatusCode::kFailedPrecondition,
                                     HasSubstr("dimensions mismatch")));
}

}  // namespace
}  // namespace intrinsic::icon
