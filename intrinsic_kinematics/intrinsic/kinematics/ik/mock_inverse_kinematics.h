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

#ifndef INTRINSIC_KINEMATICS_IK_MOCK_INVERSE_KINEMATICS_H_
#define INTRINSIC_KINEMATICS_IK_MOCK_INVERSE_KINEMATICS_H_

#include <gmock/gmock.h>

#include <memory>
#include <optional>
#include <string_view>

#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/kinematics/elements.h"
#include "intrinsic/kinematics/ik/inverse_kinematics_interface.h"
#include "intrinsic/kinematics/model_interface.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic {
namespace kinematics {

class MockInverseKinematics : public InverseKinematicsInterfaceRT {
 public:
  explicit MockInverseKinematics(int njoints = 0)
      : InverseKinematicsInterfaceRT(Chain()), njoints_(njoints) {}

  static std::unique_ptr<MockInverseKinematics> Create(
      Chain chain, const InverseKinematicsInterface::Options& options) {
    return std::make_unique<MockInverseKinematics>(
        chain.GetNumberDegreesOfFreedom());
  }

  static constexpr char kSolverName[] = "mock_inverse_kinematics";

  const std::string_view GetName() const override { return kSolverName; }

  int GetNumDof() const override { return njoints_; }

  bool ImplementsSameBranchIK() const override { return false; }

  MOCK_METHOD(icon::RealtimeStatusOr<IKResult>, RealtimeComputeIK,
              (const JointStateP& hint_joint_state,
               const Pose3d& desired_base_t_tip, const JointLimits& limits,
               absl::Span<JointStateP> solutions),
              (const, override));

  MOCK_METHOD(std::optional<ElementId>, RetrieveMatchingStartJoint,
              (const ModelInterface& model, ElementId end_joint),
              (const, override));

  MOCK_METHOD(bool, IsCompatibleWithChain, (const Chain& chain),
              (const, override));

 private:
  int njoints_;
};

}  // namespace kinematics
}  // namespace intrinsic

#endif  // INTRINSIC_KINEMATICS_IK_MOCK_INVERSE_KINEMATICS_H_
