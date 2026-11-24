#include <optional>
#include <type_traits>

#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::executive {

namespace {

template <typename CallbacksCvRefType>
struct GetContextTypeHelper {
  using value = typename std::remove_cvref_t<CallbacksCvRefType>::ContextType;
};

// Returns the context type for a given callbacks cv-ref type. E.g.:
// const VisitBehaviorTreeCallbacks& -> VisitBehaviorTreeCallbacks::ContextType.
template <typename CallbacksCvRefType>
using GetContextType = typename GetContextTypeHelper<CallbacksCvRefType>::value;

template <typename T>
concept IsBehaviorTree =
    std::is_same_v<T, const intrinsic_proto::executive::BehaviorTree> ||
    std::is_same_v<T, intrinsic_proto::executive::BehaviorTree>;
template <typename T>
concept IsBehaviorTreeCondition =
    std::is_same_v<T,
                   const intrinsic_proto::executive::BehaviorTree::Condition> ||
    std::is_same_v<T, intrinsic_proto::executive::BehaviorTree::Condition>;
template <typename T>
concept IsBehaviorTreeNode =
    std::is_same_v<T, const intrinsic_proto::executive::BehaviorTree::Node> ||
    std::is_same_v<T, intrinsic_proto::executive::BehaviorTree::Node>;

template <typename T>
concept IsVisitorCallback =
    requires { typename std::remove_cvref_t<T>::ContextType; } &&
    (std::is_same_v<std::remove_cvref_t<T>,
                    VisitBehaviorTreeContextCallbacks<GetContextType<T>>> ||
     std::is_same_v<
         std::remove_cvref_t<T>,
         VisitMutableBehaviorTreeContextCallbacks<GetContextType<T>>>);

template <typename T>
concept IsMutable =
    (IsVisitorCallback<T> &&
     std::is_same_v<
         T, VisitMutableBehaviorTreeContextCallbacks<GetContextType<T>>>) ||
    (!IsVisitorCallback<T> && !std::is_const_v<std::remove_reference_t<T>>);

// This is equivalent to:
// Callback is mutable => behavior tree part is mutable
// i.e., if a mutable callback is requested there must be a mutable behavior
// tree passed in. (The case of non-mutable callbacks accepts both mutable and
// non-mutable behavior tree parts)
template <typename BehaviorTreePartT, typename VisitCallbacksT>
concept IsCompatible =
    !IsMutable<VisitCallbacksT> || IsMutable<BehaviorTreePartT>;

template <typename BehaviorTreeT, typename VisitBehaviorTreeCallbacksT>
  requires IsBehaviorTree<BehaviorTreeT> &&
           IsVisitorCallback<VisitBehaviorTreeCallbacksT> &&
           IsCompatible<BehaviorTreeT, VisitBehaviorTreeCallbacksT>
absl::Status VisitBehaviorTreeImpl(
    BehaviorTreeT& tree, VisitBehaviorTreeCallbacksT callbacks,
    const GetContextType<VisitBehaviorTreeCallbacksT>& parent_context,
    const VisitBehaviorTreeOptions& options);

/*
Visit a condition based on its respective type:
Inputs:
  * containing_tree that contains the visited condition,
  * condition that is visited
  * callbacks are functions provided via initial VisitBehaviorTree call.
*/
template <typename BehaviorTreeT, typename BehaviorTreeConditionT,
          typename VisitBehaviorTreeCallbacksT>
  requires IsBehaviorTree<BehaviorTreeT> &&
           IsBehaviorTreeCondition<BehaviorTreeConditionT> &&
           IsVisitorCallback<VisitBehaviorTreeCallbacksT> &&
           IsCompatible<BehaviorTreeT, VisitBehaviorTreeCallbacksT> &&
           IsCompatible<BehaviorTreeConditionT, VisitBehaviorTreeCallbacksT>
absl::Status VisitCondition(
    BehaviorTreeT& containing_tree, BehaviorTreeConditionT& condition,
    VisitBehaviorTreeCallbacksT callbacks,
    const GetContextType<VisitBehaviorTreeCallbacksT>& parent_context,
    const VisitBehaviorTreeOptions& options) {
  GetContextType<VisitBehaviorTreeCallbacksT> current_context;
  if (callbacks.visit_condition) {
    INTR_ASSIGN_OR_RETURN(
        current_context,
        callbacks.visit_condition(containing_tree, condition, parent_context));
  }

  intrinsic_proto::executive::BehaviorTree::Condition::ConditionTypeCase
      condition_type = condition.condition_type_case();

  switch (condition_type) {
    case intrinsic_proto::executive::BehaviorTree::Condition::
        ConditionTypeCase::kBehaviorTree:
      if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
        INTR_RETURN_IF_ERROR(
            VisitBehaviorTreeImpl(*condition.mutable_behavior_tree(), callbacks,
                                  current_context, options));
      } else {
        INTR_RETURN_IF_ERROR(VisitBehaviorTreeImpl(
            condition.behavior_tree(), callbacks, current_context, options));
      }
      break;
    case intrinsic_proto::executive::BehaviorTree::Condition::
        ConditionTypeCase::kBlackboard:
      break;
    case intrinsic_proto::executive::BehaviorTree::Condition::
        ConditionTypeCase::kAllOf:
      if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
        for (intrinsic_proto::executive::BehaviorTree::Condition& child :
             *condition.mutable_all_of()->mutable_conditions()) {
          INTR_RETURN_IF_ERROR(VisitCondition(containing_tree, child, callbacks,
                                              current_context, options));
        }
      } else {
        for (const intrinsic_proto::executive::BehaviorTree::Condition& child :
             condition.all_of().conditions()) {
          INTR_RETURN_IF_ERROR(VisitCondition(containing_tree, child, callbacks,
                                              current_context, options));
        }
      }
      break;
    case intrinsic_proto::executive::BehaviorTree::Condition::
        ConditionTypeCase::kAnyOf:
      if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
        for (intrinsic_proto::executive::BehaviorTree::Condition& child :
             *condition.mutable_any_of()->mutable_conditions()) {
          INTR_RETURN_IF_ERROR(VisitCondition(containing_tree, child, callbacks,
                                              current_context, options));
        }
      } else {
        for (const intrinsic_proto::executive::BehaviorTree::Condition& child :
             condition.any_of().conditions()) {
          INTR_RETURN_IF_ERROR(VisitCondition(containing_tree, child, callbacks,
                                              current_context, options));
        }
      }
      break;
    case intrinsic_proto::executive::BehaviorTree::Condition::
        ConditionTypeCase::kNot:
      if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
        INTR_RETURN_IF_ERROR(
            VisitCondition(containing_tree, *condition.mutable_not_(),
                           callbacks, current_context, options));
      } else {
        INTR_RETURN_IF_ERROR(VisitCondition(containing_tree, condition.not_(),
                                            callbacks, current_context,
                                            options));
      }
      break;
    case intrinsic_proto::executive::BehaviorTree::Condition::
        ConditionTypeCase::kStatusMatch:
      break;
    case intrinsic_proto::executive::BehaviorTree::Condition::
        ConditionTypeCase::CONDITION_TYPE_NOT_SET:
      break;
  }
  return absl::OkStatus();
}

