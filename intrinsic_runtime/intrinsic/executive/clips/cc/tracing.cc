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

#include "intrinsic/executive/clips/cc/tracing.h"

#include <cstdint>
#include <functional>
#include <string>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/proto/behavior_call.pb.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "opentelemetry/trace/span_metadata.h"

namespace intrinsic::executive {

namespace {

absl::StatusOr<clips::TraceSpanReferenceId> GetSpanIdFromSlot(
    const clips::Fact& fact, absl::string_view slot_name) {
  INTR_ASSIGN_OR_RETURN(clips::Value span_id_val,
                        fact.GetSlotValue(std::string(slot_name)));
  INTR_ASSIGN_OR_RETURN(int64_t span_id, span_id_val.GetInteger());
  clips::TraceSpanReferenceId span_reference_id(span_id);
  return span_reference_id;
}

void LogAndIgnoreError(const absl::Status& st) {
  if (!st.ok()) {
    LOG(ERROR) << st;
  }
}

absl::StatusOr<clips::TraceSpanReferenceId> GetExecutionSpanByTree(
    clips::Environment* absl_nonnull env, const clips::Fact& tree_fact)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  INTR_ASSIGN_OR_RETURN(clips::Value tree_operation_name,
                        tree_fact.GetSlotValue("operation-name"));
  INTR_ASSIGN_OR_RETURN(clips::Fact operation_fact,
                        env->GetUniqueFact("operation-envelope",
                                           {{"name", tree_operation_name}}));
  return GetSpanIdFromSlot(operation_fact, "span-reference-id");
}

// Determines the correct parent span for node with node_id in tree_id.
// The parent span might originate from another tree.
absl::StatusOr<clips::TraceSpanReferenceId> DetermineNodeParentSpan(
    clips::Environment* absl_nonnull env, absl::string_view tree_id,
    uint32_t node_id, uint32_t parent_node_id, absl::string_view node_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  INTR_ASSIGN_OR_RETURN(
      clips::Fact tree_fact,
      env->GetUniqueFact("behavior-tree", {{"id", clips::Symbol(tree_id)}}));

  // If the node is the start-node of the tree, the parent span is the enclosing
  // tree's span reference ID.
  INTR_ASSIGN_OR_RETURN(clips::Value start_node_id_val,
                        tree_fact.GetSlotValue("start-node-id"));
  INTR_ASSIGN_OR_RETURN(uint32_t start_node_id, start_node_id_val.GetInteger());
  if (start_node_id == node_id) {
    VLOG(1) << "For node " << tree_id << ":" << node_id
            << " using tree span as parent span for start node";
    return GetSpanIdFromSlot(tree_fact, "span-reference-id");
  }

  // Initially check, if node is the child of another node in the same tree
  if (parent_node_id != 0) {
    INTR_ASSIGN_OR_RETURN(
        clips::Fact parent_node_fact,
        env->GetUniqueFact(
            "behavior-tree-node",
            {{"tree-id", clips::Symbol(tree_id)}, {"id", parent_node_id}}));
    INTR_ASSIGN_OR_RETURN(clips::Value parent_node_type,
                          parent_node_fact.GetSlotValue("type"));
    if (parent_node_type == clips::Symbol("LOOP") ||
        parent_node_type == clips::Symbol("RETRY")) {
      VLOG(1) << "For node " << tree_id << ":" << node_id
              << " using node iteration span as parent span for loop/retry "
                 "iteration";
      return GetSpanIdFromSlot(parent_node_fact, "iteration-span-reference-id");
    }
    VLOG(1) << "For node " << tree_id << ":" << node_id
            << " using node span as parent span";
    return GetSpanIdFromSlot(parent_node_fact, "span-reference-id");
  }

  // At this point node's parent-id must be 0. The other case is handled before.
  // Take the enclosing tree's span reference ID.
  // Given this is the root node of a tree and it is being executed it should
  // have been the start-node-id.
  LOG(WARNING)
      << "Node with id " << node_id << " in " << tree_id
      << " (name: " << node_name
      << ") and no parent requested parent span, but is not the start-node";
  VLOG(1) << "For node " << tree_id << ":" << node_id
          << " using tree span as fallback parent span";
  return GetSpanIdFromSlot(tree_fact, "span-reference-id");
}

// Create the name for a node span given the node's name, type and possibly
// the action-name for task nodes.
std::string CreateNodeSpanName(absl::string_view node_name,
                               absl::string_view node_type,
                               absl::string_view action_name) {
  // Spans always identify the node type
  std::string span_name = std::string(node_type);
  if (!span_name.empty()) {
    // capitalize the name, e.g., sequence -> Sequence
    span_name[0] = absl::ascii_toupper(span_name[0]);
  }
  // For Task nodes: Append the action name to the span name
  if (!action_name.empty()) {
    absl::StrAppend(&span_name, " (", action_name, ")");
  }
  // If a node name is set, include it in the span name
  if (!node_name.empty()) {
    absl::StrAppend(&span_name, " \"", node_name, "\"");
  }
  return span_name;
}

}  // namespace

