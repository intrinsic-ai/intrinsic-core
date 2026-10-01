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

#include "intrinsic/scene/usd/connection_graph.h"

#include <pxr/base/tf/token.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdPhysics/fixedJoint.h>
#include <pxr/usd/usdPhysics/joint.h>
#include <pxr/usd/usdPhysics/rigidBodyAPI.h>

#include <algorithm>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "intrinsic/scene/usd/entity_from_usd.h"
#include "intrinsic/scene/usd/utils.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace usd {

PhysicsPrims GetPhysicsPrims(const pxr::UsdPrim& root_prim) {
  // Iterate the hierarchy and collect rigid bodies + joints as we go.
  // Pass UsdTraverseInstanceProxies to make sure we traverse into instance
  // proxies automatically.
  // https://openusd.org/release/api/_usd__page__scenegraph_instancing.html#Usd_ScenegraphInstancing_InstanceProxies
  PhysicsPrims physics_prims;
  const pxr::UsdPrimRange prim_range{root_prim.GetPrim(),
                                     pxr::UsdTraverseInstanceProxies()};
  for (auto it = prim_range.begin(); it != prim_range.end(); ++it) {
    const pxr::UsdPrim& prim = *it;
    if (IsMarkedInvisible(prim)) {
      // Skip invisible subtrees
      it.PruneChildren();
      continue;
    }
    if (prim.HasAPI<pxr::UsdPhysicsRigidBodyAPI>()) {
      pxr::UsdPhysicsRigidBodyAPI rigid_body{prim};
      physics_prims.rigid_bodies.push_back(rigid_body);
    } else if (prim.IsA<pxr::UsdPhysicsJoint>()) {
      pxr::UsdPhysicsJoint joint{prim};
      physics_prims.joints.push_back(joint);
    }
  }
  return physics_prims;
}

std::optional<std::string> TryGetRootLinkPath(const pxr::UsdPrim& root_prim) {
  // Get the base link path from the IsaacSim attribute that provides
  // the order of robot links (if it exists).
  //
  // rel isaac:physics:robotLinks = [
  //   </my_robot/base_link>,
  //   ...
  // ]
  std::optional<pxr::UsdRelationship> robot_links_rel =
      FindRelationshipWithName(root_prim, "isaac:physics:robotLinks");
  if (!robot_links_rel.has_value()) {
    return std::nullopt;
  }
  if (!robot_links_rel->IsValid() || !robot_links_rel->HasAuthoredTargets()) {
    return std::nullopt;
  }
  auto robot_links_result = GetTargetPaths(*robot_links_rel);
  if (!robot_links_result.ok()) {
    return std::nullopt;
  }
  std::vector<std::string> robot_links = robot_links_result.value();
  if (robot_links.empty()) {
    return std::nullopt;
  }
  // Check that the root link is a valid RigidBody prim.
  pxr::UsdPrim root_link_prim =
      root_prim.GetStage()->GetPrimAtPath(pxr::SdfPath(robot_links[0]));
  if (!root_link_prim.HasAPI<pxr::UsdPhysicsRigidBodyAPI>()) {
    return std::nullopt;
  }
  return robot_links[0];
}

void ConnectionGraph::LinkNode::AddConnectedLink(
    ConnectionGraph::LinkNode* absl_nonnull connected_link) {
  auto it =
      std::find(connected_links.begin(), connected_links.end(), connected_link);
  if (it == connected_links.end()) {
    connected_links.push_back(connected_link);
  }
}

void ConnectionGraph::LinkNode::AddConnectedJoint(
    ConnectionGraph::JointNode* absl_nonnull connected_joint) {
  auto it = std::find(connected_joints.begin(), connected_joints.end(),
                      connected_joint);
  if (it == connected_joints.end()) {
    connected_joints.push_back(connected_joint);
  }
}

ConnectionGraph::LinkNode* absl_nonnull ConnectionGraph::GetOrAddLinkNode(
    const pxr::UsdPhysicsRigidBodyAPI& rigid_body) {
  auto path = rigid_body.GetPrim().GetPath().GetString();
  auto it = path_to_link_.find(path);
  if (it != path_to_link_.end()) {
    return it->second;
  }
  auto link_node = std::make_unique<LinkNode>(rigid_body);
  auto link_node_ptr = link_node.get();
  path_to_link_[path] = link_node_ptr;
  links_.push_back(std::move(link_node));
  return link_node_ptr;
}

