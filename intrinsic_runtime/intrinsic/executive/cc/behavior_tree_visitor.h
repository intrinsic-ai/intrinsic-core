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

#ifndef INTRINSIC_EXECUTIVE_CC_BEHAVIOR_TREE_VISITOR_H_
#define INTRINSIC_EXECUTIVE_CC_BEHAVIOR_TREE_VISITOR_H_

#include <functional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"

namespace intrinsic::executive {

using BehaviorTreeCallback = std::function<absl::Status(
    const intrinsic_proto::executive::BehaviorTree&)>;
using BehaviorTreeNodeCallback = std::function<absl::Status(
    const intrinsic_proto::executive::BehaviorTree&,
    const intrinsic_proto::executive::BehaviorTree::Node&)>;
using BehaviorTreeConditionCallback = std::function<absl::Status(
    const intrinsic_proto::executive::BehaviorTree&,
    const intrinsic_proto::executive::BehaviorTree::Condition&)>;

struct VisitBehaviorTreeCallbacks {
  BehaviorTreeCallback visit_tree;
  BehaviorTreeNodeCallback visit_node;
  BehaviorTreeConditionCallback visit_condition;
};

using MutableBehaviorTreeCallback =
    std::function<absl::Status(intrinsic_proto::executive::BehaviorTree&)>;
using MutableBehaviorTreeNodeCallback = std::function<absl::Status(
    intrinsic_proto::executive::BehaviorTree&,
    intrinsic_proto::executive::BehaviorTree::Node&)>;
using MutableBehaviorTreeConditionCallback = std::function<absl::Status(
    intrinsic_proto::executive::BehaviorTree&,
    intrinsic_proto::executive::BehaviorTree::Condition&)>;

struct VisitMutableBehaviorTreeCallbacks {
  MutableBehaviorTreeCallback visit_tree;
  MutableBehaviorTreeNodeCallback visit_node;
  MutableBehaviorTreeConditionCallback visit_condition;
};

template <typename ContextT>
using BehaviorTreeContextCallback = std::function<absl::StatusOr<ContextT>(
    const intrinsic_proto::executive::BehaviorTree&, const ContextT&)>;
template <typename ContextT>
using BehaviorTreeNodeContextCallback = std::function<absl::StatusOr<ContextT>(
    const intrinsic_proto::executive::BehaviorTree&,
    const intrinsic_proto::executive::BehaviorTree::Node&, const ContextT&)>;
template <typename ContextT>
using BehaviorTreeConditionContextCallback =
    std::function<absl::StatusOr<ContextT>(
        const intrinsic_proto::executive::BehaviorTree&,
        const intrinsic_proto::executive::BehaviorTree::Condition&,
        const ContextT&)>;

template <typename ContextT>
struct VisitBehaviorTreeContextCallbacks {
  using ContextType = ContextT;

  BehaviorTreeContextCallback<ContextT> visit_tree;
  BehaviorTreeNodeContextCallback<ContextT> visit_node;
  BehaviorTreeConditionContextCallback<ContextT> visit_condition;
};

template <typename ContextT>
using MutableBehaviorTreeContextCallback =
    std::function<absl::StatusOr<ContextT>(
        intrinsic_proto::executive::BehaviorTree&, const ContextT&)>;
template <typename ContextT>
using MutableBehaviorTreeNodeContextCallback =
    std::function<absl::StatusOr<ContextT>(
        intrinsic_proto::executive::BehaviorTree&,
        intrinsic_proto::executive::BehaviorTree::Node&, const ContextT&)>;
template <typename ContextT>
using MutableBehaviorTreeConditionContextCallback =
    std::function<absl::StatusOr<ContextT>(
        intrinsic_proto::executive::BehaviorTree&,
        intrinsic_proto::executive::BehaviorTree::Condition&, const ContextT&)>;

template <typename ContextT>
struct VisitMutableBehaviorTreeContextCallbacks {
  using ContextType = ContextT;

