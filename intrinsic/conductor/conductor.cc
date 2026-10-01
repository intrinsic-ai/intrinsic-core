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

#include "intrinsic/conductor/conductor.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/empty.pb.h>
#include <grpcpp/create_channel.h>

#include <algorithm>
#include <thread>

#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "intrinsic/assets/scene_objects/proto/scene_object_manifest.pb.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/executive/proto/run_metadata.pb.h"
#include "intrinsic/kubernetes/acl/cc/client_context.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/scene/config/scene_object_config_updater.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/get_extended_status.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/util/thread/stop_token.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/util/thread/util.h"
#include "intrinsic/world/conversion/scene_object_to_world_updates.h"

namespace intrinsic::conductor {

using ::intrinsic::conductor::internal::UpdateWorldFromResourceSetResult;
using ::intrinsic_proto::conductor::ConfigureSceneObjectRequest;
using ::intrinsic_proto::conductor::ConfigureSceneObjectResponse;
using ExecutionContextProto = ::intrinsic_proto::conductor::ExecutionContext;
using ::intrinsic_proto::conductor::GetExecutionContextRequest;
using ::intrinsic_proto::conductor::GetSaveStatusRequest;
using ::intrinsic_proto::conductor::GetSaveStatusResponse;
using ::intrinsic_proto::conductor::NotifyAllProcessesStoppedRequest;
using ::intrinsic_proto::conductor::NotifyAllProcessesStoppedResponse;
using ::intrinsic_proto::conductor::PrepareProcessResumeRequest;
using ::intrinsic_proto::conductor::PrepareProcessResumeResponse;
using ::intrinsic_proto::conductor::PrepareProcessStartMetadata;
using ::intrinsic_proto::conductor::PrepareProcessStartRequest;
using ::intrinsic_proto::conductor::PrepareProcessStartResponse;
using ::intrinsic_proto::conductor::ResetRequest;
using ::intrinsic_proto::conductor::ResetResponse;
using ::intrinsic_proto::conductor::RestoreWorldRequest;
using ::intrinsic_proto::conductor::RestoreWorldResponse;
using ::intrinsic_proto::conductor::SaveWorldRequest;
using ::intrinsic_proto::conductor::SaveWorldResponse;
using ConductorStartSolutionRequest =
    ::intrinsic_proto::conductor::StartSolutionRequest;
using ConductorStartSolutionResponse =
    ::intrinsic_proto::conductor::StartSolutionResponse;
using ConductorStopSolutionRequest =
    ::intrinsic_proto::conductor::StopSolutionRequest;
using ConductorStopSolutionResponse =
    ::intrinsic_proto::conductor::StopSolutionResponse;
using SimStartSolutionRequest =
    ::intrinsic_proto::simulation::first_party::StartSolutionRequest;
using SimStartSolutionResponse =
    ::intrinsic_proto::simulation::first_party::StartSolutionResponse;
using SimStopSolutionRequest =
    ::intrinsic_proto::simulation::first_party::StopSolutionRequest;
using SimStopSolutionResponse =
    ::intrinsic_proto::simulation::first_party::StopSolutionResponse;
using ::intrinsic_proto::config::OperationMode;
using ::intrinsic_proto::config::OperationMode_Name;
using ::intrinsic_proto::simulation::first_party::GetSimulationStatusResponse;
using ::intrinsic_proto::simulation::first_party::ResetSimulationRequest;
using ::intrinsic_proto::world::CloneWorldRequest;
using ::intrinsic_proto::world::DeleteWorldRequest;
using ::intrinsic_proto::world::WorldMetadata;
using namespace intrinsic_proto::world;
using namespace intrinsic_proto::simulation::first_party;
using namespace intrinsic_proto::assets::v1;
using namespace intrinsic_proto::scene_objects;
using namespace intrinsic_proto::executive;

using ::intrinsic_proto::assets::UpdateResourceRequest;
using ::intrinsic_proto::assets::v1::AssetInstance;
using ::intrinsic_proto::assets::v1::GetAssetInstanceRequest;
using ::intrinsic_proto::assets::v1::InstanceConfig;
using ::intrinsic_proto::resources::GeometricResourceInstanceData;
using ::intrinsic_proto::scene_object::v1::SceneObject;
using ::intrinsic_proto::scene_object::v1::SceneObjectConfig;
using ::intrinsic_proto::scene_object::v1::SceneObjectInstanceUpdates;

namespace {

using ::intrinsic_proto::status::ExtendedStatus;

constexpr int kExtstatusCodeEditWorldStartedWithWarning = 10001;
constexpr int kExtstatusCodeEditWorldSkippedWorldUpdate = 10002;
constexpr absl::string_view kConductorComponent = "ai.intrinsic.conductor";
constexpr absl::Duration kDefaultGrpcTimeout = absl::Seconds(30);
constexpr absl::Duration kSimResetTimeout = absl::Seconds(60);

std::unique_ptr<grpc::ClientContext> CreateClientContext(
    const acl::User* user, absl::Duration timeout = kDefaultGrpcTimeout) {
  auto context =
      user ? user->NewClientContext() : std::make_unique<grpc::ClientContext>();
  if (timeout != absl::InfiniteDuration()) {
    context->set_deadline(absl::ToChronoTime(absl::Now() + timeout));
  }
  return context;
}

ExtendedStatus MakeEditWorldStatus(
    const UpdateWorldFromResourceSetResult& compose_result) {
  int num_errors = compose_result.skipped_updates_size() +
                   compose_result.skipped_scene_objects_size();
  std::string plural = num_errors == 1 ? "" : "s";

  StatusBuilder::ExtendedStatusOptions options;
  options.title = absl::StrCat("Encountered ", num_errors, " error", plural,
                               " when composing Initial World");
  options.user_instructions =
      "Unable to compose initial world. To resolve this issue, please click "
      "'Scene -> Discard Changes'. If the issue persists, please click the "
      "'Support' button in the app bar and provide the error details.\n"
      "You can save your current solution without the problematic objects and "
      "updates by clicking 'File'  -> 'Save' in the top menu.";

  for (const auto& update : compose_result.skipped_updates()) {
    StatusBuilder::ExtendedStatusOptions child_options;
    child_options.title = "Skipped invalid world update";
    std::string msg = absl::StrCat(
        "Skipped invalid world update: ", update.update().ShortDebugString(),
        ", Error: ", update.error().message());
    LOG(WARNING) << msg;
    child_options.user_message = msg;
    absl::Status child_status =
        StatusBuilder(kConductorComponent,
                      kExtstatusCodeEditWorldSkippedWorldUpdate, child_options);
    options.context.push_back(*GetExtendedStatus(child_status));
  }

  for (const auto& update : compose_result.skipped_scene_objects()) {
    StatusBuilder::ExtendedStatusOptions child_options;
    child_options.title = "Skipped invalid scene object";
    std::string msg = absl::StrCat("Skipped invalid scene object '",
                                   update.scene_object_name(),
                                   "'. Error: ", update.error().message());
    LOG(WARNING) << msg;
    child_options.user_message = msg;
    absl::Status child_status =
        StatusBuilder(kConductorComponent,
                      kExtstatusCodeEditWorldSkippedWorldUpdate, child_options);
    options.context.push_back(*GetExtendedStatus(child_status));
  }

  absl::Status status = StatusBuilder(
      kConductorComponent, kExtstatusCodeEditWorldStartedWithWarning, options);
  return *GetExtendedStatus(status);
}

// Creates or updates init_world from solution document.
absl::StatusOr<internal::UpdateWorldFromResourceSetResult> CreateInitWorld(
    const ExecutionContext& ec, const ResourceWorld& resource_world,
    bool skip_invalid_world_updates) {
  // Creates world with retries to account for transient errors, e.g., related
  // to downloading (or uploading) geos from CAS. Technically, we can introspect
  // the error to only retry for transient errors but it seems safer to just
  // retry (actual, permanent errors should be really cheap as far retry is
  // concerned).
  const std::string world_id = ec.EditWorldID();
  constexpr int max_tries = 3;
  const absl::Duration kRetryInitialWorldInterval = absl::Seconds(2);
  UpdateWorldFromResourceSetResult update_result;
  for (int i = 0; i < max_tries; ++i) {
    if (i != 0) {
      LOG(WARNING) << absl::Substitute(
          "Got error updating world from resource set, retrying (attempt "
          "$0/$1)...",
          i + 1, max_tries);
      absl::SleepFor(kRetryInitialWorldInterval);
    }

    INTR_ASSIGN_OR_RETURN(update_result,
                          resource_world.UpdateWorldFromResourceSet(
                              world_id, skip_invalid_world_updates),
                          _ << "could not load resource set into edit world");

    if (update_result.skipped_updates_size() == 0 &&
        update_result.skipped_scene_objects_size() == 0) {
      break;
    }

    LOG(ERROR) << "Unable to construct world from resource set after " << i + 1
               << " attempts";
  }
  return update_result;
}

}  // namespace

class ExecutionContextOperation {
 public:
  using OperationFn = std::function<absl::StatusOr<google::protobuf::Any>(
      std::stop_token stop_token)>;

