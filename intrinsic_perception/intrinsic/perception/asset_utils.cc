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

#include "intrinsic/perception/asset_utils.h"

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "grpcpp/client_context.h"
#include "intrinsic/assets/data/proto/v1/data_asset.pb.h"
#include "intrinsic/assets/data/proto/v1/data_assets.grpc.pb.h"
#include "intrinsic/assets/data/proto/v1/data_assets.pb.h"
#include "intrinsic/perception/proto/v1/perception_model.pb.h"
#include "intrinsic/util/status/status_macros_grpc.h"

namespace intrinsic::perception {
namespace {

// LINT.IfChange
constexpr char kDefaultAssetPackage[] = "ai.intrinsic";
// LINT.ThenChange(//intrinsic/frontend/onprem/shared/catalog/catalog_service.ts)

// Type URL suffix of a data asset that is packaged as an ML model asset. This
// is matched by name so that this library does not depend on the inference
// service protos.
constexpr char kMlModelAssetTypeUrlSuffix[] =
    "/intrinsic_proto.ml.inference_service.v1.MlModelAsset";

}  // namespace

absl::StatusOr<intrinsic_proto::perception::v1::PerceptionModel>
GetPerceptionModelFromDataAsset(
    intrinsic_proto::assets::Id id,
    intrinsic_proto::data::v1::DataAssets::StubInterface& data_assets_stub,
    grpc::ClientContext* absl_nullable client_context) {
  if (id.package().empty()) {
    id.set_package(kDefaultAssetPackage);
  }
  intrinsic_proto::data::v1::GetDataAssetRequest get_request;
  intrinsic_proto::data::v1::DataAsset data_asset;
  *get_request.mutable_id() = id;

  grpc::ClientContext local_context;
  if (client_context == nullptr) client_context = &local_context;
  auto status = ToAbslStatus(
      data_assets_stub.GetDataAsset(client_context, get_request, &data_asset));
  if (!status.ok()) {
    return absl::InternalError(absl::StrFormat(
        "Failed to get data asset. Error: %s", status.message()));
  }

  if (absl::EndsWith(data_asset.data().type_url(),
                     kMlModelAssetTypeUrlSuffix)) {
    return absl::FailedPreconditionError(absl::StrFormat(
        "Data asset '%s' contains an ML model asset, but a perception model "
        "was expected. This software version cannot read it. Upgrade the "
        "platform and the ai.intrinsic.perception_service asset to a version "
        "that supports %s.%s packaged as an ML model asset.",
        id.name(), id.package(), id.name()));
  }

  intrinsic_proto::perception::v1::PerceptionModel perception_model;
  if (!data_asset.data().UnpackTo(&perception_model)) {
    return absl::InternalError(
        absl::StrFormat("Failed to unpack data asset to v1::perception model. "
                        "Data asset id: %s",
                        id.name()));
  }
  return perception_model;
}
}  // namespace intrinsic::perception
