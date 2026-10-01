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

#ifndef INTRINSIC_EXECUTIVE_ENGINE_CLIPS_EXECUTOR_H_
#define INTRINSIC_EXECUTIVE_ENGINE_CLIPS_EXECUTOR_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/assets/proto/id.pb.h"
#include "intrinsic/assets/proto/installed_assets.grpc.pb.h"
#include "intrinsic/assets/proto/v1alpha1/asset_info_internal.grpc.pb.h"
#include "intrinsic/conductor/proto/conductor.grpc.pb.h"
#include "intrinsic/executive/clips/cc/cel.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/trace.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/engine/blackboard.h"
#include "intrinsic/executive/engine/clips_conductor_client.h"
#include "intrinsic/executive/engine/clips_logger.h"
#include "intrinsic/executive/engine/clips_skill_dispatcher.h"
#include "intrinsic/executive/engine/clips_world.h"
#include "intrinsic/executive/engine/code_execution_dispatcher.h"
#include "intrinsic/executive/engine/pubsub.h"
#include "intrinsic/executive/engine/skill_client_generator.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"
#include "intrinsic/executive/proto/blackboard_service.pb.h"
#include "intrinsic/executive/proto/code_execution_service.grpc.pb.h"
#include "intrinsic/executive/proto/executive_config.pb.h"
#include "intrinsic/executive/proto/executive_debug_service.pb.h"
#include "intrinsic/executive/proto/executive_execution_mode.pb.h"
#include "intrinsic/executive/proto/executive_service.pb.h"
#include "intrinsic/executive/proto/run_metadata.pb.h"
#include "intrinsic/frontend/solution_service/proto/solution_service.grpc.pb.h"
#include "intrinsic/frontend/solution_service/proto/status.pb.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/resources/client/resource_registry_client_interface.h"
#include "intrinsic/simulation/service/proto/first_party/simulation_service.grpc.pb.h"
#include "intrinsic/skills/internal/skill_registry_client_interface.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/thread/stop_token.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/service/updater/world_updater.grpc.pb.h"
#include "intrinsic/world/service/world_compatibility_service.grpc.pb.h"
#include "intrinsic/world/service/world_service.grpc.pb.h"
#include "opentelemetry/trace/span.h"

namespace intrinsic {
namespace executive {

// The executor runs operations in the executive. It creates a separate thread
// in which events are processed.
//
// Use the 'Create' factory method, and 'Init' to start looping the executor.
class ClipsExecutor {
 public:
  struct ClipsExecutorCreateOptions {
    intrinsic_proto::executive::ExecutiveConfig config;
    std::shared_ptr<intrinsic::skills::SkillRegistryClientInterface>
        skill_registry_client;
    std::shared_ptr<intrinsic::resources::ResourceRegistryClientInterface>
        resource_registry_client;
    std::shared_ptr<
        intrinsic_proto::assets::v1::InstalledAssetsReader::StubInterface>
        installed_assets_stub;
    std::shared_ptr<
        intrinsic_proto::assets::v1alpha1::AssetInfoInternal::StubInterface>
        asset_info_internal_stub;
    std::shared_ptr<
        intrinsic_proto::executive::CodeExecutionService::StubInterface>
        code_execution_service_stub;
    std::shared_ptr<
        intrinsic_proto::world::internal::WorldService::StubInterface>
        world_service_stub;
    std::shared_ptr<intrinsic_proto::world::ObjectWorldService::StubInterface>
        object_world_service_stub;
    std::shared_ptr<intrinsic_proto::world::WorldUpdater::StubInterface>
        world_updater_stub;
    std::shared_ptr<
        intrinsic_proto::world::WorldCompatibilityService::StubInterface>
        world_compatibility_service_stub;
    std::shared_ptr<intrinsic_proto::simulation::first_party::
                        SimulationService::StubInterface>
        simulation_service_stub;
    std::shared_ptr<
        intrinsic_proto::solution::v1::SolutionService::StubInterface>
        solution_service_stub;
    std::shared_ptr<intrinsic_proto::conductor::ConductorService::StubInterface>
        conductor_service_stub;

    std::shared_ptr<clips::Environment> clips_env =
        std::make_unique<clips::Environment>();
    std::shared_ptr<clips::ProtobufManager> proto_mgr;
    bool loop_manually = false;
  };

