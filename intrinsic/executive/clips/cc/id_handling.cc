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

#include "intrinsic/executive/clips/cc/id_handling.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "intrinsic/executive/cc/behavior_tree_visitor.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/util/status/get_extended_status.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_specs.h"
#include "intrinsic/util/unique_id.h"
#include "re2/re2.h"

using intrinsic_proto::executive::BehaviorTree;

namespace intrinsic::executive {

namespace {

// This matches any letters that are *not* valid in kIdentifierPattern.
constexpr char kIdentifierInvalidLetter[] = "[^a-zA-Z0-9_-]";
// This matches any letters that are *not* valid as the first character in
// kIdentifierPattern.
constexpr char kIdentifierInvalidLeadLetters[] = "^[^a-zA-Z0-9]*";
// Defines the pattern for internal ids, e.g., tree ids.
constexpr char kIdentifierPattern[] = "[a-zA-Z0-9][a-zA-Z0-9_-]*";
const LazyRE2 kIdentifierRegexp = {kIdentifierPattern};
const LazyRE2 kIdentifierInvalidLetterRegexp = {kIdentifierInvalidLetter};
const LazyRE2 kIdentifierInvalidLeadLettersRegexp = {
    kIdentifierInvalidLeadLetters};
// Keys on the blackboard are more restrictive than identifiers, which just must
// be unique. In additions keys must not appear to be an expression, i.e., they
// cannot just be numbers or contain a '-' (Minus sign). Thus, this follows the
// common pattern of an alpha-numeric identifier with underscores.
constexpr char kBlackboardKeyPattern[] = "[_a-zA-Z][_a-zA-Z0-9]*";
const LazyRE2 kBlackboardKeyRegexp = {kBlackboardKeyPattern};

bool IsReservedBlackboardKey(std::string_view key) {
  // These are internal keys used in the executive + all reserved identifiers in
  // CEL: https://github.com/google/cel-spec/blob/master/doc/langdef.md#syntax
  static const absl::NoDestructor<absl::flat_hash_set<std::string>>
      kReservedKeys({"params", "process", "true",     "false",     "null",
                     "in",     "as",      "break",    "const",     "continue",
                     "else",   "for",     "function", "if",        "import",
                     "let",    "loop",    "package",  "namespace", "return",
                     "var",    "void",    "while"});
  return kReservedKeys->contains(key);
}

}  // namespace

absl::Status IsValidIdentifier(absl::string_view id) {
  if (id.empty()) {
    return absl::InvalidArgumentError("Ids must not be empty");
  }
  if (RE2::FullMatch(id, *kIdentifierRegexp)) {
    return absl::OkStatus();
  }
  return absl::InvalidArgumentError(absl::StrFormat(
      "'%s' is not a valid identifier. It must follow the form: %s", id,
      kIdentifierPattern));
}

void SanitizeIdentifier(std::string& str) {
  RE2::GlobalReplace(&str, *kIdentifierInvalidLetterRegexp, "");
  RE2::Replace(&str, *kIdentifierInvalidLeadLettersRegexp, "");
}

absl::Status IsValidBlackboardKey(absl::string_view key, bool allow_empty,
                                  absl::string_view context) {
  if (key.empty()) {
    if (allow_empty) {
      return absl::OkStatus();
    }
    return absl::InvalidArgumentError(
        absl::StrFormat("Blackboard key cannot be empty (%s)", context));
  }
  if (IsReservedBlackboardKey(key)) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Blackboard key '%s' is a reserved key (%s)", key, context));
  }
  if (RE2::FullMatch(key, *kBlackboardKeyRegexp)) {
    return absl::OkStatus();
  }
  return absl::InvalidArgumentError(absl::StrFormat(
      "'%s' in %s is not a valid key. It must follow the form: %s", key,
      context, kBlackboardKeyPattern));
}