  static std::unique_ptr<ExecutionContextOperation> New(
      const std::string& name, const google::protobuf::Any& metadata,
      OperationFn op_fn) {
    auto op = std::unique_ptr<ExecutionContextOperation>(
        new ExecutionContextOperation(name, metadata));
    op->Start(std::move(op_fn));
    return op;
  }

  ~ExecutionContextOperation() = default;

  bool IsDone() { return done_.HasBeenNotified(); }

  std::string Name() ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(mu_);
    return proto_.name();
  }

  google::longrunning::Operation Proto() ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(mu_);
    return proto_;
  }

  void Cancel() { thread_.request_stop(); }

  absl::Status Wait(grpc::ServerContext* context) {
    while (!done_.WaitForNotificationWithTimeout(absl::Milliseconds(100))) {
      if (context && context->IsCancelled()) {
        return absl::CancelledError(
            "Context cancelled while waiting for operation");
      }
    }
    return absl::OkStatus();
  }

 private:
  ExecutionContextOperation(const std::string& name,
                            const google::protobuf::Any& metadata) {
    proto_.set_name(name);
    *proto_.mutable_metadata() = metadata;
  }

  void Start(OperationFn op_fn) {
    thread_ = std::jthread([this, op_fn = std::move(op_fn)](
                               std::stop_token stop_token) {
      auto res = op_fn(stop_token);

      absl::MutexLock lock(mu_);
      proto_.set_done(true);
      if (!res.ok()) {
        LOG(INFO) << "Operation " << proto_.name()
                  << " is done with error: " << res.status();
        *proto_.mutable_error() = intrinsic::ToGoogleRpcStatus(res.status());
      } else {
        LOG(INFO) << "Operation " << proto_.name() << " succeeded";
        *proto_.mutable_response() = *res;
      }
      done_.Notify();
    });
  }

  absl::Mutex mu_;
  google::longrunning::Operation proto_ ABSL_GUARDED_BY(mu_);
  std::jthread thread_;
  absl::Notification done_;
};

ConductorImpl::ConductorImpl(ConductorOptions opts)
    : kv_store_(std::move(opts.kv_store)),
      execution_context_(std::move(opts.execution_context)),
      resource_world_(std::move(opts.resource_world)),
      world_service_(opts.world_service),
      sim_service_stub_(std::move(opts.sim_service_stub)),
      world_updater_(opts.world_updater),
      asset_instances_stub_(std::move(opts.asset_instances_stub)),
      asset_deployment_stub_(std::move(opts.asset_deployment_stub)),
      asset_deployment_lro_stub_(std::move(opts.asset_deployment_lro_stub)),
      installed_assets_stub_(std::move(opts.installed_assets_stub)),
      installed_assets_lro_stub_(std::move(opts.installed_assets_lro_stub)),
      pause_sim_(opts.enable_sim_pause),
      ec_op_(nullptr),
      save_scene_monitor_(std::move(opts.save_scene_monitor)) {}

ConductorImpl::~ConductorImpl() {
  save_scene_monitor_.reset();
  CancelExecutionContextOperation(/*context=*/nullptr, /*wait=*/true);
}

grpc::Status ConductorImpl::StartSolution(
    grpc::ServerContext* context, const ConductorStartSolutionRequest* request,
    ConductorStartSolutionResponse* response) {
  const stats::ScopedSpan span("conductor.StartSolution", context);
  OperationMode operation_mode = request->operation_mode();
  if (operation_mode == OperationMode::OPERATION_MODE_UNSPECIFIED) {
    LOG(WARNING) << "Solution was started without specified operation mode. "
                    "Defaulting to simulation.";
    operation_mode = OperationMode::SIMULATION;
  }

  absl::MutexLock lock(mu_);

  LOG(INFO) << "Resetting execution context for application running in "
            << OperationMode_Name(operation_mode);
  if (execution_context_ != nullptr) {
    LOG(INFO) << "StartSolution was called without calling StopSolution. "
                 "Overwriting execution context.";
    execution_context_->Close();
  }

  INTR_ASSIGN_OR_RETURN_GRPC(
      execution_context_,
      ExecutionContext::Create(operation_mode, kv_store_.get()));

  INTR_RETURN_IF_ERROR_GRPC(execution_context_->SetEditWorldID("init_world"))
      << "failed to set edit world ID: ";

  // Resets the world service just in case except for the ones that are
  // relevant for the execution context.
  const std::vector<std::string> worlds_to_not_delete = {
      execution_context_->BeliefWorldID(), execution_context_->EditWorldID(),
      execution_context_->SimWorldID()};
  INTR_RETURN_IF_ERROR_GRPC(ResetWorldService(worlds_to_not_delete));

  return CreateWorldsFromSolution(context,
                                  request->skip_invalid_world_updates());
}

