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

#include "intrinsic/world/conversion/sdf/collision_surface_conversion.h"

#include <optional>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/substitute.h"
#include "gz/math/Vector3.hh"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/sdf/sdf_util.h"
#include "intrinsic/scene/sdf/xml_utils.h"
#include "intrinsic/simulation/gazebo/type_conversion.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/physics_component.pb.h"
#include "sdf/Collision.hh"
#include "sdf/Element.hh"
#include "sdf/Link.hh"
#include "sdf/Surface.hh"

namespace intrinsic {
namespace sdf {

namespace {

// Converts an <ode> element into its Intrinsic equivalent Friction proto. See
// http://sdformat.org/spec?elem=collision#friction_ode for reference.
intrinsic_proto::world::Friction IntrFrictionFromSdfFrictionOde(
    const ::sdf::ODE& ode) {
  intrinsic_proto::world::Friction intr_friction;
  intr_friction.set_mu(ode.Mu());
  intr_friction.set_mu2(ode.Mu2());
  intr_friction.set_slip1(ode.Slip1());
  intr_friction.set_slip2(ode.Slip2());
  *intr_friction.mutable_fdir1() = ToProto(GzToIntrinsic(ode.Fdir1()));
  return intr_friction;
}

// Converts an <bullet> element into its Intrinsic equivalent Friction proto.
// See http://sdformat.org/spec?elem=collision#friction_bullet for reference.
intrinsic_proto::world::Friction IntrFrictionFromSdfFrictionBullet(
    const ::sdf::BulletFriction& bullet) {
  intrinsic_proto::world::Friction intr_friction;
  intr_friction.set_mu(bullet.Friction());
  intr_friction.set_mu2(bullet.Friction2());
  *intr_friction.mutable_fdir1() = ToProto(GzToIntrinsic(bullet.Fdir1()));
  return intr_friction;
}

// Converts a <torsional> element into its Intrinsic equivalent proto. See
// http://sdformat.org/spec?elem=collision#collision_surface for reference.
absl::StatusOr<intrinsic_proto::world::Torsional> IntrTorsionalFromSdfTorsional(
    const ::sdf::ElementConstPtr& torsional) {
  if (torsional == nullptr) {
    return absl::InvalidArgumentError("torsional element is nullptr");
  }
  intrinsic_proto::world::Torsional torsional_proto;

  if (auto coefficient = ParseChildAs<double>(torsional, "coefficient");
      coefficient.ok()) {
    torsional_proto.set_coefficient(*coefficient);
  }

  if (auto patch_radius = ParseChildAs<double>(torsional, "patch_radius");
      patch_radius.ok()) {
    torsional_proto.set_patch_radius(*patch_radius);
  }

  if (auto surface_radius = ParseChildAs<double>(torsional, "surface_radius");
      surface_radius.ok()) {
    torsional_proto.set_surface_radius(*surface_radius);
  }

  if (auto use_patch_radius = ParseChildAs<bool>(torsional, "use_patch_radius");
      use_patch_radius.ok()) {
    torsional_proto.set_use_patch_radius(*use_patch_radius);
  }

  if (auto ode = GetChildWithTag(torsional, "ode"); ode.ok()) {
    if (auto slip = ParseChildAs<double>(*ode, "slip"); slip.ok()) {
      torsional_proto.set_ode_slip(slip.value());
    }
  } else if (!absl::IsNotFound(ode.status())) {
    return ode.status();
  }

  return torsional_proto;
}
}  // namespace

absl::StatusOr<ParseSurfaceResult> ParseSurface(
    const ::sdf::Surface& sdf_surface) {
  ParseSurfaceResult surface_result;
  INTR_ASSIGN_OR_RETURN(surface_result.contact,
                        ParseContact(*sdf_surface.Contact()));
  INTR_ASSIGN_OR_RETURN(surface_result.friction,
                        ParseFriction(*sdf_surface.Friction()));
  return surface_result;
}

absl::StatusOr<ParseContactResult> ParseContact(const ::sdf::Contact& contact) {
  // If the contact element is nullptr, the contact is never loaded with a tag.
  // Return ok because contact is optional in collision.
  ParseContactResult contact_result;
  if (contact.Element() == nullptr) {
    return contact_result;
  }
  // TODO(b/184168467) TODO(b/168049879): Handle collision masks.
  if (contact.Element()->HasElement("bullet")) {
    INTR_ASSIGN_OR_RETURN(auto bullet,
                          GetChildWithTag(contact.Element(), "bullet"));

    if (auto kp = ParseChildAs<double>(bullet, "kp"); kp.ok()) {
      contact_result.kp = *kp;
    }

    if (auto kd = ParseChildAs<double>(bullet, "kd"); kd.ok()) {
      contact_result.kd = *kd;
    }
  }

  if (auto collide_without_contact =
          ParseChildAs<bool>(contact.Element(), "collide_without_contact");
      collide_without_contact.ok()) {
    contact_result.collide_without_contact = *collide_without_contact;
  }
  return contact_result;
}

absl::StatusOr<ParseFrictionResult> ParseFriction(
    const ::sdf::Friction& friction) {
  ParseFrictionResult result;
  // If friction.Element() is not present, that means this Friction is never
  // loaded from an actual <friction> element. Skip parsing friction.
  if (friction.Element() == nullptr) {
    return result;
  }

  if (friction.Element()->HasElement("torsional")) {
    INTR_ASSIGN_OR_RETURN(auto torsional,
                          GetChildWithTag(friction.Element(), "torsional"));
    INTR_ASSIGN_OR_RETURN(result.torsional,
                          IntrTorsionalFromSdfTorsional(torsional));
  }

  // Check for ode / bullet specific friction parameters
  auto has_ode = friction.Element()->HasElement("ode");
  auto has_bullet = friction.Element()->HasElement("bullet");
  if (!has_ode && !has_bullet) {
    return result;
  }

  // TODO (b/494333587) Consider preserving all engine-specific friction
  // parameters
  if (has_bullet) {
    result.surface_friction =
        IntrFrictionFromSdfFrictionBullet(*friction.BulletFriction());
  } else if (has_ode) {
    result.surface_friction = IntrFrictionFromSdfFrictionOde(*friction.ODE());
  }
  return result;
}

absl::StatusOr<const ::sdf::Surface*> GetSingleSurface(
    const ::sdf::Link& sdf_link) {
  std::optional<std::string> compact_surface_xml;
  const ::sdf::Surface* result = nullptr;
  const auto collision_count = sdf_link.CollisionCount();
  for (auto i = 0; i < collision_count; ++i) {
    const auto& collision = *sdf_link.CollisionByIndex(i);
    const auto* surface = collision.Surface();
    if (surface->Element() == nullptr) {
      continue;
    }
    INTR_ASSIGN_OR_RETURN(const std::string this_compact_surface_xml,
                          GetCompactXml(surface->Element()));

    if (compact_surface_xml == std::nullopt) {
      compact_surface_xml = this_compact_surface_xml;
      result = surface;
    } else if (*compact_surface_xml != this_compact_surface_xml) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Sdf link contain conflicting surface elements: $0\nand\n$1",
          *compact_surface_xml, this_compact_surface_xml));
    }
  }

  return result;
}

}  // namespace sdf
}  // namespace intrinsic
