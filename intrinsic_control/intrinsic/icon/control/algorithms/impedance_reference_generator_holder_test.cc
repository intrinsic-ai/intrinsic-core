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

#include "intrinsic/icon/control/algorithms/impedance_reference_generator_holder.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/control/algorithms/position_reflexxes_cartesian_impedance_reference_generator.h"
#include "intrinsic/icon/control/algorithms/quintic_spline_cartesian_impedance_reference_generator.h"
#include "intrinsic/icon/control/algorithms/reflexxes_nullspace_reference_generator.h"
#include "intrinsic/icon/control/algorithms/simple_cartesian_impedance_reference_generator.h"
#include "intrinsic/icon/control/algorithms/velocity_reflexxes_cartesian_impedance_reference_generator.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::WhenDynamicCastTo;

const double kFrequencyHz = 100;
const int kNumJoints = 7;

class ImpedanceReferenceGeneratorHolderTest : public ::testing::Test {
 public:
  ImpedanceReferenceGeneratorHolderTest()
      : impedance_reference_generator_holder_(kFrequencyHz) {}

 protected:
  cartesian_impedance::ReferenceGenerators reference_generators_;
  ImpedanceReferenceGeneratorHolder impedance_reference_generator_holder_;
};

TEST_F(ImpedanceReferenceGeneratorHolderTest,
       WorksForCartesianPositionReflexxes) {
  reference_generators_.cartesian_reference_generator = cartesian_impedance::
      CartesianPositionReflexxesImpedanceGeneratorParameters{};

  EXPECT_OK(impedance_reference_generator_holder_.ConfigureReferenceGenerator(
      reference_generators_.cartesian_reference_generator));

  EXPECT_THAT(
      impedance_reference_generator_holder_.GetActiveReferenceGenerator(),
      WhenDynamicCastTo<
          const PositionReflexxesCartesianImpedanceReferenceGenerator&>(_));
}

TEST_F(ImpedanceReferenceGeneratorHolderTest,
       WorksForCartesianVelocityReflexxes) {
  reference_generators_.cartesian_reference_generator = cartesian_impedance::
      CartesianVelocityReflexxesImpedanceGeneratorParameters{};

  EXPECT_OK(impedance_reference_generator_holder_.ConfigureReferenceGenerator(
      reference_generators_.cartesian_reference_generator));

  EXPECT_THAT(
      impedance_reference_generator_holder_.GetActiveReferenceGenerator(),
      WhenDynamicCastTo<
          const VelocityReflexxesCartesianImpedanceReferenceGenerator&>(_));
}

TEST_F(ImpedanceReferenceGeneratorHolderTest, WorksForSimpleImpedance) {
  reference_generators_.cartesian_reference_generator =
      cartesian_impedance::SimpleImpedanceGeneratorParameters{
          .lowpass_filter_constant = 0.5};

  EXPECT_OK(impedance_reference_generator_holder_.ConfigureReferenceGenerator(
      reference_generators_.cartesian_reference_generator));

  EXPECT_THAT(
      impedance_reference_generator_holder_.GetActiveReferenceGenerator(),
      WhenDynamicCastTo<const SimpleCartesianImpedanceReferenceGenerator&>(_));
}