grpc::Status ConductorImpl::CreateWorldsFromSolution(
    grpc::ServerContext* context, bool skip_invalid_world_updates) {
  // Seeds the initial world from the solution document.
  INTR_ASSIGN_OR_RETURN_GRPC(
      UpdateWorldFromResourceSetResult update_result,
      CreateInitWorld(*execution_context_, *resource_world_,
                      skip_invalid_world_updates));

  // Pause the world updater before we clone into the belief world.
  INTR_RETURN_IF_ERROR_GRPC(PauseWorldUpdater())
      << "Pausing world updater failed";

  // Always resume, but wait until after we have reset the updater below at a
  // minimum.
  auto resume_updater = [this](grpc::Status current_status) -> grpc::Status {
    absl::Status resume_status = ResumeWorldUpdater();
    if (resume_status.ok()) {
      return current_status;
    }

    if (current_status.ok()) {
      return StatusBuilderGrpc(resume_status).LogError()
             << "could not resume belief world updater";
    }
    return StatusBuilderGrpc(resume_status).LogError()
           << "world updater failed to resume in addition to other "
              "failures: "
           << current_status.error_message();
  };

  // Clone edit world to belief world.
  CloneWorldRequest clone_request;
  clone_request.set_world_id(execution_context_->EditWorldID());
  clone_request.set_cloned_world_id(execution_context_->BeliefWorldID());
  clone_request.set_allow_overwrite(true);
  WorldMetadata clone_response;
  grpc::ServerContext clone_context;
  grpc::Status clone_status = world_service_.CloneWorld(
      &clone_context, &clone_request, &clone_response);
  if (!clone_status.ok()) {
    return resume_updater(StatusBuilderGrpc(clone_status).LogError()
                          << "CloneEditWorldToBeliefWorld failed");
  }

  if (update_result.skipped_updates_size() > 0 ||
      update_result.skipped_scene_objects_size() > 0) {
    absl::Status status = execution_context_->SetEditWorldStatus(
        MakeEditWorldStatus(update_result));
    if (!status.ok()) {
      return resume_updater(ToGrpcStatus(status));
    }
  }

  // Checks for cancellation before resetting the world updater.
  if (context != nullptr && context->IsCancelled()) {
    return resume_updater(grpc::Status(grpc::StatusCode::CANCELLED,
                                       "Context cancelled during reset"));
  }

  // Reset the world updater so that we can pick up any newly deployed robots.
  ResetUpdaterRequest reset_request;
  reset_request.mutable_belief_world_options()->set_world_id(
      execution_context_->BeliefWorldID());
  reset_request.mutable_sim_world_options()->set_world_id(
      execution_context_->SimWorldID());
  ResetUpdaterResponse reset_response;
  grpc::ServerContext reset_context;
  grpc::Status updater_reset_status =
      world_updater_.Reset(&reset_context, &reset_request, &reset_response);
  if (!updater_reset_status.ok()) {
    return resume_updater(StatusBuilderGrpc(updater_reset_status).LogError()
                          << "Resetting world updater failed");
  }

  // Call the simulation service to initialize the sim world and the simulator.
  // On hardware deployments, this RPC is still called to disconnect from the
  // simulator (which might not be available on real hardware).
  SimStartSolutionRequest ss_request;
  ss_request.set_operation_mode(execution_context_->OperationMode());
  auto* sim_env = ss_request.mutable_simulation_env();
  sim_env->set_simulator_world_id(execution_context_->SimWorldID());
  sim_env->set_start_world_id(execution_context_->EditWorldID());
  sim_env->set_simulator_world_state_updates_pubsub_topic_name(
      reset_response.sim_world_updates_topic_name());

  auto ss_context =
      acl::NewClientContextWithDeadline(context, kDefaultGrpcTimeout);
  SimStartSolutionResponse ss_response;
  grpc::Status ss_status = sim_service_stub_->StartSolution(
      ss_context.get(), ss_request, &ss_response);
  if (!ss_status.ok()) {
    return resume_updater(
        StatusBuilderGrpc(ss_status).LogError()
        << "could not initialize the simulation service on solution start");
  }

  return resume_updater(grpc::Status::OK);
}

grpc::Status ConductorImpl::GetExecutionContext(
    grpc::ServerContext* context, const GetExecutionContextRequest* request,
    ExecutionContextProto* response) {
  const stats::ScopedSpan span("conductor.GetExecutionContext", context);
  absl::ReaderMutexLock lock(&mu_);

  if (execution_context_ == nullptr) {
    return ErrInvalidExecutionContext();
  }
  *response = execution_context_->ToProto();
  return grpc::Status::OK;
}

grpc::Status ConductorImpl::StopSolution(
    grpc::ServerContext* context, const ConductorStopSolutionRequest* request,
    ConductorStopSolutionResponse* response) {
  const stats::ScopedSpan span("conductor.StopSolution", context);
  INTR_RETURN_IF_ERROR_GRPC(
      CancelExecutionContextOperation(context, /*wait=*/true));

  absl::MutexLock lock(mu_);

  if (sim_service_stub_) {
    auto sim_stop_context =
        acl::NewClientContextWithDeadline(context, kDefaultGrpcTimeout);
    SimStopSolutionRequest sim_stop_request;
    SimStopSolutionResponse sim_stop_response;
    grpc::Status sim_stop_status = sim_service_stub_->StopSolution(
        sim_stop_context.get(), sim_stop_request, &sim_stop_response);
    if (!sim_stop_status.ok()) {
      LOG(WARNING) << "failed to notify simulation service on solution stop: "
                   << sim_stop_status.error_message();
    }
  }

  grpc::Status status = ResetWorldService({});
  if (!status.ok()) {
    return StatusBuilderGrpc(status).LogError()
           << "Failed to reset world service";
  }

  execution_context_.reset();
  return grpc::Status::OK;
}