absl::StatusOr<absl::flat_hash_set<std::string>> ValidateAndGetTreeIds(
    const BehaviorTree& tree) {
  absl::flat_hash_set<std::string> used_tree_ids;
  absl::flat_hash_set<std::string> duplicate_tree_ids;
  absl::flat_hash_set<std::string> invalid_tree_id_errors;

  auto id_collector = [&used_tree_ids, &duplicate_tree_ids,
                       &invalid_tree_id_errors](const BehaviorTree& tree) {
    if (tree.tree_id().empty()) return absl::OkStatus();
    if (used_tree_ids.contains(tree.tree_id())) {
      duplicate_tree_ids.insert(tree.tree_id());
    }
    if (absl::Status is_valid = IsValidIdentifier(tree.tree_id());
        !is_valid.ok()) {
      invalid_tree_id_errors.insert(std::string(is_valid.message()));
    }
    used_tree_ids.insert(tree.tree_id());
    return absl::OkStatus();
  };
  INTR_RETURN_IF_ERROR(VisitBehaviorTree(tree, {.visit_tree = id_collector}));

  if (!duplicate_tree_ids.empty() || !invalid_tree_id_errors.empty()) {
    std::string error_message;
    if (!duplicate_tree_ids.empty()) {
      error_message =
          absl::StrFormat("Found the following duplicate tree ids: %s. ",
                          absl::StrJoin(duplicate_tree_ids, ", "));
    }
    if (!invalid_tree_id_errors.empty()) {
      absl::StrAppend(&error_message, "Found invalid tree ids. ",
                      absl::StrJoin(invalid_tree_id_errors, "; "));
    }
    return CreateStatus(70001, error_message,
                        absl::StatusCode::kInvalidArgument);
  }
  return used_tree_ids;
}

absl::Status EnsureValidTreeIds(
    intrinsic_proto::executive::BehaviorTree& tree,
    const absl::flat_hash_set<std::string>& conflicting_tree_ids,
    absl::string_view prefix) {
  INTR_ASSIGN_OR_RETURN(absl::flat_hash_set<std::string> used_tree_ids,
                        ValidateAndGetTreeIds(tree));

  std::vector<std::string> tree_ids_with_conflict;
  for (const std::string& used_tree_id : used_tree_ids) {
    if (conflicting_tree_ids.contains(used_tree_id)) {
      tree_ids_with_conflict.push_back(used_tree_id);
    }
  }
  if (!tree_ids_with_conflict.empty()) {
    return CreateStatus(
        70003,
        absl::StrFormat("The following tree ids are already loaded: %s",
                        absl::StrJoin(tree_ids_with_conflict, ", ")),
        absl::StatusCode::kInvalidArgument);
  }
  used_tree_ids.insert(conflicting_tree_ids.begin(),
                       conflicting_tree_ids.end());

  // Within this visitor used_tree_ids will always record the ids *without* the
  // prefix applied. Likewise generated ids are verified without the prefix
  // against used_tree_ids. Thus all ids without the prefix (from
  // ValidateAndGetTreeIds or generated here) are ensured to not contain
  // duplicates. Applying prefix to each id (even when prefix is empty)
  // doesn't change the fact that all ids are unique.
  auto id_generator = [&used_tree_ids, &prefix](BehaviorTree& tree) {
    // If tree_id is set it was validated by the previous call and is already
    // included in used_tree_ids.
    if (!tree.tree_id().empty()) {
      tree.set_tree_id(absl::StrCat(prefix, tree.tree_id()));
      return absl::OkStatus();
    }

    std::string tree_name(tree.name());
    SanitizeIdentifier(tree_name);
    if (!tree_name.empty()) {
      absl::StrAppend(&tree_name, "-");
    }

    std::string new_id;
    do {
      new_id = absl::StrCat(tree_name, intrinsic::UniqueId());
    } while (used_tree_ids.contains(new_id));

    used_tree_ids.insert(new_id);
    tree.set_tree_id(absl::StrCat(prefix, new_id));
    return absl::OkStatus();
  };
  return VisitBehaviorTree(tree, {.visit_tree = id_generator});
}

absl::StatusOr<absl::flat_hash_map<std::string, absl::flat_hash_set<uint32_t>>>
ValidateAndGetNodeIds(const intrinsic_proto::executive::BehaviorTree& tree) {
  absl::flat_hash_map<std::string, absl::flat_hash_set<uint32_t>> used_node_ids;
  absl::flat_hash_map<std::string, absl::flat_hash_set<uint32_t>>
      duplicate_node_ids;

  auto id_collector = [&used_node_ids, &duplicate_node_ids](
                          const BehaviorTree& tree,
                          const BehaviorTree::Node& node) {
    if (node.id() == 0) return absl::OkStatus();
    auto used_node_ids_for_tree = used_node_ids.find(tree.tree_id());
    if (used_node_ids_for_tree != used_node_ids.end() &&
        used_node_ids_for_tree->second.contains(node.id())) {
      duplicate_node_ids[tree.tree_id()].insert(node.id());
    }
    used_node_ids[tree.tree_id()].insert(node.id());
    return absl::OkStatus();
  };
  INTR_RETURN_IF_ERROR(VisitBehaviorTree(tree, {.visit_node = id_collector}));

  if (!duplicate_node_ids.empty()) {
    std::string error_message = "Found duplicate node ids.";
    for (const auto& [tree_id, duplicate_ids] : duplicate_node_ids) {
      absl::StrAppend(
          &error_message,
          absl::StrFormat(
              " Tree with id '%s' has the following duplicate node ids: %s.",
              tree_id, absl::StrJoin(duplicate_ids, ", ")));
    }
    return CreateStatus(70002, error_message,
                        absl::StatusCode::kInvalidArgument);
  }
  return used_node_ids;
}

