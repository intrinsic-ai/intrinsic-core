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

#include "intrinsic/world/component/physics_component.h"

#include <memory>
#include <optional>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/physics_component.pb.h"

namespace intrinsic {
namespace {

class PhysicsComponentImpl : public PhysicsComponent {
 public:
  PhysicsComponentImpl() = default;
  PhysicsComponentImpl(
      double mass_kg, Pose3d this_t_center_of_mass,
      const eigenmath::Matrix3d& inertia,
      const std::optional<intrinsic_proto::world::Friction> friction,
      const std::optional<intrinsic_proto::world::Torsional> torsional,
      std::optional<double> contact_kp, std::optional<double> contact_kd)
      : mass_kg_(mass_kg),
        this_t_center_of_mass_(std::move(this_t_center_of_mass)),
        inertia_(inertia),
        friction_(friction.value_or(DefaultSurfaceFriction())),
        torsional_(torsional.value_or(DefaultTorsional())),
        contact_kp_(contact_kp),
        contact_kd_(contact_kd) {}
  absl::StatusOr<intrinsic_proto::world::PhysicsComponent> ToProto()
      const override;
  std::unique_ptr<PhysicsComponent> Clone() const override;
  double GetMassKg() const override { return mass_kg_; }
  const Pose3d& GetThisTCenterOfMass() const override {
    return this_t_center_of_mass_;
  }
  const eigenmath::Matrix3d& GetInertiaMatrix() const override {
    return inertia_;
  }
  const intrinsic_proto::world::Friction& GetSurfaceFriction() const override {
    return friction_;
  }
  const intrinsic_proto::world::Torsional& GetTorsional() const override {
    return torsional_;
  }

  std::optional<double> GetContactKp() const override { return contact_kp_; }
  std::optional<double> GetContactKd() const override { return contact_kd_; }
  void SetMassKg(double mass_kg) override { mass_kg_ = mass_kg; }
  void SetThisTCenterOfMass(const Pose3d& this_t_center_of_mass) override {
    this_t_center_of_mass_ = this_t_center_of_mass;
  }
  void SetInertiaMatrix(const eigenmath::Matrix3d& inertia) override {
    inertia_ = inertia;
  }
  void SetSurfaceFriction(
      const intrinsic_proto::world::Friction& friction) override {
    friction_ = friction;
  }
  void SetTorsional(
      const intrinsic_proto::world::Torsional& torsional) override {
    torsional_ = torsional;
  }
  void SetContactKp(std::optional<double> contact_kp) override {
    contact_kp_ = contact_kp;
  }
  void SetContactKd(std::optional<double> contact_kd) override {
    contact_kd_ = contact_kd;
  }

  absl::Status UpdateFromProto(
      const intrinsic_proto::world::PhysicsComponent& proto) override;

 private:
  double mass_kg_ = DefaultMassKg();
  Pose3d this_t_center_of_mass_;
  eigenmath::Matrix3d inertia_ = eigenmath::Matrix3d::Identity();
  intrinsic_proto::world::Friction friction_ = DefaultSurfaceFriction();
  intrinsic_proto::world::Torsional torsional_ = DefaultTorsional();
  std::optional<double> contact_kp_;
  std::optional<double> contact_kd_;
};

absl::StatusOr<intrinsic_proto::world::PhysicsComponent>
PhysicsComponentImpl::ToProto() const {
  intrinsic_proto::world::PhysicsComponent ret;
  ret.set_mass_kg(mass_kg_);
  *ret.mutable_this_t_center_of_mass() =
      intrinsic::ToProto(this_t_center_of_mass_);
  *ret.mutable_inertia() = intrinsic::ToProto(inertia_);
  *ret.mutable_friction() = friction_;
  *ret.mutable_torsional() = torsional_;
  if (contact_kp_.has_value()) {
    ret.set_contact_kp(*contact_kp_);
  }
  if (contact_kd_.has_value()) {
    ret.set_contact_kd(*contact_kd_);
  }
  return ret;
}

std::unique_ptr<PhysicsComponent> PhysicsComponentImpl::Clone() const {
  return std::make_unique<PhysicsComponentImpl>(
      mass_kg_, this_t_center_of_mass_, inertia_, friction_, torsional_,
      contact_kp_, contact_kd_);
}

absl::Status PhysicsComponentImpl::UpdateFromProto(
    const intrinsic_proto::world::PhysicsComponent& proto) {
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<PhysicsComponent> other,
                        FromProto(proto));
  this_t_center_of_mass_ = other->GetThisTCenterOfMass();
  inertia_ = other->GetInertiaMatrix();
  mass_kg_ = other->GetMassKg();
  friction_ = other->GetSurfaceFriction();
  torsional_ = other->GetTorsional();
  contact_kp_ = other->GetContactKp();
  contact_kd_ = other->GetContactKd();
  return absl::OkStatus();
}

}  // namespace

