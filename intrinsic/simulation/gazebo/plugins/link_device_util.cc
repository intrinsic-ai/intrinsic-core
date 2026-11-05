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

#include "intrinsic/simulation/gazebo/plugins/link_device_util.h"

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "gz/sim/Entity.hh"
#include "gz/sim/Model.hh"
#include "gz/sim/components/ChildLinkName.hh"
#include "gz/sim/components/DetachableJoint.hh"
#include "gz/sim/components/Joint.hh"
#include "gz/sim/components/JointType.hh"
#include "gz/sim/components/Link.hh"
#include "gz/sim/components/Model.hh"
#include "gz/sim/components/ParentLinkName.hh"
#include "intrinsic/simulation/gazebo/plugins/util.h"
#include "intrinsic/util/status/status_builder.h"
#include "sdf/Joint.hh"

namespace intrinsic {
namespace simulation {

using ::gz::sim::kNullEntity;
using ::gz::sim::components::Link;
using ::gz::sim::components::Model;

using DetachableJointComponent = ::gz::sim::components::DetachableJoint;
using GazeboECM = ::gz::sim::EntityComponentManager;
using GazeboEntity = ::gz::sim::Entity;

absl::StatusOr<GazeboEntity> GetLinkEntityFromRelativeScopedName(
    absl::string_view relative_scoped_link_name,
    absl::string_view scope_separator, GazeboEntity parent_entity,
    const GazeboECM& ecm) {
  std::vector<std::string> parts =
      absl::StrSplit(relative_scoped_link_name, scope_separator);
  if (parts.size() > 1) {
    for (auto& part : parts) {
      auto child_model_entity = GetChildModelByName(part, parent_entity, ecm);
      if (!child_model_entity.ok()) {
        break;
      }
      parent_entity = *child_model_entity;
      relative_scoped_link_name = relative_scoped_link_name.substr(
          part.size() + scope_separator.size());
    }
  }

  auto model = ::gz::sim::Model(parent_entity);
  if (!model.Valid(ecm)) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "Parent entity " << parent_entity << " is not a model";
  }

  auto link_entity =
      model.LinkByName(ecm, std::string(relative_scoped_link_name));
  if (link_entity == kNullEntity) {
    return intrinsic::NotFoundErrorBuilder()
           << "Unable to find link named " << relative_scoped_link_name;
  }

