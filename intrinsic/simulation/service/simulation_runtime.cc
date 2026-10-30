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

#include "intrinsic/simulation/service/simulation_runtime.h"

#include <chrono>  // NOLINT(build/c++11)
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/functional/function_ref.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/empty.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server_context.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/cc_client/operational_status.h"
#include "intrinsic/icon/equipment/equipment_utils.h"
#include "intrinsic/resources/client/resource_registry_client_interface.h"
#include "intrinsic/simulation/service/proto/first_party/simulation_service.pb.h"
#include "intrinsic/simulation/service/resource_registry_utils.h"
#include "intrinsic/simulation/service/simulator.h"
#include "intrinsic/simulation/service/world_visualizer.h"
#include "intrinsic/simulation/world/world_service_updater.h"
#include "intrinsic/util/grpc/connection_params.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/service/objects/object_world_updates_utils.h"

namespace intrinsic {
namespace simulation {

namespace {

using ::intrinsic_proto::simulation::first_party::GetSimulationStatusResponse;
using ::intrinsic_proto::simulation::first_party::ResetSimulationRequest;
using ::intrinsic_proto::simulation::first_party::VisualizeWorldUpdatesRequest;

// Default start world and visualized world are the canonical world.
constexpr std::string_view kDefaultStartWorld = "world";
constexpr std::string_view kDefaultVisualizedWorld = "world";

// Maximum duration for retrying simulator requests that fail with UNAVAILABLE
// if the client did not set a deadline. Clients that expect the simulator to
// still be starting up, e.g., right after a solution was deployed, should set a
// more generous deadline.
constexpr absl::Duration kMaxSimulatorRetryDurationWithoutDeadline =
    absl::Seconds(30);

// Pause between calls in `CallSimulatorWithRetries`. The backoff between
// attempts happens within each call (see the retry policy of the simulator
// channel in simulator_v1.cc); this pause only avoids a busy loop if a call
// fails with UNAVAILABLE without reaching gRPC.
constexpr absl::Duration kSimulatorRetryPause = absl::Milliseconds(250);

// Deadline for querying the simulator status in `GetSimulatorStatus` if not
// provided in the client context.
constexpr absl::Duration kSimulatorStatusTimeout = absl::Seconds(10);

// Calls `call_simulator` and calls it again while it returns UNAVAILABLE, e.g.,
// because the simulator is still starting up and the ingress cannot reach it
// yet.
//
// The simulator channel retries UNAVAILABLE errors with backoff within each
// call, but gRPC caps that at 5 attempts. This loop lifts the cap so that the
// retries span the whole deadline of `context`. Each call gets a client context
// that propagates the deadline and cancellation of `context`, so the loop ends
// with DEADLINE_EXCEEDED or CANCELLED once the incoming call expires or is
// cancelled. If the client did not set a deadline, the calls are bounded by
// `kMaxSimulatorRetryDurationWithoutDeadline`.
//
// If the deadline expires while retrying, the returned DEADLINE_EXCEEDED error
// includes the last UNAVAILABLE error, which explains why the simulator could
// not be reached.
absl::Status CallSimulatorWithRetries(
    const ::grpc::ServerContext& context, std::string_view description,
    absl::FunctionRef<absl::Status(std::unique_ptr<::grpc::ClientContext>)>
        call_simulator) {
  const bool has_client_deadline =
      context.deadline() != std::chrono::system_clock::time_point::max();
  const absl::Time deadline =
      has_client_deadline
          ? absl::FromChrono(context.deadline())
          : absl::Now() + kMaxSimulatorRetryDurationWithoutDeadline;
  const auto deadline_exceeded = [&](int num_calls,
                                     const absl::Status& last_unavailable) {
    return absl::DeadlineExceededError(absl::StrCat(
        description, " did not succeed before the deadline after ", num_calls,
        " unavailable call(s). Last error: ", last_unavailable.message()));
  };

  absl::Status last_unavailable;
  for (int attempt = 1;; ++attempt) {
    std::unique_ptr<::grpc::ClientContext> ctx =
        ::grpc::ClientContext::FromServerContext(context);
    if (!has_client_deadline) {
      ctx->set_deadline(absl::ToChronoTime(deadline));
    }
    absl::Status status = call_simulator(std::move(ctx));
    if (!absl::IsUnavailable(status)) {
      if (absl::IsDeadlineExceeded(status) && !last_unavailable.ok()) {
        return deadline_exceeded(attempt - 1, last_unavailable);
      }
      LOG_IF(INFO, attempt > 1 && status.ok())
          << description << " succeeded after " << attempt << " calls.";
      return status;
    }
    last_unavailable = status;
    if (absl::Now() + kSimulatorRetryPause >= deadline) {
      return deadline_exceeded(attempt, last_unavailable);
    }
    LOG(WARNING) << description << " failed (call " << attempt
                 << "), retrying: " << status;
    absl::SleepFor(kSimulatorRetryPause);
    if (context.IsCancelled()) {
      return absl::CancelledError(absl::StrCat(
          description, " was cancelled while retrying: ", status.message()));
    }
  }
}

struct IconClientFaultInfo {
  icon::Client client;
  ConnectionParams instance;
  bool faulted;
  std::optional<absl::Status> last_clear_faults_status;
};

}  // namespace

// static
absl::StatusOr<std::unique_ptr<SimulationRuntime>> SimulationRuntime::Create(
    SimulatorClientFactory create_simulator_client,
    IconClientFactory create_icon_client,
    std::shared_ptr<intrinsic_proto::world::ObjectWorldService::StubInterface>
        object_world_service,
    std::shared_ptr<intrinsic_proto::world::WorldUpdater::StubInterface>
        world_updater_service,
    std::string_view simulator_world_id, std::string_view start_world_id,
    std::string_view simulator_world_state_updates_topic,
    std::unique_ptr<resources::ResourceRegistryClientInterface>
        resource_registry_client,
    absl::Span<const ConnectionParams> manual_application_layer_targets) {
  if (simulator_world_id.empty()) {
    return absl::InvalidArgumentError(
        "simulator_world_id should be non-empty.");
  }
  // If no start world ID is specified, check if the target simulator world
  // already exists. If the simulator world does not exist, fall back to
  // using the default start world ID as the source clone target.
  std::string_view start_world_id_with_fallback = start_world_id;
  if (start_world_id_with_fallback.empty()) {
    INTR_ASSIGN_OR_RETURN(bool simulator_world_exists,
                          SimulatorWorldManager::CheckWorldExists(
                              *object_world_service, simulator_world_id));
    if (!simulator_world_exists) {
      LOG(WARNING) << "Creating simulator world [" << simulator_world_id
                   << "] from default start world id [" << kDefaultStartWorld
                   << "].";
      start_world_id_with_fallback = kDefaultStartWorld;
    }
  }

  // Create the simulator client instance and the class instance. The simulator
  // will be re-created when `RefreshConnectedSimulator` is called.
  absl::StatusOr<std::unique_ptr<Simulator>> simulator =
      create_simulator_client();

  INTR_ASSIGN_OR_RETURN(auto simulator_world_manager,
                        SimulatorWorldManager::Create(
                            object_world_service, world_updater_service,
                            SimulatorWorldManager::CreateOptions{
                                .simulator_world_id = simulator_world_id,
                                .start_world_id = start_world_id_with_fallback,
                                .ignore_disabled_object_deletion_error = true,
                            }));

  absl::Status simulator_status = simulator.status();
  if (absl::IsNotFound(simulator_status) ||
      absl::IsFailedPrecondition(simulator_status)) {
    LOG(INFO) << "Starting runtime without connecting to a simulator: "
              << simulator_status;
    return absl::WrapUnique(new SimulationRuntime(
        /*simulator=*/std::move(simulator), std::move(create_simulator_client),
        std::move(simulator_world_manager), std::move(create_icon_client),
        std::move(object_world_service), simulator_world_state_updates_topic,
        std::move(resource_registry_client), manual_application_layer_targets));
  }

  INTR_RETURN_IF_ERROR(simulator_status);

  if (*simulator == nullptr) {
    return absl::InternalError(
        "Simulator returned by factory function is null.");
  }

  return absl::WrapUnique(new SimulationRuntime(
      std::move(simulator), std::move(create_simulator_client),
      std::move(simulator_world_manager), std::move(create_icon_client),
      std::move(object_world_service), simulator_world_state_updates_topic,
      std::move(resource_registry_client), manual_application_layer_targets));
}

SimulationRuntime::SimulationRuntime(
    absl::StatusOr<std::unique_ptr<Simulator>> simulator,
    SimulatorClientFactory create_simulator_client,
    std::unique_ptr<SimulatorWorldManager> simulator_world_manager,
    IconClientFactory create_icon_client,
    std::shared_ptr<intrinsic_proto::world::ObjectWorldService::StubInterface>
        object_world_service,
    std::string_view simulator_world_state_updates_topic,
    std::unique_ptr<resources::ResourceRegistryClientInterface>
        resource_registry_client,
    absl::Span<const ConnectionParams> manual_application_layer_targets)
    : simulator_(std::move(simulator)),
      create_simulator_client_(std::move(create_simulator_client)),
      simulator_world_state_updates_topic_(simulator_world_state_updates_topic),
      create_icon_client_(std::move(create_icon_client)),
      object_world_service_(std::move(object_world_service)),
      resource_registry_client_(std::move(resource_registry_client)),
      manual_application_layer_targets_(
          manual_application_layer_targets.begin(),
          manual_application_layer_targets.end()),
      simulator_world_manager_(std::move(simulator_world_manager)) {}

absl::StatusOr<absl::flat_hash_set<ConnectionParams>>
SimulationRuntime::GetIconInstances() const {
  absl::flat_hash_set<ConnectionParams> result(
      manual_application_layer_targets_.begin(),
      manual_application_layer_targets_.end());

  if (!resource_registry_client_) {
    return result;
  }

  absl::StatusOr<std::vector<ResourceConnectionInfo>> icon_instances =
      GetResourcesFromRegistry(resource_registry_client_.get(),
                               icon::kIcon2ConnectionKey);
  if (absl::IsNotFound(icon_instances.status())) {
    return result;
  }
  INTR_RETURN_IF_ERROR(icon_instances.status());

  LOG(INFO) << "Received " << icon_instances->size()
            << " ICON resources back from the resource registry";

  for (const auto& instance : *icon_instances) {
    result.insert(instance.connection_params);
  }
  return result;
}

absl::Status SimulationRuntime::ClearIconFaults(
    const ::grpc::ServerContext& context) {
  INTR_ASSIGN_OR_RETURN(
      const absl::flat_hash_set<ConnectionParams> icon_instances,
      GetIconInstances());
  if (icon_instances.empty()) {
    LOG(WARNING)
        << "Unable to find an ICON instance. If your solution does not have a "
           "realtime control service, this is not a problem. Otherwise, "
           "robot(s) may not work as expected in sim.";
    return absl::OkStatus();
  }

  std::vector<IconClientFaultInfo> faulted_icon_clients;
  faulted_icon_clients.reserve(icon_instances.size());
  for (const auto& instance : icon_instances) {
    if (context.IsCancelled()) {
      LOG(INFO)
          << "ClearIconFaults was cancelled before attempting to clear faults.";
      return absl::CancelledError();
    }

    absl::StatusOr<icon::Client> client = create_icon_client_(instance);

    if (absl::IsUnavailable(client.status()) ||
        absl::IsUnimplemented(client.status())) {
      LOG(WARNING) << "ICON instance [" << instance << "] unavailable. "
                   << "Robot may remain in faulted state.";
      continue;
    }
    INTR_RETURN_IF_ERROR(client.status()).LogError()
        << "Failed to create ICON client for " << instance;

    const absl::StatusOr<icon::OperationalStatus> operational_status =
        client->GetOperationalStatus();
    if (absl::IsUnavailable(operational_status.status()) ||
        absl::IsUnimplemented(operational_status.status())) {
      LOG(WARNING) << "Operational status unavailable for ICON instance ["
                   << instance << "]. Robot may remain in faulted state.";
      continue;
    }
    if (!operational_status.ok() ||
        icon::IsFaulted(operational_status.value())) {
      faulted_icon_clients.push_back(
          IconClientFaultInfo{.client = std::move(client.value()),
                              .instance = instance,
                              .faulted = true});
    }
  }
  // Return early if there are no faulted ICON servers.
  if (faulted_icon_clients.empty()) {
    return absl::OkStatus();
  }

  absl::Time start = absl::Now();
  constexpr absl::Duration kClearFaultsTimeout = absl::Seconds(30);
  while (absl::Now() < (start + kClearFaultsTimeout)) {
    LOG(INFO) << "Trying to clear ICON faults";
    bool any_server_faulted = false;
    for (auto& client_info : faulted_icon_clients) {
      if (!client_info.faulted) {
        continue;
      }

      if (context.IsCancelled()) {
        LOG(INFO) << "ClearIconFaults was cancelled, some ICON instances may "
                     "still be faulted.";
        return absl::CancelledError();
      }

      const absl::Status clear_faults_status = client_info.client.ClearFaults();
      if (clear_faults_status.ok()) {
        LOG(INFO) << "ClearFaults() succeeded for ICON instance ["
                  << client_info.instance << "].";
        client_info.faulted = false;
        client_info.last_clear_faults_status = absl::OkStatus();
        continue;
      }

      LOG(INFO) << "ClearFaults() failed for ICON instance ["
                << client_info.instance << "] with: " << clear_faults_status;
      client_info.last_clear_faults_status = std::move(clear_faults_status);
      any_server_faulted = true;
    }
    if (!any_server_faulted) {
      return absl::OkStatus();
    }
    absl::SleepFor(absl::Seconds(1));
  }

  std::stringstream fault_status_ss;
  for (const auto& client_info : faulted_icon_clients) {
    if (client_info.faulted) {
      fault_status_ss << client_info.instance
                      << " status: " << *client_info.last_clear_faults_status
                      << "; ";
    }
  }
  return intrinsic::DeadlineExceededErrorBuilder()
         << "Could not clear ICON faults within 30s of simulation "
            "reset, aborting. "
         << fault_status_ss.str();
}

absl::Status SimulationRuntime::RestartIconServers(
    const ::grpc::ServerContext& context) {
  INTR_ASSIGN_OR_RETURN(
      const absl::flat_hash_set<ConnectionParams> icon_instances,
      GetIconInstances());
  if (icon_instances.empty()) {
    LOG(WARNING)
        << "Unable to find an ICON instance. If your solution does not have a "
           "realtime control service, this is not a problem. Otherwise, "
           "robot(s) may not work as expected in sim.";
    return absl::OkStatus();
  }

  for (const auto& instance : icon_instances) {
    if (context.IsCancelled()) {
      LOG(INFO) << "RestartIconServers was cancelled before it finished "
                   "restarting the "
                   "ICON servers. Some ICON servers may have restarted already";
      return absl::CancelledError();
    }

    absl::StatusOr<icon::Client> client = create_icon_client_(instance);

    // Skip ICON servers that are unavailable. We can get unavailable errors
    // either from the IconClientFactory, or from the RestartServer() call.
    // Neither should interrupt the rest of the simulation reset.
    if (absl::IsUnavailable(client.status()) ||
        absl::IsUnimplemented(client.status())) {
      LOG(WARNING) << "ICON instance [" << instance << "] unavailable. "
                   << "Robot may remain in faulted state.";
      continue;
    }
    INTR_RETURN_IF_ERROR(client.status()).LogError()
        << "Failed to create ICON client for " << instance;
    absl::Status restart_status = client->RestartServer();
    if (absl::IsUnavailable(restart_status) ||
        absl::IsUnimplemented(restart_status)) {
      LOG(WARNING) << "ICON instance [" << instance << "] unavailable. "
                   << "Robot may remain in faulted state.";
      continue;
    }
    INTR_RETURN_IF_ERROR(restart_status).LogError()
        << "Failed to restart ICON instance " << instance;
    LOG(INFO) << "Restarted ICON instance '" << instance << "'.";
  }
  return absl::OkStatus();
}

absl::Status SimulationRuntime::GetSimulatorName(
    ::grpc::ServerContext* context, const google::protobuf::Empty* request,
    google::protobuf::StringValue* response) {
  absl::ReaderMutexLock l(&simulator_mutex_);
  if (!simulator_.ok()) {
    return absl::FailedPreconditionError(simulator_.status().message());
  }

  std::string name = (*simulator_)->GetName();
  LOG(INFO) << "Requested simulator name: " << name;
  response->set_value(name);
  return absl::OkStatus();
}

absl::Status SimulationRuntime::ResetSimulatorWithRetries(
    ::grpc::ServerContext* context, bool start_paused, int num_reset_retries) {
  // TODO(b/575620498): Consolidate this retry logic with
  // `CallSimulatorWithRetries`, which retries UNAVAILABLE errors until the
  // deadline of `context`, e.g., while the simulator is still starting up. The
  // attempts here retry any error, without a pause in between, and stop after a
  // fixed number of attempts.
  LOG(INFO) << "Resetting the simulator...";
  for (int i = 1; i <= num_reset_retries; ++i) {
    if (context->IsCancelled()) {
      LOG(INFO) << "Simulation reset was cancelled before successfully "
                   "resetting simulator.";
      return absl::CancelledError();
    }

    auto status =
        (*simulator_)
            ->Reset({.start_paused = start_paused,
                     .simulator_world_id = std::string(simulator_world_id())},
                    ::grpc::ClientContext::FromServerContext(*context));
    if (status.ok()) {
      LOG(INFO) << "Resetting the simulator succeeded on Attempt (" << i << "/"
                << num_reset_retries << ")";
      return absl::OkStatus();
    }

    if (i == num_reset_retries) {
      return status;
    }

    LOG(WARNING) << "Resetting the simulator failed [" << status << "] "
                 << "Attempt (" << i << "/" << num_reset_retries << ")";
  }
  return absl::OkStatus();
}

absl::Status SimulationRuntime::ResetSimulation(
    ::grpc::ServerContext* context, const ResetSimulationRequest* request,
    google::protobuf::Empty* response) {
  absl::ReaderMutexLock l(&simulator_mutex_);
  const bool has_simulator = simulator_.ok();
  if (!has_simulator && !absl::IsNotFound(simulator_.status())) {
    return absl::FailedPreconditionError(simulator_.status().message());
  }

  LOG(INFO) << "Resetting sim world... ";
  std::string start_world = request->start_world_id();
  if (start_world.empty()) {
    start_world = std::string(kDefaultStartWorld);
    LOG(WARNING)
        << "Initial world not set in ResetSimulationRequest, defaulting to '"
        << kDefaultStartWorld << "'.";
  }

  // Track the return status instead of returning early here since we want to
  // ensure that the sim world updates are always restarted. An `absl::Cleanup`
  // is not used here since if restarting sim world updates fails, we want to
  // fail the `ResetSimulation` call.
  absl::Status return_status =
      simulator_world_manager_->StopUpdatesAndResetWorld(
          context, start_world,
          /*ignore_disabled_object_deletion_error=*/false);
  if (!return_status.ok()) {
    LOG(ERROR) << "Failed to stop updates and reset the sim world: "
               << return_status;
  }

  if (return_status.ok() && has_simulator) {
    constexpr int kNumResetRetries = 3;
    return_status.Update(ResetSimulatorWithRetries(
        context, request->start_paused(), kNumResetRetries));
    if (return_status.ok()) {
      LOG(INFO) << "Restarting ICON servers...";
      // If `RestartIconServers` succeeds, then the call will block until the
      // server is restarted. However, if it fails, that could indicate a
      // configuration error with the ICON real-time control service itself,
      // unrelated to the simulator. So we don't return an error if
      // `RestartIconServers` fails.
      RestartIconServers(*context).IgnoreError();
    }
  }

  absl::Status restart_updates_status =
      simulator_world_manager_->RestartUpdates();
  if (!restart_updates_status.ok()) {
    LOG(ERROR) << "Failed to restart updates: " << restart_updates_status;
  }

  if (!return_status.ok() && !restart_updates_status.ok()) {
    return absl::AbortedError(
        absl::StrCat("Reset simulation failed: ", return_status.message(),
                     "; and restart simulator world state updates failed: ",
                     restart_updates_status.message()));
  }

  return_status.Update(restart_updates_status);
  LOG_IF(INFO, return_status.ok())
      << "Simulation reset was completed successfully.";
  return return_status;
}

absl::Status SimulationRuntime::PauseSimulation(
    ::grpc::ServerContext* context, const google::protobuf::Empty* request,
    google::protobuf::Empty* response) {
  absl::ReaderMutexLock l(&simulator_mutex_);
  if (absl::IsNotFound(simulator_.status())) {
    return absl::OkStatus();
  }
  if (!simulator_.ok()) {
    return absl::FailedPreconditionError(simulator_.status().message());
  }

  LOG(INFO) << "Received PauseSimulation command.";

  Simulator& simulator = **simulator_;
  INTR_RETURN_IF_ERROR(
      CallSimulatorWithRetries(*context, "Simulator Pause",
                               [&](std::unique_ptr<::grpc::ClientContext> ctx) {
                                 return simulator.Pause(std::move(ctx));
                               }));

  LOG(INFO) << "Simulation was paused successfully.";
  return absl::OkStatus();
}

absl::Status SimulationRuntime::UnpauseSimulation(
    ::grpc::ServerContext* context, const google::protobuf::Empty* request,
    google::protobuf::Empty* response) {
  absl::ReaderMutexLock l(&simulator_mutex_);
  if (absl::IsNotFound(simulator_.status())) {
    return absl::OkStatus();
  }
  if (!simulator_.ok()) {
    return absl::FailedPreconditionError(simulator_.status().message());
  }

  LOG(INFO) << "Received UnpauseSimulation command.";

  Simulator& simulator = **simulator_;
  INTR_RETURN_IF_ERROR(
      CallSimulatorWithRetries(*context, "Simulator Unpause",
                               [&](std::unique_ptr<::grpc::ClientContext> ctx) {
                                 return simulator.Unpause(std::move(ctx));
                               }));
  // TODO(b/336962391) We can remove ClearIconFaults from here once ICON no
  // longer faults fatally due to sim pause.
  INTR_RETURN_IF_ERROR(ClearIconFaults(*context));

  LOG(INFO) << "Simulation was unpaused successfully.";
  return absl::OkStatus();
}

absl::Status SimulationRuntime::GetSimulationStatus(
    ::grpc::ServerContext* context, const google::protobuf::Empty* request,
    GetSimulationStatusResponse* response) {
  absl::ReaderMutexLock l(&simulator_mutex_);
  if (absl::IsNotFound(simulator_.status())) {
    return absl::OkStatus();
  }
  if (!simulator_.ok()) {
    return absl::FailedPreconditionError(simulator_.status().message());
  }

  Simulator& simulator = **simulator_;
  return CallSimulatorWithRetries(
      *context, "Simulator GetStatus",
      [&](std::unique_ptr<::grpc::ClientContext> ctx) -> absl::Status {
        INTR_ASSIGN_OR_RETURN(*response, simulator.GetStatus(std::move(ctx)));
        return absl::OkStatus();
      });
}

absl::Status SimulationRuntime::RunVisualization(
    ::grpc::ServerContext* context, const VisualizeWorldUpdatesRequest* request,
    google::protobuf::Empty* response) {
  // Make sure ObjectWorldUpdates do not have world IDs set, since the target
  // world id is part of `request`.
  if (intrinsic::object_world::AnyWorldIdsSet(request->world_updates())) {
    return absl::InvalidArgumentError(
        "An ObjectWorldUpdate that's being visualized should not have a "
        "world id set, since the world id is already specified by the "
        "VisualizeWorldUpdatesRequest.");
  }

  if (!request->world_updates().entity_updates().empty()) {
    return absl::InvalidArgumentError(
        "Visualize request with entity_updates is deprecated");
  }

  std::string start_world = request->start_world_id();
  if (start_world.empty()) {
    LOG(WARNING) << "Starting world not set for visualization. Defaulting to "
                 << "'" << kDefaultStartWorld << "'.";
    start_world = kDefaultStartWorld;
  }

  std::string visualized_world = request->visualized_world_id();
  if (visualized_world.empty()) {
    LOG(WARNING) << "Visualized world not set for visualization. Defaulting to "
                 << "'" << kDefaultVisualizedWorld << "'.";
    visualized_world = kDefaultVisualizedWorld;
  }

  if (visualized_world == simulator_world_id()) {
    return absl::InvalidArgumentError(
        "RunVisualization cannot target the simulator world.");
  }

  std::unique_ptr<::grpc::ClientContext> ctx;
  if (context != nullptr) {
    ctx = ::grpc::ClientContext::FromServerContext(*context);
  } else {
    ctx = std::make_unique<::grpc::ClientContext>();
    ConfigureClientContext(ctx.get());
  }

  std::string target_world_id;
  if (request->clone_start_world_to_visualized_world()) {
    intrinsic_proto::world::CloneWorldRequest clone_world_req;
    clone_world_req.set_world_id(start_world);
    clone_world_req.set_cloned_world_id(visualized_world);
    clone_world_req.set_allow_overwrite(true);
    intrinsic_proto::world::WorldMetadata clone_world_resp;
    INTR_RETURN_IF_ERROR(ToAbslStatus(object_world_service_->CloneWorld(
        ctx.get(), clone_world_req, &clone_world_resp)));
    target_world_id = clone_world_resp.id();
  } else {
    intrinsic_proto::world::GetWorldRequest get_world_req;
    get_world_req.set_world_id(start_world);
    intrinsic_proto::world::World get_world_resp;
    INTR_RETURN_IF_ERROR(ToAbslStatus(object_world_service_->GetWorld(
        ctx.get(), get_world_req, &get_world_resp)));
    target_world_id = get_world_resp.world_metadata().id();
  }

  if (!request->world_updates().updates().empty()) {
    // Start a visualizer that updates the world service.
    INTR_ASSIGN_OR_RETURN(auto world_visualizer, WorldVisualizer::Create());
    INTR_ASSIGN_OR_RETURN(auto world_service_updater,
                          WorldServiceUpdater::Create(
                              visualized_world, object_world_service_.get(),
                              world_visualizer.get()));

    if (!world_service_updater->Start()) {
      return absl::InternalError("Error starting visualizer!");
    }

    world::ObjectWorldClient object_world_client(target_world_id,
                                                 object_world_service_);

    INTR_ASSIGN_OR_RETURN(absl::Duration animation_time,
                          ToAbslDuration(request->duration()));
    if (animation_time == absl::ZeroDuration()) {
      INTR_RETURN_IF_ERROR(world_visualizer->Visualize(
          object_world_client, request->world_updates()));
    } else {
      INTR_RETURN_IF_ERROR(world_visualizer->Visualize(
          object_world_client, request->world_updates(), animation_time));
    }

    // Wait for the updates to finish.
    while (world_visualizer->HasPendingUpdate()) {
    }

    // Stop the visualizer thread
    if (!world_service_updater->Stop()) {
      return absl::InternalError("Error stopping visualizer!");
    }
  }

  return absl::OkStatus();
}

absl::Status SimulationRuntime::GetSimulatorStatus(
    ::grpc::ServerContext* context,
    const intrinsic_proto::simulation::first_party::GetSimulatorStatusRequest*
        request,
    intrinsic_proto::simulation::first_party::GetSimulatorStatusResponse*
        response) {
  return GetSimulatorStatusInternal(response);
}

absl::Status SimulationRuntime::RefreshConnectedSimulator(
    ::grpc::ServerContext* context,
    const intrinsic_proto::simulation::first_party::
        RefreshConnectedSimulatorRequest* request,
    intrinsic_proto::simulation::first_party::RefreshConnectedSimulatorResponse*
        response) {
  absl::StatusOr<std::unique_ptr<Simulator>> simulator_or =
      create_simulator_client_();
  LOG_IF(ERROR, !simulator_or.ok())
      << "RefreshConnectedSimulator: failed to connect: "
      << simulator_or.status();
  {
    absl::WriterMutexLock l(&simulator_mutex_);
    if (absl::IsNotFound(simulator_or.status()) ||
        absl::IsFailedPrecondition(simulator_or.status())) {
      LOG_IF(INFO, simulator_.ok()) << "Removing existing connection.";
      simulator_ = simulator_or.status();
    } else if (simulator_or.ok() && *simulator_or == nullptr) {
      return absl::InternalError(
          "Simulator returned by factory function is null.");
    } else if (!simulator_or.ok()) {
      return simulator_or.status();
    } else {
      simulator_ = std::move(simulator_or);
    }
  }
  return GetSimulatorStatusInternal(response);
}

template <typename ResponseType>
absl::Status SimulationRuntime::GetSimulatorStatusInternal(
    ResponseType* response) {
  absl::ReaderMutexLock l(&simulator_mutex_);
  if (absl::IsNotFound(simulator_.status())) {
    return absl::OkStatus();
  }
  INTR_RETURN_IF_ERROR(simulator_.status());

  auto* simulator_status = response->mutable_simulator_status();
  simulator_status->mutable_simulator_info()->set_simulator_name(
      (*simulator_)->GetName());

  auto ctx = std::make_unique<::grpc::ClientContext>();
  ctx->set_deadline(absl::ToChronoTime(absl::Now() + kSimulatorStatusTimeout));
  absl::StatusOr<GetSimulationStatusResponse> status_or =
      (*simulator_)->GetStatus(std::move(ctx));
  if (status_or.ok()) {
    if (status_or->has_simulator_state()) {
      simulator_status->set_simulator_state(status_or->simulator_state());
    }
    if (status_or->has_status()) {
      *simulator_status->mutable_status() = status_or->status();
    }
  } else {
    *simulator_status->mutable_status() = ToGoogleRpcStatus(status_or.status());
  }
  return absl::OkStatus();
}

template absl::Status SimulationRuntime::GetSimulatorStatusInternal<
    intrinsic_proto::simulation::first_party::GetSimulatorStatusResponse>(
    intrinsic_proto::simulation::first_party::GetSimulatorStatusResponse*
        response);

template absl::Status SimulationRuntime::GetSimulatorStatusInternal<
    intrinsic_proto::simulation::first_party::
        RefreshConnectedSimulatorResponse>(
    intrinsic_proto::simulation::first_party::RefreshConnectedSimulatorResponse*
        response);

absl::Status SimulationRuntime::SyncSimulatorWorld(
    ::grpc::ServerContext* absl_nonnull context,
    ::grpc::ServerReaderWriter<
        ::intrinsic_proto::simulation::v1::WorldMessage,
        ::intrinsic_proto::simulation::v1::SimulatorSceneMessage>* absl_nonnull
        stream,
    const ::intrinsic_proto::simulation::v1::SimulatorSceneMessage& first_msg) {
  const auto& session_init = first_msg.session_init();
  std::string_view world_id = simulator_world_id();
  std::string generation_id = simulator_world_manager_->generation_id();
  if (world_id.empty() || generation_id.empty()) {
    return absl::InternalError("Simulator world is not initialized.");
  }
  LOG(INFO) << "Starting SyncWorld session from simulator "
            << session_init.simulator_name() << " for world " << world_id
            << " with generation ID " << generation_id;

  // Send SessionAck
  ::intrinsic_proto::simulation::v1::WorldMessage ack_msg;
  auto* session_ack = ack_msg.mutable_session_ack();
  session_ack->set_simulator_world_id(world_id);
  session_ack->set_simulator_world_generation_id(std::move(generation_id));
  session_ack->set_state_updates_pubsub_topic_name(
      simulator_world_state_updates_topic());

  if (!stream->Write(ack_msg)) {
    return absl::AbortedError("Failed to write session_ack to stream.");
  }

  // Run the read loop until client disconnects or stream is cancelled
  ::intrinsic_proto::simulation::v1::SimulatorSceneMessage msg;
  while (stream->Read(&msg)) {
    // Since WorldUpdate and SimulatorUpdate are placeholders, we log and
    // discard.
    if (msg.has_simulator_updates()) {
      LOG_EVERY_N_SEC(INFO, 1)
          << "Received simulator updates placeholder (ignored)";
    }
  }

  LOG(INFO) << "SyncWorld stream closed.";
  return absl::OkStatus();
}

absl::Status SimulationRuntime::GetSimulatorWorldInfo(
    ::grpc::ServerContext* absl_nonnull context,
    const ::intrinsic_proto::simulation::v1::
        GetSimulatorWorldInfoRequest* absl_nonnull request,
    ::intrinsic_proto::simulation::v1::SimulatorWorldInfo* absl_nonnull
        response) {
  response->set_simulator_world_id(simulator_world_id());
  return absl::OkStatus();
}

}  // namespace simulation
}  // namespace intrinsic
