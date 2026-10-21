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

#ifndef INTRINSIC_ICON_EQUIPMENT_RESOLVED_DEPENDENCY_UTILS_H_
#define INTRINSIC_ICON_EQUIPMENT_RESOLVED_DEPENDENCY_UTILS_H_

#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"

namespace intrinsic {
namespace icon {

inline constexpr absl::string_view kHalArmPartTypeName = "HalArmPart";

// Pairs an ICON position part name with its corresponding kinematic object in
// the object world.
struct PositionPartWithObject {
  std::string part_name;
  world::KinematicObject kinematic_object;
};

// Combines a Channel (i.e. a connection to the ICON Application Layer) with
// part names and hardware resource mappings queried from a ResolvedDependency.
struct IconResolvedDependency {
  using PartType = std::string;
  using PartName = std::string;
  using HardwareResourceName = std::string;
  using HardwareResourceNameByPartName =
      absl::flat_hash_map<PartName, HardwareResourceName>;

  std::shared_ptr<ChannelInterface> channel;
  // Maps each `PartConfig::part_type_name` (e.g. "HalArmPart") to a map of
  // `PartConfig::name` -> `PartConfig::hardware_resource_name`. Guaranteed to
  // contain a non-empty `HardwareResourceName` for every entry.
  absl::flat_hash_map<PartType, HardwareResourceNameByPartName> parts;

  // Resolves a position part name and its corresponding `KinematicObject` in
  // `world` from `parts`.
  //
  // If `arm_part` is omitted or empty, requires `parts` to have a single
  // `"HalArmPart"` entry and looks up its kinematic object using its
  // `hardware_resource_name`. If `arm_part` is provided and non-empty, looks up
  // the kinematic object from `arm_part` in `world` and matches its name
  // against the `hardware_resource_name`s of the `"HalArmPart"` entries in
  // `parts`.
  absl::StatusOr<PositionPartWithObject> GetPositionPartAndKinematicObject(
      const world::ObjectWorldClient& world,
      const intrinsic_proto::world::ObjectReference& arm_part = {}) const;
};

// Connects to the ICON Application Layer at `resolved_dependency`, queries
// `GetConfig()`, and populates `IconResolvedDependency::parts` keyed by
// `part_type_name` and `name`.
//
// Returns an error if the connection is not made within `timeout`, if the
// dependency configuration is malformed, or if any part in the ICON server
// config has an empty `part_type_name` or `hardware_resource_name`.
absl::StatusOr<IconResolvedDependency> ConnectToIconResolvedDependency(
    const intrinsic_proto::assets::v1::ResolvedDependency& resolved_dependency,
    absl::Duration timeout = connect::kGrpcClientConnectDefaultTimeout);

}  // namespace icon
}  // namespace intrinsic

#endif  // INTRINSIC_ICON_EQUIPMENT_RESOLVED_DEPENDENCY_UTILS_H_
