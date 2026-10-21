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

#include "intrinsic/executive/clips/cc/recovery.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/text_format.h"
#include "intrinsic/executive/cc/behavior_tree_visitor.h"
#include "intrinsic/executive/clips/cc/id_handling.h"
#include "intrinsic/executive/clips/cc/tracing.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/proto/behavior_call.pb.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"
#include "intrinsic/util/status/get_extended_status.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_specs.h"

using intrinsic_proto::executive::BehaviorTree;

namespace intrinsic::executive {

namespace {

// Represents a "resolved" NodeIdentifier proto. It's main purpose is to
// uniquely represent a node in a tree proto including PBT calls. To this end
// the recursive structure of the NodeIdentifier proto is converted to a
// prefixed tree id, e.g., "MAIN-TREE:43/PBT", where the prefix signals, which
// node a PBT was called from. Thus the pair prefixed_tree_id,node_id is
// sufficient to identify the node.
struct RecoveryNodeIdentifier {
  explicit RecoveryNodeIdentifier(absl::string_view prefixed_tree_id,
                                  uint32_t node_id);
  explicit RecoveryNodeIdentifier(
      const BehaviorTree::NodeIdentifier& node_identifier);

  std::string prefixed_tree_id;
  uint32_t node_id;

  template <typename H>
  friend H AbslHashValue(H h, const RecoveryNodeIdentifier& rni) {
    return H::combine(std::move(h), rni.prefixed_tree_id, rni.node_id);
  }
};

template <typename Sink>
void AbslStringify(Sink& sink, const RecoveryNodeIdentifier& rni) {
  absl::Format(&sink, "(%s: %d)", rni.prefixed_tree_id, rni.node_id);
}

// Returns a descriptive text for a node suitable to display in error messages.
std::string GetNodeDescription(const BehaviorTree::Node& node,
                               const RecoveryNodeIdentifier& rni) {
  std::string node_description = absl::StrFormat("id: %v", rni);
  if (node.has_name()) {
    node_description =
        absl::StrCat(absl::StrFormat("'%s', ", node.name()), node_description);
  }
  return node_description;
}

RecoveryNodeIdentifier::RecoveryNodeIdentifier(
    absl::string_view prefixed_tree_id, uint32_t node_id)
    : prefixed_tree_id(prefixed_tree_id), node_id(node_id) {}

RecoveryNodeIdentifier::RecoveryNodeIdentifier(
    const BehaviorTree::NodeIdentifier& node_identifier) {
  // Compute prefixed id recursively until ending at the final nested node,
  // which is the node in question. The "parent" nestings only define the prefix
  const BehaviorTree::NodeIdentifier* current_node_identifier =
      &node_identifier;
  while (current_node_identifier->has_node_within_task_node()) {
    absl::StrAppend(&prefixed_tree_id, GenerateTreeIdPrefixForNode(
                                           current_node_identifier->tree_id(),
                                           current_node_identifier->node_id()));
    if (current_node_identifier->has_node_within_task_node()) {
      current_node_identifier =
          &current_node_identifier->node_within_task_node();
    }
  }
  absl::StrAppend(&prefixed_tree_id, current_node_identifier->tree_id());
  node_id = current_node_identifier->node_id();
}

bool operator==(const RecoveryNodeIdentifier& lhs,
                const RecoveryNodeIdentifier& rhs) {
  return lhs.prefixed_tree_id == rhs.prefixed_tree_id &&
         lhs.node_id == rhs.node_id;
}

// A RecoveryPath represents a path through a process tree starting at its root
// and going towards a recovery node. Each of the nodes that would lead to the
// execution of the recovery node is included in the path. Trees are omitted.
// The 'path' field is a structure built during construction and represents the
// path itself from the root node to 'node' and is thus not to be changed after
// creation. The 'downward_path' is a work structure that is being used up when
// applying the recovery. The recovery node itself is at the bottom of the
// stack, while the root of the process tree is at the top.
struct RecoveryPath {
  RecoveryNodeIdentifier node;
  std::vector<RecoveryNodeIdentifier> path;
  std::stack<RecoveryNodeIdentifier> downward_path;
};

template <typename Sink>
void AbslStringify(Sink& sink, const RecoveryPath& rp) {
  auto node_identifier_to_string = [](const RecoveryNodeIdentifier& rni) {
    return absl::StrFormat("%s:%d", rni.prefixed_tree_id, rni.node_id);
  };
  auto node_identifier_formatter = [node_identifier_to_string](
                                       std::string* out,
                                       const RecoveryNodeIdentifier& rni) {
    absl::StrAppend(out, node_identifier_to_string(rni));
  };
  absl::Format(&sink, "Recovery path for '%s': [%s]",
               node_identifier_to_string(rp.node),
               absl::StrJoin(rp.path, " -> ", node_identifier_formatter));
}

std::string RecoveryNodesToString(
    absl::Span<const BehaviorTree::NodeIdentifier> node_identifiers) {
  google::protobuf::TextFormat::Printer printer;
  return absl::StrJoin(
      node_identifiers, ", ",
      [&printer](std::string* out, const BehaviorTree::NodeIdentifier& ni) {
        std::string proto_txt;
        static_cast<void>(printer.PrintToString(
            ni, &proto_txt));  // TODO: Handle return value
        absl::StrReplaceAll({{"\n", " "}}, &proto_txt);
        absl::StrAppend(out, proto_txt);
      });
}

// Build a set of RecoveryNodeIdentifier from the node identifier protos. These
// must not contain any duplicates.
absl::StatusOr<absl::flat_hash_set<RecoveryNodeIdentifier>>
RecoveryNodeIdentifiersAsSet(
    absl::Span<const BehaviorTree::NodeIdentifier> node_identifiers) {
  std::vector<BehaviorTree::NodeIdentifier> duplicate_identifiers;
  absl::flat_hash_set<RecoveryNodeIdentifier> recovery_node_identifiers;
  for (const BehaviorTree::NodeIdentifier& node_identifier : node_identifiers) {
    const auto& [it, inserted] = recovery_node_identifiers.insert(
        RecoveryNodeIdentifier{node_identifier});
    if (!inserted) {
      duplicate_identifiers.push_back(node_identifier);
    }
  }
  if (!duplicate_identifiers.empty()) {
    return CreateStatus(
        70101,
        absl::StrFormat(
            "The following node identifiers were found multiple times: %s",
            RecoveryNodesToString(duplicate_identifiers)),
        absl::StatusCode::kInvalidArgument);
  }
  return recovery_node_identifiers;
}

absl::Status FindRecoveryPathsInTree(
    const BehaviorTree& tree, std::vector<RecoveryNodeIdentifier> parent_path,
    const absl::flat_hash_set<RecoveryNodeIdentifier>& recovery_nodes,
    std::vector<RecoveryPath>& recovery_paths);

absl::Status FindRecoveryPathsInNode(
    const BehaviorTree::Node& node, absl::string_view prefixed_tree_id,
    std::vector<RecoveryNodeIdentifier> parent_path,
    const absl::flat_hash_set<RecoveryNodeIdentifier>& recovery_nodes,
    std::vector<RecoveryPath>& recovery_paths) {
  RecoveryNodeIdentifier this_node(prefixed_tree_id, node.id());
  if (prefixed_tree_id.empty() || node.id() == 0) {
    return CreateStatus(
        70100,
        absl::StrFormat(
            "Found node with invalid id in tree. Tree id: '%s', Node id: %d",
            prefixed_tree_id, node.id()),
        absl::StatusCode::kInternal);
  }
  parent_path.push_back(this_node);
  if (recovery_nodes.contains(this_node)) {
    recovery_paths.push_back(
        RecoveryPath{.node = this_node, .path = std::move(parent_path)});
    // Do not recurse further if this is a recovery node. It is not possible to
    // recovery to this node and through this node at the same time.
    // If this is specified by the input, FindRecoveryPaths will output an error
    // as the recovery_nodes through this node are not found.
    return absl::OkStatus();
  }
  // At this point, this_node is not a recovery node. Keep searching for
  // possible recovery paths in children of this node. There is no need to
  // verify if all recovery_nodes have been found here as FindRecoveryPaths does
  // this for all found paths.
  switch (node.node_type_case()) {
    case BehaviorTree::Node::NodeTypeCase::kSequence:
      for (const BehaviorTree::Node& child : node.sequence().children()) {
        INTR_RETURN_IF_ERROR(
            FindRecoveryPathsInNode(child, prefixed_tree_id, parent_path,
                                    recovery_nodes, recovery_paths));
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kParallel:
      for (const BehaviorTree::Node& child : node.parallel().children()) {
        INTR_RETURN_IF_ERROR(
            FindRecoveryPathsInNode(child, prefixed_tree_id, parent_path,
                                    recovery_nodes, recovery_paths));
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kTask:
      if (node.task().has_called_tree_state()) {
        // Task node is a PBT call
        INTR_RETURN_IF_ERROR(FindRecoveryPathsInTree(
            node.task().called_tree_state(), parent_path, recovery_nodes,
            recovery_paths));
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kFail:
      break;
    case BehaviorTree::Node::NodeTypeCase::kSelector:
      for (const BehaviorTree::Node& child : node.selector().children()) {
        INTR_RETURN_IF_ERROR(
            FindRecoveryPathsInNode(child, prefixed_tree_id, parent_path,
                                    recovery_nodes, recovery_paths));
      }
      for (const BehaviorTree::SelectorNode::Branch& branch :
           node.selector().branches()) {
        if (branch.has_node()) {
          INTR_RETURN_IF_ERROR(FindRecoveryPathsInNode(
              branch.node(), prefixed_tree_id, parent_path, recovery_nodes,
              recovery_paths));
        }
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kFallback:
      for (const BehaviorTree::Node& child : node.fallback().children()) {
        INTR_RETURN_IF_ERROR(
            FindRecoveryPathsInNode(child, prefixed_tree_id, parent_path,
                                    recovery_nodes, recovery_paths));
      }
      for (const BehaviorTree::FallbackNode::Try& try_ :
           node.fallback().tries()) {
        if (try_.has_node()) {
          INTR_RETURN_IF_ERROR(FindRecoveryPathsInNode(
              try_.node(), prefixed_tree_id, parent_path, recovery_nodes,
              recovery_paths));
        }
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kBranch:
      if (node.branch().has_then()) {
        INTR_RETURN_IF_ERROR(FindRecoveryPathsInNode(
            node.branch().then(), prefixed_tree_id, parent_path, recovery_nodes,
            recovery_paths));
      }
      if (node.branch().has_else_()) {
        INTR_RETURN_IF_ERROR(FindRecoveryPathsInNode(
            node.branch().else_(), prefixed_tree_id, parent_path,
            recovery_nodes, recovery_paths));
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kLoop:
      if (node.loop().has_do_()) {
        INTR_RETURN_IF_ERROR(FindRecoveryPathsInNode(
            node.loop().do_(), prefixed_tree_id, parent_path, recovery_nodes,
            recovery_paths));
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kRetry:
      if (node.retry().has_child()) {
        INTR_RETURN_IF_ERROR(FindRecoveryPathsInNode(
            node.retry().child(), prefixed_tree_id, parent_path, recovery_nodes,
            recovery_paths));
      }
      if (node.retry().has_recovery()) {
        INTR_RETURN_IF_ERROR(FindRecoveryPathsInNode(
            node.retry().recovery(), prefixed_tree_id, parent_path,
            recovery_nodes, recovery_paths));
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kSubTree:
      if (node.sub_tree().has_tree()) {
        INTR_RETURN_IF_ERROR(
            FindRecoveryPathsInTree(node.sub_tree().tree(), parent_path,
                                    recovery_nodes, recovery_paths));
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kData:
      break;
    case BehaviorTree::Node::NodeTypeCase::kDebug:
      break;
    case BehaviorTree::Node::NodeTypeCase::NODE_TYPE_NOT_SET:
      break;
  }

  return absl::OkStatus();
}

absl::Status FindRecoveryPathsInTree(
    const BehaviorTree& tree, std::vector<RecoveryNodeIdentifier> parent_path,
    const absl::flat_hash_set<RecoveryNodeIdentifier>& recovery_nodes,
    std::vector<RecoveryPath>& recovery_paths) {
  if (tree.has_root()) {
    return FindRecoveryPathsInNode(tree.root(), tree.tree_id(), parent_path,
                                   recovery_nodes, recovery_paths);
  }
  return absl::OkStatus();
}

absl::Status CreateAmbiguousRecoveryError(
    const RecoveryNodeIdentifier node_identifier,
    const BehaviorTree::Node& node,
    const absl::flat_hash_map<RecoveryNodeIdentifier,
                              std::vector<RecoveryPath>>& path_to_next_node) {
  std::string node_description = GetNodeDescription(node, node_identifier);
  absl::string_view node_type = BehaviorTree::Node::descriptor()
                                    ->FindFieldByNumber(node.node_type_case())
                                    ->name();
  intrinsic_proto::status::ExtendedStatus::Relations related_to;
  related_to.mutable_behavior_tree_node()->set_tree_id(
      node_identifier.prefixed_tree_id);
  related_to.mutable_behavior_tree_node()->set_node_id(node_identifier.node_id);

  std::string ambiguous_children;
  std::string path_to_next_node_description = "Paths to child nodes: ";
  for (const auto& [child_node, paths] : path_to_next_node) {
    absl::StrAppend(&path_to_next_node_description, "Child: ", child_node,
                    " Path(s): ", absl::StrJoin(paths, ", "), "; ");
    absl::StrAppend(&ambiguous_children, child_node, ", ");
  }

  return CreateStatus(
      70103,
      absl::StrFormat(
          "Recovery through node %s of type %s goes through multiple "
          "children. The children with the following ids are supposed to be "
          "running at the same time: %s",
          node_description, node_type, ambiguous_children),
      absl::StatusCode::kInvalidArgument,
      {.debug_message = path_to_next_node_description,
       .related_to = related_to});
}

absl::Status CreateRecoveryPathThroughWrongNodeError(
    const RecoveryNodeIdentifier& this_node,
    const RecoveryNodeIdentifier& top_node, const RecoveryPath& rp) {
  return CreateStatus(
      70100,
      absl::StrFormat("Invalid recovery path found while visiting node %v, "
                      "where path points to node %v.",
                      this_node, top_node),
      absl::StatusCode::kInternal,
      {.debug_message = absl::StrFormat("Full recovery path: %v", rp)});
}

absl::Status CreateUnexpectedRecoverPathEndError(
    const RecoveryNodeIdentifier& node, const RecoveryPath& path) {
  // The given path's downward_path was empty, although this wasn't a node to
  // recover to. This is an internal error case as it means the recovery ends
  // here. Thus the only ways to end up in this situation is:
  // - Multiple paths are empty -> There were duplicate recovery nodes and
  //   FindRecoveryPaths should have rejected that.
  // - There are recovery paths to this node (empty) and through this node
  //   (non-empty) -> FindRecoveryPaths should have stopped at this node and
  //   not found the one through this node.
  return CreateStatus(
      70100,
      absl::StrFormat(
          "Invalid recovery paths found while visiting node %v, "
          "where the path %v ends at the visited node, but other paths "
          "exist to or through this node.",
          node, path),
      absl::StatusCode::kInternal);
}

absl::Status CreateUnappliedRecoveryPathsError(
    const RecoveryNodeIdentifier node_identifier,
    const BehaviorTree::Node& node,
    const absl::flat_hash_map<RecoveryNodeIdentifier,
                              std::vector<RecoveryPath>>&
        unapplied_paths_to_next_node,
    const std::vector<RecoveryPath>& recovery_paths) {
  // If recovery paths have not been applied then a node would be marked RUNNING
  // without any of its children in the unapplied_paths_to_next_node being
  // selected.
  // This can only happen when there is a partition pointing to a node that
  // isn't a child of this node.
  // This is also true for the leaf nodes (e.g., Fail or Task without
  // called_tree_state). Recovery must never go through the leaf nodes and if it
  // were to select a leaf node this would have been handled at the top of the
  // function.
  std::string node_description = GetNodeDescription(node, node_identifier);
  std::string unapplied_paths_description;
  for (const auto& [child_node, paths] : unapplied_paths_to_next_node) {
    absl::StrAppend(&unapplied_paths_description, "Child: ", child_node,
                    " Path(s): ", absl::StrJoin(paths, ", "), "; ");
  }
  return CreateStatus(
      70100,
      absl::StrFormat(
          "Not all recovery paths could be applied at node %s. Did not apply "
          "recovery to %d requested paths through child nodes. The following "
          "child paths were not applied: %s",
          node_description, unapplied_paths_to_next_node.size(),
          unapplied_paths_description),
      absl::StatusCode::kInternal,
      {.debug_message = absl::StrFormat("All recovery paths: %s",
                                        absl::StrJoin(recovery_paths, ", "))});
}

absl::Status ApplyRecoveryPathsInTree(
    BehaviorTree& tree, std::vector<RecoveryPath>& recovery_paths);

// Applies the given recovery_paths recursively to the given Node proto.
//
// The function applies *all* recovery_paths either to the node itself or its
// children. Thus the recovery_paths must point to node or its children. The
// function works on the downward_path of each RecoveryPath. When called the
// top of the downward_path must point to node.
//
// On success the node proto represents a proto with state after recovery. On
// failure the node proto might be in an arbitrary inconsistent state.
//
// It is an error if a path cannot be applied when recursion ends.
// It is also an error if the recovery paths do not provide a consistent
// recovery (e.g., recovery into two or more children of a sequence).
absl::Status ApplyRecoveryPathsInNode(BehaviorTree::Node& node,
                                      absl::string_view prefixed_tree_id,
                                      std::vector<RecoveryPath>& recovery_paths,
                                      bool has_decorator_condition = false) {
  RecoveryNodeIdentifier this_node(prefixed_tree_id, node.id());
  VLOG(1) << "Applying recovery paths to node: " << this_node
          << " has_decorator: " << has_decorator_condition;
  for (RecoveryPath& rp : recovery_paths) {
    const RecoveryNodeIdentifier& top_node = rp.downward_path.top();
    if (top_node != this_node) {
      return CreateRecoveryPathThroughWrongNodeError(this_node, top_node, rp);
    }
    rp.downward_path.pop();
  }

  // This is the only path and it is at its end. This means we've reached the
  // node to recover.
  if (recovery_paths.size() == 1 && recovery_paths[0].downward_path.empty()) {
    if (has_decorator_condition) {
      node.set_state(BehaviorTree::Node::READY);
    } else {
      node.set_state(BehaviorTree::Node::SELECTED);
    }
    return absl::OkStatus();
  }

  // Partition the recovery_paths by the next child they are going to.
  // This allows to select the correct child node to apply the recovery to.
  // Also record the partitions itself mainly to get the number of partitions.
  absl::flat_hash_map<RecoveryNodeIdentifier, std::vector<RecoveryPath>>
      path_to_next_node;
  for (const RecoveryPath& rp : recovery_paths) {
    if (rp.downward_path.empty()) {
      return CreateUnexpectedRecoverPathEndError(this_node, rp);
    }
    const RecoveryNodeIdentifier& next_node = rp.downward_path.top();
    path_to_next_node[next_node].push_back(rp);
  }

  // For most node types it is expected that there is exactly one partition
  // representing the path where the recovery is applied to. Only parallel nodes
  // can have more than one partition. Note that there can be multiple recovery
  // paths in one partition when there is a parallel within the child of this
  // node.
  if (path_to_next_node.size() > 1 &&
      node.node_type_case() != BehaviorTree::Node::NodeTypeCase::kParallel) {
    return CreateAmbiguousRecoveryError(this_node, node, path_to_next_node);
  }

  // Recovery goes through this node, so it must be running.
  node.set_state(BehaviorTree::Node::RUNNING);
  switch (node.node_type_case()) {
    case BehaviorTree::Node::NodeTypeCase::kSequence:
      for (BehaviorTree::Node& child :
           *node.mutable_sequence()->mutable_children()) {
        // Set each child succeeded to forward the execution until we find the
        // recovery child. This one will be selected by the recursive call.
        child.set_state(BehaviorTree::Node::SUCCEEDED);
        RecoveryNodeIdentifier child_identifier(prefixed_tree_id, child.id());
        auto child_recovery_paths = path_to_next_node.find(child_identifier);
        if (child_recovery_paths != path_to_next_node.end()) {
          INTR_RETURN_IF_ERROR(ApplyRecoveryPathsInNode(
              child, prefixed_tree_id, child_recovery_paths->second));
          path_to_next_node.erase(child_identifier);
          break;
        }
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kParallel:
      for (BehaviorTree::Node& child :
           *node.mutable_parallel()->mutable_children()) {
        RecoveryNodeIdentifier child_identifier(prefixed_tree_id, child.id());
        auto path_to_this_child = path_to_next_node.find(child_identifier);
        if (path_to_this_child != path_to_next_node.end()) {
          INTR_RETURN_IF_ERROR(ApplyRecoveryPathsInNode(
              child, prefixed_tree_id, path_to_this_child->second));
          path_to_next_node.erase(child_identifier);
        } else {
          child.set_state(BehaviorTree::Node::SELECTED);
        }
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kTask:
      if (node.task().has_called_tree_state() &&
          node.task().called_tree_state().has_root()) {
        const BehaviorTree::Node& child =
            node.task().called_tree_state().root();
        RecoveryNodeIdentifier child_identifier(
            node.task().called_tree_state().tree_id(), child.id());
        auto child_recovery_paths = path_to_next_node.find(child_identifier);
        if (child_recovery_paths != path_to_next_node.end()) {
          node.mutable_task()->set_state(BehaviorTree::TaskNode::RUNNING);
          INTR_RETURN_IF_ERROR(ApplyRecoveryPathsInTree(
              *node.mutable_task()->mutable_called_tree_state(),
              child_recovery_paths->second));
          path_to_next_node.erase(child_identifier);
        }
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kFail:
      break;
    case BehaviorTree::Node::NodeTypeCase::kSelector:
      // Set each child condition to not satisfied until we find the recovery
      // child. The recovery child's condition should then be satisfied.
      for (BehaviorTree::Node& child :
           *node.mutable_selector()->mutable_children()) {
        child.mutable_decorators()->mutable_condition()->set_state(
            BehaviorTree::Condition::FINISHED);
        child.mutable_decorators()->mutable_condition()->set_satisfied(false);
        child.set_state(BehaviorTree::Node::FAILED);
        child.set_failure_reason(BehaviorTree::Node::FAILED_CONDITION);
        RecoveryNodeIdentifier child_identifier(prefixed_tree_id, child.id());
        auto child_recovery_paths = path_to_next_node.find(child_identifier);
        if (child_recovery_paths != path_to_next_node.end()) {
          child.mutable_decorators()->mutable_condition()->set_satisfied(true);
          child.clear_failure_reason();
          INTR_RETURN_IF_ERROR(ApplyRecoveryPathsInNode(
              child, prefixed_tree_id, child_recovery_paths->second,
              /*has_decorator_condition=*/true));
          path_to_next_node.erase(child_identifier);
          break;
        }
      }
      for (BehaviorTree::SelectorNode::Branch& branch :
           *node.mutable_selector()->mutable_branches()) {
        if (branch.has_node()) {
          // Make the condition unsatisfied, so this node is skipped.
          // Do *not* change the child state for a selector child.
          branch.mutable_condition()->set_state(
              BehaviorTree::Condition::FINISHED);
          branch.mutable_condition()->set_satisfied(false);
          BehaviorTree::Node& child = *branch.mutable_node();
          // TODO(b/437345962) When conditions are loaded natively this will be
          // unnecessary. As long as underneath the child condition is still a
          // decorator also set this here, so the child gets recovered
          // correctly.
          child.mutable_decorators()->mutable_condition()->set_state(
              BehaviorTree::Condition::FINISHED);
          child.mutable_decorators()->mutable_condition()->set_satisfied(false);
          child.set_state(BehaviorTree::Node::FAILED);
          child.set_failure_reason(BehaviorTree::Node::FAILED_CONDITION);
          RecoveryNodeIdentifier child_identifier(prefixed_tree_id, child.id());
          auto child_recovery_paths = path_to_next_node.find(child_identifier);
          if (child_recovery_paths != path_to_next_node.end()) {
            branch.mutable_condition()->set_satisfied(true);
            child.mutable_decorators()->mutable_condition()->set_satisfied(
                true);
            child.clear_failure_reason();
            // While the child does *not* have a decorator condition in the
            // proto, the implementation still has one and thus needs to be set
            // to the respective state.
            INTR_RETURN_IF_ERROR(ApplyRecoveryPathsInNode(
                child, prefixed_tree_id, child_recovery_paths->second,
                /*has_decorator_condition=*/true));
            path_to_next_node.erase(child_identifier);
            break;
          }
        }
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kFallback:
      // Set each child to failed as the represents the execution continuing to
      // the next child for a fallback.
      for (BehaviorTree::Node& child :
           *node.mutable_fallback()->mutable_children()) {
        child.set_state(BehaviorTree::Node::FAILED);
        RecoveryNodeIdentifier child_identifier(prefixed_tree_id, child.id());
        auto child_recovery_paths = path_to_next_node.find(child_identifier);
        if (child_recovery_paths != path_to_next_node.end()) {
          if (child.decorators().has_condition()) {
            child.mutable_decorators()->mutable_condition()->set_state(
                BehaviorTree::Condition::FINISHED);
            child.mutable_decorators()->mutable_condition()->set_satisfied(
                true);
          }
          INTR_RETURN_IF_ERROR(ApplyRecoveryPathsInNode(
              child, prefixed_tree_id, child_recovery_paths->second,
              /*has_decorator_condition=*/child.decorators().has_condition()));
          path_to_next_node.erase(child_identifier);
          break;
        }
      }
      for (BehaviorTree::FallbackNode::Try& try_ :
           *node.mutable_fallback()->mutable_tries()) {
        if (try_.has_node()) {
          BehaviorTree::Node& child = *try_.mutable_node();
          child.set_state(BehaviorTree::Node::FAILED);
          RecoveryNodeIdentifier child_identifier(prefixed_tree_id, child.id());
          auto child_recovery_paths = path_to_next_node.find(child_identifier);
          if (child_recovery_paths != path_to_next_node.end()) {
            if (try_.has_condition()) {
              try_.mutable_condition()->set_state(
                  BehaviorTree::Condition::FINISHED);
              try_.mutable_condition()->set_satisfied(true);
              // TODO(b/437345962) When conditions are loaded natively this will
              // be unnecessary. As long as underneath the child condition is
              // still a decorator also set this here, so the child gets
              // recovered correctly.
              child.mutable_decorators()->mutable_condition()->set_state(
                  BehaviorTree::Condition::FINISHED);
              child.mutable_decorators()->mutable_condition()->set_satisfied(
                  true);
            }
            INTR_RETURN_IF_ERROR(ApplyRecoveryPathsInNode(
                child, prefixed_tree_id, child_recovery_paths->second,
                /*has_decorator_condition=*/try_.has_condition()));
            path_to_next_node.erase(child_identifier);
            break;
          }
        }
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kBranch:
      if (node.branch().has_then()) {
        BehaviorTree::Node& child = *node.mutable_branch()->mutable_then();
        RecoveryNodeIdentifier child_identifier(prefixed_tree_id, child.id());
        auto child_recovery_paths = path_to_next_node.find(child_identifier);
        if (child_recovery_paths != path_to_next_node.end()) {
          node.mutable_branch()->mutable_if_()->set_state(
              BehaviorTree::Condition::FINISHED);
          node.mutable_branch()->mutable_if_()->set_satisfied(true);
          INTR_RETURN_IF_ERROR(ApplyRecoveryPathsInNode(
              child, prefixed_tree_id, child_recovery_paths->second));
          path_to_next_node.erase(child_identifier);
        }
      }
      if (node.branch().has_else_()) {
        BehaviorTree::Node& child = *node.mutable_branch()->mutable_else_();
        RecoveryNodeIdentifier child_identifier(prefixed_tree_id, child.id());
        auto child_recovery_paths = path_to_next_node.find(child_identifier);
        if (child_recovery_paths != path_to_next_node.end()) {
          node.mutable_branch()->mutable_if_()->set_state(
              BehaviorTree::Condition::FINISHED);
          node.mutable_branch()->mutable_if_()->set_satisfied(false);
          INTR_RETURN_IF_ERROR(ApplyRecoveryPathsInNode(
              child, prefixed_tree_id, child_recovery_paths->second));
          path_to_next_node.erase(child_identifier);
        }
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kLoop:
      if (node.loop().has_do_()) {
        if (!node.loop().has_num_times()) {
          // Node is RUNNING. Without any set value it must be in the first
          // iteration
          node.mutable_loop()->set_num_times(1);
        }
        //
        // Do not set the loop condition. Loop conditions are always ACCEPTED
        // when the child is running. Even if there is a condition these are
        // reset immediately after evaluation when the child is selected.
        BehaviorTree::Node& child = *node.mutable_loop()->mutable_do_();
        RecoveryNodeIdentifier child_identifier(prefixed_tree_id, child.id());
        auto child_recovery_paths = path_to_next_node.find(child_identifier);
        if (child_recovery_paths != path_to_next_node.end()) {
          INTR_RETURN_IF_ERROR(ApplyRecoveryPathsInNode(
              child, prefixed_tree_id, child_recovery_paths->second));
          path_to_next_node.erase(child_identifier);
        }
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kRetry:
      if (node.retry().has_child()) {
        BehaviorTree::Node& child = *node.mutable_retry()->mutable_child();
        if (!node.retry().has_num_tries()) {
          // Node is RUNNING. Without any set value it must be in the first
          // iteration
          node.mutable_retry()->set_num_tries(1);
        }
        RecoveryNodeIdentifier child_identifier(prefixed_tree_id, child.id());
        auto child_recovery_paths = path_to_next_node.find(child_identifier);
        if (child_recovery_paths != path_to_next_node.end()) {
          INTR_RETURN_IF_ERROR(ApplyRecoveryPathsInNode(
              child, prefixed_tree_id, child_recovery_paths->second));
          path_to_next_node.erase(child_identifier);
        }
      }
      if (node.retry().has_recovery()) {
        BehaviorTree::Node& child = *node.mutable_retry()->mutable_recovery();
        if (!node.retry().has_num_tries()) {
          // Node is RUNNING. Without any set value it must be in the first
          // iteration
          node.mutable_retry()->set_num_tries(1);
        }
        RecoveryNodeIdentifier child_identifier(prefixed_tree_id, child.id());
        auto child_recovery_paths = path_to_next_node.find(child_identifier);
        if (child_recovery_paths != path_to_next_node.end()) {
          node.mutable_retry()->mutable_child()->set_state(
              BehaviorTree::Node::FAILED);
          INTR_RETURN_IF_ERROR(ApplyRecoveryPathsInNode(
              child, prefixed_tree_id, child_recovery_paths->second));
          path_to_next_node.erase(child_identifier);
        }
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kSubTree:
      if (node.sub_tree().has_tree() && node.sub_tree().tree().has_root()) {
        const BehaviorTree::Node& child = node.sub_tree().tree().root();
        RecoveryNodeIdentifier child_identifier(
            node.sub_tree().tree().tree_id(), child.id());
        auto child_recovery_paths = path_to_next_node.find(child_identifier);
        if (child_recovery_paths != path_to_next_node.end()) {
          INTR_RETURN_IF_ERROR(
              ApplyRecoveryPathsInTree(*node.mutable_sub_tree()->mutable_tree(),
                                       child_recovery_paths->second));
          path_to_next_node.erase(child_identifier);
        }
      }
      break;
    case BehaviorTree::Node::NodeTypeCase::kData:
      break;
    case BehaviorTree::Node::NodeTypeCase::kDebug:
      break;
    case BehaviorTree::Node::NodeTypeCase::NODE_TYPE_NOT_SET:
      break;
  }

  // All recovery paths must have been applied. This is equivalent to applying
  // all partitions. If not, then this node would be running without any child
  // being selected.
  if (!path_to_next_node.empty()) {
    return CreateUnappliedRecoveryPathsError(this_node, node, path_to_next_node,
                                             recovery_paths);
  }

  return absl::OkStatus();
}

absl::Status CreateApplyRecoveryWithoutRootError(
    const BehaviorTree& tree, const std::vector<RecoveryPath>& recovery_paths) {
  std::string tree_description = absl::StrFormat("id: %v", tree.tree_id());
  if (!tree.name().empty()) {
    tree_description =
        absl::StrCat(absl::StrFormat("'%s', ", tree.name()), tree_description);
  }
  // Internal as FindRecoveryPaths should not have found the path.
  return CreateStatus(
      70100,
      absl::StrFormat("Could not apply recovery at tree %s "
                      "as it does not have a root node.",
                      tree_description),
      absl::StatusCode::kInternal,
      {.debug_message = absl::StrFormat("All recovery paths: %s",
                                        absl::StrJoin(recovery_paths, ", "))});
}
absl::Status CreateApplyRecoveryNotThroughRootError(
    const BehaviorTree& tree, const std::vector<RecoveryPath>& recovery_paths) {
  std::string tree_description = absl::StrFormat("id: %v", tree.tree_id());
  if (!tree.name().empty()) {
    tree_description =
        absl::StrCat(absl::StrFormat("'%s', ", tree.name()), tree_description);
  }
  // Internal as FindRecoveryPaths should only have generated paths
  // consistent with tree
  return CreateStatus(
      70100,
      absl::StrFormat("Found recovery paths at tree %s "
                      "that do not go through its root.",
                      tree_description),
      absl::StatusCode::kInternal,
      {.debug_message = absl::StrFormat("All recovery paths: %s",
                                        absl::StrJoin(recovery_paths, ", "))});
}

absl::Status ApplyRecoveryPathsInTree(
    BehaviorTree& tree, std::vector<RecoveryPath>& recovery_paths) {
  if (!tree.has_root()) {
    return CreateApplyRecoveryWithoutRootError(tree, recovery_paths);
  }

  RecoveryNodeIdentifier child_identifier(tree.tree_id(), tree.root().id());
  for (const RecoveryPath& rp : recovery_paths) {
    if (rp.downward_path.empty() ||
        rp.downward_path.top() != child_identifier) {
      return CreateApplyRecoveryNotThroughRootError(tree, recovery_paths);
    }
  }

  // Recovery paths always go to a node. Thus if this function is called it is
  // recovering to a node in this tree and thus this tree must be RUNNING.
  tree.set_state(BehaviorTree::RUNNING);
  return ApplyRecoveryPathsInNode(*tree.mutable_root(), tree.tree_id(),
                                  recovery_paths);
}

// Creates an error when the parameterization results in a failed expression.
// clips::Values here must be a CLIPS style result, i.e., its first value is
// FALSE as it has failed and the second value points to an ExtendedStatus
// proto.
// This function handles the result and makes sure the proto is removed.
absl::Status CreateParameterizationFailedError(
    const clips::Values& result, const RecoveryNodeIdentifier& node_identifier,
    const BehaviorTree::Node& node,
    clips::ProtobufManager* absl_nonnull proto_mgr) {
  INTR_ASSIGN_OR_RETURN(int64_t error_es_id, result[1].GetInteger());
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<intrinsic_proto::status::ExtendedStatus> error_es,
      proto_mgr->GetProtoAs<intrinsic_proto::status::ExtendedStatus>(
          clips::ProtoMessageId(error_es_id)));
  std::string node_description = GetNodeDescription(node, node_identifier);
  intrinsic_proto::status::ExtendedStatus::Relations related_to;
  related_to.mutable_behavior_tree_node()->set_tree_id(
      node_identifier.prefixed_tree_id);
  related_to.mutable_behavior_tree_node()->set_node_id(node_identifier.node_id);
  absl::Status ret = CreateStatus(
      70150,
      absl::StrFormat("Failed to parameterize reusable process at node %s",
                      node_description),
      absl::StatusCode::kAborted,
      {.related_to = related_to, .context = {*error_es}});
  proto_mgr->RemoveProto(clips::ProtoMessageId(error_es_id));
  return ret;
}

// Creates an error based on a failed resource parameterization call. The Status
// must be the result of a call to EvaluateResult.
absl::Status CreateResourceParameterizationError(
    const absl::Status& eval_status,
    const RecoveryNodeIdentifier& node_identifier,
    const BehaviorTree::Node& node) {
  if (eval_status.code() != absl::StatusCode::kAborted) {
    // InternalError, etc. is propagated.
    return eval_status;
  }
  std::string node_description = GetNodeDescription(node, node_identifier);
  intrinsic_proto::status::ExtendedStatus::Relations related_to;
  related_to.mutable_behavior_tree_node()->set_tree_id(
      node_identifier.prefixed_tree_id);
  related_to.mutable_behavior_tree_node()->set_node_id(node_identifier.node_id);
  return CreateStatus(70150,
                      absl::StrFormat("Failed to parameterize resources for "
                                      "reusable process at node %s: %s",
                                      node_description, eval_status.message()),
                      absl::StatusCode::kAborted, {.related_to = related_to});
}

// Evaluates a CLIPS expression as part of the PBT parameterization recovery.
// The CLIPS expression is expected to return a CLIPS result, i.e., a
// multi-field that starts with TRUE/FALSE and on FALSE and ExtendedStatus. If
// FALSE this function fails and the ExtendedStatus is attached to the returned
// Status.
absl::Status EvaluateParameterizationExtendedStatusResult(
    clips::Environment* absl_nonnull env,
    clips::ProtobufManager* absl_nonnull proto_mgr,
    absl::string_view clips_expression, const BehaviorTree::Node& related_node,
    const RecoveryNodeIdentifier& related_node_identifier)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  INTR_ASSIGN_OR_RETURN(clips::Values result,
                        env->Evaluate(std::string(clips_expression)));
  if (result.size() != 2) {
    std::string result_string = absl::StrJoin(
        result, ", ", [](std::string* out, const clips::Value& value) {
          absl::StrAppend(out, value.ToString());
        });
    return absl::InternalError(absl::StrFormat(
        "Unexpected result from expression '%s' with %d entries, %s",
        clips_expression, result.size(), result_string));
  }
  if (result[0] != clips::Symbol::True()) {
    return CreateParameterizationFailedError(result, related_node_identifier,
                                             related_node, proto_mgr);
  }

  return absl::OkStatus();
}

absl::Status RecoverPBTBehaviorCall(
    clips::Environment* absl_nonnull env,
    clips::ProtobufManager* absl_nonnull proto_mgr,
    const BehaviorTree& containing_tree, const BehaviorTree::Node& node,
    clips::Symbol behavior_call_uid, absl::string_view operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  VLOG(1) << "Recovering PBT call at " << containing_tree.tree_id() << ":"
          << node.id();
  INTR_ASSIGN_OR_RETURN(clips::Fact behavior_call_instance_fact,
                        env->GetUniqueFact("behavior-call-instance",
                                           {{"uid", behavior_call_uid}}));
  INTR_ASSIGN_OR_RETURN(clips::Value behavior_call_prototype_val,
                        behavior_call_instance_fact.GetSlotValue(
                            "behavior-call-prototype-proto"));
  INTR_ASSIGN_OR_RETURN(int64_t behavior_call_prototype_int,
                        behavior_call_prototype_val.GetInteger());
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<intrinsic_proto::executive::BehaviorCall>
          behavior_call_prototype_duplicate,
      proto_mgr->GetProtoAs<intrinsic_proto::executive::BehaviorCall>(
          clips::ProtoMessageId(behavior_call_prototype_int)));

  // Predefine id/scope under explicit names here for the involved trees.
  // In the context of parameterization there are always 2 trees involved: The
  // tree that specifies the behavior call to the PBT and the PBT that is being
  // called itself. It is crucial to refer to the correct tree (calling or
  // called PBT) depending on what part of the parameterization is addressed.
  const absl::string_view kCallingPbtTreeId = containing_tree.tree_id();
  const absl::string_view kCallingPbtScope = containing_tree.blackboard_scope();
  const absl::string_view kPbtScope =
      node.task().called_tree_state().blackboard_scope();

  clips::ProtoMessageId behavior_call_instance_id =
      clips::ProtobufManager::kInvalidId;
  // PBT Parameterization recovery
  // 1. Check if 'params' key is on blackboard as it was initialized
  absl::StatusOr<clips::Fact> params_blackboard_item = env->GetUniqueFact(
      "blackboard-item", {{"key", "params"},
                          {"scope", kPbtScope},
                          {"operation-name", operation_name}});
  if (params_blackboard_item.ok()) {
    // 1a) Params are on the blackboard already.
    // Get the params message from the blackboard item and write it into the
    // BehaviorCall instance
    INTR_ASSIGN_OR_RETURN(clips::Value bb_item_proto_id_val,
                          params_blackboard_item->GetSlotValue("proto-id"));
    INTR_ASSIGN_OR_RETURN(int64_t bb_item_proto_id,
                          bb_item_proto_id_val.GetInteger());
    INTR_ASSIGN_OR_RETURN(
        google::protobuf::Any bb_item_proto,
        proto_mgr->CastToAny(clips::ProtoMessageId(bb_item_proto_id)));
    *behavior_call_prototype_duplicate->mutable_parameters() =
        std::move(bb_item_proto);
    // Finally: store the instance proto
    behavior_call_instance_id = proto_mgr->AddGeneratedProto(
        std::move(*behavior_call_prototype_duplicate));
    INTR_RET_CHECK(behavior_call_instance_id !=
                   clips::ProtobufManager::kInvalidId)
        << " while adding behavior call instance proto from blackboard for "
        << kCallingPbtTreeId << ":" << node.id();
  } else {
    // 1b) Params are not on the blackboard. Store the unparameterized duplicate
    // as the instance proto
    behavior_call_instance_id = proto_mgr->AddGeneratedProto(
        std::move(*behavior_call_prototype_duplicate));
    INTR_RET_CHECK(behavior_call_instance_id !=
                   clips::ProtobufManager::kInvalidId)
        << " while adding new behavior call instance proto for "
        << kCallingPbtTreeId << ":" << node.id();
    RecoveryNodeIdentifier node_identifier(kCallingPbtTreeId, node.id());
    // Now perform "normal" parameterization as if a task node would have been
    // called.
    INTR_RETURN_IF_ERROR(EvaluateParameterizationExtendedStatusResult(
        env, proto_mgr,
        absl::StrFormat(
            R"clips(
      (action-parameterization-assign-behavior-call-proto %d "%s" "%s"))clips",
            behavior_call_instance_id.value(), kCallingPbtScope,
            operation_name),
        node, node_identifier));

    // Unpack and insert params on blackboard
    INTR_RETURN_IF_ERROR(EvaluateParameterizationExtendedStatusResult(
        env, proto_mgr,
        absl::StrFormat(
            R"clips(
      (behavior-call-instance-parameterize-blackboard %d "%s" "%s"))clips",
            behavior_call_instance_id.value(), kPbtScope, operation_name),
        node, node_identifier));
  }

  // 2. Call equipment parameterization as if this behavior call instance would
  // be executed. This will fill all resource references in the behavior call
  // instance with handles from the enclosing BehaviorCall.
  // For nested PBTs: Due to the recursive nature of applying recovery top to
  // bottom, when this PBT is recovered its enclosing instance (or the
  // operation-envelope) already has been recovered. Thus the resources of the
  // enclosing instance are already handles.
  absl::Status parameterize_equipment_status =
      env
          ->EvaluateResult(absl::StrFormat(
              R"clips(
      (behavior-call-instance-parameterize-resources %d %s))clips",
              behavior_call_instance_id.value(), kCallingPbtTreeId))
          .status();
  if (!parameterize_equipment_status.ok()) {
    RecoveryNodeIdentifier node_identifier(kCallingPbtTreeId, node.id());
    return CreateResourceParameterizationError(parameterize_equipment_status,
                                               node_identifier, node);
  }

  // 3. Update state proto after PBT parameterization
  INTR_ASSIGN_OR_RETURN(
      clips::Fact node_fact,
      env->GetUniqueFact("behavior-tree-node",
                         {{"behavior-call-instance-uid", behavior_call_uid}}));
  INTR_ASSIGN_OR_RETURN(clips::Value state_proto_path_val,
                        node_fact.GetSlotValue("run-metadata-proto-path"));
  INTR_ASSIGN_OR_RETURN(std::string state_proto_path,
                        state_proto_path_val.GetString());
  if (!state_proto_path.empty()) {
    INTR_RETURN_IF_ERROR(
        env->Evaluate(absl::StrFormat(R"clips(
    (behavior-tree-task-node-update-state-params "%s" %d "%s" CALL-BEHAVIOR))clips",
                                      state_proto_path,
                                      behavior_call_instance_id.value(),
                                      operation_name))
            .status());
  }

  // 4. Actually update the behavior-call-instance to RUNNING
  VLOG(1) << "Setting recovery behavior-call-instance state at: "
          << containing_tree.tree_id() << ":" << node.id() << " = RUNNING";
  INTR_RETURN_IF_ERROR(env->ModifyFact(std::move(behavior_call_instance_fact),
                                       {{"state", clips::Symbol("RUNNING")},
                                        {"behavior-call-instance-proto",
                                         behavior_call_instance_id.value()}})
                           .status());
  return absl::OkStatus();
}

absl::Status RecoverConditionState(clips::Environment* absl_nonnull env,
                                   const BehaviorTree::Condition& condition,
                                   clips::Symbol condition_id)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  INTR_ASSIGN_OR_RETURN(
      clips::Fact condition_fact,
      env->GetUniqueFact("behavior-tree-condition", {{"id", condition_id}}));
  INTR_RETURN_IF_ERROR(
      env->ModifyFact(
             std::move(condition_fact),
             {{"state", clips::Symbol("FINISHED")},
              {"satisfied", condition.satisfied() ? clips::Symbol::True()
                                                  : clips::Symbol::False()}})
          .status());
  return absl::OkStatus();
}

struct RecoverOperationStateContext {
  clips::TraceSpanReferenceId parent_span_id =
      clips::TraceSpanManager::kInvalidTraceSpanReferenceId;
  bool is_start_tree = false;
};

absl::StatusOr<RecoverOperationStateContext> RecoverNodeState(
    clips::Environment* absl_nonnull env,
    clips::ProtobufManager* absl_nonnull proto_mgr,
    clips::TraceSpanManager* absl_nonnull span_mgr,
    const BehaviorTree& containing_tree, const BehaviorTree::Node& node,
    absl::string_view operation_name,
    const RecoverOperationStateContext& parent_context)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  INTR_ASSIGN_OR_RETURN(
      clips::Fact node_fact,
      env->GetUniqueFact("behavior-tree-node",
                         {{"tree-id", clips::Symbol(containing_tree.tree_id())},
                          {"id", node.id()}}));
  VLOG(1) << "Setting recovery node state for: " << containing_tree.tree_id()
          << ":" << node.id() << " = "
          << BehaviorTree::Node::State_Name(node.state());

  clips::Symbol failure_reason = clips::Symbol("UNKNOWN");
  if (node.has_failure_reason()) {
    switch (node.failure_reason()) {
      case BehaviorTree::Node::FAILED_CONDITION:
        failure_reason = clips::Symbol("CONDITION");
        break;
      case BehaviorTree::Node::FAILED_EXECUTION:
        failure_reason = clips::Symbol("EXECUTION");
        break;
      case BehaviorTree::Node::UNKNOWN_FAILURE:
        break;
      default:
        break;
    }
  }

  clips::TraceSpanReferenceId node_span =
      clips::TraceSpanManager::kInvalidTraceSpanReferenceId;
  if (node.state() == BehaviorTree::Node::SELECTED ||
      node.state() == BehaviorTree::Node::RUNNING) {
    absl::StatusOr<clips::TraceSpanReferenceId> node_span_or = StartNodeSpan(
        env, proto_mgr, span_mgr, containing_tree.tree_id(), node.id());
    // Failing to recover spans must not fail the recover. Just log the issue.
    node_span = node_span_or.value_or(
        clips::TraceSpanManager::kInvalidTraceSpanReferenceId);
    if (!node_span_or.ok()) {
      LOG(WARNING) << "Could not start node span for "
                   << containing_tree.tree_id() << ":" << node.id();
    }
  }

  RecoverOperationStateContext node_context{.parent_span_id = node_span};

  clips::Symbol recovered_state =
      clips::Symbol(BehaviorTree::Node::State_Name(node.state()));
  INTR_ASSIGN_OR_RETURN(clips::Value node_fact_state,
                        node_fact.GetSlotValue("state"));
  // Only set recovered_state when recovery actually changed the state
  clips::Symbol state_proto_recovered_state =
      (node_fact_state != recovered_state) ? recovered_state
                                           : clips::Symbol("NONE");
  INTR_ASSIGN_OR_RETURN(
      node_fact,
      env->ModifyFact(
          std::move(node_fact),
          {{"state", recovered_state},
           {"run-metadata-proto-recovered-state", state_proto_recovered_state},
           {"failure-reason", failure_reason},
           {"span-reference-id", node_span.value()}}),
      _.LogError());

  if (node.decorators().condition().has_state() &&
      node.decorators().condition().state() !=
          BehaviorTree::Condition::ACCEPTED) {
    INTR_RET_CHECK_EQ(node.decorators().condition().state(),
                      BehaviorTree::Condition::FINISHED)
        << " while setting decorator condition from unexpected state for node "
        << node.DebugString();
    INTR_ASSIGN_OR_RETURN(clips::Value condition_id_val,
                          node_fact.GetSlotValue("condition-id"));
    INTR_ASSIGN_OR_RETURN(clips::Symbol condition_id,
                          condition_id_val.GetSymbol());
    INTR_RETURN_IF_ERROR(RecoverConditionState(
        env, node.decorators().condition(), condition_id));
  }

  if (node.has_task() && node.task().has_called_tree_state() &&
      node.state() == BehaviorTree::Node::RUNNING) {
    // This is a task node pointing to a PBT (has_called_tree_state) and
    // its RUNNING, i.e., the recovery goes into the PBT and thus the
    // associated behavior call instance must be set to a running state.
    INTR_ASSIGN_OR_RETURN(clips::Value behavior_call_uid_val,
                          node_fact.GetSlotValue("behavior-call-instance-uid"),
                          _.LogError());
    INTR_ASSIGN_OR_RETURN(clips::Symbol behavior_call_uid,
                          behavior_call_uid_val.GetSymbol(), _.LogError());
    INTR_RETURN_IF_ERROR(RecoverPBTBehaviorCall(env, proto_mgr, containing_tree,
                                                node, behavior_call_uid,
                                                operation_name));
  }
  if (node.has_branch() && node.state() == BehaviorTree::Node::RUNNING) {
    INTR_RET_CHECK_EQ(node.branch().if_().state(),
                      BehaviorTree::Condition::FINISHED)
        << " while setting branch condition from unexpected state for node "
        << node.DebugString();
    INTR_ASSIGN_OR_RETURN(clips::Value condition_id_val,
                          node_fact.GetSlotValue("branch-if-id"));
    INTR_ASSIGN_OR_RETURN(clips::Symbol condition_id,
                          condition_id_val.GetSymbol());
    INTR_RETURN_IF_ERROR(
        RecoverConditionState(env, node.branch().if_(), condition_id));
  }
  if (node.has_retry() && node.state() == BehaviorTree::Node::RUNNING) {
    INTR_RET_CHECK(node.retry().has_num_tries())
        << " found retry node without num_tries set for node "
        << node.DebugString();
    // It is only necessary to start iteration spans for RUNNING nodes (in
    // contrast to starting node spans also for SELECTED nodes) as a SELECTED
    // node will start executing and then start the iteration span itself
    absl::StatusOr<clips::TraceSpanReferenceId> iteration_span_or =
        StartNodeIterationSpan(env, span_mgr, containing_tree.tree_id(),
                               node.id(), node.retry().num_tries() - 1);
    // Failing to recover spans must not fail the recover. Just log the issue.
    clips::TraceSpanReferenceId iteration_span = iteration_span_or.value_or(
        clips::TraceSpanManager::kInvalidTraceSpanReferenceId);
    if (!iteration_span_or.ok()) {
      LOG(WARNING) << "Could not start node iteration span for "
                   << containing_tree.tree_id() << ":" << node.id();
    }

    node_context.parent_span_id = iteration_span;
    INTR_ASSIGN_OR_RETURN(
        node_fact,
        env->ModifyFact(
            std::move(node_fact),
            {{"retry-num-tries", clips::Value(node.retry().num_tries())},
             {"iteration-span-reference-id", iteration_span.value()}}),
        _.LogError());
  }
  if (node.has_loop() && node.state() == BehaviorTree::Node::RUNNING) {
    INTR_RET_CHECK(node.loop().has_num_times())
        << " found loop node without num_times set for node "
        << node.DebugString();
    // It is only necessary to start iteration spans for RUNNING nodes (in
    // contrast to starting node spans also for SELECTED nodes) as a SELECTED
    // node will start executing and then start the iteration span itself
    absl::StatusOr<clips::TraceSpanReferenceId> iteration_span_or =
        StartNodeIterationSpan(env, span_mgr, containing_tree.tree_id(),
                               node.id(), node.loop().num_times() - 1);
    // Failing to recover spans must not fail the recover. Just log the issue.
    clips::TraceSpanReferenceId iteration_span = iteration_span_or.value_or(
        clips::TraceSpanManager::kInvalidTraceSpanReferenceId);
    if (!iteration_span_or.ok()) {
      LOG(WARNING) << "Could not start node iteration span for "
                   << containing_tree.tree_id() << ":" << node.id();
    }

    node_context.parent_span_id = iteration_span;
    INTR_ASSIGN_OR_RETURN(
        node_fact,
        env->ModifyFact(
            std::move(node_fact),
            {{"loop-num-times", clips::Value(node.loop().num_times())},
             {"iteration-span-reference-id", iteration_span.value()}}),
        _.LogError());
    // Note: Do not modify the condition. Running loop nodes have an ACCEPTED
    // condition that is started for each iteration, but reset when the child is
    // selected.
  }

  return node_context;
}

absl::StatusOr<RecoverOperationStateContext> RecoverTreeState(
    clips::Environment* absl_nonnull env,
    clips::TraceSpanManager* absl_nonnull span_mgr, const BehaviorTree& tree,
    const RecoverOperationStateContext& parent_context)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  INTR_ASSIGN_OR_RETURN(
      clips::Fact tree_fact,
      env->GetUniqueFact("behavior-tree",
                         {{"id", clips::Symbol(tree.tree_id())}}));
  VLOG(1) << "Setting recovery tree state for: " << tree.tree_id() << " = "
          << BehaviorTree::State_Name(tree.state());

  clips::TraceSpanReferenceId tree_span =
      clips::TraceSpanManager::kInvalidTraceSpanReferenceId;
  if (tree.state() == BehaviorTree::RUNNING) {
    if (parent_context.is_start_tree) {
      tree_span = parent_context.parent_span_id;
    } else {
      absl::StatusOr<clips::TraceSpanReferenceId> tree_span_or = StartTreeSpan(
          env, span_mgr, tree.tree_id(), parent_context.parent_span_id);
      // Failing to recover spans must not fail the recover. Just log the issue.
      tree_span = tree_span_or.value_or(
          clips::TraceSpanManager::kInvalidTraceSpanReferenceId);
      if (!tree_span_or.ok()) {
        LOG(WARNING) << "Could not start node span for " << tree.tree_id();
      }
    }
  }
  INTR_RETURN_IF_ERROR(
      env->ModifyFact(
             std::move(tree_fact),
             {{"state", clips::Symbol(BehaviorTree::State_Name(tree.state()))},
              {"span-reference-id", tree_span.value()}})
          .status());
  return RecoverOperationStateContext{.parent_span_id = tree_span};
}

absl::Status RecoverOperationStateImpl(
    clips::Environment* absl_nonnull env,
    clips::ProtobufManager* absl_nonnull proto_mgr,
    clips::TraceSpanManager* absl_nonnull span_mgr,
    absl::string_view operation_name, const BehaviorTree& recovered_state_proto,
    clips::TraceSpanReferenceId start_tree_span)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  auto set_node_state = [env, proto_mgr, span_mgr, operation_name](
                            const BehaviorTree& containing_tree,
                            const BehaviorTree::Node& node,
                            const RecoverOperationStateContext& parent_context)
      -> absl::StatusOr<RecoverOperationStateContext> {
    env->mutex()->AssertHeld();
    INTR_ASSIGN_OR_RETURN(
        RecoverOperationStateContext node_context,
        RecoverNodeState(env, proto_mgr, span_mgr, containing_tree, node,
                         operation_name, parent_context),
        _ << " while setting node with id " << containing_tree.tree_id() << ":"
          << node.id());
    return node_context;
  };
  auto set_tree_state = [env, span_mgr](
                            const BehaviorTree& tree,
                            const RecoverOperationStateContext& parent_context)
      -> absl::StatusOr<RecoverOperationStateContext> {
    env->mutex()->AssertHeld();
    INTR_ASSIGN_OR_RETURN(RecoverOperationStateContext tree_context,
                          RecoverTreeState(env, span_mgr, tree, parent_context),
                          _ << " while setting tree: " << tree.tree_id());
    return tree_context;
  };

  return VisitBehaviorTree(
      recovered_state_proto,
      {.visit_tree = set_tree_state, .visit_node = set_node_state},
      RecoverOperationStateContext{.parent_span_id = start_tree_span,
                                   .is_start_tree = true},
      {.visit_called_tree_state = true});
}

absl::Status CreateRecoveryNodesNotFoundError(
    absl::Span<const BehaviorTree::NodeIdentifier> not_found_recovery_nodes) {
  intrinsic_proto::status::ExtendedStatus::Relations related_to;
  // ExtendedStatus supports only one related_to node. Choose the first one to
  // give some hint about the error origin as in most cases this is likely
  // just one node.
  RecoveryNodeIdentifier first_not_found =
      RecoveryNodeIdentifier(not_found_recovery_nodes.front());
  related_to.mutable_behavior_tree_node()->set_tree_id(
      first_not_found.prefixed_tree_id);
  related_to.mutable_behavior_tree_node()->set_node_id(first_not_found.node_id);
  return CreateStatus(
      70102,
      absl::StrFormat("The following node identifiers were not found in the "
                      "current operation: %s",
                      RecoveryNodesToString(not_found_recovery_nodes)),
      absl::StatusCode::kNotFound, {.related_to = related_to});
}

// Finds recovery paths in 'tree' towards 'recovery_nodes' and fills these in
// 'recovery_paths'. If the functions succeeds there is one RecoveryPath for
// each recovery node.
// Does not verify if this would constitute a consistent recovery, just that
// the paths exist in the tree.
absl::Status FindRecoveryPaths(
    const BehaviorTree& tree,
    absl::Span<const BehaviorTree::NodeIdentifier> recovery_nodes,
    std::vector<RecoveryPath>& recovery_paths) {
  recovery_paths.reserve(recovery_nodes.size());
  INTR_ASSIGN_OR_RETURN(
      absl::flat_hash_set<RecoveryNodeIdentifier> recovery_node_identifiers,
      RecoveryNodeIdentifiersAsSet(recovery_nodes));
  INTR_RETURN_IF_ERROR(FindRecoveryPathsInTree(
      tree, {}, recovery_node_identifiers, recovery_paths));

  std::vector<BehaviorTree::NodeIdentifier> not_found_recovery_nodes;
  for (const BehaviorTree::NodeIdentifier& recovery_node : recovery_nodes) {
    RecoveryNodeIdentifier recovery_node_identifier(recovery_node);
    if (absl::c_find_if(recovery_paths,
                        [&recovery_node_identifier](const RecoveryPath& rp) {
                          return rp.node == recovery_node_identifier;
                        }) == recovery_paths.end()) {
      not_found_recovery_nodes.push_back(recovery_node);
    }
  }
  if (!not_found_recovery_nodes.empty()) {
    return CreateRecoveryNodesNotFoundError(not_found_recovery_nodes);
  }

  for (RecoveryPath& rp : recovery_paths) {
    for (auto it = rp.path.rbegin(); it != rp.path.rend(); ++it) {
      rp.downward_path.push(*it);
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status ApplyRecoveryPaths(
    BehaviorTree& tree,
    absl::Span<const BehaviorTree::NodeIdentifier> recovery_nodes) {
  std::vector<RecoveryPath> recovery_paths;
  INTR_RETURN_IF_ERROR(FindRecoveryPaths(tree, recovery_nodes, recovery_paths));
  return ApplyRecoveryPathsInTree(tree, recovery_paths);
}

absl::Status RecoverOperationState(
    clips::Environment* absl_nonnull env,
    clips::ProtobufManager* absl_nonnull proto_mgr,
    clips::TraceSpanManager* absl_nonnull span_mgr,
    const BehaviorTree& state_proto, absl::string_view operation_name,
    clips::TraceSpanReferenceId start_tree_span)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  return RecoverOperationStateImpl(env, proto_mgr, span_mgr, operation_name,
                                   state_proto, start_tree_span);
}

absl::Status AddClipsRecoveryFunctions(
    clips::Environment* absl_nonnull env,
    clips::ProtobufManager* absl_nonnull proto_mgr,
    clips::TraceSpanManager* absl_nonnull span_mgr)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  auto return_clips_result =
      [proto_mgr](const absl::Status& status) -> clips::Values {
    std::optional<intrinsic_proto::status::ExtendedStatus> es =
        GetExtendedStatus(status);
    if (!es.has_value()) {
      if (status.code() == absl::StatusCode::kInternal) {
        LOG(ERROR) << "Recovery failed due to an internal error: "
                   << status.message();
        es.emplace(CreateExtendedStatus(
            70199, "An unexpected error occurred when applying the recovery"));
      } else {
        es.emplace(CreateExtendedStatus(70199, status.ToString()));
      }
    }
    if (es->severity() == intrinsic_proto::status::ExtendedStatus::DEFAULT) {
      // As this happens from an error handle set the severity accordingly if it
      // hasn't been labeled differently
      es->set_severity(intrinsic_proto::status::ExtendedStatus::ERROR);
    }
    clips::ProtoMessageId es_proto_id =
        proto_mgr->AddGeneratedProto(std::move(*es));
    return {clips::Symbol::False(), clips::Value(es_proto_id.value())};
  };

  INTR_RETURN_IF_ERROR(env->AddFunction(
      "recovery-recover-operation-state",
      std::function([env, proto_mgr, span_mgr, return_clips_result](
                        int64_t recovery_state_proto_id,
                        const std::string& operation_name,
                        int64_t start_tree_span) -> clips::Values {
        env->mutex()->AssertHeld();
        INTR_ASSIGN_OR_RETURN(
            std::unique_ptr<BehaviorTree> recovery_state_proto,
            proto_mgr->GetProtoAs<BehaviorTree>(
                clips::ProtoMessageId(recovery_state_proto_id)),
            _.With(return_clips_result));
        INTR_RETURN_IF_ERROR(
            RecoverOperationState(env, proto_mgr, span_mgr,
                                  *recovery_state_proto, operation_name,
                                  clips::TraceSpanReferenceId(start_tree_span)))
            .With(return_clips_result);
        return {clips::Symbol::True(), clips::Value("")};
      })));

  return absl::OkStatus();
}

}  // namespace intrinsic::executive
