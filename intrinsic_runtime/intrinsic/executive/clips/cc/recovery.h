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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CC_RECOVERY_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CC_RECOVERY_H_

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"

namespace intrinsic {
namespace executive {

absl::Status ApplyRecoveryPaths(
    intrinsic_proto::executive::BehaviorTree& tree,
    absl::Span<const intrinsic_proto::executive::BehaviorTree::NodeIdentifier>
        recovery_nodes);

// Applies the state from state_proto to the given operation with
// 'operation_name' in the CLIPS environment. The state_proto must match the
// current operation in structure. It is usually generated with
// ApplyRecoveryPaths from a state proto from the operation.
//
// Exposed in CLIPS as recovery-recover-operation-state
// Args:
//   ?recovery-state-proto-id: Proto id to a recovery state proto.
//   ?operation-name: Name of a loaded operation. The recovery state proto must
//     match this operation's process tree.
//   ?start-tree-span: Span id of the already created span for the start tree
// Returns:
//   Result pair with TRUE/FALSE depending on success and as the second value:
//     On TRUE: empty string ""
//     On FALSE: extended status proto id
absl::Status RecoverOperationState(
    clips::Environment* absl_nonnull env,
    clips::ProtobufManager* absl_nonnull proto_mgr,
    clips::TraceSpanManager* absl_nonnull span_mgr,
    const intrinsic_proto::executive::BehaviorTree& state_proto,
    absl::string_view operation_name,
    clips::TraceSpanReferenceId start_tree_span)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex());

// Adds the main recovery functions to the CLIPS environment.
// The ProtobufManager must be available as long as the functions are registered
// in the environment.
absl::Status AddClipsRecoveryFunctions(
    clips::Environment* absl_nonnull env,
    clips::ProtobufManager* absl_nonnull mgr,
    clips::TraceSpanManager* absl_nonnull span_mgr);

}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CC_RECOVERY_H_
