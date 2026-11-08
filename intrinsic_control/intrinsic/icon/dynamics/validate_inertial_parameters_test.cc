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

#include "intrinsic/icon/dynamics/validate_inertial_parameters.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/random_number_utils.h"
#include "intrinsic/eigenmath/rotation_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/joint.h"
#include "intrinsic/kinematics/link.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::HasSubstr;

TEST(ValidateInertialParameters, FailsDueToNullptrModel) {
  EXPECT_THAT(ValidateInertialParameters(nullptr),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("The model cannot be a nullptr. Validation of "
                                 "inertial parameters failed.")));
}

TEST(ValidateInertialParameters, FailsDueToNonPositiveMass) {
  {
    kinematics::Skeleton skeleton("skeleton");

    kinematics::Link::Parameters params;
    params.center_of_gravity = Pose3d::Identity();
    params.inertia.setIdentity();
    params.mass = 0.0;

    ASSERT_OK_AND_ASSIGN(
        auto joint_element,
        kinematics::Joint::Create("joint", kinematics::Joint::Parameters()));
    ASSERT_OK_AND_ASSIGN(auto joint_id,
                         skeleton.AddJoint(std::move(joint_element)));
    auto root_element = std::make_unique<kinematics::Link>("link", params);
    ASSERT_OK_AND_ASSIGN(auto root_id,
                         skeleton.AddLink(std::move(root_element), joint_id));

    // Sanity checks.
    auto elements = skeleton.GetAllElementIds();
    EXPECT_EQ(elements.size(), 2);
    EXPECT_THAT(elements, Contains(root_id));

    // Validation.
    EXPECT_THAT(ValidateInertialParameters(&skeleton),
                StatusIs(absl::StatusCode::kFailedPrecondition,
                         AllOf(HasSubstr("The mass of link"),
                               HasSubstr("should be > 0.0"))));
  }
  {
    kinematics::Skeleton skeleton("skeleton");

    kinematics::Link::Parameters params;
    params.center_of_gravity = Pose3d::Identity();
    params.inertia.setIdentity();
    params.mass = -0.0001;

    ASSERT_OK_AND_ASSIGN(
        auto joint_element,
        kinematics::Joint::Create("joint", kinematics::Joint::Parameters()));
    ASSERT_OK_AND_ASSIGN(auto joint_id,
                         skeleton.AddJoint(std::move(joint_element)));
    auto root_element = std::make_unique<kinematics::Link>("link", params);
    ASSERT_OK_AND_ASSIGN(auto root_id,
                         skeleton.AddLink(std::move(root_element), joint_id));

    // Sanity checks.
    auto elements = skeleton.GetAllElementIds();
    EXPECT_EQ(elements.size(), 2);
    EXPECT_THAT(elements, Contains(root_id));

    // Validation.
    EXPECT_THAT(ValidateInertialParameters(&skeleton),
                StatusIs(absl::StatusCode::kFailedPrecondition,
                         AllOf(HasSubstr("The mass of link"),
                               HasSubstr("should be > 0.0"))));
  }
}

TEST(ValidateInertialParameters, FailsDueToNonSymmetricInertiaTensor) {
  kinematics::Skeleton skeleton("skeleton");

  kinematics::Link::Parameters params;
  params.center_of_gravity = Pose3d::Identity();
  params.inertia.setIdentity();
  params.inertia(1, 0) = 0.1;
  params.mass = 1.0;

  ASSERT_OK_AND_ASSIGN(
      auto joint_element,
      kinematics::Joint::Create("joint", kinematics::Joint::Parameters()));
  ASSERT_OK_AND_ASSIGN(auto joint_id,
                       skeleton.AddJoint(std::move(joint_element)));
  auto root_element = std::make_unique<kinematics::Link>("link", params);
  ASSERT_OK_AND_ASSIGN(auto root_id,
                       skeleton.AddLink(std::move(root_element), joint_id));

  // Sanity checks.
  auto elements = skeleton.GetAllElementIds();
  EXPECT_EQ(elements.size(), 2);
  EXPECT_THAT(elements, Contains(root_id));

  // Validation.
  EXPECT_THAT(ValidateInertialParameters(&skeleton),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       AllOf(HasSubstr("Inertia tensor of link"),
                             HasSubstr("is not symmetric."))));
}

