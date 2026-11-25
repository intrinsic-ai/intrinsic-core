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

#ifndef INTRINSIC_EXECUTIVE_ENGINE_CLIPS_IMPORTER_H_
#define INTRINSIC_EXECUTIVE_ENGINE_CLIPS_IMPORTER_H_

#include <string_view>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/engine/skill_client_generator.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"
#include "intrinsic/util/status/extended_status.pb.h"

namespace intrinsic {
namespace executive {
namespace clips {

// Import the given behavior tree as a process tree for operation
// 'operation_name' into env and proto_manager.
// Prior to the import makes sure that skill-infos are correctly loaded from the
// workcell, so that the import can find these. Skills descriptions are not
// expected to change while the operation is loaded.
// If diagnostics_for_import is given, this is filled with any diagnostics that
// did not prevent the tree from being imported, for example a warning if a
// proto compatibility test fails. This is independent from the returned Status.
// In particular there can be diagnostics when a tree is successfully imported.
absl::Status ImportProcessTree(
    intrinsic_proto::executive::BehaviorTree behavior_tree,
    std::string_view operation_name, Environment* absl_nonnull env,
    ProtobufManager* absl_nonnull proto_manager,
    SkillClientGenerator* absl_nonnull skill_client_generator,
    intrinsic_proto::status::ExtendedStatus* absl_nullable
        diagnostics_for_import = nullptr,
    bool test_skip_parameter_validation = false)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex());

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_ENGINE_CLIPS_IMPORTER_H_