grpc::Status ConductorImpl::Reset(grpc::ServerContext* context,
                                  const ResetRequest* request,
                                  ResetResponse* response) {
  const stats::ScopedSpan span("conductor.Reset", context);
  absl::MutexLock lock(mu_);

  if (execution_context_ == nullptr) {
    return ErrInvalidExecutionContext();
  }

  std::string src_world_id = request->edit_world_id();
  if (src_world_id.empty()) {
    src_world_id = execution_context_->EditWorldID();
    LOG(INFO) << "Reset: edit world id not provided, fallback to ec: "
              << src_world_id;
  }

  absl::Status status = ResetBeliefWorldAndSim(context, src_world_id);
  if (!status.ok()) {
    LOG(WARNING) << "Reset: could not reset: " << status.message();
    return ToGrpcStatus(status);
  }

  return grpc::Status::OK;
}

grpc::Status ConductorImpl::CancelExecutionContextOperation(
    grpc::ServerContext* context, bool wait) {
  absl::MutexLock lock(ec_op_mu_);
  if (ec_op_ == nullptr || ec_op_->IsDone()) {
    return grpc::Status::OK;
  }

  LOG(INFO) << "Cancelling existing long-running operation " << ec_op_->Name();
  ec_op_->Cancel();

  if (wait) {
    auto status = ec_op_->Wait(context);
    if (!status.ok()) {
      return ToGrpcStatus(status);
    }
  }

  return grpc::Status::OK;
}

absl::Status ConductorImpl::ResetSim(grpc::ServerContext* context,
                                     std::stop_token stop_token) {
  auto user = acl::CreateGrpcMetadataUserFromServerContext(*context);
  return ResetSim(user.get(), stop_token);
}

absl::Status ConductorImpl::ResetSim(const acl::User* user,
                                     std::stop_token stop_token) {
  if (!execution_context_) {
    return ToAbslStatus(ErrInvalidExecutionContext());
  }

  if (execution_context_->OperationMode() == OperationMode::SIMULATION) {
    LOG(INFO) << "Resetting simulation.";
    ResetSimulationRequest sim_reset_request;
    sim_reset_request.set_start_world_id(execution_context_->BeliefWorldID());

    auto sim_reset_context = CreateClientContext(user, kSimResetTimeout);

    std::stop_callback cb(stop_token,
                          [&]() { sim_reset_context->TryCancel(); });

    LOG(INFO) << "Resetting simulation with timeout.";
    google::protobuf::Empty sim_reset_response;
    grpc::Status sim_reset_status = sim_service_stub_->ResetSimulation(
        sim_reset_context.get(), sim_reset_request, &sim_reset_response);
    if (!sim_reset_status.ok()) {
      return ToAbslStatus(StatusBuilderGrpc(sim_reset_status).LogError()
                          << "Failed to reset simulation");
    }
  }

  return absl::OkStatus();
}

absl::Status ConductorImpl::ResetBeliefWorldAndSim(
    const acl::User* user, const std::string& src_world_id,
    std::stop_token stop_token) {
  if (!execution_context_) {
    return ToAbslStatus(ErrInvalidExecutionContext());
  }

  const auto& belief_world_id = execution_context_->BeliefWorldID();

  // Pause the world updater until we compose the new world. This is required to
  // avoid stale updates from being merged into the belief world after it is
  // reset from the edit world.
  INTR_RETURN_IF_ERROR(PauseWorldUpdater(stop_token))
      << "Failed to pause world updater";

  auto resume_updater = [this](absl::Status current_status) -> absl::Status {
    absl::Status resume_status = ResumeWorldUpdater();
    if (resume_status.ok()) {
      return current_status;
    }

    if (current_status.ok()) {
      return StatusBuilderGrpc(resume_status).LogError()
             << "could not resume belief world updater";
    }
    return StatusBuilderGrpc(resume_status).LogError()
           << "world updater failed to resume in addition to other failures: "
           << current_status.message();
  };

  LOG(INFO) << absl::Substitute(
      "Using world '$0' as initial state of belief world '$1'", src_world_id,
      belief_world_id);

  CloneWorldRequest clone_request;
  clone_request.set_world_id(src_world_id);
  clone_request.set_cloned_world_id(belief_world_id);
  clone_request.set_allow_overwrite(true);

  if (stop_token.stop_requested()) {
    return resume_updater(
        absl::CancelledError("ResetBeliefWorldAndSim was cancelled."));
  }

  WorldMetadata clone_response;
  grpc::ServerContext clone_context;
  std::stop_callback cb(stop_token, [&]() { clone_context.TryCancel(); });
  grpc::Status clone_status = world_service_.CloneWorld(
      &clone_context, &clone_request, &clone_response);
  if (!clone_status.ok()) {
    return resume_updater(ToAbslStatus(
        StatusBuilderGrpc(clone_status).LogError()
        << absl::Substitute("Failed to clone world '$0' to belief world '$1'",
                            src_world_id, belief_world_id)));
  }

  // Reset sim from belief world
  absl::Status sim_status = ResetSim(user, stop_token);
  if (!sim_status.ok()) {
    LOG(WARNING) << "Failed to reset sim: " << sim_status.message();
  }

  return resume_updater(sim_status);
}

absl::Status ConductorImpl::ResetBeliefWorldAndSim(
    grpc::ServerContext* context, const std::string& src_world_id,
    std::stop_token stop_token) {
  auto user = acl::CreateGrpcMetadataUserFromServerContext(*context);
  return ResetBeliefWorldAndSim(user.get(), src_world_id, stop_token);
}

