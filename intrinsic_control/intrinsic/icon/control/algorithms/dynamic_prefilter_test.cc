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

#include "intrinsic/icon/control/algorithms/dynamic_prefilter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <numeric>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/joint_position_reflexxes.h"
#include "intrinsic/icon/proto/joint_collection_prefilter_config.pb.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/signals/iir_filt.h"
#include "intrinsic/util/proto/parse_text_proto.h"

namespace intrinsic::icon {
namespace {

using eigenmath::testing::IsApprox;
using ::intrinsic::ParseTextProtoOrDie;
using ::testing::HasSubstr;

constexpr int kRacer7TransferFunctionOrder = 4;
constexpr int kRacer7NumDof = 6;
constexpr double kFilterCoeffAccuracy = 1e-12;
constexpr double kStepResponseAccuracy = 1e-6;

// Nominal denominator coefficients for a realistic low-order Racer7 transfer
// function.
static std::array<double, kRacer7TransferFunctionOrder + 1> nominal_coeffs_a{
    1.0, -3.802492667892346, 5.421963965056231, -3.435983349198671,
    0.8165176331872313};

// Create a realistic SiSo Racer7 transfer function with time delay and the
// above coefficients.
absl::StatusOr<IirFilter<double, kRacer7TransferFunctionOrder>>
GetNominalRacer7TransferFunction() {
  //                          5.581e-06 z^-4
  //  H(z) = ------------------------------------------------------
  //         1 - 3.802 z^-1 + 5.422 z^-2 - 3.436 z^-3 + 0.8165 z^-4

  // Coefficients for a normalized 4th order transfer function, which is
  // realistic for a robot manipulator. Input coefficients need to be very
  // accurate, otherwise the step response transients are not correct.
  IirFilterCoefficients<kRacer7TransferFunctionOrder> filter_coefficients{
      .a = nominal_coeffs_a,
      // The sum of the coefficients in `a` results in the only non-zero
      // coefficient in `b`.
      .b = {0.0, 0.0, 0.0, 0.0,
            std::accumulate(nominal_coeffs_a.begin(), nominal_coeffs_a.end(),
                            0.0)}};

  return IirFilter<double, kRacer7TransferFunctionOrder>::Create(
      filter_coefficients);
}

// An example SiSo prefilter config which was computed based on inversion of the
// Racer7 position dynamics. Reach out to giftthaler@ for details.
absl::StatusOr<IirFilter<double, kMaxPrefilterOrder>>
GetRacer7SiSoExamplePrefilter() {
  std::array<double, kMaxPrefilterOrder + 1> b_filter{
      1.3748702386, 0.2715469528,  -5.2080176846, -0.7741667410, 7.4165963790,
      0.7362892495, -4.7059933394, -0.2336080745, 1.1226057932};

  std::array<double, kMaxPrefilterOrder + 1> a_filter{
      1.0, -3.5789473684, 4.8033240997, -2.8651406911, 0.6408867335, 0.0,
      0.0, 0.0,           0.0};

  return IirFilter<double, kMaxPrefilterOrder>::Create(
      IirFilterCoefficients<kMaxPrefilterOrder>{.a = a_filter, .b = b_filter});
}

// Creates the very same prefilter as the previous function, but as a config
// proto, and for 6 degrees of freedom.
intrinsic_proto::icon::JointCollectionPrefilterConfig
GetRacer7SiSoExamplePrefilterConfigProto() {
  intrinsic_proto::icon::JointCollectionPrefilterConfig config =
      ParseTextProtoOrDie(
          R"pb(
            joint_prefilter: {
              coefficients_a: 1
              coefficients_a: -3.5789473684
              coefficients_a: 4.8033240997
              coefficients_a: -2.8651406911
              coefficients_a: 0.6408867335
              coefficients_a: -0
              coefficients_a: 0
              coefficients_a: -0
              coefficients_a: 0
              coefficients_b: 1.3748702386
              coefficients_b: 0.2715469528
              coefficients_b: -5.2080176846
              coefficients_b: -0.774166741
              coefficients_b: 7.416596379
              coefficients_b: 0.7362892495
              coefficients_b: -4.7059933394
              coefficients_b: -0.2336080745
              coefficients_b: 1.1226057932
            }
            joint_prefilter: {
              coefficients_a: 1
              coefficients_a: -3.5789473684
              coefficients_a: 4.8033240997
              coefficients_a: -2.8651406911
              coefficients_a: 0.6408867335
              coefficients_a: -0
              coefficients_a: 0
              coefficients_a: -0
              coefficients_a: 0
              coefficients_b: 1.3748702386
              coefficients_b: 0.2715469528
              coefficients_b: -5.2080176846
              coefficients_b: -0.774166741
              coefficients_b: 7.416596379
              coefficients_b: 0.7362892495
              coefficients_b: -4.7059933394
              coefficients_b: -0.2336080745
              coefficients_b: 1.1226057932
            }
            joint_prefilter: {
              coefficients_a: 1
              coefficients_a: -3.5789473684
              coefficients_a: 4.8033240997
              coefficients_a: -2.8651406911
              coefficients_a: 0.6408867335
              coefficients_a: -0
              coefficients_a: 0
              coefficients_a: -0
              coefficients_a: 0
              coefficients_b: 1.3748702386
              coefficients_b: 0.2715469528
              coefficients_b: -5.2080176846
              coefficients_b: -0.774166741
              coefficients_b: 7.416596379
              coefficients_b: 0.7362892495
              coefficients_b: -4.7059933394
              coefficients_b: -0.2336080745
              coefficients_b: 1.1226057932
            }
            joint_prefilter: {
              coefficients_a: 1
              coefficients_a: -3.5789473684
              coefficients_a: 4.8033240997
              coefficients_a: -2.8651406911
              coefficients_a: 0.6408867335
              coefficients_a: -0
              coefficients_a: 0
              coefficients_a: -0
              coefficients_a: 0
              coefficients_b: 1.3748702386
              coefficients_b: 0.2715469528
              coefficients_b: -5.2080176846
              coefficients_b: -0.774166741
              coefficients_b: 7.416596379
              coefficients_b: 0.7362892495
              coefficients_b: -4.7059933394
              coefficients_b: -0.2336080745
              coefficients_b: 1.1226057932
            }
            joint_prefilter: {
              coefficients_a: 1
              coefficients_a: -3.5789473684
              coefficients_a: 4.8033240997
              coefficients_a: -2.8651406911
              coefficients_a: 0.6408867335
              coefficients_a: -0
              coefficients_a: 0
              coefficients_a: -0
              coefficients_a: 0
              coefficients_b: 1.3748702386
              coefficients_b: 0.2715469528
              coefficients_b: -5.2080176846
              coefficients_b: -0.774166741
              coefficients_b: 7.416596379
              coefficients_b: 0.7362892495
              coefficients_b: -4.7059933394
              coefficients_b: -0.2336080745
              coefficients_b: 1.1226057932
            }
            joint_prefilter: {
              coefficients_a: 1
              coefficients_a: -3.5789473684
              coefficients_a: 4.8033240997
              coefficients_a: -2.8651406911
              coefficients_a: 0.6408867335
              coefficients_a: -0
              coefficients_a: 0
              coefficients_a: -0
              coefficients_a: 0
              coefficients_b: 1.3748702386
              coefficients_b: 0.2715469528
              coefficients_b: -5.2080176846
              coefficients_b: -0.774166741
              coefficients_b: 7.416596379
              coefficients_b: 0.7362892495
              coefficients_b: -4.7059933394
              coefficients_b: -0.2336080745
              coefficients_b: 1.1226057932
            }
          )pb");

  return config;
}

// Creates proto config for the discrete-time transfer function
//                          5.581e-06 z^-4
//  H(z) = ------------------------------------------------------
//         1 - 3.802 z^-1 + 5.422 z^-2 - 3.436 z^-3 + 0.8165 z^-4
intrinsic_proto::DiscreteTimeIirFilterConfig CreateStableExampleProtoConfig() {
  intrinsic_proto::DiscreteTimeIirFilterConfig config;
  std::vector<double> a{1.0, -3.802492667892346, 5.421963965056231,
                        -3.435983349198671, 8.165176331872313e-01};
  std::vector<double> b{0, 0, 0, 0, std::accumulate(a.begin(), a.end(), 0.0)};
  config.mutable_coefficients_a()->Add(a.begin(), a.end());
  config.mutable_coefficients_b()->Add(b.begin(), b.end());
  return config;
}

TEST(SisoPrefilterTest, CreateFromIirFilterConfigWorks) {
  ASSERT_OK(SisoPrefilter::Create(CreateStableExampleProtoConfig()));
}

TEST(SisoPrefilterTest, InitializeAtSteadyStateWorks) {
  ASSERT_OK_AND_ASSIGN(auto filter,
                       SisoPrefilter::Create(CreateStableExampleProtoConfig()));
  const double kInitValue = 1.123;

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  filter.InitializeInSteadyState(kInitValue);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  double input = kInitValue;
  for (int i = 0; i < 10; ++i) {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    // Stepping at steady value does not lead to filter transients.
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(double output,
                                      filter.ComputeControl(input));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    EXPECT_NEAR(output, kInitValue, kStepResponseAccuracy);
    input = output;
  }
}

TEST(JointCollectionPrefilterTest, InitFailsForWrongDimension) {
  ASSERT_OK_AND_ASSIGN(
      JointCollectionPrefilter joint_collection_prefilters_from_proto,
      JointCollectionPrefilter::Create(
          GetRacer7SiSoExamplePrefilterConfigProto()));
  ASSERT_EQ(joint_collection_prefilters_from_proto.num_prefilters(),
            kRacer7NumDof);

  // Compare the step responses.
  eigenmath::VectorNd initial_position(kRacer7NumDof + 1);
  EXPECT_THAT(joint_collection_prefilters_from_proto.InitializeInSteadyState(
                  initial_position),
              RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                               HasSubstr("Size mismatch")));
}

TEST(JointCollectionPrefilterTest, ComputeFailsForWrongDimension) {
  ASSERT_OK_AND_ASSIGN(
      JointCollectionPrefilter joint_collection_prefilters_from_proto,
      JointCollectionPrefilter::Create(
          GetRacer7SiSoExamplePrefilterConfigProto()));
  ASSERT_EQ(joint_collection_prefilters_from_proto.num_prefilters(),
            kRacer7NumDof);

  // Compare the step responses.
  eigenmath::VectorNd initial_position(kRacer7NumDof);
  initial_position.setConstant(0.5);
  EXPECT_OK(joint_collection_prefilters_from_proto.InitializeInSteadyState(
      initial_position));
  eigenmath::VectorNd input(kRacer7NumDof + 1);
  EXPECT_THAT(joint_collection_prefilters_from_proto.ComputeControl(input),
              RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                               HasSubstr("Size mismatch")));
}