absl::Status EnsureValidNodeIds(intrinsic_proto::executive::BehaviorTree& tree,
                                absl::BitGenRef id_random_generator) {
  INTR_ASSIGN_OR_RETURN(
      (absl::flat_hash_map<std::string, absl::flat_hash_set<uint32_t>>
           used_node_ids),
      ValidateAndGetNodeIds(tree));

  auto id_generator = [&used_node_ids, &id_random_generator](
                          BehaviorTree& tree, BehaviorTree::Node& node) {
    // If node_id is set it was validated by the previous call and is already
    // included in used_node_ids.
    if (node.id() != 0) {
      return absl::OkStatus();
    }

    uint32_t new_id;
    do {
      new_id = absl::Uniform<uint32_t>(id_random_generator);
    } while (new_id == 0 || used_node_ids[tree.tree_id()].contains(new_id));

    used_node_ids[tree.tree_id()].insert(new_id);
    node.set_id(new_id);
    return absl::OkStatus();
  };
  return VisitBehaviorTree(tree, {.visit_node = id_generator});
}

absl::Status EnsureValidIds(
    intrinsic_proto::executive::BehaviorTree& tree,
    const absl::flat_hash_set<std::string>& conflicting_tree_ids) {
  absl::BitGen bit_gen;
  INTR_RETURN_IF_ERROR(EnsureValidTreeIds(tree, conflicting_tree_ids, ""));
  return EnsureValidNodeIds(tree, bit_gen);
}

absl::Status EnsureValidIds(intrinsic_proto::executive::BehaviorTree& tree,
                            absl::string_view tree_id_prefix) {
  absl::BitGen bit_gen;
  INTR_RETURN_IF_ERROR(EnsureValidTreeIds(tree, {}, tree_id_prefix));
  return EnsureValidNodeIds(tree, bit_gen);
}

std::string GenerateTreeIdPrefixForNode(absl::string_view tree_id,
                                        uint32_t node_id) {
  return absl::StrFormat("%s:%d/", tree_id, node_id);
}

absl::StatusOr<BehaviorTree::NodeIdentifier> GenerateNodeIdentifier(
    absl::string_view tree_id, uint32_t node_id) {
  BehaviorTree::NodeIdentifier result;
  BehaviorTree::NodeIdentifier* current = &result;
  absl::string_view remaining = tree_id;
  while (true) {
    size_t slash_pos = remaining.find('/');
    if (slash_pos == absl::string_view::npos) {
      current->set_tree_id(remaining);
      current->set_node_id(node_id);
      break;
    }
    absl::string_view prefix_part = remaining.substr(0, slash_pos);
    remaining.remove_prefix(slash_pos + 1);

    size_t colon_pos = prefix_part.rfind(':');
    if (colon_pos == absl::string_view::npos) {
      return absl::InvalidArgumentError(
          absl::StrCat("Missing ':' in prefix part '", prefix_part,
                       "' of tree id '", tree_id, "'"));
    }
    uint32_t task_node_id = 0;
    if (!absl::SimpleAtoi(prefix_part.substr(colon_pos + 1), &task_node_id)) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Failed to parse node id from '", prefix_part.substr(colon_pos + 1),
          "' in prefix part '", prefix_part, "' of tree id '", tree_id, "'"));
    }
    current->set_tree_id(prefix_part.substr(0, colon_pos));
    current->set_node_id(task_node_id);
    current = current->mutable_node_within_task_node();
  }
  return result;
}

absl::Status SetNodeIdentifier(clips::ProtobufManager* proto_mgr,
                               clips::ProtoMessageId proto_id,
                               absl::string_view proto_path,
                               absl::string_view tree_id, uint32_t node_id) {
  if (proto_mgr == nullptr) {
    return absl::InvalidArgumentError("proto_mgr must not be null");
  }

  INTR_ASSIGN_OR_RETURN(BehaviorTree::NodeIdentifier node_identifier,
                        GenerateNodeIdentifier(tree_id, node_id));

  if (proto_path.empty()) {
    INTR_ASSIGN_OR_RETURN(
        BehaviorTree::NodeIdentifier * target,
        proto_mgr->GetMutableProtoAs<BehaviorTree::NodeIdentifier>(proto_id));
    *target = std::move(node_identifier);
    return absl::OkStatus();
  }

  clips::ProtoMessageId temp_id =
      proto_mgr->AddGeneratedProto(std::move(node_identifier));
  absl::Status status = proto_mgr->SetFieldFromProto(
      proto_id, proto_path, clips::ProtobufManager::kGeneratedDescriptorPoolId,
      temp_id);
  proto_mgr->RemoveProto(temp_id);
  return status;
}