absl::StatusOr<std::unique_ptr<ExecutionContextOperation>>
ConductorImpl::PrepareProcessStartImpl(
    grpc::ServerContext* server_context, std::shared_ptr<const acl::User> user,
    const PrepareProcessStartRequest* request, bool reset) {
  absl::MutexLock lock(mu_);

  if (execution_context_ == nullptr) {
    return ToAbslStatus(ErrInvalidExecutionContext());
  }

  std::string op_name_prefix;
  std::function<absl::Status(std::stop_token)> pps_op_fn;

  if (reset) {
    std::string iwid = request->init_world_id();
    INTR_RETURN_IF_ERROR(execution_context_->SetEditWorldID(iwid));

    CloneWorldRequest clone_request;
    clone_request.set_world_id(iwid);
    WorldMetadata world_metadata;
    grpc::ServerContext clone_context;

    INTR_RETURN_IF_ERROR(ToAbslStatus(world_service_.CloneWorld(
        &clone_context, &clone_request, &world_metadata)))
        << "CloneEditWorldToStaging";

    std::string staging_world_id = world_metadata.id();
    LOG(INFO) << absl::Substitute(
        "Using world '$0' as staging for process starting from world '$1'",
        staging_world_id, iwid);
    INTR_RETURN_IF_ERROR(
        execution_context_->SetStagingWorldID(staging_world_id));

    op_name_prefix = "ResetOperation";
    pps_op_fn = [this, staging_world_id,
                 user](std::stop_token stop_token) -> absl::Status {
      mu_.AssertHeld();
      INTR_RETURN_IF_ERROR(
          ResetBeliefWorldAndSim(user.get(), staging_world_id, stop_token));

      if (stop_token.stop_requested()) {
        return absl::CancelledError("DeleteWorld was cancelled.");
      }
      DeleteWorldRequest delete_request;
      delete_request.set_world_id(staging_world_id);
      google::protobuf::Empty empty_response;
      grpc::ServerContext delete_context;
      std::stop_callback cb(stop_token, [&]() { delete_context.TryCancel(); });
      INTR_RETURN_IF_ERROR(ToAbslStatus(world_service_.DeleteWorld(
          &delete_context, &delete_request, &empty_response)));

      return absl::OkStatus();
    };
  } else {
    // Don't need to reset, just ensure that the simulator is in a running
    // state.
    op_name_prefix = "UnpauseSimOperation";
    pps_op_fn = [this](std::stop_token stop_token) -> absl::Status {
      mu_.AssertHeld();
      if (execution_context_->OperationMode() != OperationMode::SIMULATION) {
        return absl::OkStatus();
      }
      if (pause_sim_) {
        grpc::ClientContext unpause_context;
        std::stop_callback cb(stop_token,
                              [&]() { unpause_context.TryCancel(); });
        google::protobuf::Empty empty_response;
        INTR_RETURN_IF_ERROR(ToAbslStatus(sim_service_stub_->UnpauseSimulation(
            &unpause_context, google::protobuf::Empty(), &empty_response)));
      } else {
        grpc::ClientContext status_context;
        std::stop_callback cb(stop_token,
                              [&]() { status_context.TryCancel(); });
        GetSimulationStatusResponse simulation_status_response;
        INTR_RETURN_IF_ERROR(
            ToAbslStatus(sim_service_stub_->GetSimulationStatus(
                &status_context, google::protobuf::Empty(),
                &simulation_status_response)));
        if (simulation_status_response.has_simulator_state() &&
            simulation_status_response.simulator_state() != RUNNING) {
          return absl::InternalError(
              absl::StrCat("Cannot start process with simulator in state: ",
                           SimulatorState_Name(
                               simulation_status_response.simulator_state())));
        }
      }
      return absl::OkStatus();
    };
  }

  absl::BitGen gen;
  std::string op_name =
      absl::StrCat(op_name_prefix, "-", absl::FormatTime(absl::Now()), "-",
                   absl::Hex(absl::Uniform<uint32_t>(gen)));

  PrepareProcessStartMetadata metadata;
  google::protobuf::Any op_metadata;
  op_metadata.PackFrom(metadata);

  auto operation_func =
      [this, pps_op_fn](
          std::stop_token stop_token) -> absl::StatusOr<google::protobuf::Any> {
    if (stop_token.stop_requested()) {
      return absl::CancelledError("Operation cancelled before starting");
    }
    absl::MutexLock lock(mu_);
    INTR_RETURN_IF_ERROR(pps_op_fn(stop_token));
    if (stop_token.stop_requested()) {
      return absl::CancelledError("Operation cancelled");
    }

    PrepareProcessStartResponse response;
    response.set_belief_world_id(execution_context_->BeliefWorldID());
    response.set_sim_world_id(execution_context_->SimWorldID());

    google::protobuf::Any any_response;
    any_response.PackFrom(response);
    return any_response;
  };

  return ExecutionContextOperation::New(op_name, op_metadata, operation_func);
}

grpc::Status ConductorImpl::PrepareProcessStart(
    grpc::ServerContext* context, const PrepareProcessStartRequest* request,
    google::longrunning::Operation* response) {
  const stats::ScopedSpan span("conductor.PrepareProcessStart", context);
  bool reset = false;
  std::string iwid = request->init_world_id();
  if (iwid.empty()) {
    LOG(INFO) << "PrepareProcessStart called without init world ID, not "
                 "resetting context.";
    reset = false;
  } else {
    // The conductor supports a single execution context. At the start of a new
    // process, the execution context is associated with the init world ID of
    // that process. If a new process is started while an existing process is
    // running, the init world ID for both processes must be the same. Otherwise
    // an error is returned.
    reset = true;
    for (const auto& pi : request->other_process_info()) {
      switch (pi.state()) {
        case RunMetadata_State_PREPARING:
        case RunMetadata_State_RUNNING:
        case RunMetadata_State_SUSPENDING:
        case RunMetadata_State_CANCELING:
        case RunMetadata_State_SUSPENDED:
          if (pi.init_world_id() != request->init_world_id()) {
            return grpc::Status(
                grpc::StatusCode::RESOURCE_EXHAUSTED,
                absl::Substitute("found process running with init world ID "
                                 "\"$0\" but requested to start a new "
                                 "process with init world ID \"$1\"",
                                 pi.init_world_id(), request->init_world_id()));
          }
          // Do not reset execution context since a process is running in this
          // context.
          reset = false;
          break;
        case RunMetadata_State_ACCEPTED:
        case RunMetadata_State_CANCELED:
        case RunMetadata_State_SUCCEEDED:
        case RunMetadata_State_FAILED:
          break;
        default:
          return grpc::Status(
              grpc::StatusCode::INTERNAL,
              absl::StrCat("unknown process state: ", pi.state()));
      }
    }
  }

  absl::MutexLock lock(ec_op_mu_);
  if (ec_op_ != nullptr && !ec_op_->IsDone()) {
    return grpc::Status(
        grpc::StatusCode::RESOURCE_EXHAUSTED,
        "A previous operation is still running for this execution context.");
  }

  auto user = std::shared_ptr<const acl::User>(
      acl::CreateGrpcMetadataUserFromServerContext(*context));

  INTR_ASSIGN_OR_RETURN_GRPC(
      ec_op_, PrepareProcessStartImpl(context, user, request, reset));

  *response = ec_op_->Proto();
  return grpc::Status::OK;
}
grpc::Status ConductorImpl::GetOperation(
    grpc::ServerContext* context,
    const google::longrunning::GetOperationRequest* request,
    google::longrunning::Operation* response) {
  const stats::ScopedSpan span("conductor.GetOperation", context);
  absl::MutexLock lock(ec_op_mu_);
  if (ec_op_ == nullptr || ec_op_->Name() != request->name()) {
    return grpc::Status(
        grpc::StatusCode::NOT_FOUND,
        absl::StrCat("Conductor does not have a running operation named ",
                     request->name()));
  }
  *response = ec_op_->Proto();
  return grpc::Status::OK;
}