/*
Visit a node based on its respective type:
Inputs:
  * containing_tree that contains the visited node,
  * node that is visited
  * callbacks are functions provided via initial VisitBehaviorTree call.
*/
template <typename BehaviorTreeT, typename BehaviorTreeNodeT,
          typename VisitBehaviorTreeCallbacksT>
  requires IsBehaviorTree<BehaviorTreeT> &&
           IsBehaviorTreeNode<BehaviorTreeNodeT> &&
           IsVisitorCallback<VisitBehaviorTreeCallbacksT> &&
           IsCompatible<BehaviorTreeT, VisitBehaviorTreeCallbacksT> &&
           IsCompatible<BehaviorTreeNodeT, VisitBehaviorTreeCallbacksT>
absl::Status VisitNode(
    BehaviorTreeT& containing_tree, BehaviorTreeNodeT& node,
    VisitBehaviorTreeCallbacksT callbacks,
    const GetContextType<VisitBehaviorTreeCallbacksT>& parent_context,
    const VisitBehaviorTreeOptions& options) {
  GetContextType<VisitBehaviorTreeCallbacksT> current_context;
  if (callbacks.visit_node) {
    INTR_ASSIGN_OR_RETURN(
        current_context,
        callbacks.visit_node(containing_tree, node, parent_context));
  }
  if (node.has_decorators() && node.decorators().has_condition()) {
    if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
      INTR_RETURN_IF_ERROR(VisitCondition(
          containing_tree, *node.mutable_decorators()->mutable_condition(),
          callbacks, current_context, options));
    } else {
      INTR_RETURN_IF_ERROR(VisitCondition(containing_tree,
                                          node.decorators().condition(),
                                          callbacks, current_context, options));
    }
  }

  intrinsic_proto::executive::BehaviorTree::Node::NodeTypeCase node_type =
      node.node_type_case();
  switch (node_type) {
    case intrinsic_proto::executive::BehaviorTree::Node::NodeTypeCase::
        kSequence:
      if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
        for (intrinsic_proto::executive::BehaviorTree::Node& child :
             *node.mutable_sequence()->mutable_children()) {
          INTR_RETURN_IF_ERROR(VisitNode(containing_tree, child, callbacks,
                                         current_context, options));
        }
      } else {
        for (const intrinsic_proto::executive::BehaviorTree::Node& child :
             node.sequence().children()) {
          INTR_RETURN_IF_ERROR(VisitNode(containing_tree, child, callbacks,
                                         current_context, options));
        }
      }
      break;
    case intrinsic_proto::executive::BehaviorTree::Node::NodeTypeCase::
        kParallel:
      if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
        for (intrinsic_proto::executive::BehaviorTree::Node& child :
             *node.mutable_parallel()->mutable_children()) {
          INTR_RETURN_IF_ERROR(VisitNode(containing_tree, child, callbacks,
                                         current_context, options));
        }
      } else {
        for (const intrinsic_proto::executive::BehaviorTree::Node& child :
             node.parallel().children()) {
          INTR_RETURN_IF_ERROR(VisitNode(containing_tree, child, callbacks,
                                         current_context, options));
        }
      }
      break;
    case intrinsic_proto::executive::BehaviorTree::Node::NodeTypeCase::kTask:
      if (options.visit_called_tree_state &&
          node.task().has_called_tree_state()) {
        if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
          INTR_RETURN_IF_ERROR(VisitBehaviorTreeImpl(
              *node.mutable_task()->mutable_called_tree_state(), callbacks,
              current_context, options));
        } else {
          INTR_RETURN_IF_ERROR(
              VisitBehaviorTreeImpl(node.task().called_tree_state(), callbacks,
                                    current_context, options));
        }
      }
      break;
    case intrinsic_proto::executive::BehaviorTree::Node::NodeTypeCase::kFail:
      break;
    case intrinsic_proto::executive::BehaviorTree::Node::NodeTypeCase::
        kSelector:
      if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
#pragma clang diagnostic push
// We need to support deprecated fields here.
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        for (intrinsic_proto::executive::BehaviorTree::Node& child :
             *node.mutable_selector()->mutable_children()) {
#pragma clang diagnostic pop
          INTR_RETURN_IF_ERROR(VisitNode(containing_tree, child, callbacks,
                                         current_context, options));
        }
        for (intrinsic_proto::executive::BehaviorTree::SelectorNode::Branch&
                 branch : *node.mutable_selector()->mutable_branches()) {
          if (branch.has_condition()) {
            INTR_RETURN_IF_ERROR(
                VisitCondition(containing_tree, *branch.mutable_condition(),
                               callbacks, current_context, options));
          }
          if (branch.has_node()) {
            INTR_RETURN_IF_ERROR(VisitNode(containing_tree,
                                           *branch.mutable_node(), callbacks,
                                           current_context, options));
          }
        }
      } else {
#pragma clang diagnostic push
// We need to support deprecated fields here.
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        for (const intrinsic_proto::executive::BehaviorTree::Node& child :
             node.selector().children()) {
#pragma clang diagnostic pop
          INTR_RETURN_IF_ERROR(VisitNode(containing_tree, child, callbacks,
                                         current_context, options));
        }
        for (const intrinsic_proto::executive::BehaviorTree::SelectorNode::
                 Branch& branch : node.selector().branches()) {
          if (branch.has_condition()) {
            INTR_RETURN_IF_ERROR(VisitCondition(containing_tree,
                                                branch.condition(), callbacks,
                                                current_context, options));
          }
          if (branch.has_node()) {
            INTR_RETURN_IF_ERROR(VisitNode(containing_tree, branch.node(),
                                           callbacks, current_context,
                                           options));
          }
        }
      }
      break;
    case intrinsic_proto::executive::BehaviorTree::Node::NodeTypeCase::
        kFallback:
      if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
        for (intrinsic_proto::executive::BehaviorTree::Node& child :
             *node.mutable_fallback()->mutable_children()) {
          INTR_RETURN_IF_ERROR(VisitNode(containing_tree, child, callbacks,
                                         current_context, options));
        }
        for (intrinsic_proto::executive::BehaviorTree::FallbackNode::Try&
                 try_entry : *node.mutable_fallback()->mutable_tries()) {
          if (try_entry.has_condition()) {
            INTR_RETURN_IF_ERROR(
                VisitCondition(containing_tree, *try_entry.mutable_condition(),
                               callbacks, current_context, options));
          }
          if (try_entry.has_node()) {
            INTR_RETURN_IF_ERROR(VisitNode(containing_tree,
                                           *try_entry.mutable_node(), callbacks,
                                           current_context, options));
          }
        }
      } else {
        for (const intrinsic_proto::executive::BehaviorTree::Node& child :
             node.fallback().children()) {
          INTR_RETURN_IF_ERROR(VisitNode(containing_tree, child, callbacks,
                                         current_context, options));
        }
        for (const intrinsic_proto::executive::BehaviorTree::FallbackNode::Try&
                 try_entry : node.fallback().tries()) {
          if (try_entry.has_condition()) {
            INTR_RETURN_IF_ERROR(
                VisitCondition(containing_tree, try_entry.condition(),
                               callbacks, current_context, options));
          }
          if (try_entry.has_node()) {
            INTR_RETURN_IF_ERROR(VisitNode(containing_tree, try_entry.node(),
                                           callbacks, current_context,
                                           options));
          }
        }
      }
      break;
    case intrinsic_proto::executive::BehaviorTree::Node::NodeTypeCase::kBranch:
      if (node.branch().has_if_()) {
        if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
          INTR_RETURN_IF_ERROR(VisitCondition(
              containing_tree, *node.mutable_branch()->mutable_if_(), callbacks,
              current_context, options));
        } else {
          INTR_RETURN_IF_ERROR(VisitCondition(containing_tree,
                                              node.branch().if_(), callbacks,
                                              current_context, options));
        }
      }
      if (node.branch().has_then()) {
        if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
          INTR_RETURN_IF_ERROR(VisitNode(containing_tree,
                                         *node.mutable_branch()->mutable_then(),
                                         callbacks, current_context, options));
        } else {
          INTR_RETURN_IF_ERROR(VisitNode(containing_tree, node.branch().then(),
                                         callbacks, current_context, options));
        }
      }
      if (node.branch().has_else_()) {
        if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
          INTR_RETURN_IF_ERROR(VisitNode(
              containing_tree, *node.mutable_branch()->mutable_else_(),
              callbacks, current_context, options));
        } else {
          INTR_RETURN_IF_ERROR(VisitNode(containing_tree, node.branch().else_(),
                                         callbacks, current_context, options));
        }
      }
      break;
    case intrinsic_proto::executive::BehaviorTree::Node::NodeTypeCase::kLoop:
      if (node.loop().has_while_()) {
        if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
          INTR_RETURN_IF_ERROR(VisitCondition(
              containing_tree, *node.mutable_loop()->mutable_while_(),
              callbacks, current_context, options));
        } else {
          INTR_RETURN_IF_ERROR(VisitCondition(containing_tree,
                                              node.loop().while_(), callbacks,
                                              current_context, options));
        }
      }
      if (node.loop().has_do_()) {
        if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
          INTR_RETURN_IF_ERROR(VisitNode(containing_tree,
                                         *node.mutable_loop()->mutable_do_(),
                                         callbacks, current_context, options));
        } else {
          INTR_RETURN_IF_ERROR(VisitNode(containing_tree, node.loop().do_(),
                                         callbacks, current_context, options));
        }
      }
      break;
    case intrinsic_proto::executive::BehaviorTree::Node::NodeTypeCase::kRetry:
      if (node.retry().has_child()) {
        if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
          INTR_RETURN_IF_ERROR(VisitNode(containing_tree,
                                         *node.mutable_retry()->mutable_child(),
                                         callbacks, current_context, options));
        } else {
          INTR_RETURN_IF_ERROR(VisitNode(containing_tree, node.retry().child(),
                                         callbacks, current_context, options));
        }
      }
      if (node.retry().has_recovery()) {
        if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
          INTR_RETURN_IF_ERROR(VisitNode(
              containing_tree, *node.mutable_retry()->mutable_recovery(),
              callbacks, current_context, options));
        } else {
          INTR_RETURN_IF_ERROR(VisitNode(containing_tree,
                                         node.retry().recovery(), callbacks,
                                         current_context, options));
        }
      }
      break;
    case intrinsic_proto::executive::BehaviorTree::Node::NodeTypeCase::kSubTree:
      if (node.sub_tree().has_tree()) {
        if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
          INTR_RETURN_IF_ERROR(
              VisitBehaviorTreeImpl(*node.mutable_sub_tree()->mutable_tree(),
                                    callbacks, current_context, options));
        } else {
          INTR_RETURN_IF_ERROR(VisitBehaviorTreeImpl(
              node.sub_tree().tree(), callbacks, current_context, options));
        }
      }
      break;
    case intrinsic_proto::executive::BehaviorTree::Node::NodeTypeCase::kData:
      break;
    case intrinsic_proto::executive::BehaviorTree::Node::NodeTypeCase::kDebug:
      break;
    case intrinsic_proto::executive::BehaviorTree::Node::NodeTypeCase::
        NODE_TYPE_NOT_SET:
      break;
  }
  return absl::OkStatus();
}

template <typename BehaviorTreeT, typename VisitBehaviorTreeCallbacksT>
  requires IsBehaviorTree<BehaviorTreeT> &&
           IsVisitorCallback<VisitBehaviorTreeCallbacksT> &&
           IsCompatible<BehaviorTreeT, VisitBehaviorTreeCallbacksT>
absl::Status VisitBehaviorTreeImpl(
    BehaviorTreeT& tree, VisitBehaviorTreeCallbacksT callbacks,
    const GetContextType<VisitBehaviorTreeCallbacksT>& parent_context,
    const VisitBehaviorTreeOptions& options) {
  const GetContextType<VisitBehaviorTreeCallbacksT>* current_context =
      &parent_context;
  // Storage for intermediate context (if visit_tree callback is set)
  std::optional<GetContextType<VisitBehaviorTreeCallbacksT>> visit_tree_context;
  if (callbacks.visit_tree) {
    INTR_ASSIGN_OR_RETURN(visit_tree_context,
                          callbacks.visit_tree(tree, parent_context));
    current_context = &*visit_tree_context;
  }

  if (tree.has_root()) {
    if constexpr (IsMutable<VisitBehaviorTreeCallbacksT>) {
      INTR_RETURN_IF_ERROR(VisitNode(tree, *tree.mutable_root(), callbacks,
                                     *current_context, options));
    } else {
      INTR_RETURN_IF_ERROR(
          VisitNode(tree, tree.root(), callbacks, *current_context, options));
    }
  }
  return absl::OkStatus();
}

}  // namespace

