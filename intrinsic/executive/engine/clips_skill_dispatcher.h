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

#ifndef INTRINSIC_EXECUTIVE_ENGINE_CLIPS_SKILL_DISPATCHER_H_
#define INTRINSIC_EXECUTIVE_ENGINE_CLIPS_SKILL_DISPATCHER_H_

#include <memory>
#include <optional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "intrinsic/executive/clips_cpp/assert_facade.h"
#include "intrinsic/executive/clips_cpp/function_facade.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/readonly_facade.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/engine/skill_action.h"
#include "intrinsic/executive/engine/skill_client_generator.h"
#include "intrinsic/executive/engine/skill_instance.h"
#include "intrinsic/executive/proto/behavior_call.pb.h"
#include "intrinsic/simulation/service/proto/first_party/simulation_service.grpc.pb.h"
#include "intrinsic/skills/internal/skill_registry_client_interface.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/util/thread/thread_pool.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_context.h"

namespace intrinsic {
namespace executive {

// Validates that footprint volume reservations do not contain inline triangle
// meshes or point clouds.
absl::Status ValidateFootprintVolumeReservations(
    const intrinsic_proto::skills::Footprint& footprint,
    absl::string_view skill_id);

// Asynchronously runs skills in the background.
// Receives 'StartSkill' from CLIPS and runs the skill in a separate fiber.
// When the skill has finished, it calls back to CLIPS and asserts the return
// value as a fact.
namespace clips {
class EnvironmentAssertFacade;
}  // namespace clips

class ClipsSkillDispatcher {
 public:
  ClipsSkillDispatcher(
      clips::EnvironmentAssertFacade* assert_facade,
      clips::ProtobufManager* proto_manager,
      clips::TraceSpanManager* span_manager,
      SkillClientGenerator* skill_client_generator,
      intrinsic_proto::world::ObjectWorldService::StubInterface*
          object_world_service_stub = nullptr,
      intrinsic_proto::simulation::first_party::SimulationService::
          StubInterface* simulation_service = nullptr);

  // Must be called after construction.
  // Registers the skill-related functions with the CLIPS environment, and
  // creates the thread bundle. Must be called in the same parent thread as
  // TearDown().
  absl::Status Init(clips::EnvironmentFunctionFacade* function_facade,
                    clips::EnvironmentReadonlyFacade* readonly_facade)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(function_facade->clips_mutex(),
                                    readonly_facade->GetClipsMutex());

  // Must be called in the same parent thread as Init().
  absl::Status TearDown();

  clips::EnvironmentAssertFacade* GetAssertFacade() { return assert_facade_; }

