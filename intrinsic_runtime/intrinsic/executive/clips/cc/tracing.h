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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CC_TRACING_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CC_TRACING_H_

#include <cstdint>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"

namespace intrinsic::executive {

// Starts a span for a behavior-tree.
// Typically called, when ?tree is selected.
absl::StatusOr<clips::TraceSpanReferenceId> StartTreeSpan(
    clips::Environment* absl_nonnull env,
    clips::TraceSpanManager* absl_nonnull span_mgr, absl::string_view tree_id,
    clips::TraceSpanReferenceId parent_span_id)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex());

// Starts a span for a node. Typically called, when the node is selected.
// The node is identified by its node_id and the tree_id that it is in.
absl::StatusOr<clips::TraceSpanReferenceId> StartNodeSpan(
    clips::Environment* absl_nonnull env,
    clips::ProtobufManager* absl_nonnull proto_mgr,
    clips::TraceSpanManager* absl_nonnull span_mgr, absl::string_view tree_id,
    uint32_t node_id) ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex());

//  Starts an iteration span for a node. Typically called, when the do child of
//  a loop or retry node is selected. iteration will be set as the iteration
//  attribute.
absl::StatusOr<clips::TraceSpanReferenceId> StartNodeIterationSpan(
    clips::Environment* absl_nonnull env,
    clips::TraceSpanManager* absl_nonnull span_mgr, absl::string_view tree_id,
    uint32_t node_id, int iteration)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex());

absl::Status AddClipsTracingFunctions(
    clips::Environment* absl_nonnull env,
    clips::ProtobufManager* absl_nonnull proto_mgr,
    clips::TraceSpanManager* absl_nonnull span_mgr);

}  // namespace intrinsic::executive

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CC_TRACING_H_
