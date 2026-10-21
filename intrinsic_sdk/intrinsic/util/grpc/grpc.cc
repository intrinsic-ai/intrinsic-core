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

#include "intrinsic/util/grpc/grpc.h"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpc/grpc.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/support/time.h"
#include "intrinsic/util/grpc/limits.h"
#include "intrinsic/util/thread/thread.h"
#include "src/proto/grpc/health/v1/health.pb.h"

namespace intrinsic {

/**
 * Create a grpc server using the given address and the set of services provided
 */
absl::StatusOr<std::unique_ptr<::grpc::Server>> CreateServer(
    const absl::string_view address,
    const std::vector<::grpc::Service*>& services,
    const CreateServerOptions& options) {
  ::grpc::ServerBuilder builder;

  builder.AddListeningPort(
      std::string(address),
      ::grpc::                       // NOLINTNEXTLINE
      InsecureServerCredentials());  // NO_LINT(grpc_insecure_credential_linter)

  if (options.max_receive_message_size.has_value()) {
    builder.SetMaxReceiveMessageSize(*options.max_receive_message_size);
  }

  // "0" means no port reuse. Allowing other servers on the same port could
  // introduce hard-to-debug behavior or flaky tests.
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);

  builder.AddChannelArgument(GRPC_ARG_MAX_METADATA_SIZE,
                             kGrpcRecommendedMaxMetadataSoftLimit);
  builder.AddChannelArgument(GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE,
                             kGrpcRecommendedMaxMetadataHardLimit);

  for (const auto& service : services) {
    builder.RegisterService(service);
  }

  std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    return absl::InternalError("Could not start the server.");
  }

  return server;
}

absl::StatusOr<std::unique_ptr<::grpc::Server>> CreateServer(
    uint16_t listen_port, const std::vector<::grpc::Service*>& services,
    const CreateServerOptions& options) {
  std::string address = "0.0.0.0:" + std::to_string(listen_port);
  return CreateServer(address, services, options);
}

void ConfigureClientContext(::grpc::ClientContext* client_context) {
  // Expect that gRPC service calls will block/retry if the other end isn't
  // ready yet.
  client_context->set_wait_for_ready(true);

  // Avoid indefinitely blocking service calls by default. Override separately
  // if necessary.
  client_context->set_deadline(absl::Now() +
                               kGrpcClientServiceCallDefaultTimeout);
}

ShutdownParams ShutdownParams::Aggressive() {
  return {.health_grace_duration = absl::ZeroDuration(),
          .shutdown_timeout = absl::Milliseconds(250)};
}

absl::Status RegisterSignalHandlerAndWait(
    grpc::Server* server, const ShutdownParams& params,
    absl::Notification& handlers_registered) {
  // User-provided signal handler needs to satisfy some constraints.
  // Effectively:
  // - access to atomics should be lock-free (std::atomic_flag is guaranteed to
  //   be lock-free by the standard).
  // - objects referred should have the lifetime of the process (hence, the use
  //   of `static`).
  //
  // See cpp reference for more details:
  // https://en.cppreference.com/w/cpp/utility/program/signal
  static std::atomic_flag shutdown_requested = ATOMIC_FLAG_INIT;

  auto prev_signal_handler = std::signal(SIGTERM, [](int) {
    // async-signal-safe implementation
    // for details, see here:
    // https://man7.org/linux/man-pages/man7/signal-safety.7.html
    // Note: To prevent undefined behavior, do not do any logging (unless using
    //   async-safe write) or else here without making sure that the function is
    //   async-signal-safe.

    // Ignores the returned value of `test_and_set` method.
    (void)shutdown_requested.test_and_set();
    shutdown_requested.notify_all();
  });
  if (prev_signal_handler == SIG_ERR) {
    return absl::InternalError("SIGTERM handler registration failed.");
  } else if (prev_signal_handler != nullptr) {
    LOG(WARNING) << absl::StrFormat(
        "Previously registered SIGTERM handler with address %p was "
        "overwritten.",
        prev_signal_handler);
  }

  handlers_registered.Notify();

  bool stop_shutdown_thread = false;
  intrinsic::Thread shutdown([&]() {
    constexpr bool kOldValue = false;
    shutdown_requested.wait(kOldValue);

    if (stop_shutdown_thread) {
      return;
    }

    if (server->GetHealthCheckService()) {
      server->GetHealthCheckService()->SetServingStatus(false);
      absl::SleepFor(params.health_grace_duration);
    }
    server->Shutdown(absl::Now() + params.shutdown_timeout);
  });

  server->Wait();

  // Makes the shutdown thread exit if the grpc server shut down due to
  // something other than SIGTERM.
  if (!shutdown_requested.test()) {
    stop_shutdown_thread = true;
    // Sets the flag to wake up the shutdown thread.
    (void)shutdown_requested.test_and_set();
    shutdown_requested.notify_all();
  }

  shutdown.join();
  return absl::OkStatus();
}

}  // namespace intrinsic
