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

#ifndef INTRINSIC_ICON_DYNAMICS_PINOCCHIO_UTILS_H_
#define INTRINSIC_ICON_DYNAMICS_PINOCCHIO_UTILS_H_

#include <string>

// Pinocchio forward declarations first.
#include "pinocchio/fwd.hpp"
#include "pinocchio/multibody/fwd.hpp"
#include "pinocchio/spatial/fwd.hpp"
// End of Pinocchio forward declarations.

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/link.h"
#include "pinocchio/algorithm/joint-configuration.hpp"
#include "pinocchio/eigen-macros.hpp"
#include "pinocchio/multibody/model.hpp"

namespace intrinsic::icon {

// Returns a debug string with basic information about the pinocchio `model`,
// including number of DoF, frame names, joint indices etc.
std::string PinocchioDebugString(const pinocchio::Model& model);

// Translates a Pinocchio `tangent_vector` of type `T` in minimal coordinates to
// a Pinocchio configuration vector in manifold form. This wrapper is required
// for continuous joint parameterizations, which are represented as
// 2D quaternions. Returns `kFailedPreconditionError` in case of a dimension
// mismatch.
template <typename T>
icon::RealtimeStatusOr<typename PINOCCHIO_EIGEN_PLAIN_TYPE(T)>
ToPinocchioConfigVector(const pinocchio::Model& model,
                        const T& tangent_vector) {
  if (model.nv != tangent_vector.size()) {
    return icon::FailedPreconditionError("tangent_vector dimensions mismatch.");
  }
  T q_neutral(model.nq);
  pinocchio::neutral(model, q_neutral);
  return pinocchio::integrate(model, q_neutral, tangent_vector);
}

// Translates a Pinocchio `configuration_vector` of type `T` in manifold
// coordinates to a Pinocchio tangent vector in minimal coordinates. This
// wrapper is required for continuous joint parameterizations, which are
// represented as 2D quaternions. Returns `kFailedPreconditionError` in case of
// a dimension mismatch.
template <typename T>
icon::RealtimeStatusOr<typename PINOCCHIO_EIGEN_PLAIN_TYPE(T)>
FromPinocchioConfigVector(const pinocchio::Model& model,
                          const T& configuration_vector) {
  if (model.nq != configuration_vector.size()) {
    return icon::FailedPreconditionError(
        "configuration_vector dimensions mismatch.");
  }
  T q_neutral(model.nq);
  pinocchio::neutral(model, q_neutral);
  return pinocchio::difference(model, q_neutral, configuration_vector);
}

// Converts 'link_parameters' to Pinocchio's spatial inertia format.
pinocchio::Inertia ToPinocchioInertia(
    const kinematics::Link::Parameters& link_parameters);

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_DYNAMICS_PINOCCHIO_UTILS_H_
