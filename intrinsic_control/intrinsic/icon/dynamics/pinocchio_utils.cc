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

#include <stddef.h>

#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "intrinsic/kinematics/link.h"
#include "pinocchio/algorithm/joint-configuration.hpp"
#include "pinocchio/multibody/fwd.hpp"
#include "pinocchio/multibody/model.hpp"
#include "pinocchio/spatial/fwd.hpp"

namespace intrinsic::icon {

std::string PinocchioDebugString(const pinocchio::Model& model) {
  std::string debug_string = absl::StrCat(
      "Loaded Pinocchio model: '", model.name, "' with \n", model.nv,
      " DoF, \n", model.njoints, " joints, \n", model.nframes,
      " frames, and \n", model.nbodies, " bodies. \n", "The joint names are: ");

  for (size_t i = 0; i < model.names.size(); i++) {
    absl::StrAppend(&debug_string, "\nJoint ", i, ":\t", model.names[i]);
  }

  return debug_string;
}

pinocchio::Inertia ToPinocchioInertia(
    const kinematics::Link::Parameters& link_parameters) {
  const pinocchio::Inertia::Matrix3& R =
      link_parameters.center_of_gravity.quaternion().matrix();
  return pinocchio::Inertia(link_parameters.mass,
                            link_parameters.center_of_gravity.translation(),
                            R * link_parameters.inertia * R.transpose());
}

}  // namespace intrinsic::icon
