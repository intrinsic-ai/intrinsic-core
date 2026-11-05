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

#ifndef INTRINSIC_SIMULATION_GAZEBO_PLUGINS_LINK_DEVICE_UTIL_H_
#define INTRINSIC_SIMULATION_GAZEBO_PLUGINS_LINK_DEVICE_UTIL_H_

#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gz/sim/Entity.hh"
#include "gz/sim/EntityComponentManager.hh"

namespace intrinsic {
namespace simulation {

// Preferred scope separator for link names specified in the plugin SDF for
// Timeslicer Link Devices in Gazebo.
constexpr char kLinkDeviceLinkNameScopeSeparator[] = "::";

// Get Link entity by parsing relative scoped name of the form
// "child_model::grandchild_model::joint_name". Just "link_name" also works.
absl::StatusOr<::gz::sim::Entity> GetLinkEntityFromRelativeScopedName(
    absl::string_view relative_scoped_link_name,
    absl::string_view scope_separator, ::gz::sim::Entity parent_entity,
    const ::gz::sim::EntityComponentManager& ecm);

// Creates a new DetachableJoint entity between parent link and child link
// Parent entity for the new joint entity is the parent model of the parent link
// The new joint takes effect in the Physics system starting the same simulation
// iteration, assuming that this function is called from a System PreUpdate call
// Always returns a valid entity on success or an error code on failure.
//
// Note: Adding a detachable can create kinematic loops if a joint is added
// between two links which share an ancestor link or model or world (if one of
// the parent models is static). This function does not check for this
// possibility, but the Physics system does, and will fail.
absl::StatusOr<::gz::sim::Entity> AddDetachableJoint(
    ::gz::sim::Entity parent_link, ::gz::sim::Entity child_link,
    ::gz::sim::EntityComponentManager& ecm);

// Removes the given joint entity from the ecm. Note that the Physics system
// will remove the joint AFTER integrating for one more timestep.
absl::Status RemoveDetachableJoint(::gz::sim::Entity joint,
                                   ::gz::sim::EntityComponentManager& ecm);

// Returns the links that are rigidly attached as descendants of `root_link`,
// i.e. that are reachable from `root_link` by traversing fixed `<joint>`s and
// DetachableJoints from parent link to child link. `root_link` itself is
// always part of the result. Non-fixed joints and ancestor links of
// `root_link` are never traversed.
//
// - Joints in `exclude_joints` are not traversed. Note that joints whose
//   removal has been requested but not yet processed by the ECM are still
//   traversed; callers that requested such removals should list them here.
// - Links in `stop_links` are not entered (neither they nor anything only
//   reachable through them is part of the result). `root_link` is entered even
//   if it is in `stop_links`.
absl::flat_hash_set<::gz::sim::Entity> GetRigidlyAttachedLinks(
    ::gz::sim::Entity root_link, const ::gz::sim::EntityComponentManager& ecm,
    const absl::flat_hash_set<::gz::sim::Entity>& exclude_joints,
    const absl::flat_hash_set<::gz::sim::Entity>& stop_links);

// Returns a signature of the runtime rigid-attachment topology of the ECM: the
// sorted entity ids of all DetachableJoints not in `exclude_joints`. Since
// structural fixed `<joint>`s are created with their models and runtime
// attachments/detachments always use DetachableJoints, two equal signatures
// after initialization imply that GetRigidlyAttachedLinks() would return the
// same result for the same arguments, so callers can cheaply detect when a
// recomputation is necessary without scanning every joint in the world.
std::vector<::gz::sim::Entity> GetRigidAttachmentTopologySignature(
    const ::gz::sim::EntityComponentManager& ecm,
    const absl::flat_hash_set<::gz::sim::Entity>& exclude_joints = {});

}  // namespace simulation
}  // namespace intrinsic

#endif  // INTRINSIC_SIMULATION_GAZEBO_PLUGINS_LINK_DEVICE_UTIL_H_
