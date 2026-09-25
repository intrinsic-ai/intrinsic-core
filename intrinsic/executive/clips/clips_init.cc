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

#include "intrinsic/executive/clips/clips_init.h"

#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/message.h"  // IWYU pragma: export
#include "google/protobuf/message.h"
#include "google/rpc/status.pb.h"
#include "intrinsic/executive/clips/cc/id_handling.h"
#include "intrinsic/executive/clips/cc/recovery.h"
#include "intrinsic/executive/clips/cc/time.h"
#include "intrinsic/executive/clips/cc/tracing.h"
#include "intrinsic/executive/clips_cpp/context.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/value.h"

#include "intrinsic/executive/proto/executive_events.pb.h"

#include "intrinsic/executive/proto/run_metadata.pb.h"
#include "intrinsic/executive/proto/run_response.pb.h"
#include "intrinsic/executive/proto/world_query.pb.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/status/status_macros.h"
#include "ortools/base/path.h"

namespace intrinsic {
namespace executive {
namespace clips {

namespace {

// Get basic set of files which must be loaded.
// These files are necessary in order for the CLIPS environment to be useful.
std::vector<std::string> DefaultClipsBaseFiles() {
  // Do not add files here unless absolutely essential. Prefer to add files to
  // executive-init.clp to make app developers more flexible in what they
  // include.
  //
  // Ordering of the files matters (dependencies across files)
  return {"utils.clp",       "file.clp",
          "saliences.clp",   "flags.clp",
          "time.clp",        "errors.clp",
          "log_context.clp", "tracing.clp",
          "operation.clp",   "state.clp",
          "world.clp",       "code_execution_state_update.clp",
          "skill_info.clp",  "noop_action_info.clp",
          "plan.clp",        "extended_status.clp",
          "blackboard.clp",  "skill_info_check.clp",
          "inflow.clp"};
}

std::vector<std::string> BehaviorTreeClipsFiles() {
  return {"action_parameterization.clp",
          "conductor.clp",
          "time_measurement.clp",
          "behavior_tree/behavior_tree.clp",
          "behavior_tree/behavior_call_instance.clp",
          "behavior_tree/behavior_call_instance_check.clp",
          "behavior_tree/code_execution_instance.clp",
          "behavior_tree/code_execution_instance_check.clp",
          "behavior_tree/operation_handling.clp",
          "behavior_tree/operation_check.clp",
          "behavior_tree/tracing.clp",
          "behavior_tree/breakpoint.clp",
          "behavior_tree/selective_execution.clp",
          "behavior_tree/stepwise.clp",
          "behavior_tree/import_behavior_call.clp",
          "behavior_tree/import_code_execution.clp",
          "behavior_tree/operation_events.clp",
          "behavior_tree/state_proto_update.clp",
          "behavior_tree/import.clp",
          "behavior_tree/behavior_tree_check.clp",
          "behavior_tree/tracing_check.clp",
          "behavior_tree/behavior_call_instance_skill.clp",
          "behavior_tree/behavior_call_instance_tree.clp",
          "behavior_tree/node_decorator_condition.clp",
          "behavior_tree/condition_blackboard.clp",
          "behavior_tree/condition_compound.clp",
          "behavior_tree/condition_status_match.clp",
          "behavior_tree/condition_subtree.clp",
          "behavior_tree/condition_not.clp",
          "behavior_tree/sequence_node.clp",
          "behavior_tree/parallel_node.clp",
          "behavior_tree/task_node.clp",
          "behavior_tree/fail_node.clp",
          "behavior_tree/fallback_node.clp",
          "behavior_tree/selector_node.clp",
          "behavior_tree/branch_node.clp",
          "behavior_tree/loop_node.clp",
          "behavior_tree/retry_node.clp",
          "behavior_tree/sub_tree_node.clp",
          "behavior_tree/data_node.clp",
          "behavior_tree/debug_node.clp",
          "behavior_tree/dotgraph.clp"};
}

absl::BitGen id_random_generator_;

absl::Status DefaultClipsAddFunctions(Environment* env)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  INTR_RETURN_IF_ERROR(env->AddFunction(
      "now",
      +[]() -> clips::Values { return clips::GetTimeNowAsClipsValues(); }));
  INTR_RETURN_IF_ERROR(env->AddFunction(
      "seconds-to-duration", +[](double seconds) -> clips::Values {
        absl::Duration d = absl::Seconds(seconds);
        const int64_t s = absl::IDivDuration(d, absl::Seconds(1), &d);
        const int64_t n = absl::IDivDuration(d, absl::Nanoseconds(1), &d);
        return {clips::Value(s), clips::Value(n)};
      }));

