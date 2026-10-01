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

#ifndef INTRINSIC_CONDUCTOR_CONDUCTOR_H_
#define INTRINSIC_CONDUCTOR_CONDUCTOR_H_

#include <grpcpp/grpcpp.h>

#include <functional>
#include <stop_token>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "google/longrunning/operations.grpc.pb.h"
#include "intrinsic/assets/proto/asset_deployment.grpc.pb.h"
#include "intrinsic/assets/proto/installed_assets.grpc.pb.h"
#include "intrinsic/assets/proto/v1/asset_instances.grpc.pb.h"
#include "intrinsic/conductor/execution_context.h"
#include "intrinsic/conductor/proto/conductor.grpc.pb.h"
#include "intrinsic/conductor/proto/conductor.pb.h"
#include "intrinsic/conductor/resource_world_wrapper.h"
#include "intrinsic/conductor/save_scene_monitor.h"
#include "intrinsic/kubernetes/acl/cc/client_context.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/simulation/service/proto/first_party/simulation_service.grpc.pb.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/service/updater/world_updater.grpc.pb.h"

namespace intrinsic::conductor {

class ExecutionContextOperation;

// Params to initialize conductor.
struct SrvParams {
  // Addresses to connect to platform services.
  std::string hss_service_address;
  std::string sim_service_address;

  // Used by save scene monitor loop.
  std::string object_world_service_address;
  std::string workcell_cluster_service_address;
  std::string asset_deployment_service_address;
  std::string asset_instances_service_address;
  std::string runtimedb_service_address;

  // Whether to enable simulation pause.
  bool enable_sim_pause;

  // Direct in-process reference to WorldUpdater service.
  intrinsic_proto::world::WorldUpdater::Service& world_updater;

  // Direct in-process reference to ObjectWorldService.
  intrinsic_proto::world::ObjectWorldService::Service& world_service;
};

struct ConductorOptions {
  std::shared_ptr<intrinsic::KeyValueStore> kv_store;
  std::unique_ptr<ResourceWorld> resource_world;
  std::unique_ptr<SaveSceneMonitor> save_scene_monitor;
  std::unique_ptr<ExecutionContext> execution_context;

  // Direct in-process services
  intrinsic_proto::world::ObjectWorldService::Service& world_service;
  intrinsic_proto::world::WorldUpdater::Service& world_updater;

  // Stubs
  std::unique_ptr<
      intrinsic_proto::simulation::first_party::SimulationService::Stub>
      sim_service_stub;
  std::unique_ptr<google::longrunning::Operations::Stub>
      installed_assets_lro_stub;
  std::unique_ptr<intrinsic_proto::assets::v1::AssetInstances::Stub>
      asset_instances_stub;
  std::unique_ptr<intrinsic_proto::assets::AssetDeploymentService::Stub>
      asset_deployment_stub;
  std::unique_ptr<google::longrunning::Operations::Stub>
      asset_deployment_lro_stub;
  std::unique_ptr<intrinsic_proto::assets::v1::InstalledAssets::Stub>
      installed_assets_stub;

  bool enable_sim_pause = false;
};

inline grpc::Status ErrInvalidExecutionContext() {
  return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                      "no execution context available: solution not started.");
}

// ConductorImpl implements the ConductorService gRPC interface.
using KVStoreCreator =
    std::function<absl::StatusOr<std::shared_ptr<intrinsic::KeyValueStore>>()>;