absl::StatusOr<clips::TraceSpanReferenceId> StartTreeSpan(
    clips::Environment* absl_nonnull env,
    clips::TraceSpanManager* absl_nonnull span_mgr, absl::string_view tree_id,
    clips::TraceSpanReferenceId parent_span_id)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  INTR_ASSIGN_OR_RETURN(
      clips::Fact tree_fact,
      env->GetUniqueFact("behavior-tree", {{"id", clips::Symbol(tree_id)}}),
      _.LogError());

  INTR_ASSIGN_OR_RETURN(clips::TraceSpanReferenceId execution_span_id,
                        GetExecutionSpanByTree(env, tree_fact));
  // No execution span -> any span ID would be meaningless
  if (execution_span_id ==
      clips::TraceSpanManager::kInvalidTraceSpanReferenceId) {
    return absl::FailedPreconditionError("Execution span does not exist");
  }

  // No parent span ID given, use execution span
  if (parent_span_id == clips::TraceSpanManager::kInvalidTraceSpanReferenceId) {
    parent_span_id = execution_span_id;
  }

  INTR_ASSIGN_OR_RETURN(clips::TraceSpanReferenceId current_tree_span_id,
                        GetSpanIdFromSlot(tree_fact, "span-reference-id"));

  // Trying to start a span, but there is already one running
  if (current_tree_span_id !=
      clips::TraceSpanManager::kInvalidTraceSpanReferenceId) {
    std::string message = absl::StrFormat(
        "Starting a tree span had an active tracing "
        "span with id %d. The span will be ended by force.",
        current_tree_span_id.value());
    LOG(ERROR) << message;
    LogAndIgnoreError(
        span_mgr->EndSpan(current_tree_span_id,
                          opentelemetry::trace::StatusCode::kError, message));
  }

  INTR_ASSIGN_OR_RETURN(clips::Value tree_name_val,
                        tree_fact.GetSlotValue("name"));
  INTR_ASSIGN_OR_RETURN(std::string tree_name, tree_name_val.GetString());

  std::string span_name =
      absl::StrCat("Tree ", tree_name.empty() ? tree_id : tree_name);
  INTR_ASSIGN_OR_RETURN(
      clips::TraceSpanReferenceId tree_span_id,
      span_mgr->StartSpan(span_name, "tree", parent_span_id),
      _ << "Failed to start span for tree with id " << tree_id);
  if (!tree_name.empty()) {
    LogAndIgnoreError(
        span_mgr->AddSpanAttribute(tree_span_id, "bt_tree_name", tree_name));
  }
  if (!tree_id.empty()) {
    LogAndIgnoreError(
        span_mgr->AddSpanAttribute(tree_span_id, "bt_tree_id", tree_id));
  }

  return tree_span_id;
}