TEST_F(ImpedanceReferenceGeneratorHolderTest,
       FailsForSimpleImpedanceWithInvalidParam) {
  reference_generators_.cartesian_reference_generator =
      cartesian_impedance::SimpleImpedanceGeneratorParameters{
          .lowpass_filter_constant = -0.5};

  EXPECT_THAT(impedance_reference_generator_holder_.ConfigureReferenceGenerator(
                  reference_generators_.cartesian_reference_generator),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(ImpedanceReferenceGeneratorHolderTest, WorksForQuinticSpliner) {
  reference_generators_.cartesian_reference_generator =
      cartesian_impedance::QuinticSplineCartesianGeneratorParameters{
          .movement_duration_seconds = 0.5};

  EXPECT_OK(impedance_reference_generator_holder_.ConfigureReferenceGenerator(
      reference_generators_.cartesian_reference_generator));

  EXPECT_THAT(
      impedance_reference_generator_holder_.GetActiveReferenceGenerator(),
      WhenDynamicCastTo<
          const QuinticSplineCartesianImpedanceReferenceGenerator&>(_));
}

TEST_F(ImpedanceReferenceGeneratorHolderTest,
       FailsForQuinticSplinerWithInvalidParam) {
  reference_generators_.cartesian_reference_generator =
      cartesian_impedance::QuinticSplineCartesianGeneratorParameters{
          .movement_duration_seconds = -0.5};

  EXPECT_THAT(impedance_reference_generator_holder_.ConfigureReferenceGenerator(
                  reference_generators_.cartesian_reference_generator),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

class NullspaceReferenceGeneratorHolderTest : public ::testing::Test {
 public:
  NullspaceReferenceGeneratorHolderTest()
      : impedance_reference_generator_holder_(kNumJoints, kFrequencyHz) {}

 protected:
  cartesian_impedance::ReferenceGenerators reference_generators_;
  NullspaceReferenceGeneratorHolder impedance_reference_generator_holder_;
};

TEST_F(NullspaceReferenceGeneratorHolderTest, WorksForReflexxes) {
  reference_generators_.nullspace_reference_generator =
      cartesian_impedance::ReflexxesNullspaceReferenceGeneratorParameters{};

  EXPECT_OK(impedance_reference_generator_holder_.ConfigureReferenceGenerator(
      reference_generators_.nullspace_reference_generator));

  EXPECT_THAT(
      impedance_reference_generator_holder_.GetActiveReferenceGenerator(),
      WhenDynamicCastTo<const ReflexxesNullspaceReferenceGenerator&>(_));
}

TEST_F(NullspaceReferenceGeneratorHolderTest, WorksForSimpleImpedance) {
  reference_generators_.nullspace_reference_generator =
      cartesian_impedance::SimpleNullspaceReferenceGeneratorParameters{
          .lowpass_filter_constant = 0.5};

  EXPECT_OK(impedance_reference_generator_holder_.ConfigureReferenceGenerator(
      reference_generators_.nullspace_reference_generator));

  EXPECT_THAT(
      impedance_reference_generator_holder_.GetActiveReferenceGenerator(),
      WhenDynamicCastTo<const SimpleNullspaceReferenceGenerator&>(_));
}

TEST_F(NullspaceReferenceGeneratorHolderTest,
       FailsForSimpleImpedanceWithInvalidParam) {
  reference_generators_.nullspace_reference_generator =
      cartesian_impedance::SimpleNullspaceReferenceGeneratorParameters{
          .lowpass_filter_constant = -0.5};

  EXPECT_THAT(impedance_reference_generator_holder_.ConfigureReferenceGenerator(
                  reference_generators_.nullspace_reference_generator),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(NullspaceReferenceGeneratorHolderTest, WorksForQuinticSpliner) {
  reference_generators_.nullspace_reference_generator =
      cartesian_impedance::QuinticSplineNullspaceReferenceGeneratorParameters{
          .movement_duration_seconds = 0.5};

  EXPECT_OK(impedance_reference_generator_holder_.ConfigureReferenceGenerator(
      reference_generators_.nullspace_reference_generator));

  EXPECT_THAT(
      impedance_reference_generator_holder_.GetActiveReferenceGenerator(),
      WhenDynamicCastTo<const QuinticSplineNullspaceReferenceGenerator&>(_));
}

TEST_F(NullspaceReferenceGeneratorHolderTest,
       FailsForQuinticSplinerWithInvalidParam) {
  reference_generators_.nullspace_reference_generator =
      cartesian_impedance::QuinticSplineNullspaceReferenceGeneratorParameters{
          .movement_duration_seconds = -0.5};

  EXPECT_THAT(impedance_reference_generator_holder_.ConfigureReferenceGenerator(
                  reference_generators_.nullspace_reference_generator),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

}  // namespace
}  // namespace intrinsic::icon
