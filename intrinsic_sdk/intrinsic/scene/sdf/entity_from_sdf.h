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


#ifndef INTRINSIC_SCENE_SDF_ENTITY_FROM_SDF_H_
#define INTRINSIC_SCENE_SDF_ENTITY_FROM_SDF_H_

#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/sdf/sdf_path_resolver.h"
#include "sdf/Frame.hh"
#include "sdf/Joint.hh"
#include "sdf/Link.hh"
#include "sdf/Model.hh"
#include "sdf/Sensor.hh"
#include "sdf/World.hh"

namespace intrinsic {
namespace scene_object {

// Converts a SDFormat Frame to an optional Intrinsic Scene Object Entity with
// entity_type frame.
absl::StatusOr<std::optional<intrinsic_proto::scene_object::v1::Entity>>
EntityFromSdfFrame(const ::sdf::Frame& sdf_frame);

// Converts a SDFormat Link to a collection of Intrinsic Scene Object Entities.
// Each <link> converts to one Entity with entity_type link and multiple
// Entities with entity_type sensor. A `geometry_serializer` is required to
// store the geometry in callers's preferred storage.
absl::StatusOr<std::vector<intrinsic_proto::scene_object::v1::Entity>>
EntitiesFromSdfLink(const ::sdf::Link& sdf_link,
                    const sdf::UriResolver& uri_resolver,
                    GeometrySerializer& geometry_serializer);

// Converts a SDFormat Joint to a collection of Intrinsic Scene Object Entities.
// Each <joint> converts to one Entity with entity_type joint and multiple
// Entities with entity_type sensor.
struct CreateJointEntitiesResult {
  intrinsic_proto::scene_object::v1::Entity joint_entity;
  std::vector<intrinsic_proto::scene_object::v1::Entity> sensor_entities;
  std::string joint_child_name;
};

// Returns the joint entity, any attached sensor entities, and the child link
// name converted from `sdf_joint`.
//
// `child_link`, if not null, is the joint's child link. It allows poses that
// are expressed directly relative to the joint to be parsed exactly; see
// sdf::ParseJoint().
absl::StatusOr<CreateJointEntitiesResult> EntitiesFromSdfJoint(
    const ::sdf::Joint& sdf_joint, const ::sdf::Link* child_link = nullptr);

// Converts a SDFormat Sensor to an Intrinsic Scene Object Entity with
// entity_type sensor.
absl::StatusOr<intrinsic_proto::scene_object::v1::Entity> EntityFromSdfSensor(
    const ::sdf::Sensor& sdf_sensor);

}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SDF_ENTITY_FROM_SDF_H_
