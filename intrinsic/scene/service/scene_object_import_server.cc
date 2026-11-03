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

#include "intrinsic/scene/service/scene_object_import_server.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.grpc.pb.h"
#include "grpc/grpc.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "intrinsic/assets/proto/asset_deployment.grpc.pb.h"
#include "intrinsic/assets/proto/installed_assets.grpc.pb.h"
#include "intrinsic/assets/proto/installed_assets.pb.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/longrunning/cc/grpc_operations_proxy.h"
#include "intrinsic/longrunning/cc/operation_scheduler.h"
#include "intrinsic/longrunning/cc/operations_proxy.h"
#include "intrinsic/longrunning/cc/operations_service_impl.h"
#include "intrinsic/longrunning/cc/wrappers.h"
#include "intrinsic/scene/cad/grpc_cad_importer.h"
#include "intrinsic/scene/instantiation/asset_and_resource_instantiator.h"
#include "intrinsic/scene/service/scene_object_edit_service_impl.h"
#include "intrinsic/scene/service/scene_object_import.h"
#include "intrinsic/scene/usd/utils.h"
#include "intrinsic/stats/opencensus.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

namespace {

using ::google::longrunning::Operations;
using ::intrinsic_proto::assets::v1::InstalledAssets;

// Five minute wait for all other services required to be available.
// This is fine for world import service because the first client request
// usually happens way later after the initial deployment.
constexpr absl::Duration kGrpcClientConnectTimeout = absl::Minutes(5);

absl::StatusOr<
    std::unique_ptr<intrinsic_proto::geometry::GeometryService::StubInterface>>
CreateGeometryServiceStub(absl::string_view geometry_service_address) {
  INTR_ASSIGN_OR_RETURN(
      auto channel,
      connect::CreateClientChannel(
          geometry_service_address, absl::Now() + kGrpcClientConnectTimeout,
          connect::UnlimitedMessageSizeGrpcChannelArgs()));
  return intrinsic_proto::geometry::GeometryService::NewStub(channel);
}

absl::StatusOr<std::unique_ptr<longrunning::OperationsProxy>>
CreateGeometryOperationsProxy(absl::string_view geometry_service_address) {
  INTR_ASSIGN_OR_RETURN(auto operations_proxy_stub,
                        longrunning::GrpcOperationsProxy::CreateStubFromAddress(
                            geometry_service_address, kGrpcClientConnectTimeout,
                            /*use_default_application_credentials=*/false));
  return std::make_unique<longrunning::GrpcOperationsProxy>(
      std::move(operations_proxy_stub));
}

absl::StatusOr<std::shared_ptr<
    intrinsic_proto::assets::AssetDeploymentService::StubInterface>>
CreateAssetDeploymentServiceStub(
    absl::string_view asset_deployment_service_address) {
  INTR_ASSIGN_OR_RETURN(auto channel,
                        connect::CreateClientChannel(
                            asset_deployment_service_address,
                            absl::Now() + kGrpcClientConnectTimeout,
                            connect::UnlimitedMessageSizeGrpcChannelArgs()));
  return intrinsic_proto::assets::AssetDeploymentService::NewStub(channel);
}

absl::StatusOr<std::shared_ptr<Operations::StubInterface>> CreateOperationsStub(
    absl::string_view workcell_cluster_service_address) {
  INTR_ASSIGN_OR_RETURN(auto channel,
                        connect::CreateClientChannel(
                            workcell_cluster_service_address,
                            absl::Now() + kGrpcClientConnectTimeout,
                            connect::UnlimitedMessageSizeGrpcChannelArgs()));
  return Operations::NewStub(channel);
}

absl::StatusOr<std::shared_ptr<InstalledAssets::StubInterface>>
CreateInstalledAssetsStub(absl::string_view workcell_cluster_service_address) {
  INTR_ASSIGN_OR_RETURN(auto channel,
                        connect::CreateClientChannel(
                            workcell_cluster_service_address,
                            absl::Now() + kGrpcClientConnectTimeout,
                            connect::UnlimitedMessageSizeGrpcChannelArgs()));
  return InstalledAssets::NewStub(channel);
}

}  // namespace

