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

#include "intrinsic/scene/util/scene_object_from_asset.h"

#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/client_context.h"
#include "intrinsic/assets/id_utils.h"
#include "intrinsic/assets/proto/installed_assets.grpc.pb.h"
#include "intrinsic/assets/proto/installed_assets.pb.h"
#include "intrinsic/assets/scene_objects/proto/scene_object_manifest.pb.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/scene/config/scene_object_config.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"

namespace intrinsic {

using ::intrinsic_proto::assets::v1::InstalledAssetsReader;

namespace {

absl::StatusOr<std::unique_ptr<InstalledAssetsReader::StubInterface>>
CreateInstalledAssetsStub(
    const absl::string_view installed_asset_reader_address) {
  INTR_ASSIGN_OR_RETURN(
      auto channel, connect::CreateClientChannel(
                        installed_asset_reader_address,
                        absl::Now() + connect::kGrpcClientConnectDefaultTimeout,
                        connect::UnlimitedMessageSizeGrpcChannelArgs()));
  return InstalledAssetsReader::NewStub(channel);
}

absl::StatusOr<intrinsic_proto::scene_objects::ProcessedSceneObjectManifest>
GetSceneObjectData(intrinsic_proto::assets::Id id,
                   InstalledAssetsReader::StubInterface* stub) {
  intrinsic_proto::assets::v1::InstalledAsset installed_asset;
  {
    grpc::ClientContext client_context;
    intrinsic_proto::assets::v1::GetInstalledAssetRequest
        get_installed_asset_request;
    *get_installed_asset_request.mutable_id() = std::move(id);
    get_installed_asset_request.set_view(
        intrinsic_proto::catalog::AssetViewType::ASSET_VIEW_TYPE_FULL);
    INTR_RETURN_IF_ERROR_GRPC(stub->GetInstalledAsset(
        &client_context, get_installed_asset_request, &installed_asset));
  }

  if (!installed_asset.deployment_data().has_scene_object()) {
    INTR_ASSIGN_OR_RETURN(
        auto idv_str,
        assets::IdVersionFromProto(installed_asset.metadata().id_version()));
    return absl::NotFoundError(absl::Substitute(
        "Installed asset $0 does not have a scene object., it contains $1",
        idv_str, installed_asset));
  }
  return installed_asset.deployment_data().scene_object().manifest();
}

}  // namespace

absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromAssetManifest(
    const intrinsic_proto::scene_objects::ProcessedSceneObjectManifest&
        manifest) {
  const auto& scene_object_data = manifest.assets();
  auto scene_object = scene_object_data.scene_object_model();

  INTR_ASSIGN_OR_RETURN(
      auto updated,
      scene_object::ProcessSceneObjectConfig(
          scene_object, scene_object_data.default_scene_object_config()));
  scene_object = updated.result;

  return scene_object;
}

absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromAsset(const intrinsic_proto::assets::Id& id,
                     absl::string_view installed_asset_reader_address) {
  INTR_ASSIGN_OR_RETURN(
      auto installed_assets,
      CreateInstalledAssetsStub(installed_asset_reader_address));
  return SceneObjectFromAsset(id, installed_assets.get());
}

absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromAsset(
    const intrinsic_proto::assets::Id& id,
    intrinsic_proto::assets::v1::InstalledAssetsReader::StubInterface* stub) {
  INTR_ASSIGN_OR_RETURN(auto scene_object_data, GetSceneObjectData(id, stub));
  return SceneObjectFromAssetManifest(scene_object_data);
}

}  // namespace intrinsic
