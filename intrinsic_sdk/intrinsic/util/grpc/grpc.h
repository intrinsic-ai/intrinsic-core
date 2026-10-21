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

#ifndef INTRINSIC_UTIL_GRPC_GRPC_H_
#define INTRINSIC_UTIL_GRPC_GRPC_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "grpcpp/grpcpp.h"

namespace intrinsic {

// Default client-side timeout for invoking services (when configuring the
// client context with ConfigureClientContext().
constexpr absl::Duration kGrpcClientServiceCallDefaultTimeout =
    absl::Seconds(60);

// Options for CreateServer calls.
struct CreateServerOptions {
  // Set the maximum receive message size.
  std::optional<int> max_receive_message_size;
};

/**
 * Create a grpc server using the listen port on the default interface
 * and the set of services provided
 */
absl::StatusOr<std::unique_ptr<::grpc::Server>> CreateServer(
    uint16_t listen_port, const std::vector<::grpc::Service*>& services,
    const CreateServerOptions& options = CreateServerOptions());

/**
 * Create a grpc server using a specific address to listen to.
 */
absl::StatusOr<std::unique_ptr<::grpc::Server>> CreateServer(
    absl::string_view address, const std::vector<::grpc::Service*>& services,
    const CreateServerOptions& options = CreateServerOptions());

/**
 * Apply the default configuration of our project to the given ClientContext.
 *
 * Configurations set on the context:
 * - enable initial waiting for a connection to the service
 * - set a fixed maximum deadline (see kGrpcClientServiceCallDefaultTimeout),
 *   override if you know that this can be shorter, or if it needs to be
 *   longer. In particular mind deadlines for wait calls (e.g., long-running
 *   operations) and streaming calls which often need to be increased or be
 *   set to absl::InfiniteFuture().
 */
void ConfigureClientContext(::grpc::ClientContext* client_context);

// Parameters to configure the shutdown behavior of a gRPC server.
struct ShutdownParams {
  // Duration to wait for the grpc's health service state (if relevant) to
  // propagate to the load balancers.
  absl::Duration health_grace_duration;
  // Timeout passed into grpc::Server::Shutdown on a sigterm.
  absl::Duration shutdown_timeout;

  // Returns params that aggressively shutdowns the server.
  static ShutdownParams Aggressive();
};

// Registers a custom signal handler for SIGTERM, serves the server and blocks
// till it is shutdown. The custom handler is left registered when the function
// returns.
//
// `handlers_registered` notification is triggered once the signal handler is
// registered. This is mainly useful in unit tests to know when it is okay to
// raise a SIGTERM signal.
//
// Returns an error if registering the signal handler fails.
//
// Typical usage:
// ```
//   int main() {
//      auto server = intrinsic::CreateServer(...);
//      absl::Notification registered;
//      QCHECK_OK(RegisterSignalHandlerAndWait(server.get(),
//                ShutdownParams{...}, registered));
//      return EXIT_SUCCESS;
//   }
// ```
absl::Status RegisterSignalHandlerAndWait(
    ::grpc::Server* server, const ShutdownParams& params,
    absl::Notification& handlers_registered);

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_GRPC_GRPC_H_
