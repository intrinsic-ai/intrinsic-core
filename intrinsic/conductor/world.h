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

#ifndef INTRINSIC_CONDUCTOR_WORLD_H_
#define INTRINSIC_CONDUCTOR_WORLD_H_

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic/assets/proto/v1alpha1/asset_info_internal.grpc.pb.h"
#include "intrinsic/conductor/proto/wrapper_bridge.pb.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"

namespace intrinsic::conductor {

// Default deadline for fetching the geometric resource set data from the
// AssetInfoInternal service.
inline constexpr absl::Duration kDefaultAssetInfoInternalTimeout =
    absl::Seconds(30);

// Composes a world from the current resource set and stores it as `world_id`,
// overwriting any existing world with that ID.
//
// If `skip_invalid_world_updates` is true, world updates and scene objects that
// cannot be applied are skipped and reported in the result. Otherwise, the
// composition fails on the first error.
absl::StatusOr<internal::UpdateWorldFromResourceSetResult> CreateOrUpdateWorld(
    absl::string_view world_id,
    intrinsic_proto::assets::v1alpha1::AssetInfoInternal::StubInterface&
        asset_info_internal,
    intrinsic_proto::world::ObjectWorldService::Service& world_service,
    bool skip_invalid_world_updates,
    absl::Duration asset_info_internal_timeout =
        kDefaultAssetInfoInternalTimeout);

}  // namespace intrinsic::conductor

#endif  // INTRINSIC_CONDUCTOR_WORLD_H_