std::unique_ptr<PhysicsComponent> PhysicsComponent::Create() {
  return std::make_unique<PhysicsComponentImpl>();
}

absl::StatusOr<std::unique_ptr<PhysicsComponent>> PhysicsComponent::FromProto(
    const intrinsic_proto::world::PhysicsComponent& proto) {
  Pose3d this_t_center_of_mass;
  if (proto.has_this_t_center_of_mass()) {
    // The pose converter rejects everything that is not exactly normalized,
    // including zero-quats.
    INTR_ASSIGN_OR_RETURN(
        this_t_center_of_mass,
        intrinsic_proto::FromProto(proto.this_t_center_of_mass()),
        _ << "Failed to parse this_t_center_of_mass from PhysicsComponent");
  }

  eigenmath::MatrixXd inertia =
      eigenmath::MatrixXd(eigenmath::Matrix3d::Identity());
  if (proto.has_inertia()) {
    INTR_ASSIGN_OR_RETURN(inertia, intrinsic_proto::FromProto(proto.inertia()),
                          _ << "Failed to parse inertia from PhysicsComponent");
  }

  if (inertia.rows() != 3 || inertia.cols() != 3) {
    return absl::InvalidArgumentError("Non 3x3 intertia matrix provided.");
  }

  std::optional<intrinsic_proto::world::Friction> friction;
  if (proto.has_friction()) {
    friction = proto.friction();
  }

  std::optional<intrinsic_proto::world::Torsional> torsional;
  if (proto.has_torsional()) {
    torsional = proto.torsional();
  }

  std::optional<double> contact_kp;
  if (proto.has_contact_kp()) {
    contact_kp = proto.contact_kp();
  }

  std::optional<double> contact_kd;
  if (proto.has_contact_kd()) {
    contact_kd = proto.contact_kd();
  }

  return std::make_unique<PhysicsComponentImpl>(
      proto.mass_kg(), this_t_center_of_mass, eigenmath::Matrix3d(inertia),
      friction, torsional, contact_kp, contact_kd);
}

double PhysicsComponent::DefaultMassKg() {
  // http://sdformat.org/spec?ver=1.11&elem=collision#collision_density
  // Default is the density of water 1000 kg/m^3.
  return 1;
}

const intrinsic_proto::world::Friction&
PhysicsComponent::DefaultSurfaceFriction() {
  // http://sdformat.org/spec?ver=1.11&elem=collision#bullet_friction
  static auto* default_friction =
      new intrinsic_proto::world::Friction(ParseTextProtoOrDie("mu: 1 mu2: 1"));
  return *default_friction;
}

const intrinsic_proto::world::Torsional& PhysicsComponent::DefaultTorsional() {
  // http://sdformat.org/spec?ver=1.11&elem=collision#friction_torsional
  static auto* default_torsional = new intrinsic_proto::world::Torsional(
      ParseTextProtoOrDie("coefficient: 1 use_patch_radius: true"));
  return *default_torsional;
}

}  // namespace intrinsic
