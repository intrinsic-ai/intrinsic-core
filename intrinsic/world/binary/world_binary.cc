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

// Startup code for WorldServiceImpl server.
#include <cfenv>  // NOLINT(build/c++11)
#include <cstdint>
#include <future>  // NOLINT(build/c++11)
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/server.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/time.h"
#include "intrinsic/conductor/conductor.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/geometry/storage/cas_client.h"
#include "intrinsic/geometry/storage/cas_storage.h"
#include "intrinsic/geometry/storage/geo_cache.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/logging/data_logger_client.h"
#include "intrinsic/resources/client/resource_registry_client.h"
#include "intrinsic/stats/opencensus.h"
#include "intrinsic/storage/content_addressable_storage/proto/cas_service.grpc.pb.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/status/log_if_error.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/pubsub/tf_echo_guard.h"
#include "intrinsic/world/pubsub/world_tf_publisher.h"
#include "intrinsic/world/service/object_world_service.h"
#include "intrinsic/world/service/updater/world_updater.h"
#include "intrinsic/world/service/world_service.grpc.pb.h"
#include "intrinsic/world/service/world_storage.h"
#include "intrinsic/world/service/worldservice_impl.h"
#include "intrinsic/world/util/tf_frame_util.h"
#include "intrinsic/world/world.h"
#pragma STDC FENV_ACCESS ON

ABSL_FLAG(int32_t, port, 10000, "port to listen on for the world services");
ABSL_FLAG(std::string, resource_registry_address, "",
          "address for the resource registry service");
ABSL_FLAG(std::string, cas_service_address, "",
          "Address of the content-addressable storage (CAS) service.");
ABSL_FLAG(std::string, worlds_path, "",
          "location to use for reading and writing worlds");
ABSL_FLAG(std::string, data_logger_grpc_service_address, "",
          "(optional) Address of the gRPC service to send logs to, in the form "
          "'host:port'.");
ABSL_FLAG(absl::Duration, grpc_client_connect_timeout,
          intrinsic::connect::kGrpcClientConnectDefaultTimeout,
          "Time to wait for the grpc server to become available.");
ABSL_FLAG(bool, disable_asset_frame_edits, false,
          "Disallows reparenting, renaming, and deleting of frames that are "
          "added as an asset.");
ABSL_FLAG(absl::Duration, shutdown_grace_period, absl::Seconds(10),
          "Time to wait for graceful cancellation on shutdown");

ABSL_FLAG(std::string, hss_service_address, "",
          "The hot shared state service address");
ABSL_FLAG(std::string, sim_service_address, "", "The world server address");
ABSL_FLAG(std::string, workcell_cluster_service, "",
          "The workcell cluster service address");
ABSL_FLAG(std::string, asset_deployment_service, "",
          "The asset deployment service address");
ABSL_FLAG(std::string, asset_instances_service, "",
          "The asset instances service address");
ABSL_FLAG(std::string, runtime_db_service_address, "",
          "The runtime DB service address");
ABSL_FLAG(std::string, asset_info_internal_service_address, "",
          "The asset info internal service address");
ABSL_FLAG(std::string, logger_address, "", "The logger address");
ABSL_FLAG(int32_t, conductor_port, 8082, "Port to serve conductor gRPC on.");
ABSL_FLAG(bool, enable_sim_pause, false,
          "Whether sim should be paused/unpaused when process is "
          "paused/resumed.");