absl::StatusOr<clips::TraceSpanReferenceId> StartNodeSpan(
    clips::Environment* absl_nonnull env,
    clips::ProtobufManager* absl_nonnull proto_mgr,
    clips::TraceSpanManager* absl_nonnull span_mgr, absl::string_view tree_id,
    uint32_t node_id) ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  INTR_ASSIGN_OR_RETURN(
      clips::Fact tree_fact,
      env->GetUniqueFact("behavior-tree", {{"id", clips::Symbol(tree_id)}}),
      _.LogError());
  INTR_ASSIGN_OR_RETURN(clips::Fact node_fact,
                        env->GetUniqueFact("behavior-tree-node",
                                           {{"tree-id", clips::Symbol(tree_id)},
                                            {"id", clips::Value(node_id)}}),
                        _.LogError());
  INTR_ASSIGN_OR_RETURN(clips::Value node_name_val,
                        node_fact.GetSlotValue("name"));
  INTR_ASSIGN_OR_RETURN(std::string node_name, node_name_val.GetString());
  INTR_ASSIGN_OR_RETURN(clips::Value node_type, node_fact.GetSlotValue("type"));
  INTR_ASSIGN_OR_RETURN(clips::Value node_parent_id_val,
                        node_fact.GetSlotValue("parent-id"));
  INTR_ASSIGN_OR_RETURN(uint32_t node_parent_id,
                        node_parent_id_val.GetInteger());

  INTR_ASSIGN_OR_RETURN(clips::TraceSpanReferenceId execution_span_id,
                        GetExecutionSpanByTree(env, tree_fact));
  // No execution span -> any span ID would be meaningless
  if (execution_span_id ==
      clips::TraceSpanManager::kInvalidTraceSpanReferenceId) {
    return absl::FailedPreconditionError("Execution span does not exist");
  }

  INTR_ASSIGN_OR_RETURN(clips::TraceSpanReferenceId current_node_span_id,
                        GetSpanIdFromSlot(node_fact, "span-reference-id"));
  // Trying to start a span, but there is already one running
  if (current_node_span_id !=
      clips::TraceSpanManager::kInvalidTraceSpanReferenceId) {
    INTR_ASSIGN_OR_RETURN(clips::Value retry_num_tries_val,
                          node_fact.GetSlotValue("retry-num-tries"));
    INTR_ASSIGN_OR_RETURN(int64_t retry_num_tries,
                          retry_num_tries_val.GetInteger());
    if (node_type == clips::Symbol("RETRY") && retry_num_tries > 0) {
      //  This is a retry node that selected itself again after the child had
      //  failed. It is performing a retry (indicated by retry-num-tries > 0).
      //  In this case, the running span is the span started during the first
      //  retry, when retry-num-tries == 0. We keep this span active.
      VLOG(1) << "For node " << tree_id << ":" << node_id
              << " using current span for retry node";
      return current_node_span_id;
    }

    std::string message = absl::StrFormat(
        "Starting a node span had an active tracing "
        "span with id %d. The span will be ended by force.",
        current_node_span_id.value());
    LOG(ERROR) << message;
    LogAndIgnoreError(
        span_mgr->EndSpan(current_node_span_id,
                          opentelemetry::trace::StatusCode::kError, message));
  }

  INTR_ASSIGN_OR_RETURN(
      clips::TraceSpanReferenceId parent_span_id,
      DetermineNodeParentSpan(env, tree_id, node_id, node_parent_id, node_name),
      _ << " Could not determine parent span for node " << node_id << " in "
        << tree_id << " with name: " << node_name);
  // There is an execution span, thus we expect a valid parent span
  if (parent_span_id == clips::TraceSpanManager::kInvalidTraceSpanReferenceId) {
    return absl::FailedPreconditionError(absl::StrFormat(
        "Invalid parent span for node with id %d in %s", node_id, tree_id));
  }

  // All information to be able to create a node span is available. Now gather
  // additional attributes to attach to the span.
  std::string action_name;
  int64_t action_id = 0;
  std::string action_prototype_plan_id;
  std::string action_plan_id;
  if (node_type == clips::Symbol("TASK")) {
    // Inspect things called by the task node. It is not an error if these do
    // not exist.
    INTR_ASSIGN_OR_RETURN(clips::Value task_action_prototype_uid,
                          node_fact.GetSlotValue("task-action-prototype-uid"));
    absl::StatusOr<clips::Fact> plan_action_fact =
        env->GetUniqueFact("plan-action", {{"uid", task_action_prototype_uid}});
    if (plan_action_fact.ok()) {
      INTR_ASSIGN_OR_RETURN(clips::Value action_name_val,
                            plan_action_fact->GetSlotValue("skill-id"));
      INTR_ASSIGN_OR_RETURN(action_name, action_name_val.GetString());
      INTR_ASSIGN_OR_RETURN(clips::Value action_id_val,
                            plan_action_fact->GetSlotValue("id"));
      INTR_ASSIGN_OR_RETURN(action_id, action_id_val.GetInteger());
      INTR_ASSIGN_OR_RETURN(clips::Value action_prototype_plan_id_val,
                            plan_action_fact->GetSlotValue("plan-id"));
      INTR_ASSIGN_OR_RETURN(action_prototype_plan_id,
                            action_prototype_plan_id_val.GetSymbolAsString());
    }

    INTR_ASSIGN_OR_RETURN(clips::Value behavior_call_instance_uid,
                          node_fact.GetSlotValue("behavior-call-instance-uid"));
    absl::StatusOr<clips::Fact> behavior_call_instance_fact =
        env->GetUniqueFact("behavior-call-instance",
                           {{"uid", behavior_call_instance_uid}});
    if (behavior_call_instance_fact.ok()) {
      INTR_ASSIGN_OR_RETURN(clips::Value behavior_call_prototype_proto_val,
                            behavior_call_instance_fact->GetSlotValue(
                                "behavior-call-prototype-proto"));
      INTR_ASSIGN_OR_RETURN(int64_t behavior_call_prototype_proto_id,
                            behavior_call_prototype_proto_val.GetInteger());
      INTR_ASSIGN_OR_RETURN(
          clips::Value skill_id_val,
          proto_mgr->GetFieldValue(
              clips::ProtoMessageId(behavior_call_prototype_proto_id),
              "skill_id"));
      INTR_ASSIGN_OR_RETURN(action_name, skill_id_val.GetString());
    }

    INTR_ASSIGN_OR_RETURN(clips::Value plan_id_val,
                          tree_fact.GetSlotValue("plan-id"));
    INTR_ASSIGN_OR_RETURN(action_plan_id, plan_id_val.GetSymbolAsString());
  }

  INTR_ASSIGN_OR_RETURN(std::string node_type_lower,
                        node_type.GetSymbolAsString());
  absl::AsciiStrToLower(&node_type_lower);
  //  (bind ?node-type (lowcase (str-cat (fact-slot-value ?node type))))
  // For subtree nodes: Choose the name of the subtree, if set
  if (node_name.empty() && node_type == clips::Symbol("SUB-TREE")) {
    INTR_ASSIGN_OR_RETURN(clips::Value sub_tree_id,
                          node_fact.GetSlotValue("sub-tree-id"));
    INTR_ASSIGN_OR_RETURN(
        clips::Fact sub_tree_fact,
        env->GetUniqueFact("behavior-tree", {{"id", sub_tree_id}}));
    INTR_ASSIGN_OR_RETURN(clips::Value sub_tree_name_val,
                          sub_tree_fact.GetSlotValue("name"));
    INTR_ASSIGN_OR_RETURN(node_name, sub_tree_name_val.GetString());
  }

  std::string span_name =
      CreateNodeSpanName(node_name, node_type_lower, action_name);

  INTR_ASSIGN_OR_RETURN(
      clips::TraceSpanReferenceId node_span_id,
      span_mgr->StartSpan(span_name, node_type_lower, parent_span_id),
      _ << "Failed to start span for node with id " << node_id << " in "
        << tree_id);

  if (!node_name.empty()) {
    LogAndIgnoreError(
        span_mgr->AddSpanAttribute(node_span_id, "bt_node_name", node_name));
  }
  if (!action_name.empty()) {
    LogAndIgnoreError(
        span_mgr->AddSpanAttribute(node_span_id, "action_name", action_name));
    LogAndIgnoreError(
        span_mgr->AddSpanAttribute(node_span_id, "skill_id", action_name));
  }
  if (action_id != 0) {
    LogAndIgnoreError(
        span_mgr->AddSpanAttribute(node_span_id, "action_id", action_id));
  }
  if (!action_prototype_plan_id.empty()) {
    LogAndIgnoreError(span_mgr->AddSpanAttribute(
        node_span_id, "action_prototype_plan_id", action_prototype_plan_id));
  }
  if (!action_plan_id.empty()) {
    LogAndIgnoreError(span_mgr->AddSpanAttribute(node_span_id, "action_plan_id",
                                                 action_plan_id));
  }
  LogAndIgnoreError(
      span_mgr->AddSpanAttribute(node_span_id, "bt_tree_id", tree_id));
  LogAndIgnoreError(
      span_mgr->AddSpanAttribute(node_span_id, "bt_node_id", node_id));

  return node_span_id;
}

