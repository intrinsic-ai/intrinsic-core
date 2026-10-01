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

#ifndef INTRINSIC_ICON_CONTROL_SERVICES_KINEMATICS_FROM_WORLD_SERVICE_H_
#define INTRINSIC_ICON_CONTROL_SERVICES_KINEMATICS_FROM_WORLD_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/control/services/kinematics_service.h"
#include "intrinsic/icon/control/services/service.h"
#include "intrinsic/icon/control/services/world_service.h"
#include "intrinsic/icon/testing/realtime_annotations.h"
#include "intrinsic/kinematics/ik/inverse_kinematics_interface.h"
#include "intrinsic/kinematics/model_interface.h"
#include "intrinsic/kinematics/skeleton.h"

namespace intrinsic::icon {

// Implements KinematicsService by reading from a WorldService dependency.
//
// This works by extracting any RobotCollection entities from the World supplied
// by WorldService.
//
// KinematicsFromWorldService creates a Skeleton from each RobotCollection in
// Initialize(), then uses those skeletons to create Chains and IK solvers on
// demand in the corresponding functions.
class KinematicsFromWorldService final
    : public ServiceImplBase<KinematicsFromWorldService, KinematicsService,
                             WorldService> {
 public:
  static absl::StatusOr<std::unique_ptr<KinematicsFromWorldService>> Create(
      const Service::InitParameters& parameters, WorldService* world_service,
      bool allow_non_real_time_inverse_kinematics = false);

  // Creates an inverse kinematics solver by part name.
  //
  // NON-REALTIME
  //
  // Returns nullptr if 1) the part is not found; 2) if the part does not have
  // kinematics associated with it; or 3) if the inverse kinematics solver could
  // not be created for the part.
  std::unique_ptr<kinematics::InverseKinematicsInterface>
  CreateInverseKinematicsSolverForPart(
      absl::string_view part_name) const override INTRINSIC_NON_REALTIME_ONLY;

  // Creates a kinematics model interface pointer by part name.
  //
  // NON-REALTIME
  //
  // Returns nullptr if 1) the part is not found; 2) if the part does not have
  // kinematics associated with it;
  std::unique_ptr<kinematics::Skeleton> CreateKinematicsModelForPart(
      absl::string_view part_name) const override INTRINSIC_NON_REALTIME_ONLY;

  // Returns a pointer to the ModelInterface for the part, or nullptr if the
  // part is not found or it does not have an associated kinematics model.
  const kinematics::ModelInterface* GetKinematicsModelForPart(
      absl::string_view part_name) const ABSL_ATTRIBUTE_LIFETIME_BOUND override;

  std::vector<std::string> ListPartNames() const override
      INTRINSIC_NON_REALTIME_ONLY;

 private:
  absl::Status Initialize(const Service::InitParameters& parameters,
                          WorldService* world_service,
                          bool allow_non_real_time_inverse_kinematics = false);

  bool allow_non_real_time_inverse_kinematics_ = false;
  double control_frequency_hz_ = 0;
  absl::flat_hash_map<std::string, std::unique_ptr<kinematics::Skeleton>>
      skeleton_by_part_name_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_SERVICES_KINEMATICS_FROM_WORLD_SERVICE_H_