  return link_entity;
}

absl::StatusOr<GazeboEntity> AddDetachableJoint(GazeboEntity parent_link,
                                                GazeboEntity child_link,
                                                GazeboECM& ecm) {
  if (parent_link == kNullEntity) {
    return absl::InvalidArgumentError("Parent link is kNullEntity");
  }
  if (child_link == kNullEntity) {
    return absl::InvalidArgumentError("Child link is kNullEntity");
  }

  auto joint_entity = ecm.CreateEntity();
  if (joint_entity == kNullEntity) {
    return absl::InternalError("Failed to create entity");
  }

  ::gz::sim::components::DetachableJointInfo info(
      {.parentLink = parent_link, .childLink = child_link});

  auto* component =
      ecm.CreateComponent(joint_entity, DetachableJointComponent(info));
  if (component == nullptr) {
    return absl::InternalError("Failed to create component");
  }

  return joint_entity;
}

absl::Status RemoveDetachableJoint(GazeboEntity joint, GazeboECM& ecm) {
  if (joint == kNullEntity) {
    return absl::InvalidArgumentError("Joint is kNullEntity");
  }
  ecm.RequestRemoveEntity(joint);
  return absl::OkStatus();
}

namespace {

// Resolves the parent or child link name of a fixed `<joint>` to a link entity.
// Joint link names are relative to the model the joint belongs to and may be
// scoped (e.g. "nested_model::link") when the joint connects to a nested model.
// Returns kNullEntity if the name cannot be resolved (e.g. "world").
GazeboEntity ResolveJointLink(absl::string_view link_name,
                              GazeboEntity joint_model, const GazeboECM& ecm) {
  if (link_name == "world" || joint_model == kNullEntity) {
    return kNullEntity;
  }
  absl::StatusOr<GazeboEntity> link = GetLinkEntityFromRelativeScopedName(
      link_name, kLinkDeviceLinkNameScopeSeparator, joint_model, ecm);
  return link.ok() ? *link : kNullEntity;
}

// Collects directed parent -> child adjacency over rigid edges (fixed
// `<joint>`s and DetachableJoints) in the ECM, excluding `exclude_joints`.
absl::flat_hash_map<GazeboEntity, std::vector<GazeboEntity>>
CollectRigidChildren(const GazeboECM& ecm,
                     const absl::flat_hash_set<GazeboEntity>& exclude_joints) {
  absl::flat_hash_map<GazeboEntity, std::vector<GazeboEntity>> children;
  ecm.Each<::gz::sim::components::Joint, ::gz::sim::components::JointType,
           ::gz::sim::components::ParentLinkName,
           ::gz::sim::components::ChildLinkName>(
      [&](const GazeboEntity& joint, const ::gz::sim::components::Joint*,
          const ::gz::sim::components::JointType* type,
          const ::gz::sim::components::ParentLinkName* parent_name,
          const ::gz::sim::components::ChildLinkName* child_name) -> bool {
        if (type->Data() != ::sdf::JointType::FIXED ||
            exclude_joints.contains(joint)) {
          return true;
        }
        const GazeboEntity joint_model = ecm.ParentEntity(joint);
        const GazeboEntity parent =
            ResolveJointLink(parent_name->Data(), joint_model, ecm);
        const GazeboEntity child =
            ResolveJointLink(child_name->Data(), joint_model, ecm);
        if (parent != kNullEntity && child != kNullEntity) {
          children[parent].push_back(child);
        }
        return true;
      });
  ecm.Each<::gz::sim::components::DetachableJoint>(
      [&](const GazeboEntity& joint,
          const ::gz::sim::components::DetachableJoint* component) -> bool {
        if (exclude_joints.contains(joint)) {
          return true;
        }
        const auto& info = component->Data();
        if (info.parentLink != kNullEntity && info.childLink != kNullEntity) {
          children[info.parentLink].push_back(info.childLink);
        }
        return true;
      });
  return children;
}

}  // namespace

absl::flat_hash_set<GazeboEntity> GetRigidlyAttachedLinks(
    GazeboEntity root_link, const GazeboECM& ecm,
    const absl::flat_hash_set<GazeboEntity>& exclude_joints,
    const absl::flat_hash_set<GazeboEntity>& stop_links) {
  absl::flat_hash_set<GazeboEntity> attached;
  if (root_link == kNullEntity) {
    return attached;
  }

  // Directed parent -> child adjacency over rigid edges so traversal only
  // visits descendant links mounted onto `root_link`, never ancestor links
  // (such as the gripper base or robot arm).
  const absl::flat_hash_map<GazeboEntity, std::vector<GazeboEntity>> children =
      CollectRigidChildren(ecm, exclude_joints);

  // Breadth first search from the root link.
  std::deque<GazeboEntity> queue{root_link};
  attached.insert(root_link);
  while (!queue.empty()) {
    const GazeboEntity current = queue.front();
    queue.pop_front();
    auto it = children.find(current);
    if (it == children.end()) {
      continue;
    }
    for (GazeboEntity next : it->second) {
      if (attached.contains(next) || stop_links.contains(next)) {
        continue;
      }
      attached.insert(next);
      queue.push_back(next);
    }
  }
  return attached;
}

std::vector<GazeboEntity> GetRigidAttachmentTopologySignature(
    const GazeboECM& ecm,
    const absl::flat_hash_set<GazeboEntity>& exclude_joints) {
  std::vector<GazeboEntity> signature;
  ecm.Each<::gz::sim::components::DetachableJoint>(
      [&](const GazeboEntity& joint,
          const ::gz::sim::components::DetachableJoint*) -> bool {
        if (!exclude_joints.contains(joint)) {
          signature.push_back(joint);
        }
        return true;
      });
  std::sort(signature.begin(), signature.end());
  return signature;
}

}  // namespace simulation
}  // namespace intrinsic