absl::Status RunSceneObjectImportServer(
    const SceneObjectImportServerOptions& options) {
  OpenCensusPlugin open_census;

  const std::string server_address = absl::StrCat("[::]:", options.port);

  if (options.geometry_service_address.empty()) {
    return absl::InvalidArgumentError("Geometry service address is empty");
  }

  if (options.asset_deployment_service_address.empty()) {
    return absl::InvalidArgumentError(
        "Assert deployment service address is empty");
  }

  if (options.workcell_cluster_service_address.empty()) {
    return absl::InvalidArgumentError(
        "Workcell cluster service address is empty");
  }

  // Initialize the OpenUSD library. Must be done before any USD functions are
  // called.
  INTR_RETURN_IF_ERROR(usd::InitializeOpenUsdLibrary());

  INTR_ASSIGN_OR_RETURN(
      auto scheduler, longrunning::wrap_internal::WrapSchedulerWithPrefix(
                          "SceneObjectImport",
                          std::make_shared<longrunning::OperationScheduler>()));

  INTR_ASSIGN_OR_RETURN(
      auto geometry_service_stub,
      CreateGeometryServiceStub(options.geometry_service_address));

  INTR_ASSIGN_OR_RETURN(
      auto geometry_operations_proxy,
      CreateGeometryOperationsProxy(options.geometry_service_address));

  INTR_ASSIGN_OR_RETURN(auto asset_deployment_service_stub,
                        CreateAssetDeploymentServiceStub(
                            options.asset_deployment_service_address));

  INTR_ASSIGN_OR_RETURN(
      auto asset_deployment_operations_stub,
      CreateOperationsStub(options.asset_deployment_service_address));
  INTR_ASSIGN_OR_RETURN(
      auto installed_assets_service_stub,
      CreateInstalledAssetsStub(options.workcell_cluster_service_address));
  INTR_ASSIGN_OR_RETURN(
      auto installed_assets_operations_stub,
      CreateOperationsStub(options.workcell_cluster_service_address));

  INTR_ASSIGN_OR_RETURN(auto asset_and_resource_instantiator,
                        AssetAndResourceInstantiator::Create(
                            std::move(asset_deployment_service_stub),
                            std::move(asset_deployment_operations_stub),
                            std::move(installed_assets_service_stub),
                            std::move(installed_assets_operations_stub)));

  INTR_ASSIGN_OR_RETURN(SceneObjectImportServices services,
                        CreateSceneObjectImportServices(
                            std::move(geometry_service_stub),
                            std::move(geometry_operations_proxy),
                            std::move(asset_and_resource_instantiator),
                            scheduler));

  INTR_ASSIGN_OR_RETURN(
      auto scene_object_edit_service,
      scene_object::SceneObjectEditServiceImpl::CreateService());

  intrinsic::longrunning::OperationsServiceImpl operations_service(scheduler);

  // Set the authentication mechanism.
  // NOTE(pushkarj): Loas2ServerCredentials is not available for config=gce,
  // which is how all our apps are invoked from within the cluster.
  std::shared_ptr<grpc::ServerCredentials> creds =
      grpc::InsecureServerCredentials();  // NOLINT

  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, creds);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.RegisterService(services.soii_service.get());
  builder.RegisterService(services.soi_service.get());
  builder.RegisterService(services.soe_service.get());
  builder.RegisterService(scene_object_edit_service.get());
  builder.RegisterService(&operations_service);

  // Set the max message send and receive size to be unlimited. The default
  // GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH is 4MB, too small for cad files
  // containing geometry. GRPC_DEFAULT_MAX_SEND_MESSAGE_LENGTH is currently -1
  // but to prevent future breakages, set send size explicitly here.
  builder.SetMaxSendMessageSize(-1);
  builder.SetMaxReceiveMessageSize(-1);
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    LOG(QFATAL) << "Cannot create World Import Server " << server_address;
  }

  LOG(INFO) << "World Import Server listening on " << server_address;

  // Registers signal handler and waits till shutdown.
  intrinsic::ShutdownParams shutdown_params;
  shutdown_params.health_grace_duration = absl::ZeroDuration();
  shutdown_params.shutdown_timeout = options.shutdown_grace_period;
  INTR_RETURN_IF_ERROR(intrinsic::RegisterSignalHandlerAndWait(
      server.get(), shutdown_params, options.handlers_registered));

  return absl::OkStatus();
}

}  // namespace intrinsic