ConnectionGraph::JointNode* absl_nonnull ConnectionGraph::GetOrAddJointNode(
    const pxr::UsdPhysicsJoint& joint) {
  auto path = joint.GetPrim().GetPath().GetString();
  auto it = path_to_joint_.find(path);
  if (it != path_to_joint_.end()) {
    return it->second;
  }
  auto joint_node = std::make_unique<JointNode>(joint);
  auto joint_node_ptr = joint_node.get();
  path_to_joint_[path] = joint_node_ptr;
  joints_.push_back(std::move(joint_node));
  return joint_node_ptr;
}

absl::StatusOr<const ConnectionGraph::LinkNode* absl_nonnull>
ConnectionGraph::GetLinkNode(
    const pxr::UsdPhysicsRigidBodyAPI& rigid_body) const {
  auto path = rigid_body.GetPrim().GetPath().GetString();
  auto it = path_to_link_.find(path);
  INTR_RET_CHECK(it != path_to_link_.end());
  return it->second;
}

absl::StatusOr<const ConnectionGraph::JointNode* absl_nonnull>
ConnectionGraph::GetJointNode(const pxr::UsdPhysicsJoint& joint) const {
  auto path = joint.GetPrim().GetPath().GetString();
  auto it = path_to_joint_.find(path);
  INTR_RET_CHECK(it != path_to_joint_.end());
  return it->second;
}

absl::StatusOr<ConnectionGraph> ComputeConnectionGraph(
    const PhysicsPrims& physics_prims, pxr::UsdGeomXformCache& xform_cache) {
  ConnectionGraph connection_graph;

  for (const auto& rigid_body : physics_prims.rigid_bodies) {
    connection_graph.GetOrAddLinkNode(rigid_body);
  }
  for (const auto& joint : physics_prims.joints) {
    INTR_ASSIGN_OR_RETURN(auto joint_connection_type,
                          GetJointConnectionType(joint));
    if (joint_connection_type != JointConnectionType::RigidBodyToRigidBody) {
      // Ignore joints that do not connect two rigid bodies. USD files commonly
      // contain a "root_joint" that connects one body to the static
      // environment. We can safely ignore these.
      continue;
    }
    INTR_ASSIGN_OR_RETURN(
        auto joint_bodies, ParseJointBodies(joint, xform_cache),
        _ << "While parsing joint " << joint.GetPrim().GetPath().GetString());
    auto body0_node = connection_graph.GetOrAddLinkNode(joint_bodies.body0);
    auto body1_node = connection_graph.GetOrAddLinkNode(joint_bodies.body1);

    if (joint.GetPrim().IsA<pxr::UsdPhysicsFixedJoint>()) {
      // For fixed joints, we directly connect the rigid bodies, instead of
      // creating a separate joint node.
      body0_node->AddConnectedLink(body1_node);
      body1_node->AddConnectedLink(body0_node);
    } else {
      // For other joints (revolute, prismatic, etc.), we connect each body
      // to the joint.
      ConnectionGraph::JointNode* joint_node =
          connection_graph.GetOrAddJointNode(joint);
      joint_node->link0 = body0_node;
      joint_node->link1 = body1_node;
      body0_node->AddConnectedJoint(joint_node);
      body1_node->AddConnectedJoint(joint_node);
    }
  }

  return connection_graph;
}

std::string ConnectionGraph::ToDebugString() const {
  std::string str;
  absl::StrAppend(&str, "Links:\n");
  for (const auto& link : links_) {
    absl::StrAppend(&str, "  ",
                    link->rigid_body.GetPrim().GetPath().GetString(), "\n");
    for (const auto& connected_link : link->connected_links) {
      absl::StrAppend(
          &str, "  -> ",
          connected_link->rigid_body.GetPrim().GetPath().GetString(), "\n");
    }
    for (const auto& connected_joint : link->connected_joints) {
      absl::StrAppend(&str, "  -> ",
                      connected_joint->joint.GetPrim().GetPath().GetString(),
                      "\n");
    }
  }
  absl::StrAppend(&str, "\nJoints:\n");
  for (const auto& joint : joints_) {
    absl::StrAppend(&str, "  ", joint->joint.GetPrim().GetPath().GetString(),
                    "\n");
    absl::StrAppend(&str, "  -> ",
                    joint->link0->rigid_body.GetPrim().GetPath().GetString(),
                    "\n");
    absl::StrAppend(&str, "  -> ",
                    joint->link1->rigid_body.GetPrim().GetPath().GetString(),
                    "\n");
  }
  return str;
}