  return absl::OkStatus();
}

absl::Status LoadClipsRunfiles(Environment* env, std::string_view clips_dir,
                               absl::Span<const std::string> files,
                               bool is_test)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  for (const auto& file : files) {
    VLOG(1) << "Loading " << file;
    if (is_test) {
      INTR_RETURN_IF_ERROR(
          env->LoadTestRunfile(file::JoinPath(clips_dir, file)));
    } else {
      INTR_RETURN_IF_ERROR(env->LoadRunfile(file::JoinPath(clips_dir, file)));
    }
  }
  return absl::OkStatus();
}

absl::Status DefaultClipsInitializeImpl(Environment* env,
                                        ProtobufManager* proto_mgr,
                                        TraceSpanManager* span_mgr,
                                        std::string_view clips_dir,
                                        bool is_test) {
  env->mutex()->AssertHeld();
  INTR_RETURN_IF_ERROR(DefaultClipsAddFunctions(env));
  INTR_RETURN_IF_ERROR(AddClipsIdHandlingFunctions(env, proto_mgr));
  INTR_RETURN_IF_ERROR(AddClipsRecoveryFunctions(env, proto_mgr, span_mgr));
  INTR_RETURN_IF_ERROR(AddClipsTracingFunctions(env, proto_mgr, span_mgr));
  INTR_RETURN_IF_ERROR(
      LoadClipsRunfiles(env, clips_dir, DefaultClipsBaseFiles(), is_test));
  INTR_RETURN_IF_ERROR(
      env->AssertFact("executive-state", {{"state", Symbol("CREATING")}})
          .status());
  std::string resolved_clips_dir =
      is_test ? PathResolver::ResolveRunfilesPathForTest(clips_dir)
              : PathResolver::ResolveRunfilesPath(clips_dir);
  const std::string statement = absl::StrFormat(
      R"((file-load-add-search-dir "%s"))", file::AddSlash(resolved_clips_dir));
  INTR_RETURN_IF_ERROR(env->Evaluate(statement).status());

  return absl::OkStatus();
}

}  // namespace

absl::Status DefaultClipsInitialize(Environment* env,
                                    ProtobufManager* proto_mgr,
                                    TraceSpanManager* span_mgr,
                                    std::string_view clips_dir) {
  return DefaultClipsInitializeImpl(env, proto_mgr, span_mgr, clips_dir,
                                    /*is_test=*/false);
}

absl::Status DefaultClipsInitializeForTest(ClipsContext* clips,
                                           std::string_view clips_dir) {
  return DefaultClipsInitializeImpl(clips->GetClipsEnvironment(),
                                    clips->GetProtobufManager(),
                                    clips->GetSpanManager(), clips_dir,
                                    /*is_test=*/true);
}

namespace {

absl::Status InitClipsBehaviorTreeSupportImpl(Environment* env,
                                              ProtobufManager* proto_mgr) {
  env->mutex()->AssertHeld();
  google::protobuf::LinkMessageReflection<
      intrinsic_proto::executive::RunMetadata>();
  google::protobuf::LinkMessageReflection<
      intrinsic_proto::executive::RunResponse>();
  google::protobuf::LinkMessageReflection<google::rpc::Status>();

  google::protobuf::LinkMessageReflection<
      intrinsic_proto::executive::OperationEvents>();

  google::protobuf::LinkMessageReflection<
      intrinsic_proto::executive::WorldQuery>();
  for (const std::string& file : BehaviorTreeClipsFiles()) {
    INTR_ASSIGN_OR_RETURN(clips::Value loaded,
                          env->EvaluateExpectSingleReturn(
                              absl::StrFormat(R"((file-load "%s"))", file)));
    if (loaded.GetValueType() == clips::Value::Type::kSymbol &&
        loaded.GetSymbol().value() == clips::Symbol::False()) {
      return absl::NotFoundError(
          absl::StrFormat(R"(File "%s" could not be found)", file));
    }
  }

  return absl::OkStatus();
}

}  // namespace

absl::Status InitClipsBehaviorTreeSupport(Environment* env,
                                          ProtobufManager* proto_mgr) {
  return InitClipsBehaviorTreeSupportImpl(env, proto_mgr);
}

absl::Status InitClipsBehaviorTreeSupportForTest(ClipsContext* clips) {
  return InitClipsBehaviorTreeSupportImpl(clips->GetClipsEnvironment(),
                                          clips->GetProtobufManager());
}

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic
