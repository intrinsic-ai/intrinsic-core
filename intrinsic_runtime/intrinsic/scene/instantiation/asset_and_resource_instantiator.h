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

#ifndef INTRINSIC_SCENE_INSTANTIATION_ASSET_AND_RESOURCE_INSTANTIATOR_H_
#define INTRINSIC_SCENE_INSTANTIATION_ASSET_AND_RESOURCE_INSTANTIATOR_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/statusor.h"
#include "google/longrunning/operations.grpc.pb.h"
#include "intrinsic/assets/proto/asset_deployment.grpc.pb.h"
#include "intrinsic/assets/proto/installed_assets.grpc.pb.h"
#include "intrinsic/kubernetes/acl/cc/client_context.h"
#include "intrinsic/scene/instantiation/imported_scene_instantiator.h"
#include "intrinsic/scene/proto/v1/imported_scene.pb.h"
#include "intrinsic/scene/proto/v1/imported_scene_updates.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_internal.pb.h"

namespace intrinsic {

// Instantiator implementation by installing imported scene objects as
// sideloaded assets and instantiate resource instances from sideloaded assets.
class AssetAndResourceInstantiator : public ImportedSceneInstantiator {
 public:
  // Creates an instantiator based on given asset deployment and installer
  // service stubs.
  static absl::StatusOr<std::unique_ptr<AssetAndResourceInstantiator>> Create(
      absl_nonnull std::shared_ptr<
          intrinsic_proto::assets::AssetDeploymentService::StubInterface>
          asset_deployment_service_stub,
      absl_nonnull
      std::shared_ptr<google::longrunning::Operations::StubInterface>
          asset_deployment_operations_stub,
      absl_nonnull std::shared_ptr<
          intrinsic_proto::assets::v1::InstalledAssets::StubInterface>
          installed_assets_service_stub,
      absl_nonnull
      std::shared_ptr<google::longrunning::Operations::StubInterface>
          installed_assets_operations_stub);

  absl::StatusOr<
      intrinsic_proto::scene_object::v1::InstantiateImportedSceneResponse>
  InstantiateImportedScene(
      const intrinsic_proto::scene_object::v1::InstantiateImportedSceneRequest&
          request,
      const acl::User& identity,
      std::shared_ptr<
          intrinsic_proto::scene_object::v1::InstantiateImportedSceneStatus>
          status) override;

  // Bundle scene object instance id and a reparent with instance_id as child
  // together because ADS requires parent and parent_t_this information together
  // with create resource instance request.
  struct InstanceWithReparentUpdate {
    std::string instance_id;
    std::optional<
        intrinsic_proto::scene_object::v1::ReparentSceneObjectInstance>
        reparent;
  };
  // Returns sorted list of scene object instance ids with reparent update so we
  // can guarantee when an instance is created its parent is already created.
  static absl::StatusOr<std::vector<InstanceWithReparentUpdate>>
  GetSortedSceneObjectInstancesWithReparentUpdate(
      const intrinsic_proto::scene_object::v1::ImportedSceneObjectInstances&
          scene_object_instances,
      const intrinsic_proto::scene_object::v1::ImportedSceneUpdates&
          instance_updates);

 private:
  explicit AssetAndResourceInstantiator(
      std::shared_ptr<
          intrinsic_proto::assets::AssetDeploymentService::StubInterface>
          asset_deployment_service_stub,
      std::shared_ptr<google::longrunning::Operations::StubInterface>
          asset_deployment_operations_stub,
      std::shared_ptr<
          intrinsic_proto::assets::v1::InstalledAssets::StubInterface>
          installed_assets_service_stub,
      std::shared_ptr<google::longrunning::Operations::StubInterface>
          installed_assets_operations_stub);

  std::shared_ptr<
      intrinsic_proto::assets::AssetDeploymentService::StubInterface>
      asset_deployment_service_stub_;
  std::shared_ptr<google::longrunning::Operations::StubInterface>
      asset_deployment_operations_stub_;
  std::shared_ptr<intrinsic_proto::assets::v1::InstalledAssets::StubInterface>
      installed_assets_service_stub_;
  std::shared_ptr<google::longrunning::Operations::StubInterface>
      installed_assets_operations_stub_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_INSTANTIATION_ASSET_AND_RESOURCE_INSTANTIATOR_H_
