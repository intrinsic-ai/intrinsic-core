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

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "grpcpp/grpcpp.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/simulation/gazebo/grpc_proxy/grpc_relay.h"
#include "intrinsic/simulation/gazebo/proto/v1/gazebo_service.grpc.pb.h"
#include "intrinsic/simulation/gazebo/proto/v1/gazebo_service.pb.h"
#include "intrinsic/simulation/service/proto/first_party/simulation_service.grpc.pb.h"
#include "intrinsic/simulation/service/proto/first_party/simulation_service.pb.h"
#include "intrinsic/stats/opencensus.h"
#include "intrinsic/util/grpc/channel.h"
#include "intrinsic/util/grpc/connection_params.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"

// Use ingress instead of internal service address.
ABSL_FLAG(std::string, simulation_service_address,
          "istio-ingressgateway.app-ingress.svc.cluster.local:80",
          "Address of the simulation service.");

namespace intrinsic {
namespace simulation {
namespace {

constexpr std::string_view kNoSimulatorErrorMessage =
    "Please install a Gazebo Simulator Asset service to use this "
    "functionality.";
constexpr absl::Duration kGrpcConnectTimeout = absl::Seconds(10);
constexpr absl::Duration kRpcDeadline = absl::Seconds(5);
constexpr absl::Duration kServerShutdownTimeout = absl::Seconds(2);

absl::StatusOr<int> PortFromEnvVar(std::string_view env_var) {
  // Convert string_view to string to ensure null-termination before
  // passing to `getenv.`
  const char* address = getenv(std::string(env_var).c_str());
  if (address == nullptr) {
    return InvalidArgumentErrorBuilder()
           << "Address env var [" << env_var << "] is not set!";
  }
  std::string_view address_str(address);
  size_t last_colon = address_str.find_last_of(':');

  if (last_colon != std::string_view::npos) {
    int port;
    std::string_view port_str = address_str.substr(last_colon + 1);
    if (absl::SimpleAtoi(port_str, &port)) {
      return port;
    }
  }
  return InvalidArgumentErrorBuilder()
         << "Address env var [" << env_var << "] is malformed: " << address;
}

// Generic gRPC service that returns a configured error on all RPCs routed to
// it.
class ErrorService : public ::grpc::CallbackGenericService {
  class ErrorReactor : public ::grpc::ServerGenericBidiReactor {
   public:
    explicit ErrorReactor(const ::grpc::Status& status) {
      // Immediately finish the call with the error
      this->Finish(status);
    }

    void OnDone() override { delete this; }

    // No-op overrides for other pure virtual methods (unused here)
    void OnReadDone(bool ok) override {}
    void OnWriteDone(bool ok) override {}
  };

 public:
  explicit ErrorService(const absl::Status& status)
      : status_(intrinsic::ToGrpcStatus(status)) {}

  void SetErrorStatus(const absl::Status& status) {
    absl::MutexLock l(mutex_);
    ::grpc::Status grpc_status = intrinsic::ToGrpcStatus(status);
    if (grpc_status.error_code() == status_.error_code() &&
        grpc_status.error_message() == status_.error_message()) {
      return;
    }
    LOG(INFO) << "Updating ErrorService status: " << status;
    status_ = grpc_status;
  }

  // CreateReactor is called for every incoming RPC
  ::grpc::ServerGenericBidiReactor* CreateReactor(
      ::grpc::GenericCallbackServerContext* context) override {
    absl::ReaderMutexLock l(mutex_);
    return new ErrorReactor(status_);
  }

 private:
  absl::Mutex mutex_;
  ::grpc::Status status_ ABSL_GUARDED_BY(mutex_);
};

// gRPC server hosting `ErrorService` on a set of ports.
class ErrorProxyServer {
 public:
  static absl::StatusOr<std::unique_ptr<ErrorProxyServer>> CreateAndStart(
      absl::Span<const int> ports, const absl::Status& status) {
    auto server = absl::WrapUnique(new ErrorProxyServer(status));
    ::grpc::ServerBuilder builder;

    for (int port : ports) {
      builder.AddListeningPort(absl::StrCat("[::]:", port),
                               ::grpc::InsecureServerCredentials());
    }

    builder.RegisterCallbackGenericService(&server->service_);
    server->server_ = builder.BuildAndStart();
    if (server->server_ == nullptr) {
      return AbortedErrorBuilder().LogError()
             << "Failed to start error proxy server.";
    }
    return server;
  }

  ~ErrorProxyServer() { Shutdown(); }

  void SetErrorStatus(const absl::Status& status) {
    service_.SetErrorStatus(status);
  }

  void Shutdown() {
    if (server_) {
      server_->Shutdown(
          absl::ToChronoTime(absl::Now() + kServerShutdownTimeout));
      server_.reset();
    }
  }