absl::StatusOr<clips::TraceSpanReferenceId> StartNodeIterationSpan(
    clips::Environment* absl_nonnull env,
    clips::TraceSpanManager* absl_nonnull span_mgr, absl::string_view tree_id,
    uint32_t node_id, int iteration)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  INTR_ASSIGN_OR_RETURN(
      clips::Fact tree_fact,
      env->GetUniqueFact("behavior-tree", {{"id", clips::Symbol(tree_id)}}),
      _.LogError());
  INTR_ASSIGN_OR_RETURN(clips::Fact node_fact,
                        env->GetUniqueFact("behavior-tree-node",
                                           {{"tree-id", clips::Symbol(tree_id)},
                                            {"id", clips::Value(node_id)}}),
                        _.LogError());

  INTR_ASSIGN_OR_RETURN(clips::TraceSpanReferenceId execution_span_id,
                        GetExecutionSpanByTree(env, tree_fact));
  // No execution span -> any span ID would be meaningless
  if (execution_span_id ==
      clips::TraceSpanManager::kInvalidTraceSpanReferenceId) {
    return absl::FailedPreconditionError("Execution span does not exist");
  }

  INTR_ASSIGN_OR_RETURN(
      clips::TraceSpanReferenceId current_iteration_span_id,
      GetSpanIdFromSlot(node_fact, "iteration-span-reference-id"));
  // In the context of an iteration the node's span is the parent of an
  // iteration span as a node has one span that engulfes the N iterations.
  INTR_ASSIGN_OR_RETURN(clips::TraceSpanReferenceId parent_span_id,
                        GetSpanIdFromSlot(node_fact, "span-reference-id"));

  // There is an execution span, thus we expect a valid parent span
  if (parent_span_id == clips::TraceSpanManager::kInvalidTraceSpanReferenceId) {
    return absl::FailedPreconditionError(
        absl::StrFormat("Iteration: Invalid node span in node with id %d in %s",
                        node_id, tree_id));
  }

  // Trying to start a span, but there is already one running
  if (current_iteration_span_id !=
      clips::TraceSpanManager::kInvalidTraceSpanReferenceId) {
    std::string message = absl::StrFormat(
        "Starting an iteration span had an active tracing "
        "span with id %d. The span will be ended by force.",
        current_iteration_span_id.value());
    LOG(ERROR) << message;
    LogAndIgnoreError(
        span_mgr->EndSpan(current_iteration_span_id,
                          opentelemetry::trace::StatusCode::kError, message));
  }

  INTR_ASSIGN_OR_RETURN(clips::Value node_type, node_fact.GetSlotValue("type"));
  INTR_ASSIGN_OR_RETURN(std::string node_type_lower,
                        node_type.GetSymbolAsString());
  absl::AsciiStrToLower(&node_type_lower);
  INTR_ASSIGN_OR_RETURN(clips::Value node_name_val,
                        node_fact.GetSlotValue("name"));
  INTR_ASSIGN_OR_RETURN(std::string node_name, node_name_val.GetString());

  std::string span_name = CreateNodeSpanName(node_name, node_type_lower, "");

  absl::StrAppend(&node_type_lower, "-iteration");
  absl::StrAppend(&span_name, " (iteration)");

  INTR_ASSIGN_OR_RETURN(
      clips::TraceSpanReferenceId iteration_span_id,
      span_mgr->StartSpan(span_name, node_type_lower, parent_span_id),
      _ << "Failed to start span for node iteration with id " << node_id
        << " in " << tree_id);

  if (!node_name.empty()) {
    LogAndIgnoreError(span_mgr->AddSpanAttribute(iteration_span_id,
                                                 "bt_node_name", node_name));
  }
  LogAndIgnoreError(
      span_mgr->AddSpanAttribute(iteration_span_id, "iteration", iteration));
  return iteration_span_id;
}

