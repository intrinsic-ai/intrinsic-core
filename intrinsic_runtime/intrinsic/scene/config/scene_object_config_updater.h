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

#ifndef INTRINSIC_SCENE_CONFIG_SCENE_OBJECT_CONFIG_UPDATER_H_
#define INTRINSIC_SCENE_CONFIG_SCENE_OBJECT_CONFIG_UPDATER_H_

#include "absl/status/status.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_config.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_updates.pb.h"

namespace intrinsic {
namespace scene_object {

// Modifies the declarative configuration using the provided updates.
// Only handles named joint configs and cartesian limits.
absl::Status ApplyUpdatesToConfig(
    const intrinsic_proto::scene_object::v1::SceneObject& object,
    const intrinsic_proto::scene_object::v1::SceneObjectInstanceUpdates&
        updates,
    intrinsic_proto::scene_object::v1::SceneObjectConfig* config);

}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_CONFIG_SCENE_OBJECT_CONFIG_UPDATER_H_