// Test that checks that the number of created DoF-filters is correct, and the
// parsed transfer function coefficients match.
TEST(JointCollectionPrefilterTest,
     CreateFromTextprotoResultsInCorrectImplementation) {
  ASSERT_OK_AND_ASSIGN(
      JointCollectionPrefilter joint_collection_prefilters_from_proto,
      JointCollectionPrefilter::Create(
          GetRacer7SiSoExamplePrefilterConfigProto()));
  ASSERT_EQ(joint_collection_prefilters_from_proto.num_prefilters(),
            kRacer7NumDof);

  ASSERT_OK_AND_ASSIGN(auto iir_function_reference,
                       GetRacer7SiSoExamplePrefilter());
  const auto& reference_filter_coeffs =
      iir_function_reference.GetFilterCoefficients();

  // Loop through all filter coefficients per DoF and make they are identical to
  // the reference filter implementation.
  for (int i = 0; i < kRacer7NumDof; ++i) {
    const auto& dof_prefilter =
        joint_collection_prefilters_from_proto.GetDofPrefilter(i);
    const auto& dof_transfer_function = dof_prefilter.GetIirFilter();
    EXPECT_EQ(dof_transfer_function.Order(), 8);
    const auto& filter_coeffs = dof_transfer_function.GetFilterCoefficients();

    for (int j = 0; j < dof_transfer_function.Order() + 1; ++j) {
      EXPECT_NEAR(filter_coeffs.a.at(j), reference_filter_coeffs.a.at(j),
                  kFilterCoeffAccuracy);
      EXPECT_NEAR(filter_coeffs.b.at(j), reference_filter_coeffs.b.at(j),
                  kFilterCoeffAccuracy);
    }
  }

  // Compare the step responses.
  eigenmath::VectorNd initial_position(kRacer7NumDof);
  const double kInitValue = 0.5;
  initial_position.setConstant(kInitValue);
  ASSERT_OK(joint_collection_prefilters_from_proto.InitializeInSteadyState(
      initial_position));
  iir_function_reference.Initialize(kInitValue);

  const double kTargetValue = 1.5;
  eigenmath::VectorNd step_input_target_position(kRacer7NumDof);
  step_input_target_position.setConstant(kTargetValue);

  const int kNumSteps = 100;
  for (int i = 0; i < kNumSteps; ++i) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        double filt_command_ref, iir_function_reference.Step(kTargetValue));
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        auto filt_command_joint_collection,
        joint_collection_prefilters_from_proto.ComputeControl(
            step_input_target_position));
    EXPECT_NEAR(filt_command_joint_collection(0), filt_command_ref,
                kStepResponseAccuracy);
    EXPECT_NEAR(filt_command_joint_collection(1), filt_command_ref,
                kStepResponseAccuracy);
    EXPECT_NEAR(filt_command_joint_collection(2), filt_command_ref,
                kStepResponseAccuracy);
    EXPECT_NEAR(filt_command_joint_collection(3), filt_command_ref,
                kStepResponseAccuracy);
    EXPECT_NEAR(filt_command_joint_collection(4), filt_command_ref,
                kStepResponseAccuracy);
    EXPECT_NEAR(filt_command_joint_collection(5), filt_command_ref,
                kStepResponseAccuracy);
  }
}