  // Creates the executor with the given config, skill registry client, and
  // resource registry client. Init() needs to be called to start execution.
  // CLIPS Environment injection supported for testing. If not passed, will
  // create an environment by itself.
  static absl::StatusOr<std::unique_ptr<ClipsExecutor>> Create(
      ClipsExecutorCreateOptions create_options);

  virtual ~ClipsExecutor();

  // Starts looping the executor. This will start the execution of the default
  // plan (which might be empty) from the 'config_'. Can only be called once.
  virtual absl::Status Init();

  // If loop_manually is enabled (see constructor) used to run one loop.
  absl::Status Loop() ABSL_LOCKS_EXCLUDED(clips_->mutex());

  // Get the CLIPS environment mutex. Useful, e.g., with WaitForLoop().
  absl::Mutex* GetClipsMutex() const ABSL_LOCK_RETURNED(clips_->mutex()) {
    return clips_->mutex();
  }

  virtual intrinsic_proto::data_logger::Context GetStateLogContext(
      std::string_view operation_name) ABSL_LOCKS_EXCLUDED(clips_->mutex());

  void SetSkillServiceClientCreatorForTest(
      SkillServiceClientCreator skill_service_client_creator_mock) {
    skill_client_generator_->SetSkillServiceClientCreatorForTest(
        std::move(skill_service_client_creator_mock));
  }

  static constexpr absl::string_view kDefaultSceneId = "";
  struct StartOptions {
    clips::TraceSpanReferenceId execution_span_id =
        clips::TraceSpanManager::kInvalidTraceSpanReferenceId;
    std::optional<google::protobuf::Any> parameters = std::nullopt;
    absl::flat_hash_map<std::string, std::string> resources = {};

    // The scene ID to be associated with the operation.
    absl::string_view scene_id = kDefaultSceneId;

    std::vector<intrinsic_proto::executive::BehaviorTree::NodeIdentifier>
        recovery_nodes;
  };
  // Transitions the state from ACTIVE to RUNNING.
  // This call is async, and returns OK if all preconditions are met.
  // To block until the state has been updated, call WaitForLoop().
  // The passed in execution_span_id via options refers to an already created
  // span to be used for the execution. Iff returning with OK, this span will be
  // ended by the executor.
  virtual absl::Status RequestToStart(absl::string_view operation_name,
                                      const StartOptions& options)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  // Transitions the state from SUSPENDED/SUSPENDING to RUNNING.
  // This call is async, and returns OK if all preconditions are met.
  // To block until the state has been updated, call WaitForLoop().
  virtual absl::Status RequestToResume(
      absl::string_view operation_name,
      intrinsic_proto::executive::ResumeOperationRequest::ResumeMode mode)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  absl::Status RequestToResume(absl::string_view operation_name) {
    return RequestToResume(
        operation_name,
        intrinsic_proto::executive::ResumeOperationRequest::CONTINUE);
  }

  // Transitions the state to SUSPENDING, and, after the last action has
  // terminated, to SUSPENDED.
  // This call is async, and returns OK if all preconditions are met.
  // To block until the state has been updated, call WaitForLoop().
  virtual absl::Status RequestToSuspend(absl::string_view operation_name)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  // Transitions the state from RUNNING/SUSPENDING/SUSPENDED to CANCELLING. The
  // executive will then transition to either SUCCEEDED or FAILED, depending on
  // the outcome of the affected actions.
  // This call is async, returning OK if all preconditions are met.
  // To block until the state has been updated, call WaitForLoop().
  virtual absl::Status RequestToCancel(absl::string_view operation_name);

  // Abandon this operation, i.e., just ignore all ongoing skill calls.
  // Must already be CANCELLING.
  virtual absl::Status RequestToAbandon(absl::string_view operation_name);

