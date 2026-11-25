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

#include "intrinsic/executive/engine/clips_importer.h"

#include <string_view>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "intrinsic/executive/clips/cc/id_handling.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/engine/skill_client_generator.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"
#include "intrinsic/executive/validation/validate_behavior_tree.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_specs.h"

ABSL_FLAG(bool, validate_on_import, true, "Validates behavior trees on import");

namespace intrinsic {
namespace executive {
namespace clips {

namespace {

// TODO(b/479155807): Remove this when it becomes unnecessary
absl::flat_hash_set<std::string> GetNoopActions(Environment* absl_nonnull env)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  absl::flat_hash_set<std::string> noop_actions;
  std::vector<clips::Fact> noop_facts = env->GetFacts("noop-action-info");
  for (const clips::Fact& fact : noop_facts) {
    absl::StatusOr<clips::Values> action_values =
        fact.GetSlotValues("noop-action-names");
    if (!action_values.ok()) {
      continue;
    }
    for (const clips::Value& action_value : *action_values) {
      absl::StatusOr<std::string> action_name = action_value.GetString();
      if (!action_name.ok()) {
        continue;
      }
      noop_actions.insert(*action_name);
    }
  }
  return noop_actions;
}

// Assumes that there is at least one issue.
intrinsic_proto::status::ExtendedStatus CombineValidationIssues(
    std::vector<intrinsic_proto::status::ExtendedStatus> issues) {
  // Propagate a single issue instead of wrapping it
  if (issues.size() == 1) {
    return std::move(issues.front());
  }

  // Combine issues in a single wrapper status whose severity is the max
  // severity of all issues.
  intrinsic_proto::status::ExtendedStatus wrapper_es = CreateExtendedStatus(
      99004, absl::StrFormat("Found %d validation issues", issues.size()),
      {.severity = intrinsic_proto::status::ExtendedStatus::INFO});
  for (const intrinsic_proto::status::ExtendedStatus& issue : issues) {
    *wrapper_es.add_context() = std::move(issue);
    if (issue.severity() > wrapper_es.severity()) {
      wrapper_es.set_severity(issue.severity());
    }
  }
  return wrapper_es;
}

absl::StatusOr<std::vector<intrinsic_proto::status::ExtendedStatus>>
PerformDependencyAndPreImportValidation(
    const intrinsic_proto::executive::BehaviorTree& behavior_tree,
    SkillClientGenerator& skill_client_generator,
    const absl::flat_hash_set<std::string>& ignore_asset_ids,
    bool test_skip_parameter_validation) {
  // Validation step 1: Validate dependencies. All further validation checks
  // assume that no dependency issues are found.
  INTR_ASSIGN_OR_RETURN(
      std::vector<intrinsic_proto::status::ExtendedStatus> dep_issues,
      ValidateBehaviorTreeDependencies(behavior_tree, skill_client_generator,
                                       ignore_asset_ids),
      std::move(_).With(AttachExtendedStatus(
          99000,
          absl::StrFormat("Failed validating dependencies of tree '%s' (%s)",
                          behavior_tree.name(), behavior_tree.tree_id()))));

  if (!dep_issues.empty()) {
    // All dependency issues are errors, fail early if there are any.
    return intrinsic::InvalidArgumentErrorBuilder().AttachExtendedStatus(
               CombineValidationIssues(std::move(dep_issues)))
           << "Behavior tree has invalid dependencies";
  }

  // Validation step 2: Pre-import validation. Validate as much as possible
  // before the CLIPS import.
  INTR_ASSIGN_OR_RETURN(
      std::vector<intrinsic_proto::status::ExtendedStatus> pre_import_issues,
      ValidateBehaviorTreePreImport(behavior_tree, skill_client_generator,
                                    ignore_asset_ids,
                                    test_skip_parameter_validation),
      std::move(_).With(AttachExtendedStatus(
          99000,
          absl::StrFormat("Failed validating tree '%s' (%s) before import",
                          behavior_tree.name(), behavior_tree.tree_id()))));

  return pre_import_issues;
}

void DeleteClipsOperationIgnoringError(std::string_view operation_name,
                                       Environment& env)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env.mutex()) {
  absl::Status status =
      env.EvaluateResult(
             absl::StrFormat("(operation-delete \"%s\")", operation_name))
          .status();
  if (!status.ok()) {
    LOG(ERROR) << "Failed to delete operation in CLIPS after validation error "
                  "during import: "
               << status;
  }
}

absl::Status PerformPostImportValidation(
    const intrinsic_proto::executive::BehaviorTree& behavior_tree,
    std::string_view operation_name,
    const std::vector<intrinsic_proto::status::ExtendedStatus>&
        pre_import_issues,
    SkillClientGenerator& skill_client_generator, Environment& env,
    ProtobufManager& proto_manager,
    intrinsic_proto::status::ExtendedStatus* absl_nullable
        diagnostics_for_import,
    const absl::flat_hash_set<std::string>& ignore_asset_ids)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env.mutex()) {
  // Validation step 3: Post-import validation. Validate everything that
  // requires the CLIPS import to have run. E.g., checking parameter Any's
  // requires the skill descriptor pools to have been created.
  INTR_ASSIGN_OR_RETURN(
      std::vector<intrinsic_proto::status::ExtendedStatus> post_import_issues,
      ValidateBehaviorTreePostImport(behavior_tree, operation_name,
                                     skill_client_generator, env, proto_manager,
                                     ignore_asset_ids),
      std::move(_).With(AttachExtendedStatus(
          99000,
          absl::StrFormat("Failed validating tree '%s' (%s) after import",
                          behavior_tree.name(), behavior_tree.tree_id()))));

  if (!pre_import_issues.empty() || !post_import_issues.empty()) {
    std::vector<intrinsic_proto::status::ExtendedStatus> issues =
        pre_import_issues;
    issues.reserve(pre_import_issues.size() + post_import_issues.size());
    issues.insert(issues.end(),
                  std::make_move_iterator(post_import_issues.begin()),
                  std::make_move_iterator(post_import_issues.end()));

    // Validation does not return any INFO statuses (yet). "issues non-empty"
    // hence means that we have at least one issue with severity >= WARNING.
    intrinsic_proto::status::ExtendedStatus es =
        CombineValidationIssues(std::move(issues));

    if (es.severity() >= intrinsic_proto::status::ExtendedStatus::ERROR) {
      // Try to clean up operation in clips before returning error
      DeleteClipsOperationIgnoringError(operation_name, env);
      return intrinsic::InvalidArgumentErrorBuilder().AttachExtendedStatus(es)
             << "Behavior tree is invalid";
    } else if (diagnostics_for_import != nullptr) {
      *diagnostics_for_import = es;
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status ImportProcessTree(
    intrinsic_proto::executive::BehaviorTree behavior_tree,
    std::string_view operation_name, Environment* absl_nonnull env,
    ProtobufManager* absl_nonnull proto_manager,
    SkillClientGenerator* absl_nonnull skill_client_generator,
    intrinsic_proto::status::ExtendedStatus* absl_nullable
        diagnostics_for_import,
    bool test_skip_parameter_validation)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {

  INTR_RETURN_IF_ERROR(EnsureUniqueElementIds(behavior_tree));


  INTR_RETURN_IF_ERROR(
      skill_client_generator->UpdateSkillsAndResources(operation_name));
  absl::flat_hash_set<std::string> ignore_asset_ids = GetNoopActions(env);

  std::vector<intrinsic_proto::status::ExtendedStatus> pre_import_issues;
  if (absl::GetFlag(FLAGS_validate_on_import)) {
    INTR_ASSIGN_OR_RETURN(
        pre_import_issues,
        PerformDependencyAndPreImportValidation(
            behavior_tree, *skill_client_generator, ignore_asset_ids,
            test_skip_parameter_validation));
  }

  ScopedProto bt_proto(behavior_tree, proto_manager);
  absl::Status import_status =
      env
          ->EvaluateResult(absl::StrFormat(
              "(behavior-tree-import-process-tree-proto %i \"%s\")",
              bt_proto.proto_id().value(), operation_name))
          .status();
  if (!import_status.ok()) {
    StatusBuilder final_status(import_status);
    if (import_status.code() == absl::StatusCode::kAborted) {
      final_status.SetCode(absl::StatusCode::kInvalidArgument).SetPrepend()
          << "Failed to import behavior tree: ";
    }
    // Attach already found validation results if there are any.
    if (!pre_import_issues.empty()) {
      final_status.AttachExtendedStatus(
          CombineValidationIssues(std::move(pre_import_issues)));
    }
    return final_status;
  }

  if (absl::GetFlag(FLAGS_validate_on_import)) {
    RETURN_IF_ERROR(PerformPostImportValidation(
        behavior_tree, operation_name, pre_import_issues,
        *skill_client_generator, *env, *proto_manager, diagnostics_for_import,
        ignore_asset_ids));
  }

  return absl::OkStatus();
}

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic
