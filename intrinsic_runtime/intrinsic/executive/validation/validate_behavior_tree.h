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

#ifndef INTRINSIC_EXECUTIVE_VALIDATION_VALIDATE_BEHAVIOR_TREE_H
#define INTRINSIC_EXECUTIVE_VALIDATION_VALIDATE_BEHAVIOR_TREE_H

#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/engine/skill_client_generator.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"
#include "intrinsic/util/status/extended_status.pb.h"

namespace intrinsic::executive {

// Extracts possible process dependencies from the behavior tree and checks if
// these are available in the skill_client_generator. Validation will fail if
// there are missing skill or process dependencies or cyclic process
// dependencies.
//
// If issues are found, they are returned as a list of extended statuses with
// ERROR severity. A non-ok status means that the validation itself has failed.
//
// PREREQUISITES for this check to run:
//  - The skill_client_generator must have been updated to a current state,
//  i.e., this will not call UpdateSkillsAndResources.
absl::StatusOr<std::vector<intrinsic_proto::status::ExtendedStatus>>
ValidateBehaviorTreeDependencies(
    const intrinsic_proto::executive::BehaviorTree& tree,
    SkillClientGenerator& skill_client_generator,
    const absl::flat_hash_set<std::string>& ignore_asset_ids = {});

// Validates a behavior tree, performing all checks that can already be
// performed before the CLIPS import. The main tree and all called sub-processes
// are checked recursively.
//
// If test_skip_parameter_validation is true, BehaviorCall parameters are not
// validated.
//
// If issues are found, they are returned as a list of extended statuses with
// WARNING or ERROR severity. Issues caused by disabled nodes have a maximum
// severity of WARNING. A non-ok status means that the validation itself has
// failed.
//
// PREREQUISITES for this check to run:
//  - The skill_client_generator must have been updated to a current state,
//  i.e., this will not call UpdateSkillsAndResources.
//  - ValidateBehaviorTreeDependencies() must have succeeded, i.e., there are no
//  missing or cyclic dependencies.
absl::StatusOr<std::vector<intrinsic_proto::status::ExtendedStatus>>
ValidateBehaviorTreePreImport(
    const intrinsic_proto::executive::BehaviorTree& tree,
    SkillClientGenerator& skill_client_generator,
    const absl::flat_hash_set<std::string>& ignore_asset_ids = {},
    bool test_skip_parameter_validation = false);

// Validates a behavior tree, performing all checks that can only be done after
// the CLIPS import. The main tree and all called sub-processes are checked
// recursively.
//
// If issues are found, they are returned as a list of extended statuses with
// WARNING or ERROR severity. Issues caused by disabled nodes have a maximum
// severity of WARNING. A non-ok status means that the validation itself has
// failed.
//
// PREREQUISITES for this check to run:
//  - The skill_client_generator must have been updated to a current state,
//  i.e., this will not call UpdateSkillsAndResources.
//  - ValidateBehaviorTreeDependencies() must have succeeded, i.e., there are no
//  missing or cyclic dependencies.
//  - The given tree proto must have been imported under the given operation
//  name.
absl::StatusOr<std::vector<intrinsic_proto::status::ExtendedStatus>>
ValidateBehaviorTreePostImport(
    const intrinsic_proto::executive::BehaviorTree& tree,
    std::string_view operation_name,
    SkillClientGenerator& skill_client_generator, const clips::Environment& env,
    clips::ProtobufManager& proto_manager,
    const absl::flat_hash_set<std::string>& ignore_asset_ids)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env.mutex());

}  // namespace intrinsic::executive

#endif  // INTRINSIC_EXECUTIVE_VALIDATION_VALIDATE_BEHAVIOR_TREE_H