class ConductorImpl final
    : public intrinsic_proto::conductor::ConductorService::Service {
 public:
  // Factory function to create the conductor service implementation.
  static absl::StatusOr<std::unique_ptr<ConductorImpl>> Create(
      const SrvParams& params, KVStoreCreator create_kv_store = nullptr);

  ConductorImpl() = delete;

  explicit ConductorImpl(ConductorOptions opts);
  ~ConductorImpl() override;

  grpc::Status StartSolution(
      grpc::ServerContext* context,
      const intrinsic_proto::conductor::StartSolutionRequest* request,
      intrinsic_proto::conductor::StartSolutionResponse* response) override;

  grpc::Status GetExecutionContext(
      grpc::ServerContext* context,
      const intrinsic_proto::conductor::GetExecutionContextRequest* request,
      intrinsic_proto::conductor::ExecutionContext* response) override;

  grpc::Status GetSaveStatus(
      grpc::ServerContext* context,
      const intrinsic_proto::conductor::GetSaveStatusRequest* request,
      intrinsic_proto::conductor::GetSaveStatusResponse* response) override;

  grpc::Status StopSolution(
      grpc::ServerContext* context,
      const intrinsic_proto::conductor::StopSolutionRequest* request,
      intrinsic_proto::conductor::StopSolutionResponse* response) override;

  grpc::Status Reset(
      grpc::ServerContext* context,
      const intrinsic_proto::conductor::ResetRequest* request,
      intrinsic_proto::conductor::ResetResponse* response) override;

  grpc::Status PrepareProcessStart(
      grpc::ServerContext* context,
      const intrinsic_proto::conductor::PrepareProcessStartRequest* request,
      google::longrunning::Operation* response) override;
  grpc::Status GetOperation(
      grpc::ServerContext* context,
      const google::longrunning::GetOperationRequest* request,
      google::longrunning::Operation* response) override;

  grpc::Status CancelOperation(
      grpc::ServerContext* context,
      const google::longrunning::CancelOperationRequest* request,
      google::protobuf::Empty* response) override;

  grpc::Status PrepareProcessResume(
      grpc::ServerContext* context,
      const intrinsic_proto::conductor::PrepareProcessResumeRequest* request,
      intrinsic_proto::conductor::PrepareProcessResumeResponse* response)
      override;
  grpc::Status NotifyAllProcessesStopped(
      grpc::ServerContext* context,
      const intrinsic_proto::conductor::NotifyAllProcessesStoppedRequest*
          request,
      intrinsic_proto::conductor::NotifyAllProcessesStoppedResponse* response)
      override;

  grpc::Status SaveWorld(
      grpc::ServerContext* context,
      const intrinsic_proto::conductor::SaveWorldRequest* request,
      intrinsic_proto::conductor::SaveWorldResponse* response) override;

  grpc::Status ConfigureSceneObject(
      grpc::ServerContext* context,
      const intrinsic_proto::conductor::ConfigureSceneObjectRequest* request,
      intrinsic_proto::conductor::ConfigureSceneObjectResponse* response)
      override;

  grpc::Status RestoreWorld(
      grpc::ServerContext* context,
      const intrinsic_proto::conductor::RestoreWorldRequest* request,
      intrinsic_proto::conductor::RestoreWorldResponse* response) override;

 private:
  grpc::Status CancelExecutionContextOperation(grpc::ServerContext* context,
                                               bool wait)
      ABSL_LOCKS_EXCLUDED(ec_op_mu_);

  absl::StatusOr<std::unique_ptr<ExecutionContextOperation>>
  PrepareProcessStartImpl(
      grpc::ServerContext* server_context,
      std::shared_ptr<const acl::User> user,
      const intrinsic_proto::conductor::PrepareProcessStartRequest* request,
      bool reset) ABSL_LOCKS_EXCLUDED(mu_);
  std::shared_ptr<intrinsic::KeyValueStore> kv_store_;
  absl::Mutex mu_;
  std::unique_ptr<ExecutionContext> execution_context_ ABSL_GUARDED_BY(mu_);
  std::unique_ptr<ResourceWorld> resource_world_;
  intrinsic_proto::world::ObjectWorldService::Service& world_service_;
  std::unique_ptr<
      intrinsic_proto::simulation::first_party::SimulationService::Stub>
      sim_service_stub_;
  intrinsic_proto::world::WorldUpdater::Service& world_updater_;
  std::unique_ptr<intrinsic_proto::assets::v1::AssetInstances::Stub>
      asset_instances_stub_;
  std::unique_ptr<intrinsic_proto::assets::AssetDeploymentService::Stub>
      asset_deployment_stub_;
  std::unique_ptr<google::longrunning::Operations::Stub>
      asset_deployment_lro_stub_;
  std::unique_ptr<intrinsic_proto::assets::v1::InstalledAssets::Stub>
      installed_assets_stub_;
  std::unique_ptr<google::longrunning::Operations::Stub>
      installed_assets_lro_stub_;
  [[maybe_unused]] bool pause_sim_;

  absl::Mutex ec_op_mu_;
  std::unique_ptr<ExecutionContextOperation> ec_op_ ABSL_GUARDED_BY(ec_op_mu_);
  std::unique_ptr<SaveSceneMonitor> save_scene_monitor_;

  grpc::Status ResetWorldService(
      const std::vector<std::string>& worlds_to_not_delete);

  absl::Status ResetSim(grpc::ServerContext* context,
                        std::stop_token stop_token = {})
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  absl::Status ResetSim(const acl::User* user, std::stop_token stop_token = {})
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  absl::Status ResetBeliefWorldAndSim(grpc::ServerContext* context,
                                      const std::string& src_world_id,
                                      std::stop_token stop_token = {})
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  absl::Status ResetBeliefWorldAndSim(const acl::User* user,
                                      const std::string& src_world_id,
                                      std::stop_token stop_token = {})
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Constructs the default worlds (init_world and belief world) and connects to
  // the simulation service to initialize the sim_world.
  grpc::Status CreateWorldsFromSolution(grpc::ServerContext* context,
                                        bool skip_invalid_world_updates)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Pauses the WorldUpdater. Note that every pause of the world updater
  // must be matched with a corresponding ResumeWorldUpdater call, to prevent
  // leaving the world updater in a paused state.
  absl::Status PauseWorldUpdater(std::stop_token stop_token = {});
  absl::Status ResumeWorldUpdater();
};

}  // namespace intrinsic::conductor

#endif  // INTRINSIC_CONDUCTOR_CONDUCTOR_H_
