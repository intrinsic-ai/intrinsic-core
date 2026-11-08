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

#include "intrinsic/icon/dynamics/validate_rigid_body_interface.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/dynamics/mock_rigid_body_interface.h"
#include "intrinsic/icon/utils/realtime_status.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::testing::ByMove;
using ::testing::HasSubstr;
using ::testing::Return;

constexpr int kGoodNumDof = 6;
constexpr int kBadNumDof = 5;

TEST(ValidateRigidBodyInterfaceTest, ValidateRigidBodyInterfaceSucceeds) {
  auto mock_dynamics =
      std::make_unique<intrinsic::icon::MockRigidBodyInterface>();

  eigenmath::VectorNd some_q = eigenmath::VectorNd::Zero(kGoodNumDof);
  eigenmath::VectorNd some_dq = eigenmath::VectorNd::Ones(kGoodNumDof);
  eigenmath::VectorNd some_joint_vec_result =
      eigenmath::VectorNd::Ones(kGoodNumDof);
  eigenmath::MatrixNd some_joint_matrix_result =
      eigenmath::MatrixNd::Identity(kGoodNumDof, kGoodNumDof);

  EXPECT_CALL(*mock_dynamics, GetNumDof).WillRepeatedly(Return(kGoodNumDof));
  EXPECT_CALL(*mock_dynamics, ComputeGeneralizedGravityVector(some_q))
      .WillRepeatedly(Return(ByMove(some_joint_vec_result)));
  EXPECT_CALL(*mock_dynamics, ComputeCoriolisVector(some_q, some_dq))
      .WillRepeatedly(Return(ByMove(some_joint_vec_result)));
  EXPECT_CALL(*mock_dynamics, ComputeJointSpaceInertiaMatrix(some_q))
      .WillRepeatedly(Return(ByMove(some_joint_matrix_result)));

  EXPECT_OK(ValidateRigidBodyInterface(mock_dynamics.get(), some_q, some_dq));
}

TEST(ValidateRigidBodyInterfaceTest, FailsForInputDimensionMismatch) {
  auto mock_dynamics =
      std::make_unique<intrinsic::icon::MockRigidBodyInterface>();

  eigenmath::VectorNd some_q = eigenmath::VectorNd::Zero(kGoodNumDof);
  eigenmath::VectorNd some_dq = eigenmath::VectorNd::Ones(kBadNumDof);

  EXPECT_CALL(*mock_dynamics, GetNumDof)
      .WillRepeatedly(Return(ByMove(kGoodNumDof)));

  EXPECT_THAT(ValidateRigidBodyInterface(mock_dynamics.get(), some_q, some_dq),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("joint position and velocity vector")));
}

TEST(ValidateRigidBodyInterfaceTest, FailsForDynamicsDimensionMismatch) {
  auto mock_dynamics =
      std::make_unique<intrinsic::icon::MockRigidBodyInterface>();

  eigenmath::VectorNd some_q = eigenmath::VectorNd::Zero(kGoodNumDof);
  eigenmath::VectorNd some_dq = eigenmath::VectorNd::Ones(kGoodNumDof);

  EXPECT_CALL(*mock_dynamics, GetNumDof).WillRepeatedly(Return(kBadNumDof));

  EXPECT_THAT(
      ValidateRigidBodyInterface(mock_dynamics.get(), some_q, some_dq),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("degrees of freedom of the rigid body interface")));
}

TEST(ValidateRigidBodyInterfaceTest, FailsIfGeneralizedGravityUnavailable) {
  auto mock_dynamics =
      std::make_unique<intrinsic::icon::MockRigidBodyInterface>();

  eigenmath::VectorNd some_q = eigenmath::VectorNd::Zero(kGoodNumDof);
  eigenmath::VectorNd some_dq = eigenmath::VectorNd::Ones(kGoodNumDof);

  EXPECT_CALL(*mock_dynamics, GetNumDof).WillRepeatedly(Return(kGoodNumDof));
  EXPECT_CALL(*mock_dynamics, ComputeGeneralizedGravityVector(some_q))
      .WillOnce(Return(ByMove(icon::InternalError("oh!"))));

  EXPECT_THAT(
      ValidateRigidBodyInterface(mock_dynamics.get(), some_q, some_dq),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("generalized gravity computation with error")));
}

TEST(ValidateRigidBodyInterfaceTest, FailsIfCoriolisUnavailable) {
  auto mock_dynamics =
      std::make_unique<intrinsic::icon::MockRigidBodyInterface>();

  eigenmath::VectorNd some_q = eigenmath::VectorNd::Zero(kGoodNumDof);
  eigenmath::VectorNd some_dq = eigenmath::VectorNd::Ones(kGoodNumDof);
  eigenmath::VectorNd some_joint_vec_result =
      eigenmath::VectorNd::Ones(kGoodNumDof);

  EXPECT_CALL(*mock_dynamics, GetNumDof).WillRepeatedly(Return(kGoodNumDof));
  EXPECT_CALL(*mock_dynamics, ComputeGeneralizedGravityVector(some_q))
      .WillRepeatedly(Return(ByMove(some_joint_vec_result)));
  EXPECT_CALL(*mock_dynamics, ComputeCoriolisVector(some_q, some_dq))
      .WillOnce(Return(ByMove(icon::InternalError("oh!"))));

  EXPECT_THAT(
      ValidateRigidBodyInterface(mock_dynamics.get(), some_q, some_dq),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Coriolis")));
}

TEST(ValidateRigidBodyInterfaceTest,
     FailsIfJointSpaceInertiaMatrixUnavailable) {
  auto mock_dynamics =
      std::make_unique<intrinsic::icon::MockRigidBodyInterface>();

  eigenmath::VectorNd some_q = eigenmath::VectorNd::Zero(kGoodNumDof);
  eigenmath::VectorNd some_dq = eigenmath::VectorNd::Ones(kGoodNumDof);
  eigenmath::VectorNd some_joint_vec_result =
      eigenmath::VectorNd::Ones(kGoodNumDof);

  EXPECT_CALL(*mock_dynamics, GetNumDof).WillRepeatedly(Return(kGoodNumDof));
  EXPECT_CALL(*mock_dynamics, ComputeGeneralizedGravityVector(some_q))
      .WillRepeatedly(Return(ByMove(some_joint_vec_result)));
  EXPECT_CALL(*mock_dynamics, ComputeCoriolisVector(some_q, some_dq))
      .WillRepeatedly(Return(ByMove(some_joint_vec_result)));
  EXPECT_CALL(*mock_dynamics, ComputeJointSpaceInertiaMatrix(some_q))
      .WillOnce(Return(ByMove(icon::InternalError("oh!"))));

  EXPECT_THAT(ValidateRigidBodyInterface(mock_dynamics.get(), some_q, some_dq),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("joint space inertia")));
}

}  // namespace
}  // namespace intrinsic::icon
