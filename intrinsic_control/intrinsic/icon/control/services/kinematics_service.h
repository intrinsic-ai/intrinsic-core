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

#ifndef INTRINSIC_ICON_CONTROL_SERVICES_KINEMATICS_SERVICE_H_
#define INTRINSIC_ICON_CONTROL_SERVICES_KINEMATICS_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "intrinsic/kinematics/ik/inverse_kinematics_interface.h"
#include "intrinsic/kinematics/skeleton.h"

namespace intrinsic::icon {

// KinematicsService provides access to a robot's kinematics solvers or models.
//
// See kinematics_service_impl.cc for configuration details.
class KinematicsService {
 public:
  virtual ~KinematicsService() = default;

  // Creates an inverse kinematics solver by part name.
  //
  // NON-REALTIME
  //
  // Returns nullptr if 1) the part is not found; 2) if the part does not have
  // kinematics associated with it; or 3) if the inverse kinematics solver could
  // not be created for the part.
  virtual std::unique_ptr<kinematics::InverseKinematicsInterface>
  CreateInverseKinematicsSolverForPart(absl::string_view part_name) const = 0;

  // Creates a kinematics model interface pointer by part name.
  //
  // NON-REALTIME
  //
  // Returns nullptr if 1) the part is not found; 2) if the part does not have
  // kinematics associated with it;
  //
  virtual std::unique_ptr<kinematics::Skeleton> CreateKinematicsModelForPart(
      absl::string_view part_name) const = 0;

  // Returns a pointer to the ModelInterface for the part, or nullptr if the
  // part is not found or it does not have an associated kinematics model.
  virtual const kinematics::ModelInterface* GetKinematicsModelForPart(
      absl::string_view part_name) const ABSL_ATTRIBUTE_LIFETIME_BOUND = 0;

  // Lists all available part names that can be used as `part_name`.
  //
  // NON-REALTIME
  virtual std::vector<std::string> ListPartNames() const = 0;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_SERVICES_KINEMATICS_SERVICE_H_