grpc::Status ConductorImpl::CancelOperation(
    grpc::ServerContext* context,
    const google::longrunning::CancelOperationRequest* request,
    google::protobuf::Empty* response) {
  const stats::ScopedSpan span("conductor.CancelOperation", context);
  absl::MutexLock lock(ec_op_mu_);
  if (ec_op_ == nullptr || ec_op_->Name() != request->name()) {
    return grpc::Status(
        grpc::StatusCode::NOT_FOUND,
        absl::StrCat("Conductor does not have a running operation named ",
                     request->name()));
  }
  ec_op_->Cancel();
  return grpc::Status::OK;
}

grpc::Status ConductorImpl::PrepareProcessResume(
    grpc::ServerContext* context, const PrepareProcessResumeRequest* request,
    PrepareProcessResumeResponse* response) {
  const stats::ScopedSpan span("conductor.PrepareProcessResume", context);
  absl::MutexLock lock(mu_);

  LOG(INFO) << "Preparing to resume process ...";

  if (execution_context_ == nullptr) {
    return ErrInvalidExecutionContext();
  }

  if (pause_sim_ &&
      execution_context_->OperationMode() == OperationMode::SIMULATION) {
    grpc::ClientContext client_context;
    google::protobuf::Empty empty_response;
    INTR_RETURN_IF_ERROR_GRPC(sim_service_stub_->UnpauseSimulation(
        &client_context, google::protobuf::Empty(), &empty_response))
        << "UnpauseSimulation";
  }

  return grpc::Status::OK;
}
grpc::Status ConductorImpl::NotifyAllProcessesStopped(
    grpc::ServerContext* context,
    const NotifyAllProcessesStoppedRequest* request,
    NotifyAllProcessesStoppedResponse* response) {
  const stats::ScopedSpan span("conductor.NotifyAllProcessesStopped", context);
  LOG(INFO) << "All processes were stopped.";

  INTR_RETURN_IF_ERROR_GRPC(
      CancelExecutionContextOperation(context, /*wait=*/true));

  absl::MutexLock lock(mu_);

  if (execution_context_ != nullptr && pause_sim_ &&
      execution_context_->OperationMode() == OperationMode::SIMULATION) {
    grpc::ClientContext client_context;
    google::protobuf::Empty empty_response;
    INTR_RETURN_IF_ERROR_GRPC(sim_service_stub_->PauseSimulation(
        &client_context, google::protobuf::Empty(), &empty_response))
        << "PauseSimulation failed";
  }

  return grpc::Status::OK;
}
grpc::Status ConductorImpl::ConfigureSceneObject(
    grpc::ServerContext* context, const ConfigureSceneObjectRequest* request,
    ConfigureSceneObjectResponse* response) {
  const stats::ScopedSpan span("conductor.ConfigureSceneObject", context);
  grpc::ClientContext ai_context;
  GetAssetInstanceRequest ai_request;
  ai_request.set_name(request->name());
  AssetInstance ai_response;

  INTR_RETURN_IF_ERROR_GRPC(asset_instances_stub_->GetAssetInstance(
      &ai_context, ai_request, &ai_response))
      << "unable to get instance config";

  std::vector<GeometricResourceInstanceData> grids;
  intrinsic_proto::world::ObjectWorldUpdates unused_updates;
  INTR_ASSIGN_OR_RETURN_GRPC(
      std::tie(grids, unused_updates),
      resource_world_->GetResourceReader()->GetGeometricResourceSetData());

  auto grid = absl::c_find_if(
      grids, [request](const GeometricResourceInstanceData& grid) {
        return grid.name() == request->name();
      });
  if (grid == grids.end()) {
    return ToGrpcStatus(absl::NotFoundError(
        absl::StrCat("Unable to find geometric data for instance named '",
                     request->name(), "'.")));
  }

  SceneObjectConfig config = grid->scene_object_config();
  SceneObject scene_object = grid->scene_object();
  SceneObjectInstanceUpdates so_updates;
  *so_updates.add_updates() = request->update();

  absl::MutexLock lock(mu_);
  if (execution_context_ == nullptr) {
    return ErrInvalidExecutionContext();
  }
  // Apply the updates to both the belief world and the edit (initial) world.
  // TODO: b/349116895 -- Full removal of world processing from ADS.
  //
  // Why init? We apply the changes to init right away because `UpdateResource`
  // API call below explicitly decomposes and extracts the world updates
  // *before* reconstructing init_world with the newly configured SceneObject.
  // This means that saved world updates can override the configured
  // SceneObject. Let's take an example:
  // - Let's assume that a SceneObject asset has color "red".
  // - An instance of it, named `box`, has color "blue" in the init_world
  // (because, it was overriden via an ObjectWorldUpdate before).
  // - Now we call `ConfigureSceneObject` API on this instance to change it to
  // "purple".
  //    - `UpdateResource` first decomposes the init_world and this emits an
  //       ObjectWorldUpdate to change the color to "blue".
  //    - However, the reconstructed `init_world` would also have the box as
  //      "blue" since this ObjectWorldUpdate also overrides the "purple"
  //      configured SceneObject.
  //
  // To circumvent this bug, we apply the update directly to init_world. This
  // does emit a duplicate ObjectWorldUpdate, which would disappear in the
  // following save operation.
  std::vector<std::string> world_ids = {execution_context_->EditWorldID(),
                                        execution_context_->BeliefWorldID()};

  for (const auto& world_id : world_ids) {
    grpc::ServerContext ws_get_context;
    GetObjectRequest ws_get_request;
    ws_get_request.set_world_id(world_id);
    ws_get_request.mutable_object()->mutable_by_name()->set_object_name(
        request->name());
    ws_get_request.set_view(ObjectView::FULL);
    Object object_response;

    grpc::Status get_object_status = world_service_.GetObject(
        &ws_get_context, &ws_get_request, &object_response);
    if (!get_object_status.ok()) {
      if (get_object_status.error_code() == grpc::StatusCode::NOT_FOUND) {
        LOG(WARNING) << absl::Substitute(
            "Object \"$0\" not found in world \"$1\", skipping call to "
            "UpdateWorldResources.",
            request->name(), world_id);
        continue;
      }
      return StatusBuilderGrpc(get_object_status).LogError()
             << "GetObject failed";
    }

    INTR_ASSIGN_OR_RETURN_GRPC(
        ObjectWorldUpdates world_updates,
        intrinsic::world::ConvertSceneObjectUpdatesToWorldUpdates(
            "", object_response, so_updates),
        _ << "unable to convert scene object update to world update");

    grpc::ServerContext ws_update_context;
    UpdateWorldResourcesRequest ws_update_request;
    ws_update_request.set_world_id(world_id);
    *ws_update_request.mutable_world_updates() = world_updates;
    UpdateWorldResourcesResponse ws_update_response;

    INTR_RETURN_IF_ERROR_GRPC(world_service_.UpdateWorldResources(
        &ws_update_context, &ws_update_request, &ws_update_response))
        << "UpdateWorld failed";
  }

  INTR_RETURN_IF_ERROR_GRPC(
      scene_object::ApplyUpdatesToConfig(scene_object, so_updates, &config))
      << "unable to apply scene object config";

  grpc::ClientContext ads_context;
  UpdateResourceRequest ads_request;
  ads_request.mutable_resource()->set_name(request->name());
  *ads_request.mutable_resource()
       ->mutable_configuration()
       ->mutable_scene_object_config() = config;
  google::longrunning::Operation ads_operation;

  INTR_RETURN_IF_ERROR_GRPC(asset_deployment_stub_->UpdateResource(
      &ads_context, ads_request, &ads_operation))
      << "unable to start update instance op";

  std::string op_name = ads_operation.name();
  while (!ads_operation.done()) {
    grpc::ClientContext lro_context;
    google::longrunning::WaitOperationRequest wait_request;
    wait_request.set_name(op_name);

    INTR_RETURN_IF_ERROR_GRPC(asset_deployment_lro_stub_->WaitOperation(
        &lro_context, wait_request, &ads_operation))
        << "unable to check status of update operation for " << op_name;
  }

  if (ads_operation.result_case() == google::longrunning::Operation::kError) {
    return intrinsic::StatusBuilderGrpc(
               intrinsic::MakeStatusFromRpcStatus(ads_operation.error()))
               .SetPrepend()
               .LogError()
           << "unable to update instance";
  }

  LOG(INFO) << "Configured scene object " << request->name()
            << " with new update: " << request->update().ShortDebugString();

  // TODO(dhirajgoel): Save again to remove potentially duplicate
  // ObjectWorldUpdate.

  return grpc::Status::OK;
}

grpc::Status ConductorImpl::ResetWorldService(
    const std::vector<std::string>& worlds_to_not_delete) {
  LOG(INFO) << "Removing existing worlds from world service";

  grpc::ServerContext list_context;
  ListWorldsRequest request;
  ListWorldsResponse response;
  grpc::Status status =
      world_service_.ListWorlds(&list_context, &request, &response);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to list worlds from world service: "
                 << status.error_message();
    return status;
  }

  for (const auto& world : response.world_metadatas()) {
    const std::string& world_id = world.id();
    if (std::find(worlds_to_not_delete.begin(), worlds_to_not_delete.end(),
                  world_id) != worlds_to_not_delete.end()) {
      LOG(INFO) << "Not deleting special world: " << world_id;
      continue;
    }
    grpc::ServerContext delete_context;
    DeleteWorldRequest delete_request;
    delete_request.set_world_id(world_id);
    google::protobuf::Empty delete_response;
    grpc::Status delete_status = world_service_.DeleteWorld(
        &delete_context, &delete_request, &delete_response);
    if (!delete_status.ok()) {
      LOG(WARNING) << "unable to delete world '" << world_id
                   << "': " << delete_status.error_message();
    }
  }

  return grpc::Status::OK;
}

absl::Status ConductorImpl::PauseWorldUpdater(std::stop_token stop_token) {
  if (stop_token.stop_requested()) {
    return absl::CancelledError("PauseWorldUpdater was cancelled.");
  }
  // Creates a fresh server context to avoid deadline exceeded error due to
  // previous rpc call that took longer. This can sometimes happen with large
  // worlds.
  grpc::ServerContext context;
  std::stop_callback cb(stop_token, [&]() { context.TryCancel(); });
  PauseUpdaterRequest pause_request;
  PauseUpdaterResponse pause_response;
  return ToAbslStatus(
      world_updater_.Pause(&context, &pause_request, &pause_response));
}

absl::Status ConductorImpl::ResumeWorldUpdater() {
  // Always resumes the world updater irrespective of cancellation request.
  grpc::ServerContext context;
  ResumeUpdaterRequest resume_request;
  ResumeUpdaterResponse resume_response;
  return ToAbslStatus(
      world_updater_.Resume(&context, &resume_request, &resume_response));
}

grpc::Status ConductorImpl::SaveWorld(grpc::ServerContext* context,
                                      const SaveWorldRequest* request,
                                      SaveWorldResponse* response) {
  const stats::ScopedSpan span("conductor.SaveWorld", context);
  absl::MutexLock lock(mu_);

  if (execution_context_ == nullptr) {
    return ErrInvalidExecutionContext();
  }

  // TODO: b/294256260 -- When we have multiple scenes we should relax this
  // restriction.
  if (request->id() != execution_context_->EditWorldID()) {
    LOG(ERROR) << "We can only save the current edit world: "
               << execution_context_->EditWorldID();
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        absl::StrCat("can only save the current edit world: ",
                                     execution_context_->EditWorldID()));
  }

  auto status = resource_world_->UpdateResourceSetWorldRelations(request->id());
  if (!status.ok()) {
    LOG(WARNING)
        << "SaveWorld: could not update resource set from world with id "
        << request->id() << ": " << status.message();
    return grpc::Status(
        grpc::StatusCode::INTERNAL,
        absl::StrCat("could not update resource set from world with id ",
                     request->id(), ": ", status.message()));
  }

  // We should assume that the resource set now reflects the saved world, so
  // we'll simply compose the world into the passed world ID.
  auto update_result =
      resource_world_->UpdateWorldFromResourceSet(request->id(), false);
  if (!update_result.ok()) {
    LOG(WARNING)
        << "SaveWorld: could not reload world from resource set (should have "
           "checked this in UpdateResourceSetWorldRelations): "
        << update_result.status().message();
    return grpc::Status(
        grpc::StatusCode::INTERNAL,
        absl::StrCat("could not reload world from resource set (should have "
                     "checked this in UpdateResourceSetWorldRelations): ",
                     update_result.status().message()));
  }

  INTR_RETURN_IF_ERROR_GRPC(
      execution_context_->SetEditWorldStatus(std::nullopt));

  return grpc::Status::OK;
}

