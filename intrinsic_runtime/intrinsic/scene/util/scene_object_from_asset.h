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

#ifndef INTRINSIC_SCENE_UTIL_SCENE_OBJECT_FROM_ASSET_H_
#define INTRINSIC_SCENE_UTIL_SCENE_OBJECT_FROM_ASSET_H_

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/assets/proto/id.pb.h"
#include "intrinsic/assets/proto/installed_assets.grpc.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"

namespace intrinsic {

absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromAssetManifest(
    const intrinsic_proto::scene_objects::ProcessedSceneObjectManifest&
        manifest);

// Returns the SceneObject for the given asset id installed in the solution.
// The returned SceneObject is post-processed with the default SceneObjectConfig
// installed with the asset.
absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromAsset(const intrinsic_proto::assets::Id& id,
                     absl::string_view installed_asset_reader_address);

// Same as above but uses the provided stub to retrieve the installed asset.
absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromAsset(
    const intrinsic_proto::assets::Id& id,
    intrinsic_proto::assets::v1::InstalledAssetsReader::StubInterface* stub);

}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_UTIL_SCENE_OBJECT_FROM_ASSET_H_