absl::Status AddClipsIdHandlingFunctions(clips::Environment* env,
                                         clips::ProtobufManager* proto_mgr)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  auto return_clips_result =
      [proto_mgr](const absl::Status& status) -> clips::Values {
    std::optional<intrinsic_proto::status::ExtendedStatus> es =
        GetExtendedStatus(status);
    if (!es.has_value()) {
      es.emplace(CreateExtendedStatus(70099, status.ToString()));
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
      "ensure-valid-ids",
      std::function(
          [proto_mgr, return_clips_result](
              int64_t tree_proto_id,
              const clips::Values& global_tree_id_values) -> clips::Values {
            INTR_ASSIGN_OR_RETURN(BehaviorTree * tree_proto,
                                  proto_mgr->GetMutableProtoAs<BehaviorTree>(
                                      clips::ProtoMessageId(tree_proto_id)),
                                  _.With(return_clips_result));
            absl::flat_hash_set<std::string> global_tree_ids;
            for (const clips::Value& global_tree_id_val :
                 global_tree_id_values) {
              INTR_ASSIGN_OR_RETURN(std::string global_tree_id,
                                    global_tree_id_val.GetSymbolAsString(),
                                    _.With(return_clips_result));
              global_tree_ids.insert(global_tree_id);
            }
            INTR_RETURN_IF_ERROR(EnsureValidIds(*tree_proto, global_tree_ids))
                .With(return_clips_result);
            return {clips::Symbol::True(), clips::Value("")};
          })));
  INTR_RETURN_IF_ERROR(env->AddFunction(
      "ensure-valid-ids-with-prefix",
      std::function([proto_mgr, return_clips_result](
                        int64_t tree_proto_id,
                        const std::string& prefix) -> clips::Values {
        INTR_ASSIGN_OR_RETURN(BehaviorTree * tree_proto,
                              proto_mgr->GetMutableProtoAs<BehaviorTree>(
                                  clips::ProtoMessageId(tree_proto_id)),
                              _.With(return_clips_result));
        INTR_RETURN_IF_ERROR(EnsureValidIds(*tree_proto, prefix))
            .With(return_clips_result);
        return {clips::Symbol::True(), clips::Value("")};
      })));
  INTR_RETURN_IF_ERROR(
      env->AddFunction("generate-tree-id-prefix-for-node",
                       std::function([](const std::string& tree_id,
                                        int64_t node_id) -> std::string {
                         return GenerateTreeIdPrefixForNode(tree_id, node_id);
                       })));

  INTR_RETURN_IF_ERROR(env->AddFunction(
      "is-valid-blackboard-key",
      +[](const std::string& key, const std::string& where,
          const clips::Symbol& empty_option) -> clips::Values {
        bool allow_empty = empty_option == clips::Symbol("EMPTY-OK");
        if (!allow_empty) {
          if (empty_option != clips::Symbol("NON-EMPTY")) {
            LOG(ERROR)
                << "is-valid-blackboard-key got an unknown empty_option: "
                << empty_option.ToString() << " - ignoring";
            allow_empty = true;
          }
        }
        absl::Status key_valid = IsValidBlackboardKey(key, allow_empty, where);
        if (key_valid.ok()) {
          return {clips::Symbol::True(), clips::Value("")};
        }
        return {clips::Symbol::False(), clips::Value(key_valid.message())};
      }));

  std::function set_node_identifier_func(
      [proto_mgr](int64_t proto_id, const std::string& proto_path,
                  const std::string& tree_id,
                  int64_t node_id) -> clips::Symbol {
        absl::Status status = SetNodeIdentifier(
            proto_mgr, clips::ProtoMessageId(proto_id), proto_path, tree_id,
            static_cast<uint32_t>(node_id));
        if (!status.ok()) {
          LOG_EVERY_N_SEC(ERROR, 5)
              << "Failed to set node identifier on field '" << proto_path
              << "' in proto " << proto_id << ": " << status;
          return clips::Symbol::False();
        }
        return clips::Symbol::True();
      });
  INTR_RETURN_IF_ERROR(
      env->AddFunction("set-node-identifier-proto", set_node_identifier_func));

  return absl::OkStatus();
}

}  // namespace intrinsic::executive