absl::Status ConnectionTree::LinkNode::AddChild(
    LinkNode* absl_nonnull new_child_link) {
  INTR_RET_CHECK(new_child_link->parent_node == nullptr);
  new_child_link->parent_node = this;
  child_links.push_back(new_child_link);
  return absl::OkStatus();
}

absl::Status ConnectionTree::LinkNode::AddChild(
    JointNode* absl_nonnull new_child_joint) {
  INTR_RET_CHECK(new_child_joint->parent_link == nullptr);
  new_child_joint->parent_link = this;
  child_joints.push_back(new_child_joint);
  return absl::OkStatus();
}

void ConnectionTree::LinkNode::RemoveChildJoint(
    const std::string& joint_prim_path) {
  std::erase_if(child_joints, [&](JointNode* absl_nonnull joint) {
    return joint->GetPrimPath() == joint_prim_path;
  });
}

absl::Status ConnectionTree::JointNode::SetChild(
    LinkNode* absl_nonnull new_child_link) {
  INTR_RET_CHECK(new_child_link->parent_node == nullptr);
  new_child_link->parent_node = this;
  child_link = new_child_link;
  return absl::OkStatus();
}

bool ConnectionTree::HasLink(
    const pxr::UsdPhysicsRigidBodyAPI& rigid_body) const {
  auto path = rigid_body.GetPrim().GetPath().GetString();
  auto it = path_to_link_.find(path);
  return it != path_to_link_.end();
}

bool ConnectionTree::HasJoint(const pxr::UsdPhysicsJoint& joint) const {
  auto path = joint.GetPrim().GetPath().GetString();
  auto it = path_to_joint_.find(path);
  return it != path_to_joint_.end();
}

absl::StatusOr<ConnectionTree::LinkNode* absl_nonnull>
ConnectionTree::GetLinkNode(const pxr::UsdPhysicsRigidBodyAPI& rigid_body) {
  auto path = rigid_body.GetPrim().GetPath().GetString();
  auto it = path_to_link_.find(path);
  INTR_RET_CHECK(it != path_to_link_.end()) << absl::Substitute(
      "No node $0", rigid_body.GetPrim().GetPath().GetString());
  return it->second;
}

absl::StatusOr<ConnectionTree::JointNode* absl_nonnull>
ConnectionTree::GetJointNode(const pxr::UsdPhysicsJoint& joint) {
  auto path = joint.GetPrim().GetPath().GetString();
  auto it = path_to_joint_.find(path);
  INTR_RET_CHECK(it != path_to_joint_.end())
      << absl::Substitute("No joint $0", joint.GetPrim().GetPath().GetString());
  return it->second;
}

absl::StatusOr<ConnectionTree::LinkNode* absl_nonnull>
ConnectionTree::AddLinkNode(const pxr::UsdPhysicsRigidBodyAPI& rigid_body) {
  INTR_RET_CHECK(!HasLink(rigid_body)) << absl::Substitute(
      "Already has link $0", rigid_body.GetPrim().GetPath().GetString());
  auto path = rigid_body.GetPrim().GetPath().GetString();
  auto new_node = std::make_unique<ConnectionTree::LinkNode>(rigid_body);
  auto new_node_ptr = new_node.get();
  path_to_link_[path] = new_node_ptr;
  links_.push_back(std::move(new_node));
  return new_node_ptr;
}