grpc::Status ConductorImpl::GetSaveStatus(grpc::ServerContext* context,
                                          const GetSaveStatusRequest* request,
                                          GetSaveStatusResponse* response) {
  const stats::ScopedSpan span("conductor.GetSaveStatus", context);
  INTR_ASSIGN_OR_RETURN_GRPC(auto res,
                             save_scene_monitor_->GetSaveStatus(request->id()));

  auto* state = response->mutable_state();
  state->set_status(res.status);

  INTR_ASSIGN_OR_RETURN_GRPC(*state->mutable_last_save_time(),
                             intrinsic::FromAbslTime(res.last_save_time));

  state->set_world_id(request->id());

  return grpc::Status::OK;
}

grpc::Status ConductorImpl::RestoreWorld(grpc::ServerContext* context,
                                         const RestoreWorldRequest* request,
                                         RestoreWorldResponse* response) {
  const stats::ScopedSpan span("conductor.RestoreWorld", context);
  // TODO(b/375021242): Consider skipping invalid updates here if there are user
  // complaints about not restorable saves.
  auto update_result =
      resource_world_->UpdateWorldFromResourceSet(request->id(), false);
  if (!update_result.ok()) {
    const std::string error_msg =
        absl::StrCat("could not load resource set into world with id ",
                     request->id(), ": ", update_result.status().message());
    LOG(WARNING) << "RestoreWorld: " << error_msg;
    return grpc::Status(grpc::StatusCode::INTERNAL, error_msg);
  }

  absl::MutexLock lock(mu_);
  if (execution_context_ != nullptr &&
      request->id() == execution_context_->EditWorldID()) {
    INTR_RETURN_IF_ERROR_GRPC(
        execution_context_->SetEditWorldStatus(std::nullopt));
  }
  return grpc::Status::OK;
}

absl::StatusOr<std::unique_ptr<ConductorImpl>> ConductorImpl::Create(
    const SrvParams& params, KVStoreCreator create_kv_store) {
  auto sim_channel = grpc::CreateCustomChannel(
      params.sim_service_address, grpc::InsecureChannelCredentials(),
      intrinsic::connect::DefaultGrpcChannelArgs());
  auto sim_service_stub = SimulationService::NewStub(sim_channel);

  auto workcell_channel =
      grpc::CreateChannel(params.workcell_cluster_service_address,
                          grpc::InsecureChannelCredentials());
  auto installed_assets_lro_stub =
      google::longrunning::Operations::NewStub(workcell_channel);
  auto installed_assets_stub =
      intrinsic_proto::assets::v1::InstalledAssets::NewStub(workcell_channel);

  auto ads_channel =
      grpc::CreateChannel(params.asset_deployment_service_address,
                          grpc::InsecureChannelCredentials());
  auto asset_deployment_lro_stub =
      google::longrunning::Operations::NewStub(ads_channel);
  auto asset_deployment_stub =
      intrinsic_proto::assets::AssetDeploymentService::NewStub(ads_channel);

  auto ais_channel = grpc::CreateChannel(params.asset_instances_service_address,
                                         grpc::InsecureChannelCredentials());
  auto asset_instances_stub =
      intrinsic_proto::assets::v1::AssetInstances::NewStub(ais_channel);

  // Create KVStore
  std::shared_ptr<intrinsic::KeyValueStore> kv_store;
  if (create_kv_store != nullptr) {
    INTR_ASSIGN_OR_RETURN(kv_store, create_kv_store());
  } else {
    intrinsic::PubSub pubsub("conductor");
    INTR_ASSIGN_OR_RETURN(auto kv_store_value, pubsub.KeyValueStore());
    kv_store =
        std::make_shared<intrinsic::KeyValueStore>(std::move(kv_store_value));
  }

  // Load ExecutionContext
  std::unique_ptr<ExecutionContext> execution_context;
  absl::Notification notification;
  auto deadline = absl::Now() + absl::Minutes(1);
  absl::StatusOr<ExecutionContextProto> ec_proto;

  intrinsic::WaitForNotificationWithDeadlineAndInterrupt(
      notification, deadline,
      [&kv_store, &ec_proto]() -> bool {
        ec_proto = kv_store->Get<ExecutionContextProto>(
            kExecutionContextStorageKey, absl::Seconds(1));
        const absl::StatusCode code = ec_proto.status().code();
        return code != absl::StatusCode::kDeadlineExceeded &&
               code != absl::StatusCode::kUnavailable &&
               code != absl::StatusCode::kResourceExhausted;
      },
      absl::Seconds(5));
  if (ec_proto.ok()) {
    LOG(INFO) << "Recovered previous execution context: "
              << ec_proto->ShortDebugString();
    INTR_ASSIGN_OR_RETURN(execution_context, ExecutionContext::FromProto(
                                                 *ec_proto, kv_store.get()));
  } else if (!absl::IsNotFound(ec_proto.status())) {
    LOG(ERROR) << "Failed to recover previous execution context: "
               << ec_proto.status().message();
    return ec_proto.status();
  } else {
    // No previous execution context found. This happens when the app is
    // deployed for the first time.
    LOG(INFO) << "No previous execution context found: "
              << ec_proto.status().message();
  }

  // Create ResourceWorld
  std::unique_ptr<ResourceReader> resource_reader = ResourceReader::Create(
      params.hss_service_address, params.runtimedb_service_address);
  if (resource_reader == nullptr) {
    return absl::InternalError("Failed to create ResourceReader");
  }

  INTR_ASSIGN_OR_RETURN(
      auto resource_world,
      ResourceWorld::Create(params.hss_service_address,
                            params.object_world_service_address,
                            std::move(resource_reader)),
      _ << "ResourceWorld::Create");

  // Create SaveSceneMonitor
  INTR_ASSIGN_OR_RETURN(
      auto save_scene_monitor,
      SaveSceneMonitor::Create(
          "init_world",
          SaveSceneMonitorOptions{
              .resource_world = resource_world.get(),
              .hss_address = params.hss_service_address,
              .ows_address = params.object_world_service_address,
          }),
      _ << "savemonitor.Create");

  return std::make_unique<ConductorImpl>(ConductorOptions{
      .kv_store = std::move(kv_store),
      .resource_world = std::move(resource_world),
      .save_scene_monitor = std::move(save_scene_monitor),
      .execution_context = std::move(execution_context),
      .world_service = params.world_service,
      .world_updater = params.world_updater,
      .sim_service_stub = std::move(sim_service_stub),
      .installed_assets_lro_stub = std::move(installed_assets_lro_stub),
      .asset_instances_stub = std::move(asset_instances_stub),
      .asset_deployment_stub = std::move(asset_deployment_stub),
      .asset_deployment_lro_stub = std::move(asset_deployment_lro_stub),
      .installed_assets_stub = std::move(installed_assets_stub),
      .enable_sim_pause = params.enable_sim_pause,
  });
}

}  // namespace intrinsic::conductor