  // Non-blocking.
  // 'action_id' must have CLIPS symbol syntax, i.e. start with a character.
  // 'behavior_call_proto_id' must be a valid ID that yields an
  // 'intrinsic_proto::executive::BehaviorCall' message from protobuf_manager_.
  // parent_span should usually be a valid TraceSpanReferenceId and chosen as
  // the parent span of the Execute span. If it is not valid, a new trace is
  // created for this call.
  void StartSkillExecution(absl::string_view action_id,
                           absl::string_view world_id,
                           const clips::Symbol& sim_mode,
                           clips::ProtoMessageId behavior_call_proto_id,
                           clips::ProtoMessageId context_proto_id,
                           clips::TraceSpanReferenceId parent_span,
                           clips::DescriptorPoolId descriptor_pool_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(GetAssertFacade()->GetClipsMutex());

  // Non-blocking.
  // 'action_id' is an action UID (see uid slot in plan-action template).
  void CancelSkillExecution(const std::string& action_id,
                            const clips::Symbol& sim_mode);

  // Give up on a skill call on the client side. This will not stop anything on
  // the actual skill.
  // Non-blocking.
  // 'action_id' is an action UID (see uid slot in plan-action template).
  void AbandonSkillExecution(const std::string& action_id);

  // Non-blocking.
  // This will run a "what-if" request to the skill service to determine the
  // nominal outcome world, a footprint, and possibly internal_data (e.g., a
  // computed trajectory which Execute() would then follow).
  // It sets the footprint and internal_data field on the BehaviorCall proto
  // referenced by the behavior_call_proto_id.
  // 'action_id' must have CLIPS symbol syntax, i.e. start with a character.
  // 'proto_id' must be a valid ID that yields an
  // 'intrinsic_proto::executive::BehaviorCall' message from protobuf_manager_.
  // parent_span should usually be a valid TraceSpanReferenceId and chosen as
  // the parent span of the Project span. If it is not valid, a new trace is
  // created for this call.
  void StartSkillProjection(absl::string_view action_id,
                            absl::string_view world_id,
                            clips::ProtoMessageId behavior_call_proto_id,
                            clips::ProtoMessageId context_proto_id,
                            clips::TraceSpanReferenceId parent_span,
                            clips::DescriptorPoolId descriptor_pool_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(GetAssertFacade()->GetClipsMutex());

  // Blocking.
  absl::Status ResetSkillInstanceIds();

  // For testing.
  void WaitForFinishAll();

  // Must be in sync with allowed values for status (skill_info.clp)
  static constexpr absl::string_view kSkillStatusProjected = "PROJECTED";
  static constexpr absl::string_view kSkillStatusRunning = "RUNNING";
  static constexpr absl::string_view kSkillStatusCanceling = "CANCELING";
  static constexpr absl::string_view kSkillStatusCanceled = "CANCELED";
  static constexpr absl::string_view kSkillStatusSucceeded = "SUCCEEDED";
  static constexpr absl::string_view kSkillStatusFailed = "FAILED";
  static constexpr absl::string_view kSkillStatusCancelingExecutionTimeout =
      "CANCELING-EXECUTION-TIMEOUT";

 private:
  // Start a tracing span for project or execute.
  // Will always start a span, even if parent_context is invalid. In that case
  // a root span in a new trace is started. Normally, this should not happen.
  std::shared_ptr<opentelemetry::trace::Span> StartSkillSpan(
      absl::string_view skill_name,
      const opentelemetry::trace::SpanContext& parent_context,
      SkillAction skill_action);

  // Preview execution of the skill.
  //
  // The result is returned as a Prediction, so it can be passed to
  // VisualizePredictions.
  //
  // If execute_timeout is omitted, a default timeout will be used.
  // This function runs inside the skill thread.
  absl::StatusOr<intrinsic_proto::skills::Prediction> PreviewSkill(
      SkillInstance& skill_instance, absl::string_view world_id,
      const intrinsic_proto::executive::BehaviorCall& behavior_call_proto,
      std::optional<absl::Duration> execute_timeout,
      const intrinsic_proto::data_logger::Context& context_proto);

  // Executes Preview for an active skill call.
  // This function runs inside the skill thread.
  absl::StatusOr<std::optional<::google::protobuf::Any>> PerformSkillPreview(
      absl::string_view action_id, SkillInstance& skill_instance,
      const clips::Symbol& sim_mode, absl::string_view world_id,
      const intrinsic_proto::executive::BehaviorCall& behavior_call,
      const std::optional<absl::Duration>& execute_timeout,
      const intrinsic_proto::data_logger::Context& context);

  // Deals with a skill that reported a timeout during PerformSkillExecute.
  // This function runs inside the skill thread.
  absl::StatusOr<intrinsic_proto::skills::ExecuteResult>
  PerformSkillExecuteTimeoutHandling(absl::string_view action_id,
                                     SkillInstance& skill_instance);

  // Actually does the execute call handling for a skill.
  // The function might report intermediate state updated for action_id (e.g.,
  // RUNNING), but the outcome of the skill is determined by the return value of
  // this function.
  // This function runs inside the skill thread.
  absl::StatusOr<std::optional<::google::protobuf::Any>> PerformSkillExecute(
      absl::string_view action_id, SkillInstance& skill_instance,
      absl::string_view world_id,
      const intrinsic_proto::executive::BehaviorCall& behavior_call,
      const std::optional<absl::Duration>& execute_timeout,
      const intrinsic_proto::data_logger::Context& context);

  clips::EnvironmentAssertFacade* assert_facade_;  // externally owned.
  clips::ProtobufManager* proto_manager_;          // externally owned.
  clips::TraceSpanManager* span_manager_;          // externally owned.
  SkillClientGenerator* skill_client_generator_;   // externally owned.
  intrinsic_proto::world::ObjectWorldService::StubInterface*
      object_world_service_stub_;  // externally owned, may be nullptr.
  intrinsic_proto::simulation::first_party::SimulationService::StubInterface*
      simulation_service_;  // externally owned.
  std::optional<intrinsic::ThreadPool> bundle_;
  absl::Mutex action_id_to_skill_instance_mutex_;
  absl::node_hash_map<std::string, std::shared_ptr<SkillInstance>>
      action_id_to_skill_instance_
          ABSL_GUARDED_BY(action_id_to_skill_instance_mutex_);
};

}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_ENGINE_CLIPS_SKILL_DISPATCHER_H_