absl::Status AddClipsTracingFunctions(
    clips::Environment* absl_nonnull env,
    clips::ProtobufManager* absl_nonnull proto_mgr,
    clips::TraceSpanManager* absl_nonnull span_mgr)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  INTR_RETURN_IF_ERROR(env->AddFunction(
      "tracing-cc-start-tree-span",
      std::function([env, span_mgr](const std::string& tree_id,
                                    int64_t parent_span_id) -> clips::Value {
        env->mutex()->AssertHeld();
        INTR_ASSIGN_OR_RETURN(
            clips::TraceSpanReferenceId tree_span_id,
            StartTreeSpan(env, span_mgr, tree_id,
                          clips::TraceSpanReferenceId(parent_span_id)),
            _.LogError().With(Return(clips::Value(
                clips::TraceSpanManager::kInvalidTraceSpanReferenceId
                    .value()))));

        return clips::Value(tree_span_id.value());
      })));
  INTR_RETURN_IF_ERROR(env->AddFunction(
      "tracing-cc-start-node-span",
      std::function(
          [env, proto_mgr, span_mgr](const std::string& tree_id,
                                     int64_t node_id) -> clips::Value {
            env->mutex()->AssertHeld();
            INTR_ASSIGN_OR_RETURN(
                clips::TraceSpanReferenceId tree_span_id,
                StartNodeSpan(env, proto_mgr, span_mgr, tree_id, node_id),
                _.LogError().With(Return(clips::Value(
                    clips::TraceSpanManager::kInvalidTraceSpanReferenceId
                        .value()))));

            return clips::Value(tree_span_id.value());
          })));
  INTR_RETURN_IF_ERROR(env->AddFunction(
      "tracing-cc-start-node-iteration-span",
      std::function([env, span_mgr](const std::string& tree_id, int64_t node_id,
                                    int64_t iteration) -> clips::Value {
        env->mutex()->AssertHeld();
        INTR_ASSIGN_OR_RETURN(
            clips::TraceSpanReferenceId tree_span_id,
            StartNodeIterationSpan(env, span_mgr, tree_id, node_id, iteration),
            _.LogError().With(Return(clips::Value(
                clips::TraceSpanManager::kInvalidTraceSpanReferenceId
                    .value()))));

        return clips::Value(tree_span_id.value());
      })));

  return absl::OkStatus();
}

}  // namespace intrinsic::executive