absl::StatusOr<ConnectionTree::JointNode* absl_nonnull>
ConnectionTree::AddJointNode(const pxr::UsdPhysicsJoint& joint) {
  INTR_RET_CHECK(!HasJoint(joint)) << absl::Substitute(
      "Already has joint $0", joint.GetPrim().GetPath().GetString());
  auto path = joint.GetPrim().GetPath().GetString();
  auto new_node = std::make_unique<ConnectionTree::JointNode>(joint);
  auto new_node_ptr = new_node.get();
  path_to_joint_[path] = new_node_ptr;
  joints_.push_back(std::move(new_node));
  return new_node_ptr;
}

std::vector<const ConnectionTree::LinkNode* absl_nonnull>
ConnectionTree::GetToplevelLinks() const {
  std::vector<const LinkNode* absl_nonnull> toplevel_links;
  for (const auto& link : links_) {
    if (link->parent_node == nullptr) {
      toplevel_links.push_back(link.get());
    }
  }
  return toplevel_links;
}

void ConnectionTree::RemoveIncompleteJoints() {
  std::erase_if(joints_, [this](const std::unique_ptr<JointNode>& joint) {
    if (!joint->parent_link || !joint->child_link) {
      // Joint is incomplete.
      if (joint->parent_link) {
        joint->parent_link->RemoveChildJoint(joint->GetPrimPath());
      }
      if (joint->child_link) {
        joint->child_link->parent_node = nullptr;
      }
      path_to_joint_.erase(joint->GetPrimPath());
      return true;
    }
    return false;
  });
}