inline absl::Status VisitBehaviorTree(
    const intrinsic_proto::executive::BehaviorTree& tree,
    VisitBehaviorTreeCallbacks callbacks,
    const VisitBehaviorTreeOptions& options) {
  struct DummyContext {};
  auto visit_tree =
      [callbacks](const intrinsic_proto::executive::BehaviorTree& tree,
                  const DummyContext& unused) -> absl::StatusOr<DummyContext> {
    if (callbacks.visit_tree) {
      INTR_RETURN_IF_ERROR(callbacks.visit_tree(tree));
    }
    return DummyContext();
  };
  auto visit_node =
      [callbacks](const intrinsic_proto::executive::BehaviorTree& tree,
                  const intrinsic_proto::executive::BehaviorTree::Node& node,
                  const DummyContext& unused) -> absl::StatusOr<DummyContext> {
    if (callbacks.visit_node) {
      INTR_RETURN_IF_ERROR(callbacks.visit_node(tree, node));
    }
    return DummyContext();
  };
  auto visit_condition =
      [callbacks](
          const intrinsic_proto::executive::BehaviorTree& tree,
          const intrinsic_proto::executive::BehaviorTree::Condition& cond,
          const DummyContext& unused) -> absl::StatusOr<DummyContext> {
    if (callbacks.visit_condition) {
      INTR_RETURN_IF_ERROR(callbacks.visit_condition(tree, cond));
    }
    return DummyContext();
  };
  VisitBehaviorTreeContextCallbacks<DummyContext> context_callbacks = {
      .visit_tree = visit_tree,
      .visit_node = visit_node,
      .visit_condition = visit_condition};
  return VisitBehaviorTreeImpl(tree, context_callbacks, DummyContext(),
                               options);
}