 private:
  explicit ErrorProxyServer(const absl::Status& status) : service_(status) {}

  ErrorService service_;
  std::unique_ptr<::grpc::Server> server_;
};

absl::StatusOr<
    intrinsic_proto::simulation::first_party::GetSimulatorStatusResponse>
GetSimulatorStatus(
    intrinsic_proto::simulation::first_party::SimulationService::Stub&
        simulation_service_stub,
    const ClientContextFactory& client_context_factory) {
  auto ctx = client_context_factory();
  ctx->set_deadline(absl::ToChronoTime(absl::Now() + kRpcDeadline));
  intrinsic_proto::simulation::first_party::GetSimulatorStatusRequest request;
  intrinsic_proto::simulation::first_party::GetSimulatorStatusResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(simulation_service_stub.GetSimulatorStatus(
      ctx.get(), request, &response)));
  return response;
}

absl::StatusOr<std::string> GetGazeboAddress(
    intrinsic_proto::simulation::v1::GazeboService::Stub& gazebo_service_stub,
    const ClientContextFactory& client_context_factory) {
  auto ctx = client_context_factory();
  ctx->set_deadline(absl::ToChronoTime(absl::Now() + kRpcDeadline));
  intrinsic_proto::simulation::v1::GetIPRequest request;
  intrinsic_proto::simulation::v1::GetIPResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(gazebo_service_stub.GetIPForTransportRelay(
      ctx.get(), request, &response)));
  return response.ip_address();
}

struct RelayData {
  // Service identifier.
  std::string service_id;

  // Remote server port on which the service is running.
  // The local relay is started on the same port.
  int port = 0;

  // The address of the remote server that the channel is connected on.
  std::string remote_address = "";