  MutableBehaviorTreeContextCallback<ContextT> visit_tree;
  MutableBehaviorTreeNodeContextCallback<ContextT> visit_node;
  MutableBehaviorTreeConditionContextCallback<ContextT> visit_condition;
};

struct VisitBehaviorTreeOptions {
  bool visit_called_tree_state = false;
};

// Visit all parts of the given BehaviorTree tree recursively.
//
// All objects in the BehaviorTree are visited and the callback is called on
// every one. Objects can be
//   * BehaviorTree objects, e.g., the tree itself, sub trees, or behavior
//     trees in conditions
//   * Node objects, e.g., Task or Sequence nodes
//   * Condition objects, e.g., AllOf, Not or SubTreeCondition
//
// The visit callbacks are called for every object. For example, when called on
// a tree the visit_tree callback is first called with the tree itself, when
// called on a SubtreeNode visit_node is first called on the SubTreeNode and
// then visit_tree on the sub-tree followed by visit_node on its root, when
// called on a SubTreeCondition visit_condition is first called on the condition
// and then visit_tree on the tree within that condition.
//
// Callbacks are performed in a natural order for the different objects. For
// example, for a sequence node, its children are visited as in the node's
// order; for a loop node first its while condition is visited, then its
// do_child; a retry node first visits its child and then the recovery.
//
// The BehaviorTreeNodeCallback and BehaviorTreeConditionCallback also get
// passed the containing BehaviorTree, e.g., to extract a matching tree id.
// If any of the callbacks fail, the visit call is stopped and returns that
// Status of the callback.
absl::Status VisitBehaviorTree(
    const intrinsic_proto::executive::BehaviorTree& tree,
    VisitBehaviorTreeCallbacks callbacks,
    const VisitBehaviorTreeOptions& options = {});

// Same functionality as VisitBehaviorTree, but accepts mutable callbacks that
// modify the tree.
// The tree is modified in place during traversal. Thus it is only safe to
// modify fields of the tree that are not part of the traversal (e.g., a node
// id). It is undefined behavior to modify the tree structure during the
// traversal of its contained nodes.
// If any of the callbacks fail, the visit call is stopped and returns that
// Status of the callback.
absl::Status VisitBehaviorTree(intrinsic_proto::executive::BehaviorTree& tree,
                               VisitMutableBehaviorTreeCallbacks callbacks,
                               const VisitBehaviorTreeOptions& options = {});

// Same as VisitBehaviorTree, but allows to pass in a user-defined Context.
// The context will be passed along the tree structure. The context produced
// from a callback on an entity (e.g., a node) will be passed along to its
// sub-entities (e.g., the children of a sequence).
// The input context is always guaranteed to come from its direct parent.
// The output context is always guaranteed to be passed to its direct children.
// If a callback type is not defined, its context will be default constructed.
//
// Note that this is not the same as having a collector object that is
// referenced by all callbacks as this context is guaranteed to be called
// according to the tree structure. For example:
// - a node callback with ContextT=int that just adds 1 to the context
// - call with initial_context 0 on:          A
//                                           / \
//                                          B   C
// - B and C will both be called with 1.
// - additional children to A will all receive 1 in the visit_node callback.
// - children of B or C will receive 2 in the visit_node callback.
template <typename ContextT>
absl::Status VisitBehaviorTree(
    const intrinsic_proto::executive::BehaviorTree& tree,
    VisitBehaviorTreeContextCallbacks<ContextT> callbacks,
    const ContextT& initial_context,
    const VisitBehaviorTreeOptions& options = {});

// Same as above for mutable trees.
template <typename ContextT>
absl::Status VisitBehaviorTree(
    intrinsic_proto::executive::BehaviorTree& tree,
    VisitMutableBehaviorTreeContextCallbacks<ContextT> callbacks,
    const ContextT& initial_context,
    const VisitBehaviorTreeOptions& options = {});

}  // namespace intrinsic::executive

#include "intrinsic/executive/cc/behavior_tree_visitor.inc"

#endif  // INTRINSIC_EXECUTIVE_CC_BEHAVIOR_TREE_VISITOR_H_
