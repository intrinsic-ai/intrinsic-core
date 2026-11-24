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

#include "intrinsic/world/footprints/object_world_reservation.h"

#include <optional>
#include <ostream>
#include <utility>
#include <variant>

#include "absl/functional/overload.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"

namespace intrinsic {

namespace {

std::variant<WorldObjectName, FrameReferenceByName> ToVariant(
    const intrinsic_proto::world::TransformNodeReferenceByName& ref) {
  if (ref.transform_node_reference_by_name_case() ==
      intrinsic_proto::world::TransformNodeReferenceByName::kFrame) {
    return FrameReferenceByName(WorldObjectName(ref.frame().object_name()),
                                FrameName(ref.frame().frame_name()));
  } else {
    return WorldObjectName(ref.object().object_name());
  }
}

}  // namespace

ObjectWorldReservation::ObjectWorldReservation(
    SharingType sharing_type,
    const intrinsic_proto::world::ObjectReferenceByName& object)
    : sharing_type_(sharing_type),
      reservation_(WorldObjectName(object.object_name())) {}

ObjectWorldReservation::ObjectWorldReservation(SharingType sharing_type,
                                               WorldObjectName object)
    : sharing_type_(sharing_type), reservation_(object) {}

ObjectWorldReservation::ObjectWorldReservation(
    SharingType sharing_type,
    const intrinsic_proto::world::FrameReferenceByName& frame)
    : sharing_type_(sharing_type),
      reservation_(FrameReferenceByName(WorldObjectName(frame.object_name()),
                                        FrameName(frame.frame_name()))) {}

ObjectWorldReservation::ObjectWorldReservation(
    SharingType sharing_type,
    const intrinsic_proto::world::TransformNodeReferenceByName& transform_node)
    : sharing_type_(sharing_type), reservation_(ToVariant(transform_node)) {}

ObjectWorldReservation::ObjectWorldReservation(SharingType sharing_type,
                                               FrameReferenceByName frame)
    : sharing_type_(sharing_type), reservation_(std::move(frame)) {}

absl::StatusOr<ObjectWorldReservation> ObjectWorldReservation::FromProto(
    const intrinsic_proto::skills::ObjectWorldReservation& proto) {
  switch (proto.object_type_case()) {
    case intrinsic_proto::skills::ObjectWorldReservation::kObject:
      return ObjectWorldReservation(proto.type(), proto.object());
    case intrinsic_proto::skills::ObjectWorldReservation::kFrame:
      return ObjectWorldReservation(proto.type(), proto.frame());
    case intrinsic_proto::skills::ObjectWorldReservation::OBJECT_TYPE_NOT_SET:
      return absl::InvalidArgumentError(
          "Cannot create an instance of ObjectWorldReservation from an "
          "intrinsic_proto::skills::ObjectWorldReservation which has no "
          "'object_type' set.");
  }
}

intrinsic_proto::skills::ObjectWorldReservation
ObjectWorldReservation::ToProto() const {
  intrinsic_proto::skills::ObjectWorldReservation result;
  result.set_type(sharing_type_);
  std::visit(absl::Overload(
                 [&](const WorldObjectName& object) {
                   intrinsic_proto::world::ObjectReferenceByName object_ref;
                   object_ref.set_object_name(object.value());
                   *result.mutable_object() = object_ref;
                 },
                 [&](const FrameReferenceByName& frame) {
                   intrinsic_proto::world::FrameReferenceByName frame_ref;
                   frame_ref.set_object_name(frame.object_name.value());
                   frame_ref.set_frame_name(frame.frame_name.value());
                   *result.mutable_frame() = frame_ref;
                 }),
             reservation_);
  return result;
}

ObjectWorldReservation::SharingType ObjectWorldReservation::GetSharingType()
    const {
  return sharing_type_;
}

ObjectWorldReservation::ReservationType
ObjectWorldReservation::GetReservationType() const {
  return std::visit(
      absl::Overload(
          [](const WorldObjectName&) {
            return ObjectWorldReservation::ReservationType::kObject;
          },
          [](const FrameReferenceByName&) {
            return ObjectWorldReservation::ReservationType::kFrame;
          }),
      reservation_);
}

std::optional<WorldObjectName> ObjectWorldReservation::GetObjectReservation()
    const {
  return std::visit(
      absl::Overload(
          [](const WorldObjectName& object) { return std::optional(object); },
          [](const FrameReferenceByName&) {
            return std::optional<WorldObjectName>();
          }),
      reservation_);
}

std::optional<FrameReferenceByName>
ObjectWorldReservation::GetFrameReservation() const {
  return std::visit(absl::Overload(
                        [](const WorldObjectName&) {
                          return std::optional<FrameReferenceByName>();
                        },
                        [](const FrameReferenceByName& frame) {
                          return std::optional(frame);
                        }),
                    reservation_);
}

bool ObjectWorldReservation::operator==(
    const ObjectWorldReservation& other) const {
  return sharing_type_ == other.sharing_type_ &&
         reservation_ == other.reservation_;
}

bool ObjectWorldReservation::operator!=(
    const ObjectWorldReservation& other) const {
  return !(*this == other);
}

std::ostream& operator<<(std::ostream& os,
                         const ObjectWorldReservation& reservation) {
  os << "ObjectWorldReservation(sharing_type="
     << (reservation.sharing_type_ == ObjectWorldReservation::kSharingTypeRead
             ? "READ"
             : "WRITE")
     << ", reservation=";
  std::visit(absl::Overload(
                 [&](const WorldObjectName& object) {
                   os << "WorldObjectName(" << object << ")";
                 },
                 [&](const FrameReferenceByName& frame) {
                   os << "FrameReferenceByName(" << frame << ")";
                 }),
             reservation.reservation_);
  os << ")";
  return os;
}

}  // namespace intrinsic
