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

/*
The Intrinsic Executive executes and monitors Behavior Trees.
It is run as part of the Intrinsic platform.
*/

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpc/grpc.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "intrinsic/executive/engine/clips_executive_service.h"
#include "intrinsic/executive/engine/clips_metrics.h"
#include "intrinsic/executive/engine/extended_status_codes.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/stats/opencensus.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_specs.h"
#include "intrinsic/util/thread/thread.h"

ABSL_FLAG(int32_t, port, 8080,
          "Port to listen on for CLIPS executive service.");
ABSL_FLAG(absl::Duration, shutdown_grace_period, absl::Seconds(10),
          "Time to wait for graceful cancellation on shutdown");

namespace {
// std::atomic is safe, as long as it is lock-free
std::atomic<bool> g_shutdown_requested = false;
static_assert(std::atomic<bool>::is_always_lock_free);

// 64MB. Mainly for receiving large processes. This cannot be unlimited as these
// are stored in executive memory. Reasonable single processes are expected to
// be much smaller. User data could make these larger than necessary.
// TODO(b/552305883): Warn the user when a large process is received once we
// have a warning mechanism.
constexpr int kExecutiveGrpcMaxMessageSize = 64 * 1024 * 1024;
}  // namespace

int main(int argc, char** argv) {
  InitIntrinsic(argv[0], argc, argv);
  intrinsic::OpenCensusPlugin open_census(/*service_name=*/"executive",
                                          /*use_otel_metrics=*/true);
  intrinsic::executive::RegisterExecutiveMetrics();

  INTR_RETURN_IF_ERROR(intrinsic::InitExtendedStatusSpecs(
                           intrinsic::executive::kExtendedStatusComponent,
                           intrinsic::PathResolver::ResolveRunfilesPath(
                               intrinsic::executive::kExtendedStatusFile)))
      .With(intrinsic::ExtraMessage()
            << "Failed to initialize extended status specs.")
      .With(intrinsic::Return(EXIT_FAILURE));

  const std::string server_address =
      absl::StrFormat("[::]:%d", absl::GetFlag(FLAGS_port));

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<intrinsic::executive::ClipsExecutiveService>
          executive_service,
      intrinsic::executive::ClipsExecutiveService::CreateService(),
      _.LogError()
          .With(intrinsic::ExtraMessage()
                << "Failed starting executive service.")
          .With(intrinsic::Return(EXIT_FAILURE)));
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<intrinsic::executive::ExecutiveDebugService>
          executive_debug_service,
      intrinsic::executive::ExecutiveDebugService::CreateService(
          executive_service.get()),
      _.LogError()
          .With(intrinsic::ExtraMessage()
                << "Failed starting executive debug service.")
          .With(intrinsic::Return(EXIT_FAILURE)));

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<intrinsic::executive::ExecutiveBlackboardService>
          executive_blackboard_service,
      intrinsic::executive::ExecutiveBlackboardService::CreateService(
          executive_service.get()),
      _.LogError()
          .With(intrinsic::ExtraMessage()
                << "Failed starting executive blackboard service.")
          .With(intrinsic::Return(EXIT_FAILURE)));

  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address,
                           grpc::InsecureServerCredentials());  // NOLINT
  // "0" means no port reuse. Allowing other servers on the same port could
  // introduce hard-to-debug behavior or flaky tests.
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.SetMaxReceiveMessageSize(kExecutiveGrpcMaxMessageSize);
  builder.RegisterService(executive_service.get());
  builder.RegisterService(executive_blackboard_service.get());
  builder.RegisterService(executive_debug_service.get());
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  LOG_IF(QFATAL, server == nullptr)
      << "Cannot create ClipsExecutive server " << server_address;
  LOG(INFO) << "ClipsExecutive server listening on " << server_address;

  std::signal(SIGTERM, [](int) {
    // async-signal-safe implementation
    // for details, see here:
    // https://man7.org/linux/man-pages/man7/signal-safety.7.html
    // Note: To prevent undefined behavior, do not do any logging (unless using
    //   async-safe write) or else here without making sure that the function is
    //   async-signal-safe.
    g_shutdown_requested = true;
    g_shutdown_requested.notify_all();
  });

  intrinsic::Thread shutdown([&]() {
    g_shutdown_requested.wait(/*old=*/false);
    if (server->GetHealthCheckService()) {
      server->GetHealthCheckService()->SetServingStatus(false);
    }
    executive_service->Shutdown();
    // Give grace period to gracefully cancel operations
    absl::SleepFor(absl::GetFlag(FLAGS_shutdown_grace_period));
    server->Shutdown();
  });

  server->Wait();
  shutdown.join();

  server.reset();
  executive_debug_service.reset();

  return EXIT_SUCCESS;
}