constexpr double kMinPos = -3;
constexpr double kMaxPos = 3;
constexpr double kMaxVel = 1;
constexpr double kMaxAcc = 10;
constexpr double kMaxJerk = 500;
constexpr double kTimeStepSec = 400e-6;

// Create some default limits for Reflexxes.
JointLimits DefaultJointLimits() {
  JointLimits limits;
  CHECK_EQ(limits.SetSize(kRacer7NumDof), icon::OkStatus());
  limits.min_position.setConstant(kMinPos);
  limits.max_position.setConstant(kMaxPos);
  limits.max_velocity.setConstant(kMaxVel);
  limits.max_acceleration.setConstant(kMaxAcc);
  limits.max_jerk.setConstant(kMaxJerk);

  return limits;
}

// This test makes sure the dynamic prefilter follows an external (smooth)
// reference without error. This test also includes a nominal Racer7 plant model
// for simulation.
TEST(JointCollectionPrefilterTest, FollowsMotionGeneratedByReflexxes) {
  ASSERT_OK_AND_ASSIGN(auto prefilter,
                       JointCollectionPrefilter::Create(
                           GetRacer7SiSoExamplePrefilterConfigProto()));
  ASSERT_EQ(prefilter.num_prefilters(), kRacer7NumDof);

  // Create some initial joint position that is within limits.
  eigenmath::VectorNd initial_joint_position(kRacer7NumDof);
  initial_joint_position << 0.5, -1, 1.5, -2, 2.5, -2.9;

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(
      prefilter.InitializeInSteadyState(initial_joint_position));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  ASSERT_OK_AND_ASSIGN(auto nominal_racer7_model,
                       GetNominalRacer7TransferFunction());
  std::vector<IirFilter<double, kRacer7TransferFunctionOrder>> racer7_sim_model(
      kRacer7NumDof, nominal_racer7_model);

  for (int i = 0; i < kRacer7NumDof; ++i) {
    racer7_sim_model[i].Initialize(initial_joint_position(i));
  }

  // Get an instance of joint position Reflexxes.
  JointPositionReflexxes reflexxes(kRacer7NumDof, 1. / kTimeStepSec);
  reflexxes.SetSelection(eigenmath::VectorNb::Constant(kRacer7NumDof, 1, true));
  reflexxes.SetLimits(DefaultJointLimits());

  JointStatePVA reflexxes_joint_command;
  ASSERT_OK(reflexxes_joint_command.SetSize(kRacer7NumDof));
  reflexxes_joint_command.position = initial_joint_position;

  JointStatePV joint_target;
  ASSERT_OK(joint_target.SetSize(kRacer7NumDof));
  joint_target.position.setZero();
  reflexxes.SetTarget(joint_target);

  // Remains at initial joint position without much of a transient.
  eigenmath::VectorNd sensed_pos = initial_joint_position;
  for (int i = 0; i < 2000; ++i) {
    reflexxes.SetPrevious(reflexxes_joint_command);
    reflexxes.ComputeSetpoint(&reflexxes_joint_command);

    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        eigenmath::VectorNd prefilter_output_position_command,
        (prefilter.ComputeControl(reflexxes_joint_command.position)));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    // Use the dynamic prefilter output to forward simulate the robot transfer
    // function.
    for (int i = 0; i < racer7_sim_model.size(); ++i) {
      INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
          sensed_pos(i),
          racer7_sim_model.at(i).Step(prefilter_output_position_command(i)));
    }

    DVLOG(2) << "Reflexxes position command: "
             << reflexxes_joint_command.position.transpose();
    DVLOG(2) << "simulated system position: " << sensed_pos.transpose();
    DVLOG(2) << "prefilter_output_position_command : "
             << prefilter_output_position_command.transpose();

    // We expect to stay near the position reference despite dynamic
    // prefiltering. Note that this test deliberately ignores the transients
    // coming from the prefilter, it only validates basic following properties.
    // The threshold chosen below may change with the system limits or prefilter
    // design.
    EXPECT_THAT(sensed_pos, IsApprox(reflexxes_joint_command.position, 0.01));
  }
}

}  // namespace
}  // namespace intrinsic::icon