absl::StatusOr<ConnectionTree> ComputeConnectionTreeFromGraph(
    const ConnectionGraph& connection_graph,
    std::optional<std::string> root_link_path) {
  // Convert the prim graph to a valid connection tree, according to
  // these rules:
  //
  // 1. By default, all links are parented to the root (aka "").
  // 2. All Entities have exactly one parent (which may be the root)
  // 3. An Entity may have multiple children, in the case of a Link with many
  //    child Links fixed to it.
  // 4. No cycles are allowed.

  auto logCycleWarning = [&](const pxr::UsdPrim& prim1,
                             const pxr::UsdPrim& prim2) {
    LOG(WARNING) << absl::Substitute(
        "Detected a cycle in the rigid-body & joint hierarchy, formed by "
        "connection "
        "$0 -> $1. This connection will be ignored, to break the cycle.",
        prim1.GetPath().GetString(), prim2.GetPath().GetString());
  };

  // Construct the list of graph links to traverse.
  std::vector<ConnectionGraph::LinkNode* absl_nonnull> all_graph_links;
  for (const auto& link_node : connection_graph.GetLinks()) {
    all_graph_links.push_back(link_node.get());
  }
  // If the user specified the root prim path, sort the graph links so that
  // the root link is sent to the front of the list (preserving the order of the
  // other links).
  if (root_link_path.has_value()) {
    std::stable_partition(
        all_graph_links.begin(), all_graph_links.end(), [&](const auto& link) {
          return link->GetPrimPath() == root_link_path.value();
        });
  }

  // Traverse the ConnectionGraph as a DFS forest, using stack-based DFS.
  // Error if we detect any cycles.
  ConnectionTree connection_tree;
  for (const auto& link_node : all_graph_links) {
    pxr::UsdPhysicsRigidBodyAPI rigid_body = link_node->rigid_body;
    if (connection_tree.HasLink(rigid_body)) {
      // Already processed this Link
      continue;
    }
    INTR_ASSIGN_OR_RETURN(auto _, connection_tree.AddLinkNode(rigid_body));

    // Traverse the DFS tree rooted at this node
    // Each StackItem is {parent_node, current_node}
    using StackItem = std::pair<const ConnectionGraph::BaseNode*,
                                const ConnectionGraph::BaseNode*>;
    std::vector<StackItem> stack;
    stack.push_back({nullptr, link_node});
    while (!stack.empty()) {
      auto [parent_node, current_node] = stack.back();
      stack.pop_back();

      if (auto link_node =
              dynamic_cast<const ConnectionGraph::LinkNode* absl_nonnull>(
                  current_node);
          link_node) {
        INTR_ASSIGN_OR_RETURN(
            auto tree_link, connection_tree.GetLinkNode(link_node->rigid_body));

        // Add all connected links as child links (except the parent)
        std::vector<StackItem> new_link_items;
        for (const auto& connected_link : link_node->connected_links) {
          if (connected_link == parent_node) {
            continue;
          }
          if (connection_tree.HasLink(connected_link->rigid_body)) {
            // Detected a cycle. Log a warning and continue without
            // adding this edge.
            logCycleWarning(link_node->rigid_body.GetPrim(),
                            connected_link->rigid_body.GetPrim());
            continue;
          }
          INTR_ASSIGN_OR_RETURN(
              auto child_tree_link,
              connection_tree.AddLinkNode(connected_link->rigid_body));
          INTR_RETURN_IF_ERROR(tree_link->AddChild(child_tree_link));
          new_link_items.push_back({link_node, connected_link});
        }
        // Add all connected joints as child joints (except the parent)
        std::vector<StackItem> new_joint_items;
        for (const auto& connected_joint : link_node->connected_joints) {
          if (connected_joint == parent_node) {
            continue;
          }
          if (connection_tree.HasJoint(connected_joint->joint)) {
            // Detected a cycle. Log a warning and continue without adding
            // this edge.
            logCycleWarning(link_node->rigid_body.GetPrim(),
                            connected_joint->joint.GetPrim());
            continue;
          }
          INTR_ASSIGN_OR_RETURN(
              auto child_tree_joint,
              connection_tree.AddJointNode(connected_joint->joint));
          INTR_RETURN_IF_ERROR(tree_link->AddChild(child_tree_joint));
          new_joint_items.push_back({link_node, connected_joint});
        }

        // Add the new links and joints to the stack, in reverse order (so that
        // we pop them off and process them in the original order of
        // appearance).
        stack.insert(stack.end(), new_joint_items.rbegin(),
                     new_joint_items.rend());
        stack.insert(stack.end(), new_link_items.rbegin(),
                     new_link_items.rend());

      } else if (auto joint_node = dynamic_cast<
                     const ConnectionGraph::JointNode* absl_nonnull>(
                     current_node);
                 joint_node) {
        INTR_ASSIGN_OR_RETURN(auto tree_joint,
                              connection_tree.GetJointNode(joint_node->joint));

        // Add the non-parent link as this joint's child link. Push the
        // child node to the stack for processing.
        auto child_link = joint_node->link0 == parent_node ? joint_node->link1
                                                           : joint_node->link0;
        if (connection_tree.HasLink(child_link->rigid_body)) {
          // Detected a cycle. Log a warning and continue without adding this
          // edge.
          logCycleWarning(joint_node->joint.GetPrim(),
                          child_link->rigid_body.GetPrim());
        } else {
          INTR_ASSIGN_OR_RETURN(
              auto child_tree_link,
              connection_tree.AddLinkNode(child_link->rigid_body));
          INTR_RETURN_IF_ERROR(tree_joint->SetChild(child_tree_link));
          stack.push_back({joint_node, child_link});
        }
      } else {
        return absl::InternalError(
            "Unexpected ConnectionTree Node type. Expected a link or a joint.");
      }
    }
  }

  // We may have incomplete joints as a result of breaking cycles.
  connection_tree.RemoveIncompleteJoints();

  return connection_tree;
}

std::string ConnectionTree::ToDebugString() const {
  std::string str;

  absl::StrAppend(&str, "Links:\n");
  for (const auto& link : links_) {
    absl::StrAppend(&str, "  ",
                    link->rigid_body.GetPrim().GetPath().GetString(), "\n");
    for (const auto& child_link : link->child_links) {
      absl::StrAppend(&str, "  -> ",
                      child_link->rigid_body.GetPrim().GetPath().GetString(),
                      "\n");
    }
    for (const auto& child_joint : link->child_joints) {
      absl::StrAppend(&str, "  -> ",
                      child_joint->joint.GetPrim().GetPath().GetString(), "\n");
    }
  }
  absl::StrAppend(&str, "\nJoints:\n");
  for (const auto& joint : joints_) {
    absl::StrAppend(&str, "  ", joint->joint.GetPrim().GetPath().GetString(),
                    "\n");
    absl::StrAppend(
        &str, "  -> ",
        joint->child_link->rigid_body.GetPrim().GetPath().GetString(), "\n");
  }
  return str;
}

}  // namespace usd
}  // namespace intrinsic
