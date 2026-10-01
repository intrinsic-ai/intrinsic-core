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

#ifndef INTRINSIC_SCENE_USD_CONNECTION_GRAPH_USD_H_
#define INTRINSIC_SCENE_USD_CONNECTION_GRAPH_USD_H_

#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdPhysics/joint.h>
#include <pxr/usd/usdPhysics/rigidBodyAPI.h>

#include <memory>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

// This file contains the `ConnectionGraph` and related `ConnectionTree`
// classes. These are helper classes used during UsdStage to SceneObject
// conversion. A UsdStage is first converted to a graph, then traversed to form
// a tree, and then finally converted to our SceneObject Entity hierarchy (which
// is itself a tree).

namespace intrinsic {
namespace usd {

// Helper class to store the rigid bodies and joints of a stage
struct PhysicsPrims {
  std::vector<pxr::UsdPhysicsRigidBodyAPI> rigid_bodies;
  std::vector<pxr::UsdPhysicsJoint> joints;
};

// Traverse `root_prim` recursively, collecting all rigid bodies and joints
// as we go.
PhysicsPrims GetPhysicsPrims(const pxr::UsdPrim& root_prim);

// Attempts to find the path of the root link of the robot by reading the
// IsaacSim robotLinks attribute, if it exists. If not, returns none. When
// given, that attribute gives an ordered list of the links of a robot. The
// first link will be the root link (aka base link).
//
// Docs here:
// https://docs.isaacsim.omniverse.nvidia.com/5.1.0/omniverse_usd/robot_schema.html
std::optional<std::string> TryGetRootLinkPath(const pxr::UsdPrim& root_prim);

// Helper class that represents an undirected graph of link and joint
// connections, parsed from the USD stage by `ComputeConnectionGraph`.
// It is converted to a ConnectionTree using a DFS traversal (see
// `ComputeConnectionTreeFromGraph`).
class ConnectionGraph {
 public:
  struct LinkNode;
  struct JointNode;

  struct BaseNode {
    virtual ~BaseNode() = default;
    virtual std::string GetPrimPath() const = 0;
  };

  struct LinkNode : public BaseNode {
    pxr::UsdPhysicsRigidBodyAPI rigid_body;
    // `connected_links` are the Links connected to this one by
    // fixed joints.
    std::vector<LinkNode* absl_nonnull> connected_links;
    std::vector<JointNode* absl_nonnull> connected_joints;

    LinkNode(const pxr::UsdPhysicsRigidBodyAPI& rigid_body)
        : rigid_body(rigid_body) {}

    // Adds a connected link, preventing duplicates
    void AddConnectedLink(LinkNode* absl_nonnull connected_link);
    // Adds a connected joint, preventing duplicates
    void AddConnectedJoint(JointNode* absl_nonnull connected_joint);

    virtual std::string GetPrimPath() const override {
      return rigid_body.GetPrim().GetPath().GetString();
    }
  };

  struct JointNode : public BaseNode {
    pxr::UsdPhysicsJoint joint;
    LinkNode* absl_nullable link0{nullptr};
    LinkNode* absl_nullable link1{nullptr};

    JointNode(const pxr::UsdPhysicsJoint& joint) : joint(joint) {}

    std::string GetPrimPath() const override {
      return joint.GetPrim().GetPath().GetString();
    }
  };

  ConnectionGraph() = default;

  LinkNode* absl_nonnull GetOrAddLinkNode(
      const pxr::UsdPhysicsRigidBodyAPI& rigid_body);
  JointNode* absl_nonnull GetOrAddJointNode(const pxr::UsdPhysicsJoint& joint);

  absl::StatusOr<const LinkNode* absl_nonnull> GetLinkNode(
      const pxr::UsdPhysicsRigidBodyAPI& rigid_body) const;
  absl::StatusOr<const JointNode* absl_nonnull> GetJointNode(
      const pxr::UsdPhysicsJoint& joint) const;

  const std::vector<absl_nonnull std::unique_ptr<LinkNode>>& GetLinks() const {
    return links_;
  }
  const std::vector<absl_nonnull std::unique_ptr<JointNode>>& GetJoints()
      const {
    return joints_;
  }

  // Get a string describing all nodes and edges.
  std::string ToDebugString() const;

