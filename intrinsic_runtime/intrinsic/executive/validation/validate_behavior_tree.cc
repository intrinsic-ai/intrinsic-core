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

#include "intrinsic/executive/validation/validate_behavior_tree.h"

#include <string>
#include <string_view>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "intrinsic_runtime/intrinsic/proto_tools/compatibility/proto_compat.h"
#include "intrinsic/executive/cc/behavior_tree_visitor.h"
#include "intrinsic/executive/clips/cc/skill_info.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/engine/skill_client_generator.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_specs.h"

using intrinsic_proto::executive::BehaviorTree;
using intrinsic_proto::status::ExtendedStatus;

namespace intrinsic::executive {

namespace {

void SetRelations(ExtendedStatus& es, const BehaviorTree& tree) {
  es.mutable_related_to()->mutable_behavior_tree_node()->set_tree_id(
      tree.tree_id());
}

void SetRelations(ExtendedStatus& es, const BehaviorTree& tree,
                  const BehaviorTree::Node& node) {
  SetRelations(es, tree);
  es.mutable_related_to()->mutable_behavior_tree_node()->set_node_id(node.id());
}

std::string ProcessName(const BehaviorTree& tree, bool is_process_tree) {
  if (is_process_tree) {
    return tree.description().id().empty()
               ? "(process tree)"
               : absl::StrCat(tree.description().id(), " (process tree)");
  }
  // tree.description().id() must be set
  return tree.description().id();
}

std::string TreeName(const BehaviorTree& tree, bool is_process_tree) {
  std::string process_name = ProcessName(tree, is_process_tree);
  if (tree.name().empty()) {
    if (tree.tree_id().empty()) {
      return absl::StrCat(" in ", process_name);
    } else {
      return absl::StrCat("(", tree.tree_id(), ") in ", process_name);
    }
  }
  return absl::StrCat("'", tree.name(), "' in ", process_name);
}

std::string NodeName(const BehaviorTree& tree, const BehaviorTree::Node& node,
                     bool is_process_tree) {
  std::string process_name = ProcessName(tree, is_process_tree);
  if (node.name().empty() && node.id() != 0) {
    return absl::StrCat("(", node.id(), ") in ", process_name);
  }
  return absl::StrCat("'", node.name(), "' in ", process_name);
}

// Context for behavior tree visitor callbacks which perform validation.
struct ValidateContext {
  bool disabled;
  bool is_process_tree;