namespace {

using ::intrinsic::conductor::ConductorImpl;
using ::intrinsic::conductor::SrvParams;
using ShouldLoadWorlds = ::intrinsic::WorldStorage::LoadWorlds;

template <typename T>
std::future<T*> PointerFromSharedFuture(
    std::shared_future<std::unique_ptr<T>> fut) {
  return std::async(std::launch::deferred, [fut] { return fut.get().get(); });
}

constexpr absl::string_view kBeliefWorldId = "world";
constexpr absl::string_view kSimWorldId = "sim_world";

// Creates a gRPC server that serves `services` on `address` and starts
// accepting requests. Dies if the server cannot be started.
//
// All servers of this binary share the same configuration, in particular an
// unlimited maximum receive message size: the gRPC default of 4MB is too small
// for geometry meshes.
std::unique_ptr<grpc::Server> CreateAndStartServerOrDie(
    absl::string_view address, const std::vector<grpc::Service*>& services) {
  absl::StatusOr<std::unique_ptr<grpc::Server>> server =
      intrinsic::CreateServer(
          address, services,
          intrinsic::CreateServerOptions{.max_receive_message_size = -1});
  QCHECK_OK(server.status()) << "Cannot create gRPC server on " << address;
  return *std::move(server);
}

}  // namespace