inline absl::Status VisitBehaviorTree(
    intrinsic_proto::executive::BehaviorTree& tree,
    VisitMutableBehaviorTreeCallbacks callbacks,
    const VisitBehaviorTreeOptions& options) {
  struct DummyContext {};
  auto visit_tree =
      [callbacks](intrinsic_proto::executive::BehaviorTree& tree,
                  const DummyContext& unused) -> absl::StatusOr<DummyContext> {
    if (callbacks.visit_tree) {
      INTR_RETURN_IF_ERROR(callbacks.visit_tree(tree));
    }
    return DummyContext();
  };
  auto visit_node =
      [callbacks](intrinsic_proto::executive::BehaviorTree& tree,
                  intrinsic_proto::executive::BehaviorTree::Node& node,
                  const DummyContext& unused) -> absl::StatusOr<DummyContext> {
    if (callbacks.visit_node) {
      INTR_RETURN_IF_ERROR(callbacks.visit_node(tree, node));
    }
    return DummyContext();
  };
  auto visit_condition =
      [callbacks](intrinsic_proto::executive::BehaviorTree& tree,
                  intrinsic_proto::executive::BehaviorTree::Condition& cond,
                  const DummyContext& unused) -> absl::StatusOr<DummyContext> {
    if (callbacks.visit_condition) {
      INTR_RETURN_IF_ERROR(callbacks.visit_condition(tree, cond));
    }
    return DummyContext();
  };

  VisitMutableBehaviorTreeContextCallbacks<DummyContext> context_callbacks = {
      .visit_tree = visit_tree,
      .visit_node = visit_node,
      .visit_condition = visit_condition};
  return VisitBehaviorTreeImpl(tree, context_callbacks, DummyContext(),
                               options);
}

template <typename ContextT>
absl::Status VisitBehaviorTree(
    const intrinsic_proto::executive::BehaviorTree& tree,
    VisitBehaviorTreeContextCallbacks<ContextT> callbacks,
    const ContextT& initial_context, const VisitBehaviorTreeOptions& options) {
  return VisitBehaviorTreeImpl(tree, callbacks, initial_context, options);
}

template <typename ContextT>
absl::Status VisitBehaviorTree(
    intrinsic_proto::executive::BehaviorTree& tree,
    VisitMutableBehaviorTreeContextCallbacks<ContextT> callbacks,
    const ContextT& initial_context, const VisitBehaviorTreeOptions& options) {
  return VisitBehaviorTreeImpl(tree, callbacks, initial_context, options);
}

}  // namespace intrinsic::executive
