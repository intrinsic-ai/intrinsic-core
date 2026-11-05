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

#include "intrinsic/world/conversion/sdf/physics_component_from_sdf.h"

#include <memory>
#include <optional>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "gz/math/Inertial.hh"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/simulation/gazebo/type_conversion.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/physics_component.h"
#include "intrinsic/world/conversion/sdf/collision_surface_conversion.h"
#include "intrinsic/world/proto/physics_component.pb.h"
#include "sdf/Collision.hh"
#include "sdf/Link.hh"
#include "sdf/Surface.hh"

namespace intrinsic {
namespace sdf {

namespace {

// Intrinsic type friendly data parsed on a gz Inertial.
struct ParseInertialResult {
  double mass_kg;
  Pose3d this_t_center_of_mass;
  eigenmath::Matrix3d inertia_matrix;
};

// Parses a SDF <inertial> element represented by `gz_inertial` and store
// relevant data in `physics_component`.
ParseInertialResult ParseInertial(const gz::math::Inertiald& gz_inertial) {
  ParseInertialResult result;
  result.mass_kg = gz_inertial.MassMatrix().Mass();
  result.this_t_center_of_mass = GzToIntrinsic(gz_inertial.Pose());

  const double ixx = gz_inertial.MassMatrix().Ixx();
  const double ixy = gz_inertial.MassMatrix().Ixy();
  const double ixz = gz_inertial.MassMatrix().Ixz();
  const double iyy = gz_inertial.MassMatrix().Iyy();
  const double iyz = gz_inertial.MassMatrix().Iyz();
  const double izz = gz_inertial.MassMatrix().Izz();
  result.inertia_matrix << ixx, ixy, ixz, ixy, iyy, iyz, ixz, iyz, izz;
  return result;
}

}  // namespace

absl::StatusOr<std::unique_ptr<PhysicsComponent>> PhysicsComponentFromSdfLink(
    const ::sdf::Link& sdf_link) {
  std::unique_ptr<PhysicsComponent> physics_component =
      PhysicsComponent::Create();

  ParseInertialResult parse_inertial_result =
      ParseInertial(sdf_link.Inertial());
  physics_component->SetMassKg(parse_inertial_result.mass_kg);
  physics_component->SetThisTCenterOfMass(
      parse_inertial_result.this_t_center_of_mass);
  physics_component->SetInertiaMatrix(parse_inertial_result.inertia_matrix);

  // Process optional <surface> elements from <collisions>
  INTR_ASSIGN_OR_RETURN(const ::sdf::Surface* surface,
                        GetSingleSurface(sdf_link));
  if (surface != nullptr) {
    INTR_ASSIGN_OR_RETURN(const ParseSurfaceResult parse_surface_result,
                          ParseSurface(*surface));
    physics_component->SetTorsional(
        parse_surface_result.friction.torsional.value_or(
            PhysicsComponent::DefaultTorsional()));
    physics_component->SetSurfaceFriction(
        parse_surface_result.friction.surface_friction.value_or(
            PhysicsComponent::DefaultSurfaceFriction()));
    physics_component->SetContactKp(parse_surface_result.contact.kp);
    physics_component->SetContactKd(parse_surface_result.contact.kd);
  }
  return physics_component;
}

}  // namespace sdf
}  // namespace intrinsic