TEST(ValidateInertialParameters, FailsDueToNonPositiveDefiniteInertiaTensor) {
  kinematics::Skeleton skeleton("skeleton");

  kinematics::Link::Parameters params;
  params.center_of_gravity = Pose3d::Identity();
  params.inertia.setIdentity();
  params.inertia(1, 1) = -0.00001;
  params.mass = 1.0;

  ASSERT_OK_AND_ASSIGN(
      auto joint_element,
      kinematics::Joint::Create("joint", kinematics::Joint::Parameters()));
  ASSERT_OK_AND_ASSIGN(auto joint_id,
                       skeleton.AddJoint(std::move(joint_element)));
  auto root_element = std::make_unique<kinematics::Link>("link", params);
  ASSERT_OK_AND_ASSIGN(auto root_id,
                       skeleton.AddLink(std::move(root_element), joint_id));

  // Sanity checks.
  auto elements = skeleton.GetAllElementIds();
  EXPECT_EQ(elements.size(), 2);
  EXPECT_THAT(elements, Contains(root_id));

  // Validation.
  EXPECT_THAT(ValidateInertialParameters(&skeleton),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       AllOf(HasSubstr("Inertia tensor of link"),
                             HasSubstr("is not positive definite."))));
}

TEST(ValidateInertialParameters,
     FailsDueToNotSatisfiedTriangleInequalitiesForInertiaTensorEigenvalues) {
  kinematics::Skeleton skeleton("skeleton");

  kinematics::Link::Parameters params;
  params.center_of_gravity = Pose3d::Identity();
  params.inertia.setIdentity();
  params.inertia(0, 0) = 1.0;
  params.inertia(1, 1) = 3.0;
  params.inertia(2, 2) = 5.0;
  params.mass = 1.0;

  ASSERT_OK_AND_ASSIGN(
      auto joint_element,
      kinematics::Joint::Create("joint", kinematics::Joint::Parameters()));
  ASSERT_OK_AND_ASSIGN(auto joint_id,
                       skeleton.AddJoint(std::move(joint_element)));
  auto root_element = std::make_unique<kinematics::Link>("link", params);
  ASSERT_OK_AND_ASSIGN(auto root_id,
                       skeleton.AddLink(std::move(root_element), joint_id));

  // Sanity checks.
  auto elements = skeleton.GetAllElementIds();
  EXPECT_EQ(elements.size(), 2);
  EXPECT_THAT(elements, Contains(root_id));

  // Validation.
  EXPECT_THAT(ValidateInertialParameters(&skeleton),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       AllOf(HasSubstr("The inertia eigenvalues do not satisfy "
                                       "the triangle inequality"))));
}

TEST(ValidateInertialParameters, Succeeds) {
  absl::BitGen bitgen = GetRandomBitGenerator();

  kinematics::Skeleton skeleton("skeleton");

  eigenmath::Vector3d random_euler_angles =
      eigenmath::UnverifiedGetUniformRandomVector(
          eigenmath::Vector3d(-M_PI_4, -M_PI_4, -M_PI_4),
          eigenmath::Vector3d(-M_PI_4, M_PI_4, M_PI_4), bitgen);
  eigenmath::Quaterniond random_quaternion = eigenmath::QuaternionFromRPY(
      random_euler_angles.x(), random_euler_angles.y(),
      random_euler_angles.z());

  kinematics::Link::Parameters params;
  params.center_of_gravity = Pose3d::Identity();
  params.inertia.setIdentity();
  params.inertia(0, 0) = 1.0;
  params.inertia(1, 1) = 3.0;
  params.inertia(2, 2) = 2.5;
  params.inertia = random_quaternion.matrix() * params.inertia *
                   random_quaternion.matrix().transpose();
  params.mass = 1.0;

  ASSERT_OK_AND_ASSIGN(
      auto joint_element,
      kinematics::Joint::Create("joint", kinematics::Joint::Parameters()));
  ASSERT_OK_AND_ASSIGN(auto joint_id,
                       skeleton.AddJoint(std::move(joint_element)));
  auto root_element = std::make_unique<kinematics::Link>("link", params);
  ASSERT_OK_AND_ASSIGN(auto root_id,
                       skeleton.AddLink(std::move(root_element), joint_id));

  // Sanity checks.
  auto elements = skeleton.GetAllElementIds();
  EXPECT_EQ(elements.size(), 2);
  EXPECT_THAT(elements, Contains(root_id));

  // Validation.
  EXPECT_OK(ValidateInertialParameters(&skeleton));
}

}  // namespace
}  // namespace intrinsic::icon