 private:
  std::vector<absl_nonnull std::unique_ptr<LinkNode>> links_;
  std::vector<absl_nonnull std::unique_ptr<JointNode>> joints_;
  absl::flat_hash_map<std::string, LinkNode* absl_nonnull> path_to_link_;
  absl::flat_hash_map<std::string, JointNode* absl_nonnull> path_to_joint_;
};

// Create a ConnectionGraph of the given links and joints
absl::StatusOr<ConnectionGraph> ComputeConnectionGraph(
    const PhysicsPrims& physics_prims, pxr::UsdGeomXformCache& xform_cache);

// Helper class that stores the connection tree of the links and joints.
// This class is similar to ConnectionGraph, with the important difference that
// the LinkNode and JointNode classes only store their direct children instead
// of all connections, making a tree. A ConnectionGraph can be converted to a
// ConnectionTree using `ComputeConnectionTreeFromGraph` so long as there are no
// cycles in the graph. The tree can then be converted to our SceneObject Entity
// hierarchy.
class ConnectionTree {
 public:
  struct LinkNode;
  struct JointNode;

  struct BaseNode {
    virtual ~BaseNode() = default;
    virtual const pxr::UsdPrim GetPrim() const = 0;
    std::string GetPrimPath() const { return GetPrim().GetPath().GetString(); }
  };

  // A Link has child links fixed to it, and child joints
  struct LinkNode : public BaseNode {
    const pxr::UsdPhysicsRigidBodyAPI rigid_body;
    BaseNode* absl_nullable parent_node{nullptr};
    std::vector<LinkNode* absl_nonnull> child_links;
    std::vector<JointNode* absl_nonnull> child_joints;

    LinkNode(const pxr::UsdPhysicsRigidBodyAPI& rigid_body)
        : rigid_body(rigid_body) {}

    absl::Status AddChild(LinkNode* absl_nonnull new_child_link);
    absl::Status AddChild(JointNode* absl_nonnull new_child_joint);

    void RemoveChildJoint(const std::string& joint_prim_path);

    virtual const pxr::UsdPrim GetPrim() const override {
      return rigid_body.GetPrim();
    }
  };

  struct JointNode : public BaseNode {
    const pxr::UsdPhysicsJoint joint;
    LinkNode* absl_nullable parent_link{nullptr};
    LinkNode* absl_nullable child_link{nullptr};

    JointNode(const pxr::UsdPhysicsJoint& joint) : joint(joint) {}

    absl::Status SetChild(LinkNode* absl_nonnull new_child_link);

    virtual const pxr::UsdPrim GetPrim() const override {
      return joint.GetPrim();
    }
  };

  ConnectionTree() = default;

  bool HasLink(const pxr::UsdPhysicsRigidBodyAPI& rigid_body) const;
  bool HasJoint(const pxr::UsdPhysicsJoint& joint) const;

  absl::StatusOr<LinkNode* absl_nonnull> GetLinkNode(
      const pxr::UsdPhysicsRigidBodyAPI& rigid_body);
  absl::StatusOr<JointNode* absl_nonnull> GetJointNode(
      const pxr::UsdPhysicsJoint& joint);

  absl::StatusOr<LinkNode* absl_nonnull> AddLinkNode(
      const pxr::UsdPhysicsRigidBodyAPI& rigid_body);
  absl::StatusOr<JointNode* absl_nonnull> AddJointNode(
      const pxr::UsdPhysicsJoint& joint);

  const std::vector<absl_nonnull std::unique_ptr<LinkNode>>& GetLinks() const {
    return links_;
  }

  const std::vector<absl_nonnull std::unique_ptr<JointNode>>& GetJoints()
      const {
    return joints_;
  }

  // Returns the LinkNodes that have no parents
  std::vector<const LinkNode* absl_nonnull> GetToplevelLinks() const;

  // This removes any joints that don't have both links set. This may occur
  // as a result of ignoring joint edges that would create cycles, during
  // creation of the tree.
  void RemoveIncompleteJoints();

  // Get a string describing all nodes and edges.
  std::string ToDebugString() const;

 private:
  std::vector<absl_nonnull std::unique_ptr<LinkNode>> links_;
  std::vector<absl_nonnull std::unique_ptr<JointNode>> joints_;
  absl::flat_hash_map<std::string, LinkNode* absl_nonnull> path_to_link_;
  absl::flat_hash_map<std::string, JointNode* absl_nonnull> path_to_joint_;
};

// Creates the ConnectionTree object by performing a DFS traversal through the
// ConnectionGraph object. Errors if there are cycles in the graph, which we
// do not support.
// The caller may optionally pass the path to the root link (aka base link)
// primitive. If given, the tree conversion will start from this primitive,
// making it the root of the tree. If not specified, the first link that appears
// in the USD file will be made the root.
absl::StatusOr<ConnectionTree> ComputeConnectionTreeFromGraph(
    const ConnectionGraph& connection_graph,
    std::optional<std::string> root_link_path = std::nullopt);

}  // namespace usd
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_USD_CONNECTION_GRAPH_H_
