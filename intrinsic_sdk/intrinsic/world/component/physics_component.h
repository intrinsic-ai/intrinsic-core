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

#ifndef INTRINSIC_WORLD_COMPONENT_PHYSICS_COMPONENT_H_
#define INTRINSIC_WORLD_COMPONENT_PHYSICS_COMPONENT_H_

#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/world/proto/physics_component.pb.h"

namespace intrinsic {

// A component to hold information about the physics of a specific entity.
class PhysicsComponent {
 public:
  virtual ~PhysicsComponent() = default;

  // Returns a new PhysicsComponent instance.
  static std::unique_ptr<PhysicsComponent> Create();

  // Returns a new PhysicsComponent instance derived from the given proto.
  static absl::StatusOr<std::unique_ptr<PhysicsComponent>> FromProto(
      const intrinsic_proto::world::PhysicsComponent& proto);

  // Returns a new PhysicsComponent instance that is a copy of this one.
  virtual std::unique_ptr<PhysicsComponent> Clone() const = 0;

  // Returns a proto representation of this component.
  virtual absl::StatusOr<intrinsic_proto::world::PhysicsComponent> ToProto()
      const = 0;

  // Returns the entity's mass in kilograms.
  virtual double GetMassKg() const = 0;

  // Returns the default mass in kilograms.
  static double DefaultMassKg();

  // Returns the center of mass's pose in the entity's local space.
  virtual const Pose3d& GetThisTCenterOfMass() const = 0;

  // Returns the inertia matrix.
  virtual const eigenmath::Matrix3d& GetInertiaMatrix() const = 0;

  // Returns surface friction parameters.
  virtual const intrinsic_proto::world::Friction& GetSurfaceFriction()
      const = 0;

  // Returns torsional friction parameters.
  virtual const intrinsic_proto::world::Torsional& GetTorsional() const = 0;

  // Returns surface friction parameters.
  static const intrinsic_proto::world::Friction& DefaultSurfaceFriction();

  // Returns torsional friction parameters.
  static const intrinsic_proto::world::Torsional& DefaultTorsional();

  // Returns dynamically "stiffness"-equivalent coefficient for contact joints.
  virtual std::optional<double> GetContactKp() const = 0;

  // Returns dynamically "damping"-equivalent coefficient for contact joints.
  virtual std::optional<double> GetContactKd() const = 0;

  // Sets the entity's mass in kilograms.
  virtual void SetMassKg(double mass_kg) = 0;

  // Sets the center of mass's pose in the entity's local space.
  virtual void SetThisTCenterOfMass(const Pose3d& this_t_center_of_mass) = 0;

  // Sets the inertia matrix.
  virtual void SetInertiaMatrix(const eigenmath::Matrix3d& inertia) = 0;

  // Sets surface friction parameters.
  virtual void SetSurfaceFriction(
      const intrinsic_proto::world::Friction& friction) = 0;

  // Sets torsional friction parameters.
  virtual void SetTorsional(
      const intrinsic_proto::world::Torsional& torsional) = 0;

  // Sets dynamically "stiffness"-equivalent coefficient for contact joints.
  virtual void SetContactKp(std::optional<double> contact_kp) = 0;

  // Sets dynamically "damping"-equivalent coefficient for contact joints.
  virtual void SetContactKd(std::optional<double> contact_kd) = 0;

  // Updates the component based on the given proto. This will do a full
  // override, if something is missing from this proto it will override any
  // existing values with the defaults.
  virtual absl::Status UpdateFromProto(
      const intrinsic_proto::world::PhysicsComponent& proto) = 0;
};

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_COMPONENT_PHYSICS_COMPONENT_H_