int main(int argc, char** argv) {
  InitIntrinsic(argv[0], argc, argv);
  std::fesetround(FE_TOWARDZERO);
  intrinsic::OpenCensusPlugin open_census(/*service_name=*/"world_service",
                                          /*use_otel_metrics=*/true);

  const int32_t world_port = absl::GetFlag(FLAGS_port);
  // Address the world services listen on.
  const std::string world_server_address = absl::StrCat("[::]:", world_port);
  // Address that clients inside this process (the conductor) use to dial the
  // world services. Clients connect over the loopback interface instead of the
  // wildcard listening address.
  const std::string world_client_address =
      absl::StrCat("localhost:", world_port);

  ASSIGN_OR_DIE(
      std::shared_ptr<intrinsic_proto::content_addressable_storage::v1::
                          ContentAddressableStorageService::StubInterface>
          cas_service_stub,
      intrinsic::geo::MakeCASServiceStub(
          absl::GetFlag(FLAGS_cas_service_address)));

  intrinsic::geo::GeometryFingerprintCache geo_cache;
  std::shared_future<std::unique_ptr<intrinsic::GeometryLibrary>> geo_lib =
      std::async(std::launch::async, [&geo_cache,
                                      cas_stub = cas_service_stub]() {
        return intrinsic::GetCasGeometryLibrary(cas_stub, nullptr, geo_cache);
      }).share();

  std::shared_future<std::unique_ptr<intrinsic::WorldStorage>> world_storage =
      std::async(std::launch::async, [geo_lib] {
        const std::string worlds_path = absl::GetFlag(FLAGS_worlds_path);
        std::vector<intrinsic::WorldStorage::InitWorld> init_worlds;
        init_worlds.push_back({.world_id = "world",
                               .world = intrinsic::World::CreateEmptyWorld()});
        init_worlds.push_back({.world_id = "init_world",
                               .world = intrinsic::World::CreateEmptyWorld()});

        absl::StatusOr<std::unique_ptr<intrinsic::WorldStorage>> storage =
            intrinsic::WorldStorage::Create(
                worlds_path, PointerFromSharedFuture(geo_lib),
                intrinsic::object_world::ObjectWorld::
                    VerifyWorldCouldBeCompatible,
                ShouldLoadWorlds::kYes, std::move(init_worlds));

        if (!storage.ok()) {
          // Failed to create worlds from the provided path. But continue using
          // this path to store the worlds later. In the worst case, the saving
          // operation will fail but that's not catastrophic.
          LOG(ERROR) << "Error initializing world storage: " << storage.status()
                     << ". Skip loading worlds.";
          init_worlds = std::vector<intrinsic::WorldStorage::InitWorld>();
          init_worlds.push_back(
              {.world_id = "world",
               .world = intrinsic::World::CreateEmptyWorld()});
          init_worlds.push_back(
              {.world_id = "init_world",
               .world = intrinsic::World::CreateEmptyWorld()});
          return intrinsic::WorldStorage::Create(
                     worlds_path, PointerFromSharedFuture(geo_lib), {},
                     ShouldLoadWorlds::kNo, std::move(init_worlds))
              .value();
        }
        return std::move(*storage);
      }).share();

  ASSIGN_OR_DIE(
      std::unique_ptr<intrinsic::WorldServiceImpl> world_service,
      intrinsic::WorldServiceImpl::CreateService(
          PointerFromSharedFuture(world_storage),
          intrinsic::object_world::ObjectWorld::VerifyWorldCouldBeCompatible,
          PointerFromSharedFuture(geo_lib)));

  ASSIGN_OR_DIE(std::unique_ptr<intrinsic::object_world::ObjectWorldService>
                    object_world_service,
                intrinsic::object_world::ObjectWorldService::CreateService(
                    PointerFromSharedFuture(world_storage),
                    [&geo_cache, cas_stub = cas_service_stub](
                        grpc::ServerContext* context) {
                      return intrinsic::GetCasGeometryLibrary(cas_stub, nullptr,
                                                              geo_cache);
                    }));

  // We connect to the logger after creation of world_service so that we do
  // not have to wait for the logger service to be available before starting
  // the world loads from disk.
  if (std::string data_logger_address =
          absl::GetFlag(FLAGS_data_logger_grpc_service_address);
      !data_logger_address.empty()) {
    absl::Status status = intrinsic::data_logger::StartUpIntrinsicLoggerViaGrpc(
        data_logger_address, absl::GetFlag(FLAGS_grpc_client_connect_timeout));
    if (!status.ok()) {
      LOG(ERROR) << "Failed to connect to data logger: " << status;
    }
  }

  ASSIGN_OR_DIE(
      auto resource_registry_client,
      intrinsic::resources::CreateResourceRegistryClient(
          absl::GetFlag(FLAGS_resource_registry_address), absl::Seconds(60)));
  intrinsic::icon::DefaultChannelFactory icon_channel_factory;

  intrinsic::TfEchoGuard tf_echo_guard;

  intrinsic::WorldUpdater::Options options(
      std::string(kBeliefWorldId), std::string(kSimWorldId), world_storage,
      resource_registry_client.get(), &icon_channel_factory, &tf_echo_guard);

  ASSIGN_OR_DIE(std::unique_ptr<intrinsic::WorldUpdater> world_updater,
                intrinsic::WorldUpdater::Create(std::move(options)));

  // Finishes the service initialization before the server starts accepting
  // requests so that no request can observe a partially initialized service.
  object_world_service->Init(absl::GetFlag(FLAGS_disable_asset_frame_edits));

  // Serves the low- and high-level world services for access to the same
  // underlying worlds. The server starts serving in the background, main()
  // blocks on the conductor server below.
  std::unique_ptr<grpc::Server> world_server = CreateAndStartServerOrDie(
      world_server_address,
      {world_service.get(), object_world_service.get(), world_updater.get()});

  LOG(INFO) << "World Server listening on " << world_server_address;

  // The conductor is created after the world server is serving because it
  // connects to the world services as a gRPC client.
  const SrvParams conductor_params{
      .hss_service_address = absl::GetFlag(FLAGS_hss_service_address),
      .sim_service_address = absl::GetFlag(FLAGS_sim_service_address),
      .object_world_service_address = world_client_address,
      .workcell_cluster_service_address =
          absl::GetFlag(FLAGS_workcell_cluster_service),
      .asset_deployment_service_address =
          absl::GetFlag(FLAGS_asset_deployment_service),
      .asset_instances_service_address =
          absl::GetFlag(FLAGS_asset_instances_service),
      .runtimedb_service_address =
          absl::GetFlag(FLAGS_runtime_db_service_address),
      .asset_info_internal_service_address =
          absl::GetFlag(FLAGS_asset_info_internal_service_address),
      .enable_sim_pause = absl::GetFlag(FLAGS_enable_sim_pause),
      .world_updater = *world_updater,
      .world_service = *object_world_service};

  LOG(INFO) << "Creating C++ Conductor service...";
  ASSIGN_OR_DIE(std::unique_ptr<ConductorImpl> conductor,
                ConductorImpl::Create(conductor_params));

  // Recreates the solution worlds from a recovered execution context before the
  // conductor starts serving so that no conductor request observes missing or
  // stale worlds after a restart. A failure is logged but not fatal: the
  // conductor still serves requests and the worlds are recreated by the next
  // StartSolution call, i.e., when the solution is (re)started.
  INTR_LOG_IF_ERROR(absl::LogSeverity::kError,
                    conductor->CreateSolutionWorldsIfValidExecutionContext());

  // The conductor is served on a separate server so that its RPCs cannot be
  // starved by long-running world RPCs sharing the same completion queues.
  const std::string conductor_address =
      absl::StrCat("[::]:", absl::GetFlag(FLAGS_conductor_port));
  std::unique_ptr<grpc::Server> conductor_server =
      CreateAndStartServerOrDie(conductor_address, {conductor.get()});

  LOG(INFO) << "Conductor listening on " << conductor_address;

  intrinsic::WorldTfPublisher::Options world_tf_options{
      .world_id = std::string(intrinsic::kBeliefWorldId),
      .tf_topic = std::string(intrinsic::kBeliefWorldTfTopic),
      .tf_associations_topic =
          std::string(intrinsic::kBeliefWorldTfAssociationTopic),
  };

  ASSIGN_OR_DIE(std::unique_ptr<intrinsic::WorldTfPublisher> tf_publisher,
                intrinsic::WorldTfPublisher::Create(
                    PointerFromSharedFuture(world_storage), &tf_echo_guard,
                    world_tf_options));
  INTR_LOG_IF_ERROR(absl::LogSeverity::kError, tf_publisher->Start());

  intrinsic::WorldTfPublisher::Options sim_world_tf_options{
      .world_id = "sim_world",
      .tf_topic = "tf_sim",
      .tf_associations_topic = "tf_associations_sim",
  };

  ASSIGN_OR_DIE(std::unique_ptr<intrinsic::WorldTfPublisher> sim_tf_publisher,
                intrinsic::WorldTfPublisher::Create(
                    PointerFromSharedFuture(world_storage), nullptr,
                    sim_world_tf_options));
  INTR_LOG_IF_ERROR(absl::LogSeverity::kError, sim_tf_publisher->Start());

  // Registers the process-wide SIGTERM handler and blocks until shutdown.
  //
  // `RegisterSignalHandlerAndWait()` installs a single, process-wide SIGTERM
  // handler, so it must be called exactly once even though this binary serves
  // two servers. It is called for the conductor server, which is drained first
  // on SIGTERM: in-flight conductor RPCs can still reach the world services
  // because the world server keeps serving until the call below returns.
  const intrinsic::ShutdownParams shutdown_params{
      .health_grace_duration = absl::ZeroDuration(),
      .shutdown_timeout = absl::GetFlag(FLAGS_shutdown_grace_period),
  };
  absl::Notification handlers_registered;
  // Dies instead of returning early: without a SIGTERM handler the process
  // would exit immediately and skip the graceful shutdown below.
  QCHECK_OK(intrinsic::RegisterSignalHandlerAndWait(
      conductor_server.get(), shutdown_params, handlers_registered));

  // Destroys the conductor (including any polling threads) before shutting down
  // the world server since it directly calls world C++ APIs.
  conductor_server.reset();
  conductor.reset();

  // The conductor is shut down, so the world server is drained next.
  LOG(INFO) << "Conductor stopped, shutting down the World Server.";
  world_server->Shutdown(absl::Now() + shutdown_params.shutdown_timeout);
  world_server->Wait();

  // World storage should flush the worlds to disk in the destructor.
  LOG(INFO) << "Goodbye world!";

  return 0;
}
