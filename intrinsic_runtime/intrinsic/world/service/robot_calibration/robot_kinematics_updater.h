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

#ifndef INTRINSIC_WORLD_SERVICE_ROBOT_CALIBRATION_ROBOT_KINEMATICS_UPDATER_H_
#define INTRINSIC_WORLD_SERVICE_ROBOT_CALIBRATION_ROBOT_KINEMATICS_UPDATER_H_

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/longrunning/operations.grpc.pb.h"
#include "grpcpp/server.h"
#include "grpcpp/support/status.h"
#include "intrinsic/assets/proto/asset_deployment.grpc.pb.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/resources/client/resource_registry_client_interface.h"
#include "intrinsic/resources/proto/resource_registry.pb.h"
#include "intrinsic/util/grpc/channel.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/service/robot_calibration/robot_update_service.grpc.pb.h"
#include "intrinsic/world/service/robot_calibration/robot_update_service.pb.h"

namespace intrinsic::icon {

class RobotKinematicsUpdater
    : public ::intrinsic_proto::world::RobotUpdateService::Service {
 public:
  RobotKinematicsUpdater(
      intrinsic_proto::world::ObjectWorldService::StubInterface*
          object_world_stub,
      intrinsic_proto::assets::AssetDeploymentService::StubInterface*
          asset_deployment_service_stub,
      google::longrunning::Operations::StubInterface*
          asset_deployment_operations_stub,
      resources::ResourceRegistryClientInterface* resource_registry_client,
      icon::ChannelFactory* icon_channel_factory);

  ::grpc::Status UpdateRobotKinematics(
      ::grpc::ServerContext* context,
      const ::intrinsic_proto::world::RobotKinematicsUpdateRequest* request,
      ::intrinsic_proto::world::RobotUpdateResponse* response) override;

  ::grpc::Status CheckWorldMatchesHardwareKinematics(
      ::grpc::ServerContext* context,
      const ::intrinsic_proto::world::
          CheckWorldMatchesHardwareKinematicsRequest* request,
      ::intrinsic_proto::world::CheckWorldMatchesHardwareKinematicsResponse*
          response) override;

  ::grpc::Status UpdateRobotLimits(
      ::grpc::ServerContext* context,
      const ::intrinsic_proto::world::UpdateRobotLimitsRequest* request,
      ::intrinsic_proto::world::UpdateRobotLimitsResponse* response) override;

 private:
  absl::Status RestartICON(absl::string_view resource_id);
  absl::StatusOr<std::shared_ptr<intrinsic::Channel>> CreateChannel(
      const ::intrinsic_proto::resources::ResourceHandle& resource_handle)
      const;

  intrinsic_proto::world::ObjectWorldService::StubInterface* object_world_stub_;
  intrinsic_proto::assets::AssetDeploymentService::StubInterface*
      asset_deployment_service_stub_;
  google::longrunning::Operations::StubInterface*
      asset_deployment_operations_stub_;
  resources::ResourceRegistryClientInterface* resource_registry_client_;
  icon::ChannelFactory* icon_channel_factory_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_WORLD_SERVICE_ROBOT_CALIBRATION_ROBOT_KINEMATICS_UPDATER_H_