  ValidateContext CreateContextForChildNode(
      const BehaviorTree::Node& child_node) const {
    ValidateContext child_context = *this;
    if (child_node.decorators().has_execution_settings()) {
      if (child_node.decorators().execution_settings().mode() ==
          BehaviorTree::Node::ExecutionSettings::DISABLED) {
        child_context.disabled = true;
      }
    }
    return child_context;
  }
};

struct BehaviorTreeDependencies {
  // Asset ids from any BehaviorCall
  absl::flat_hash_set<std::string> asset_ids;
  // Asset ids from a BehaviorCall that are in a node that is enabled (i.e. not
  // disabled or a child of a disabled node)
  absl::flat_hash_set<std::string> enabled_asset_ids;
  // Asset ids from a BehaviorCall that are in a node that is disabled (or a
  // child of a disabled node)
  absl::flat_hash_set<std::string> disabled_asset_ids;
};
// Extracts asset ids for all dependencies called via a BehaviorCall.
absl::StatusOr<BehaviorTreeDependencies> GetAssetDependencies(
    const intrinsic_proto::executive::BehaviorTree& tree) {
  BehaviorTreeDependencies deps;

  auto collect_ids = [&deps](const BehaviorTree& tree,
                             const BehaviorTree::Node& node, bool disabled) {
    if (node.decorators().has_execution_settings()) {
      if (node.decorators().execution_settings().mode() ==
          BehaviorTree::Node::ExecutionSettings::DISABLED) {
        disabled = true;
      }
    }
    if (node.task().has_call_behavior() &&
        !node.task().call_behavior().skill_id().empty()) {
      deps.asset_ids.insert(node.task().call_behavior().skill_id());
      if (disabled) {
        deps.disabled_asset_ids.insert(node.task().call_behavior().skill_id());
      } else {
        deps.enabled_asset_ids.insert(node.task().call_behavior().skill_id());
      }
    }
    return disabled;
  };

  INTR_RETURN_IF_ERROR(VisitBehaviorTree(tree,
                                         {
                                             .visit_node = collect_ids,
                                         },
                                         false));

  return deps;
}

// Determines, which of the asset_dependencies are processes and returns only
// these.
// Will fail if the type of dependency cannot be determined from the
// skill_client_generator.
absl::StatusOr<BehaviorTreeDependencies> GetProcessDependencies(
    const BehaviorTree& tree, SkillClientGenerator& skill_client_generator,
    const absl::flat_hash_set<std::string>& ignore_asset_ids,
    absl::flat_hash_set<std::string>& missing_dependencies) {
  // Skill or process IDs that do not exist
  auto filter_process_dependencies =
      [&missing_dependencies, &skill_client_generator,
       &ignore_asset_ids](const absl::flat_hash_set<std::string>& asset_ids)
      -> absl::StatusOr<absl::flat_hash_set<std::string>> {
    absl::flat_hash_set<std::string> process_deps;
    for (const std::string& skill_id : asset_ids) {
      if (ignore_asset_ids.contains(skill_id)) {
        continue;
      }
      absl::StatusOr<SkillClientGenerator::SkillRegistration> skill =
          skill_client_generator.GetSkillRegistration(skill_id);
      if (skill.status().code() == absl::StatusCode::kNotFound) {
        // This skill is unknown.
        missing_dependencies.insert(skill_id);
        continue;
      }
      INTR_RETURN_IF_ERROR(skill.status());

      if (skill->is_process) {
        process_deps.insert(skill_id);
      }
    }
    return process_deps;
  };

  INTR_ASSIGN_OR_RETURN(BehaviorTreeDependencies asset_dependencies,
                        GetAssetDependencies(tree));

  BehaviorTreeDependencies process_dependencies;
  INTR_ASSIGN_OR_RETURN(
      process_dependencies.asset_ids,
      filter_process_dependencies(asset_dependencies.asset_ids));
  INTR_ASSIGN_OR_RETURN(
      process_dependencies.enabled_asset_ids,
      filter_process_dependencies(asset_dependencies.enabled_asset_ids));
  INTR_ASSIGN_OR_RETURN(
      process_dependencies.disabled_asset_ids,
      filter_process_dependencies(asset_dependencies.disabled_asset_ids));

  return process_dependencies;
}

// Gets process dependencies for tree recursively, i.e., following reusable tree
// calls.
// Dependencies must exist in the SkillClientGenerator and there must not be
// dependency cycles in tree.
absl::Status GetProcessDependenciesRecursively(
    const BehaviorTree& tree, bool is_process_tree, bool disabled,
    SkillClientGenerator& skill_client_generator,
    const absl::flat_hash_set<std::string>& ignore_asset_ids,
    BehaviorTreeDependencies& recursive_dependencies) {
  if (!is_process_tree) {
    recursive_dependencies.asset_ids.insert(tree.description().id());
    if (disabled) {
      recursive_dependencies.disabled_asset_ids.insert(tree.description().id());
    } else {
      recursive_dependencies.enabled_asset_ids.insert(tree.description().id());
    }
  }

  //  Skill or process IDs that do not exist
  absl::flat_hash_set<std::string> missing_dependencies;
  INTR_ASSIGN_OR_RETURN(
      BehaviorTreeDependencies process_dependencies,
      GetProcessDependencies(tree, skill_client_generator, ignore_asset_ids,
                             missing_dependencies));
  if (!missing_dependencies.empty()) {
    // This should not happen in this context
    return absl::InternalError(absl::StrFormat(
        "The following skill or process dependencies were "
        "missing: [%s] being called from process '%s'.",
        absl::StrJoin(missing_dependencies, ", "), tree.description().id()));
  }

  for (const std::string& process_dependency :
       process_dependencies.enabled_asset_ids) {
    INTR_ASSIGN_OR_RETURN(
        BehaviorTree recursive_tree,
        skill_client_generator.GetAndCacheBehaviorTree(process_dependency));
    INTR_RETURN_IF_ERROR(GetProcessDependenciesRecursively(
        recursive_tree, /*is_process_tree=*/false, disabled,
        skill_client_generator, ignore_asset_ids, recursive_dependencies));
  }
  for (const std::string& process_dependency :
       process_dependencies.disabled_asset_ids) {
    INTR_ASSIGN_OR_RETURN(
        BehaviorTree recursive_tree,
        skill_client_generator.GetAndCacheBehaviorTree(process_dependency));
    INTR_RETURN_IF_ERROR(GetProcessDependenciesRecursively(
        recursive_tree, /*is_process_tree=*/false, /*disabled=*/true,
        skill_client_generator, ignore_asset_ids, recursive_dependencies));
  }

  return absl::OkStatus();
}

// Recursive call for ValidateBehaviorTreeDependencies. Tracks the process
// ids that have been calling this process in call_chain_set. Call chain is a
// string "process_id -> reusable_process_id -> reusable_process_id_2".
// Found issues are collected in issues.
absl::Status ValidateBehaviorTreeDependenciesRecursively(
    const BehaviorTree& tree, SkillClientGenerator& skill_client_generator,
    absl::flat_hash_set<std::string> call_chain_set, std::string call_chain,
    const absl::flat_hash_set<std::string>& ignore_asset_ids,
    std::vector<intrinsic_proto::status::ExtendedStatus>& issues) {
  //  Skill or process IDs that do not exist
  absl::flat_hash_set<std::string> missing_dependencies;
  INTR_ASSIGN_OR_RETURN(
      BehaviorTreeDependencies process_dependencies,
      GetProcessDependencies(tree, skill_client_generator, ignore_asset_ids,
                             missing_dependencies));

  if (!missing_dependencies.empty()) {
    ExtendedStatus dep_es = CreateExtendedStatus(
        99002,
        absl::StrFormat("The following skill or process dependencies were "
                        "missing: [%s] being called from '%s'.",
                        absl::StrJoin(missing_dependencies, ", "), call_chain),
        {.severity = ExtendedStatus::ERROR});
    issues.push_back(dep_es);
    return absl::OkStatus();
  }

  absl::flat_hash_set<std::string> cycles;
  for (const std::string& process_dependency : process_dependencies.asset_ids) {
    if (call_chain_set.contains(process_dependency)) {
      cycles.insert(process_dependency);
    }
  }
  if (!cycles.empty()) {
    // sort cycles for consistent output
    std::vector<std::string> sorted_cycles;
    absl::c_copy(cycles, std::back_inserter(sorted_cycles));
    absl::c_sort(sorted_cycles);
    absl::StrAppend(&call_chain, " -> [", absl::StrJoin(sorted_cycles, ", "),
                    "]");
    ExtendedStatus cycle_es = CreateExtendedStatus(
        99003, absl::StrFormat("Cyclic dependency found: %s", call_chain),
        {.severity = ExtendedStatus::ERROR});
    issues.push_back(cycle_es);
    return absl::OkStatus();
  }

  for (const std::string& process_dependency : process_dependencies.asset_ids) {
    std::string recursive_call_chain =
        absl::StrCat(call_chain, " -> ", process_dependency);
    absl::flat_hash_set<std::string> recursive_call_chain_set = call_chain_set;
    recursive_call_chain_set.insert(process_dependency);
    INTR_ASSIGN_OR_RETURN(
        BehaviorTree recursive_tree,
        skill_client_generator.GetAndCacheBehaviorTree(process_dependency));
    INTR_RETURN_IF_ERROR(ValidateBehaviorTreeDependenciesRecursively(
        recursive_tree, skill_client_generator, recursive_call_chain_set,
        recursive_call_chain, ignore_asset_ids, issues));
  }

  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::vector<ExtendedStatus>> ValidateBehaviorTreeDependencies(
    const BehaviorTree& tree, SkillClientGenerator& skill_client_generator,
    const absl::flat_hash_set<std::string>& ignore_asset_ids) {
  absl::flat_hash_set<std::string> call_chain_set;
  std::string call_chain = "(process tree)";
  if (!tree.description().id().empty()) {
    call_chain_set.insert(tree.description().id());
    call_chain = absl::StrCat(tree.description().id(), " (process tree)");
  }
  std::vector<ExtendedStatus> issues;
  INTR_RETURN_IF_ERROR(ValidateBehaviorTreeDependenciesRecursively(
      tree, skill_client_generator, call_chain_set, call_chain,
      ignore_asset_ids, issues));

  return issues;
}

namespace {

// Visits the given behavior tree proto and all of its called sub-processes
// (recursively) using the given validation callbacks.
//
// Call chains must not have cycles and processes/skills are expected to be
// available.
absl::Status VisitAllBehaviorTrees(
    const intrinsic_proto::executive::BehaviorTree& tree,
    const VisitBehaviorTreeContextCallbacks<ValidateContext>&
        validation_callbacks,
    SkillClientGenerator& skill_client_generator,
    const absl::flat_hash_set<std::string>& ignore_asset_ids) {
  BehaviorTreeDependencies all_dependencies;
  INTR_RETURN_IF_ERROR(GetProcessDependenciesRecursively(
      tree, /*is_process_tree=*/true, /*disabled=*/false,
      skill_client_generator, ignore_asset_ids, all_dependencies));

  // Validate the process tree separately. There is no guarantee that a process
  // tree with the same id as a sub-process is the same proto as it's sent by
  // the user.
  INTR_RETURN_IF_ERROR(
      VisitBehaviorTree(tree, validation_callbacks,
                        {.disabled = false, .is_process_tree = true}));

  // Validate called processes
  for (const std::string& process_id : all_dependencies.asset_ids) {
    INTR_ASSIGN_OR_RETURN(
        BehaviorTree dep_tree,
        skill_client_generator.GetAndCacheBehaviorTree(process_id));

    // If a process appears both enabled and disabled, validate it once while
    // treating it as enabled (and generate errors, not just warnings).
    bool enabled = all_dependencies.enabled_asset_ids.contains(process_id);
    INTR_RETURN_IF_ERROR(
        VisitBehaviorTree(dep_tree, validation_callbacks,
                          {.disabled = !enabled, .is_process_tree = false}));
  }

  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::vector<intrinsic_proto::status::ExtendedStatus>>
ValidateBehaviorTreePreImport(
    const intrinsic_proto::executive::BehaviorTree& tree,
    SkillClientGenerator& skill_client_generator,
    const absl::flat_hash_set<std::string>& ignore_asset_ids,
    bool test_skip_parameter_validation) {
  std::vector<ExtendedStatus> issues;

  auto validate_tree = [&issues](const BehaviorTree& tree,
                                 const ValidateContext& context) {
    ExtendedStatus::Severity error_severity =
        context.disabled ? ExtendedStatus::WARNING : ExtendedStatus::ERROR;
    std::string tree_name = TreeName(tree, context.is_process_tree);

    // Tree must have a root
    if (!tree.has_root()) {
      ExtendedStatus issue = CreateExtendedStatus(
          99011, absl::StrFormat("Tree %s has no root node", tree_name),
          {.severity = error_severity});
      SetRelations(issue, tree);
      issues.push_back(issue);
    }
    return context;
  };
  auto validate_node =
      [&issues, &skill_client_generator, test_skip_parameter_validation](
          const BehaviorTree& tree, const BehaviorTree::Node& node,
          const ValidateContext& parent_context)
      -> absl::StatusOr<ValidateContext> {
    ValidateContext context = parent_context.CreateContextForChildNode(node);
    ExtendedStatus::Severity error_severity =
        context.disabled ? ExtendedStatus::WARNING : ExtendedStatus::ERROR;
    std::string node_name = NodeName(tree, node, context.is_process_tree);

    // Node without a type
    if (node.node_type_case() == BehaviorTree::Node::NODE_TYPE_NOT_SET) {
      ExtendedStatus issue = CreateExtendedStatus(
          99012, absl::StrFormat("Node %s does not have a type", node_name),
          {.severity = error_severity});
      SetRelations(issue, tree, node);
      issues.push_back(issue);
    }

    // Skill/process calls must have valid parameters
    if (node.task().has_call_behavior()) {
      if (node.task().call_behavior().skill_id().empty()) {
        ExtendedStatus issue = CreateExtendedStatus(
            99010,
            absl::StrFormat("Task node %s does not have a skill_id set",
                            node_name),
            {.severity = error_severity});
        SetRelations(issue, tree, node);
        issues.push_back(issue);
      } else if (!test_skip_parameter_validation) {
        INTR_ASSIGN_OR_RETURN(SkillClientGenerator::SkillRegistration skill,
                              skill_client_generator.GetSkillRegistration(
                                  node.task().call_behavior().skill_id()));

        if (!node.task().call_behavior().has_parameters()) {
          if (!skill.parameter_message_full_name.empty()) {
            // No parameters in call but skill requires parameters
            ExtendedStatus issue = CreateExtendedStatus(
                99010,
                absl::StrFormat(
                    "Task node %s calling %s does not have any parameters "
                    "set for parameters of type %s",
                    node_name, node.task().call_behavior().skill_id(),
                    skill.parameter_message_full_name),
                {.severity = error_severity});
            SetRelations(issue, tree, node);
            issues.push_back(issue);
          }
        } else {
          if (skill.parameter_message_full_name.empty()) {
            // Call has parameters but skill does not require parameters
            ExtendedStatus issue = CreateExtendedStatus(
                99010,
                absl::StrFormat(
                    "Task node %s calling %s has parameters but the skill or "
                    "process does not require any parameters",
                    node_name, node.task().call_behavior().skill_id()),
                // Treat the same as if passing a value to a non-existing
                // parameter. This is always only a warning.
                {.severity = ExtendedStatus::WARNING});
            SetRelations(issue, tree, node);
            issues.push_back(issue);
          }
        }
      }
    }
    // Loop/Retry nodes without do/child
    if (node.has_loop() && !node.loop().has_do_()) {
      ExtendedStatus issue = CreateExtendedStatus(
          99011, absl::StrFormat("Loop node %s has no do child", node_name),
          {.severity = error_severity});
      SetRelations(issue, tree, node);
      issues.push_back(issue);
    }
    // Retry recovery is optional
    if (node.has_retry() && !node.retry().has_child()) {
      ExtendedStatus issue = CreateExtendedStatus(
          99011, absl::StrFormat("Retry node %s has no child", node_name),
          {.severity = error_severity});
      SetRelations(issue, tree, node);
      issues.push_back(issue);
    }
    return context;
  };

  INTR_RETURN_IF_ERROR(VisitAllBehaviorTrees(tree,
                                             {
                                                 .visit_tree = validate_tree,
                                                 .visit_node = validate_node,
                                             },
                                             skill_client_generator,
                                             ignore_asset_ids));

  return issues;
}

namespace {

ExtendedStatus::Severity ToExtendedStatusSeverity(
    IsMessageDeserializableResult::IssueSeverityLevel level,
    bool node_disabled) {
  switch (level) {
    case IsMessageDeserializableResult::IssueSeverityLevel::kWarning:
      return ExtendedStatus::WARNING;
    case IsMessageDeserializableResult::IssueSeverityLevel::kError:
      return node_disabled ? ExtendedStatus::WARNING : ExtendedStatus::ERROR;
  }
}

int ToExtendedStatusCode(IsMessageDeserializableResult::IssueType type) {
  using enum IsMessageDeserializableResult::IssueType;

  switch (type) {
    case kInvalidBinaryRepresentation:
      return 99100;
    case kFieldMissing:
      return 99101;
    case kFieldTypeMismatch:
      return 99102;
    case kInvalidTypeUrl:
      return 99010;
  }
}

void AppendIssuesFromIsMessageDeserializableResult(
    const IsMessageDeserializableResult& result, const BehaviorTree& tree,
    const BehaviorTree::Node& node, const ValidateContext& context,
    std::vector<ExtendedStatus>& issues) {
  for (const IsMessageDeserializableResult::Issue& result_issue :
       result.issues) {
    std::string msg = absl::StrFormat(
        "Task node %s calling %s has parameters which cannot be deserialized "
        "by the skill or process: %s",
        NodeName(tree, node, context.is_process_tree),
        node.task().call_behavior().skill_id(), result_issue.summary);
    ExtendedStatus status =
        CreateExtendedStatus(ToExtendedStatusCode(result_issue.type), msg,
                             {.severity = ToExtendedStatusSeverity(
                                  result_issue.severity, context.disabled)});
    SetRelations(status, tree, node);
    issues.push_back(std::move(status));
  }
  return;
}

bool TypeUrlIsValidAndDoesNotMatchMessageFullName(
    std::string_view type_url, std::string_view message_full_name) {
  size_t slash_pos = type_url.find_last_of('/');
  if (slash_pos == std::string::npos) {
    return false;
  }
  std::string_view name = type_url.substr(slash_pos + 1);
  bool is_valid = !name.empty();  // Not a perfect check
  return is_valid && name != message_full_name;
}

}  // namespace

absl::StatusOr<std::vector<intrinsic_proto::status::ExtendedStatus>>
ValidateBehaviorTreePostImport(
    const intrinsic_proto::executive::BehaviorTree& tree,
    std::string_view operation_name,
    SkillClientGenerator& skill_client_generator, const clips::Environment& env,
    clips::ProtobufManager& proto_manager,
    const absl::flat_hash_set<std::string>& ignore_asset_ids)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env.mutex()) {
  std::vector<ExtendedStatus> issues;

  auto validate_node = [operation_name, &issues, &skill_client_generator, &env,
                        &proto_manager](const BehaviorTree& tree,
                                        const BehaviorTree::Node& node,
                                        const ValidateContext& parent_context)
                           ABSL_EXCLUSIVE_LOCKS_REQUIRED(
                               env.mutex()) -> absl::StatusOr<ValidateContext> {
    ValidateContext context = parent_context.CreateContextForChildNode(node);
    ExtendedStatus::Severity error_severity =
        context.disabled ? ExtendedStatus::WARNING : ExtendedStatus::ERROR;

    if (!node.task().has_call_behavior()) {
      return context;
    }
    const intrinsic_proto::executive::BehaviorCall& call =
        node.task().call_behavior();

    // Check deserializability of parameter proto if present. Builds on top of
    // the parameter checks already performed in
    // ValidateBehaviorTreePreImport(). For example, we don't need to add issues
    // here if skill_id is empty or if "call has paramaters" != "skill requires
    // parameters".
    if (call.has_parameters() && !call.skill_id().empty()) {
      INTR_ASSIGN_OR_RETURN(
          SkillClientGenerator::SkillRegistration skill,
          skill_client_generator.GetSkillRegistration(call.skill_id()));

      if (!skill.parameter_message_full_name.empty()) {
        // If the type URL looks valid, check whether it matches what the skill
        // expects. If it does not look valid (empty, missing slash, ...), just
        // proceed with calling IsMessageDeserializable() below which covers
        // those cases internally.
        if (TypeUrlIsValidAndDoesNotMatchMessageFullName(
                call.parameters().type_url(),
                skill.parameter_message_full_name)) {
          // Call has parameters and type URL looks valid but does not match the
          // type expected by the skill.
          ExtendedStatus issue = CreateExtendedStatus(
              99010,
              absl::StrFormat(
                  "Task node %s calling %s has parameters of type %s but "
                  "the skill or process requires %s",
                  NodeName(tree, node, context.is_process_tree),
                  call.skill_id(), call.parameters().type_url(),
                  skill.parameter_message_full_name),
              {.severity = error_severity});
          SetRelations(issue, tree, node);
          issues.push_back(issue);
        } else {
          // Call has parameters, skill requires parameters and one of:
          // - type URL looks valid and matches skill parameter type
          // - type URL looks invalid (empty, missing slash, ...)
          INTR_ASSIGN_OR_RETURN(
              clips::ProtobufManager::DescriptorPoolInfo pool_info,
              GetParameterPoolInfoForSkill(call.skill_id(), operation_name, env,
                                           proto_manager));

          INTR_ASSIGN_OR_RETURN(
              IsMessageDeserializableResult result,
              IsMessageDeserializable(call.parameters(),
                                      *pool_info.descriptor_pool,
                                      *pool_info.message_factory));

          AppendIssuesFromIsMessageDeserializableResult(result, tree, node,
                                                        context, issues);
        }
      }
    }
    return context;
  };

  INTR_RETURN_IF_ERROR(VisitAllBehaviorTrees(tree,
                                             {
                                                 .visit_node = validate_node,
                                             },
                                             skill_client_generator,
                                             ignore_asset_ids));

  return issues;
}

}  // namespace intrinsic::executive