  // Only allowed while executive is in ACTIVE, SUSPENDED, FAILED, or SUCCEEDED
  // state. Resets to process tree (as if it were freshly loaded) and switches
  // to ACTIVE state.
  virtual absl::Status ResetOperation(absl::string_view operation_name,
                                      bool keep_blackboard)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  // Only allowed while executive is in ACTIVE, SUSPENDED, FAILED, or SUCCEEDED
  // state. Deletes the current process tree for operation_name.
  virtual absl::Status DeleteOperation(std::string_view operation_name)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  // Create a new operation in CLIPS. The given tree is imported as the process
  // tree for the operation.
  virtual absl::Status CreateOperation(
      absl::string_view operation_name,
      const intrinsic_proto::executive::BehaviorTree& process_tree,
      intrinsic_proto::status::ExtendedStatus* absl_nullable
          diagnostics_for_create,
      bool test_skip_parameter_validation = false)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  // Same as CreateOperation, but retrieves the BehaviorTree from process_id.
  virtual absl::Status CreateOperationFromProcessId(
      absl::string_view operation_name, intrinsic_proto::assets::Id process_id,
      intrinsic_proto::status::ExtendedStatus* absl_nullable
          diagnostics_for_create,
      bool test_skip_parameter_validation = false)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  // Blocks until Loop() has been called (from another thread).
  // This is useful when waiting for a state update after
  // RequestToStart/RequestToResume/RequestToSuspend has been called.
  virtual void WaitForLoop() ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());

  virtual absl::Status SetSimulationMode(
      absl::string_view operation_name,
      intrinsic_proto::executive::SimulationMode mode)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::Status SetExecutionMode(
      absl::string_view operation_name,
      intrinsic_proto::executive::ExecutionMode mode)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::Status SetStartNode(absl::string_view operation_name,
                                    absl::string_view start_tree_id,
                                    uint32_t start_node_id)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::Status UpdateInitialMetadata(
      std::string_view operation_name,
      intrinsic_proto::executive::RunMetadata* metadata)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());