  // Only one of the following servers will be active at any given time.
  // If `relay` is set, it proxies traffic to the connected simulator.
  // If `error_server` is set, it returns configured errors.
  std::unique_ptr<GrpcRelay> relay = nullptr;
  std::unique_ptr<ErrorProxyServer> error_server = nullptr;
};

// Shuts down any active `GrpcRelay` on the specified `relay_data` port and
// starts an `ErrorProxyServer` with the given `error_status`. If an
// `ErrorProxyServer` is already running, its error status is updated.
absl::Status CloseRelayWithError(RelayData& relay_data,
                                 const absl::Status& error_status) {
  relay_data.remote_address.clear();
  relay_data.relay.reset();
  if (relay_data.error_server == nullptr) {
    LOG(WARNING) << "Serving error status for service id: "
                 << relay_data.service_id << ", error: " << error_status;
    auto error_server_or =
        ErrorProxyServer::CreateAndStart({relay_data.port}, error_status);
    if (error_server_or.ok()) {
      relay_data.error_server = *std::move(error_server_or);
    } else {
      return error_server_or.status();
    }
  } else {
    relay_data.error_server->SetErrorStatus(error_status);
  }
  return absl::OkStatus();
}

// Shuts down all active `GrpcRelay` instances and starts `ErrorProxyServer`
// instances on each port with the given `error_status`.
absl::Status CloseRelaysWithError(std::vector<RelayData>& relays,
                                  const absl::Status& error_status) {
  for (auto& relay_data : relays) {
    INTR_RETURN_IF_ERROR(CloseRelayWithError(relay_data, error_status));
  }
  return absl::OkStatus();
}

// Updates active `GrpcRelay` instances to forward traffic to the specified
// `remote_address`.
// For each relay:
// - If already connected to `remote_address`, skips update.
// - Connects a channel directly to `remote_address:port`. If direct connection
//   fails, starts an `ErrorProxyServer` on the port.
// - Restarts the relay using the connected channel.
absl::Status UpdateRelays(std::string_view remote_address,
                          std::vector<RelayData>& relays,
                          absl::Duration connect_timeout) {
  for (auto& relay_data : relays) {
    if (relay_data.relay != nullptr &&
        relay_data.remote_address == remote_address) {
      continue;
    }

    std::string remote_service_address =
        absl::StrCat(remote_address, ":", relay_data.port);
    absl::StatusOr<std::shared_ptr<Channel>> channel = Channel::MakeFromAddress(
        ConnectionParams::NoIngress(remote_service_address), connect_timeout);
    if (!channel.ok()) {
      absl::Status error_status = absl::UnavailableError(
          absl::StrCat("Failed to connect channel at ", remote_service_address,
                       " for service ", relay_data.service_id, ": ",
                       channel.status().message()));
      INTR_RETURN_IF_ERROR(CloseRelayWithError(relay_data, error_status));
      continue;
    }

    LOG(INFO) << "Creating new relay to service address "
              << remote_service_address << " for service "
              << relay_data.service_id;
    relay_data.remote_address = std::string(remote_address);

    relay_data.relay.reset();
    relay_data.error_server.reset();
    absl::StatusOr<std::unique_ptr<GrpcRelay>> new_relay =
        GrpcRelay::CreateAndStart(relay_data.port, *std::move(channel));
    if (!new_relay.ok()) {
      absl::Status error_status = absl::UnavailableError(absl::StrCat(
          "Failed to create new relay for service [", relay_data.service_id,
          "]: ", new_relay.status().message()));
      INTR_RETURN_IF_ERROR(CloseRelayWithError(relay_data, error_status));
      continue;
    }
    relay_data.relay = *std::move(new_relay);
  }
  return absl::OkStatus();
}

// Runs a loop that periodically polls SimulationService and manages proxy
// servers on the configured target ports forever:
// 1. Performs an initial poll of `SimulationService::GetSimulatorStatus`:
//    - If successful and a simulator is available, starts `GrpcRelay` instances
//      immediately.
//    - If unsuccessful with `FailedPreconditionError` or because simulator
//      status is not populated, starts in `FailedPrecondition` server mode.
//    - If the initial RPC fails for any other reason, returns the error status.
// 2. Periodically polls `SimulationService::GetSimulatorStatus` to discover
//    connected simulator instances or status changes.
// 3. Connects to `GazeboService` to retrieve the current IP address for Gazebo.
// 4. Dynamically switches mode based on simulator availability:
//    - If no simulator is available (or `FailedPreconditionError` is returned),
//      shuts down active relays and starts or updates
//      `FailedPrecondition` server with the error status.
//    - If a simulator is available, shuts down `FailedPrecondition` server
//      and starts or updates `GrpcRelay` instances, attempting a direct
//      connection to the resolved Gazebo IP address with a fallback to cluster
//      ingress.
//
// The loop protects against Gazebo crashes, pod restarts, or asset changes by
// dynamically updating relay connections whenever the simulator asset instance
// or the simulator IP address changes.
absl::Status RunRemoteServersWatcherLoop(
    std::shared_ptr<Channel> simulation_service_channel,
    std::vector<RelayData> relays) {
  LOG(INFO) << "Starting remote server watcher loop.";

  constexpr absl::Duration kWatcherLoopGrpcConnectTimeout = absl::Seconds(1);

  std::vector<int> all_ports;
  all_ports.reserve(relays.size());
  for (const auto& relay_data : relays) {
    all_ports.push_back(relay_data.port);
  }

  std::unique_ptr<
      intrinsic_proto::simulation::first_party::SimulationService::Stub>
      simulation_service_stub =
          intrinsic_proto::simulation::first_party::SimulationService::NewStub(
              simulation_service_channel->GetChannel());

  std::unique_ptr<ErrorProxyServer> failed_precondition_server;
  std::string current_simulator_name;
  std::shared_ptr<Channel> gazebo_channel;
  std::unique_ptr<intrinsic_proto::simulation::v1::GazeboService::Stub>
      gazebo_service_stub;

  auto switch_to_failed_precondition_mode =
      [&](std::string_view error_message) -> absl::Status {
    // Close existing relays and individual error servers.
    for (auto& relay_data : relays) {
      relay_data.relay.reset();
      relay_data.error_server.reset();
      relay_data.remote_address.clear();
    }
    current_simulator_name.clear();
    gazebo_service_stub.reset();
    gazebo_channel.reset();

    absl::Status error_status = absl::FailedPreconditionError(error_message);
    if (failed_precondition_server == nullptr) {
      LOG(INFO)
          << "Starting FailedPrecondition dummy proxy server with status: "
          << error_status;
      INTR_ASSIGN_OR_RETURN(
          failed_precondition_server,
          ErrorProxyServer::CreateAndStart(all_ports, error_status));
    } else {
      failed_precondition_server->SetErrorStatus(error_status);
    }
    return absl::OkStatus();
  };

  auto connect_to_gazebo =
      [&](std::string_view simulator_name) -> absl::Status {
    INTR_ASSIGN_OR_RETURN(
        gazebo_channel, Channel::MakeFromAddress(
                            ConnectionParams::ResourceInstance(simulator_name),
                            kWatcherLoopGrpcConnectTimeout));
    gazebo_service_stub =
        intrinsic_proto::simulation::v1::GazeboService::NewStub(
            gazebo_channel->GetChannel());
    return absl::OkStatus();
  };

  bool is_first_poll = true;
  while (true) {
    if (!is_first_poll) {
      // Poll more frequently while disconnected and less frequently once
      // connected to a Gazebo instance.
      bool any_relay_connected =
          std::any_of(relays.begin(), relays.end(),
                      [](const RelayData& rd) { return rd.relay != nullptr; });
      constexpr absl::Duration kDisconnectedSleepTime = absl::Seconds(2);
      constexpr absl::Duration kRelayModeSleepTime = absl::Seconds(10);
      absl::Duration sleep_time =
          any_relay_connected ? kRelayModeSleepTime : kDisconnectedSleepTime;
      absl::SleepFor(sleep_time);
    }

    auto response = GetSimulatorStatus(
        *simulation_service_stub,
        simulation_service_channel->GetClientContextFactory());

    if (is_first_poll) {
      if (!response.ok() && !absl::IsFailedPrecondition(response.status())) {
        // Exit early on first poll for all other simulation service errors to
        // trigger k8s retry backoff.
        return response.status();
      }
      is_first_poll = false;
    }

    if (!response.ok()) {
      if (absl::IsFailedPrecondition(response.status())) {
        INTR_RETURN_IF_ERROR(switch_to_failed_precondition_mode(
            absl::StrCat("Failed to get Gazebo simulator info: ",
                         response.status().message())));
      } else {
        LOG(WARNING)
            << "Failed to get simulator status from SimulationService: "
            << response.status();
      }
      continue;
    }

    if (!response->has_simulator_status() || response->simulator_status()
                                                 .simulator_info()
                                                 .simulator_name()
                                                 .empty()) {
      INTR_RETURN_IF_ERROR(
          switch_to_failed_precondition_mode(kNoSimulatorErrorMessage));
      continue;
    }

    std::string_view simulator_name =
        response->simulator_status().simulator_info().simulator_name();

    // Stop `FailedPrecondition` server if it is currently running, as we have
    // found a simulator asset.
    if (failed_precondition_server != nullptr) {
      LOG(INFO) << "Simulator [" << simulator_name << "] assigned"
                << ". Shutting down FailedPrecondition server.";
      failed_precondition_server.reset();
    }

    if (gazebo_service_stub == nullptr ||
        current_simulator_name != simulator_name) {
      absl::Status status = connect_to_gazebo(simulator_name);
      if (!status.ok()) {
        INTR_RETURN_IF_ERROR(CloseRelaysWithError(
            relays, absl::UnavailableError(absl::StrCat(
                        "Failed to connect channel at simulator address ",
                        simulator_name, ": ", status.message()))));
        continue;
      }
      current_simulator_name = simulator_name;
    }

    absl::StatusOr<std::string> remote_address = GetGazeboAddress(
        *gazebo_service_stub, gazebo_channel->GetClientContextFactory());
    if (!remote_address.ok()) {
      absl::Status error_status(
          remote_address.status().code(),
          absl::StrCat("Failed to get Gazebo address: ",
                       remote_address.status().message()));
      LOG(WARNING) << error_status
                   << ". Keeping existing relays active if present.";
      for (auto& relay_data : relays) {
        // If the relay is already connected, then don't disconnect it,
        // otherwise start an error proxy service so that the port is opened.
        if (relay_data.relay == nullptr) {
          INTR_RETURN_IF_ERROR(CloseRelayWithError(relay_data, error_status));
        }
      }
      continue;
    }

    INTR_RETURN_IF_ERROR(
        UpdateRelays(*remote_address, relays, kWatcherLoopGrpcConnectTimeout));
  }
  return absl::OkStatus();
}

absl::Status RunProxyServer() {
  static constexpr std::string_view kGrpcServiceAddressEnvVars[] = {
      "PINCH_GRIPPER_SERVICE_ADDRESS",
      "CAMERA_SERVICE_ADDRESS",
      "GPIO_SERVICE_ADDRESS",
      "SPAWNER_SERVICE_ADDRESS",
      "OUTFEED_SERVICE_ADDRESS",
      "HEALTH_AGGREGATOR_SERVICE_ADDRESS",
      "SET_SIMULATED_INPUTS_SERVICE_ADDRESS"};

  LOG(INFO) << "Starting proxy server to Gazebo simulator asset.";

  std::vector<RelayData> relays;
  for (const auto& env_var : kGrpcServiceAddressEnvVars) {
    INTR_ASSIGN_OR_RETURN(int port, PortFromEnvVar(env_var));
    relays.push_back(
        RelayData{.service_id = std::string(env_var), .port = port});
  }

  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<Channel> simulation_service_channel,
      Channel::MakeFromAddress(ConnectionParams::NoIngress(absl::GetFlag(
                                   FLAGS_simulation_service_address)),
                               kGrpcConnectTimeout));

  return RunRemoteServersWatcherLoop(std::move(simulation_service_channel),
                                     std::move(relays));
}

}  // namespace
}  // namespace simulation
}  // namespace intrinsic

int main(int argc, char* argv[]) {
  InitIntrinsic(argv[0], argc, argv);
  intrinsic::OpenCensusPlugin opencensus;
  CHECK_OK(intrinsic::simulation::RunProxyServer());
}