  virtual absl::Status SetSkillTraceHandling(
      absl::string_view operation_name,
      intrinsic_proto::executive::RunMetadata::TracingInfo::SkillTraceHandling
          skill_trace_handling) ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::StatusOr<std::vector<std::string>> GetOperationNames()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());

  virtual absl::StatusOr<intrinsic_proto::executive::RunMetadata::State>
  GetOperationState(absl::string_view operation_name)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::StatusOr<google::protobuf::Any> GetOperationReturnValue(
      absl::string_view operation_name) const
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  absl::StatusOr<intrinsic_proto::status::ExtendedStatus>
  GetOperationExtendedStatusWithLegacyErrors(
      absl::string_view operation_name) const;

  virtual absl::StatusOr<clips::Symbol> GetProcessTreeId(
      absl::string_view operation_name)
      ABSL_ASSERT_EXCLUSIVE_LOCK(clips_->mutex());

  virtual absl::StatusOr<std::string> GetProcessTreeScope(
      absl::string_view operation_name) ABSL_LOCKS_EXCLUDED(clips_->mutex());
  // Retrieves the process tree with state information. This should only ever be
  // used to report state information about the tree's nodes, but not for state
  // of an operation. Use GetOperationStateNoLock for that.
  virtual absl::StatusOr<intrinsic_proto::executive::BehaviorTree>
  GetProcessTree(absl::string_view operation_name)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());
  virtual absl::StatusOr<intrinsic_proto::executive::BehaviorTree>
  GetProcessTreeNoLock(absl::string_view operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());

  virtual absl::StatusOr<google::protobuf::Any> GetBlackboardValue(
      absl::string_view key, absl::string_view scope,
      absl::string_view operation_name) ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::Status UpdateBlackboardValue(absl::string_view key,
                                             absl::string_view scope,
                                             absl::string_view operation_name,
                                             const google::protobuf::Any& value)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::StatusOr<
      std::vector<intrinsic_proto::executive::BlackboardValue>>
  ListBlackboardValues(
      absl::string_view request_scope, absl::string_view operation_name,
      intrinsic_proto::executive::ListBlackboardValuesRequest::View view)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::Status DeleteBlackboardValue(absl::string_view key,
                                             absl::string_view scope,
                                             absl::string_view operation_name)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::StatusOr<intrinsic_proto::executive::BlackboardSnapshot>
  CreateBlackboardSnapshot(
      absl::string_view operation_name, absl::string_view display_name,
      intrinsic_proto::executive::BlackboardSnapshot::SnapshotSource
          snapshot_source) ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::StatusOr<intrinsic_proto::status::ExtendedStatus>
  LoadBlackboardSnapshot(const Blackboard::SnapshotHandle& handle,
                         absl::string_view target_operation_name)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual std::pair<std::vector<intrinsic_proto::executive::BlackboardSnapshot>,
                    int>
  ListBlackboardSnapshots(int page_size, int page_offset)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::Status DeleteBlackboardSnapshot(
      const Blackboard::SnapshotHandle& handle)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::Status CreateBreakpoint(
      const std::string& operation_name, const std::string& tree_id,
      uint32_t node_id,
      intrinsic_proto::executive::BehaviorTree::Breakpoint::Type
          breakpoint_type) ABSL_LOCKS_EXCLUDED(clips_->mutex());
  virtual absl::Status DeleteBreakpoint(const std::string& operation_name,
                                        const std::string& tree_id,
                                        uint32_t node_id)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());
  virtual absl::Status DeleteAllBreakpoints(const std::string& operation_name)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());
  virtual absl::StatusOr<
      std::vector<intrinsic_proto::executive::BehaviorTree::Breakpoint>>
  ListBreakpoints(const std::string& operation_name)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::Status SetNodeExecutionSettings(
      absl::string_view operation_name, absl::string_view tree_id,
      uint32_t node_id,
      const intrinsic_proto::executive::BehaviorTree::Node::ExecutionSettings&
          execution_settings) ABSL_LOCKS_EXCLUDED(clips_->mutex());

  // Starts an execution span and stores it in the span_mgr_.
  // This span is not set in the executive state and must be handled by the
  // caller appropriately in particular to ensure it is ended. Either pass the
  // responsibility to the executor, e.g., via RequestToStart or make
  // sure that the span is ended otherwise.
  absl::StatusOr<clips::TraceSpanReferenceId> StartExecutionSpan();

  // Retrieves the current execution duration for the current operation. An
  // operation must be active to have a valid execution duration.
  // The duration is the time from start to finishing the operation, but without
  // counting suspended intervals.
  virtual absl::StatusOr<absl::Duration> GetExecutionDuration(
      std::string_view operation_name) ABSL_LOCKS_EXCLUDED(clips_->mutex());

  // Retrieves the start time looking for a running span of the operation. Thus
  // this will result in a valid start time from the time the operation is
  // started until it is finished.
  virtual absl::StatusOr<absl::Time> GetStartTime(
      std::string_view operation_name) ABSL_LOCKS_EXCLUDED(clips_->mutex());

  clips::TraceSpanManager* GetSpanManager() { return span_mgr_.get(); }

  virtual absl::Status SetClipsTracing(
      intrinsic_proto::executive::SetClipsTracingRequest::ClipsTracing
          clips_tracing) ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::StatusOr<uint64_t> GetClipsTracingPosition()
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::StatusOr<std::string> GetClipsTraceFileContents(
      uint64_t start_offset) ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::StatusOr<intrinsic_proto::executive::GetClipsDataResponse>
  GetClipsData() ABSL_LOCKS_EXCLUDED(clips_->mutex());

  virtual absl::StatusOr<std::string> GetSceneId(
      absl::string_view operation_name) ABSL_LOCKS_EXCLUDED(clips_->mutex());

 protected:
  ClipsExecutor();  // Only for testing via Mock.

 private:
  explicit ClipsExecutor(ClipsExecutorCreateOptions create_options);

  // Initializers.
  absl::Status InitExecutor() ABSL_LOCKS_EXCLUDED(clips_->mutex());
  absl::Status AddFunctions() ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  absl::Status InitWorld() ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  absl::Status InitPubSub() ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  absl::Status InitDispatchers() ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  absl::Status InitSkillClientGenerator()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  absl::Status InitFlags() ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  absl::Status InitNoOpActions() ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  absl::Status InitClipsTracing()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  absl::Status InitInternalLogger()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  absl::Status InitGCloudProject()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  absl::Status InitTracingAddTracingInfoToState()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  absl::Status InitTracingLinkOpencensus()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  absl::Status InitReportActiveActionsTimeout()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  absl::Status InitConductor() ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  void RetrieveSolutionInfoAndWarn();

  // This method is called at a fixed (maximum) rate after the executor is
  // initialized (with 'Init') and until it is terminated ('TearDown'). The
  // executor thread will yield briefly on each iteration to allow other threads
  // to interject by acquiring the CLIPS mutex, e.g. to call WaitForLoop().
  absl::Status InternalLoop() ABSL_LOCKS_EXCLUDED(clips_->mutex());

  // Record ExtendedStatus when the operation FAILED after running InternalLoop.
  absl::Status RecordOperationExtendedStatus(
      absl::string_view operation_name,
      const std::vector<clips::Fact>& facts_before, const clips::Trace& trace)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());

  // Adds additional debug information to the given ExtendedStatus.
  //
  // This is only available after a run has finished, as it reports on the facts
  // and the trace of that run.
  void AddExtendedStatusDebugInformation(
      intrinsic_proto::status::ExtendedStatus& es,
      const std::vector<clips::Fact>& facts_before, const clips::Trace& trace)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());

  // Logs the given ExtendedStatus for the operation
  absl::Status LogExecutiveExtendedStatus(
      const intrinsic_proto::status::ExtendedStatus& es,
      std::string_view operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());

  // Internal helpers.
  absl::StatusOr<intrinsic_proto::executive::RunMetadata::State>
  GetOperationStateNoLock(absl::string_view operation_name)
      ABSL_ASSERT_EXCLUSIVE_LOCK(clips_->mutex());
  absl::StatusOr<absl::Time> GetStartTimeNoLock(std::string_view operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  // Get the fact for the given NodeIdentifier node.
  // The function returns the node fact, if the node fact can be found, its
  // referred tree exists and - when operation_name is not empty - if the node
  // is in the tree given by operation_name. An error is returned if any of
  // these conditions do not hold.
  absl::StatusOr<clips::Fact> GetNodeFact(absl::string_view operation_name,
                                          absl::string_view tree_id,
                                          uint32_t node_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  absl::StatusOr<clips::TraceSpanReferenceId> GetExecutionSpanIdNoLock()
      ABSL_ASSERT_EXCLUSIVE_LOCK(clips_->mutex());
  intrinsic_proto::data_logger::Context GetStateLogContextNoLock(
      std::string_view operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  absl::Status SetWorldId(absl::string_view flag_name,
                          absl::string_view world_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());

  // Returns OK if executive is initialized and loop has not been
  // terminated.
  absl::Status RunStatus() ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());
  void Run(StopToken stop_token) ABSL_LOCKS_EXCLUDED(clips_->mutex());
  absl::Status RunWithStatus(StopToken stop_token)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());
  absl::Status TearDown() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&teardown_mutex_);

  // Starts a span to capture gRPC calls during one clips run.
  // Besides the returned ScopedSpan it will also automatically update
  // clips_runs_span_ - the parent span for an individual clips run.
  // A clips run span is started iff there is a valid execution span.
  // The function also makes sure that a new clips_runs_span_ is started
  // for each new execution span.
  std::shared_ptr<opentelemetry::trace::Span> StartClipsRunSpan();

  // Assert tracing-clips-run-context depending on the given run_span for the
  // current CLIPS runs. The given span is added to the TraceSpanManager to make
  // it available in CLIPS and should thus be ended via the span manager using
  // the returned TraceSpanReferenceId.
  clips::TraceSpanReferenceId SetClipsRunContext(
      std::shared_ptr<opentelemetry::trace::Span> run_span)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());

  // Remove tracing-clips-run-context that might have been set previously.
  void RemoveClipsRunContext() ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex());

  // Call the `Conductor::PrepareProcessStart` RPC and returns the client
  // operation name. Annotated as ABSL_LOCKS_EXCLUDED because the RPC is done
  // without blocking the CLIPS thread, but a successful call will acquire the
  // mutex to assert facts in CLIPS.
  absl::StatusOr<std::string> CallConductorPrepareProcessStart(
      absl::string_view operation_name, absl::string_view scene_id,
      clips::TraceSpanReferenceId execution_span_id)
      ABSL_LOCKS_EXCLUDED(clips_->mutex());

  std::shared_ptr<clips::Environment> clips_;
  std::shared_ptr<clips::ProtobufManager> proto_mgr_;
  std::unique_ptr<clips::CelManager> cel_mgr_;
  std::unique_ptr<clips::TraceSpanManager> span_mgr_;
  Blackboard blackboard_;

  std::shared_ptr<intrinsic::skills::SkillRegistryClientInterface>
      skill_registry_client_;
  std::shared_ptr<intrinsic::resources::ResourceRegistryClientInterface>
      resource_registry_client_;
  std::unique_ptr<SkillClientGenerator> skill_client_generator_;
  std::unique_ptr<ClipsSkillDispatcher> skill_dispatcher_;
  std::shared_ptr<
      intrinsic_proto::assets::v1::InstalledAssetsReader::StubInterface>
      installed_assets_stub_;
  std::unique_ptr<CodeExecutionDispatcher> code_execution_dispatcher_;
  std::shared_ptr<
      intrinsic_proto::executive::CodeExecutionService::StubInterface>
      code_execution_service_stub_;
  std::shared_ptr<intrinsic_proto::world::internal::WorldService::StubInterface>
      world_service_stub_;
  std::shared_ptr<intrinsic_proto::world::ObjectWorldService::StubInterface>
      object_world_service_stub_;
  std::shared_ptr<intrinsic_proto::world::WorldUpdater::StubInterface>
      world_updater_stub_;
  std::shared_ptr<
      intrinsic_proto::world::WorldCompatibilityService::StubInterface>
      world_compatibility_service_stub_;
  std::shared_ptr<intrinsic_proto::simulation::first_party::SimulationService::
                      StubInterface>
      simulation_service_stub_;
  std::unique_ptr<ClipsLogger> clips_logger_;
  std::unique_ptr<ClipsWorld> clips_world_;
  std::unique_ptr<ClipsPubSub> clips_pub_sub_;
  std::shared_ptr<intrinsic_proto::solution::v1::SolutionService::StubInterface>
      solution_service_stub_;
  std::shared_ptr<intrinsic_proto::conductor::ConductorService::StubInterface>
      conductor_service_stub_;
  std::unique_ptr<ClipsConductorClient> clips_conductor_client_;

  // Guards calls to 'TearDown' and fiber cleanups during object destruction.
  absl::Mutex teardown_mutex_;
  bool is_initialized_ = false;  // Toggled after Init().
  bool quit_ = false;  // Toggled when executive receives 'Quit' signal.

  // The ExtendedStatus to be reported for each FAILED operation.
  // It is set exactly once as soon as the operation is FAILED. If an operation
  // is not FAILED there is no entry. This contains the ExtendedStatus of the
  // operation and additionally contexts from legacy error reporting via (error)
  // facts.
  // Existence of a key also indicates that it has been logged.
  absl::flat_hash_map<std::string, intrinsic_proto::status::ExtendedStatus>
      operation_extended_status_with_legacy_errors_
          ABSL_GUARDED_BY(clips_->mutex());

  absl::Status init_status_ ABSL_GUARDED_BY(clips_->mutex()) = absl::OkStatus();
  absl::Status run_status_ ABSL_GUARDED_BY(clips_->mutex()) = absl::OkStatus();
  intrinsic_proto::executive::ExecutiveConfig config_;
  absl::CondVar loop_cv_;       // Used to wait until loop signal to be done.
  absl::CondVar terminate_cv_;  // Used to wait until termination done.
  absl::Mutex loop_wait_mutex_;
  absl::CondVar loop_wait_cv_;      // Used to wait until next loop must run.
  std::unique_ptr<Thread> thread_;  // Continuous loops run in this thread.

  // TraceSpanReferenceId of the last execution span that the ClipsExecutor
  // started. This does not necessarily mean that the execution span is still
  // running. Query the span_mgr_ to determine if the span still exists.
  clips::TraceSpanReferenceId last_execution_span_reference_id_ =
      clips::TraceSpanManager::kInvalidTraceSpanReferenceId;
  // parent span for all internal clips run spans
  std::unique_ptr<intrinsic::stats::ScopedSpan> clips_runs_span_;
  // records the trace id of the clips_runs_span_'s parent span.
  // Used to determine, if we need to start a new clips_runs_span_,
  // because the parent changed (i.e., because we are in a new trace).
  std::string clips_runs_span_parent_trace_id_;

  std::optional<intrinsic_proto::solution::v1::Status> solution_status_;

  // If true, 'Init' will NOT loop continuously, but 'Loop' needs to be
  // called manually. This is useful for, e.g., tests.
  bool loop_manually_ = false;
};

}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_ENGINE_CLIPS_EXECUTOR_H_
