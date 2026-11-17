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

#ifndef INTRINSIC_SCENE_UTIL_SCENE_OBJECT_CREATION_H_
#define INTRINSIC_SCENE_UTIL_SCENE_OBJECT_CREATION_H_

#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_updates.pb.h"

namespace intrinsic {
namespace scene_object {

absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SingleLinkSceneObjectFromGeometry(const Geometry& geo,
                                  GeometrySerializer* geo_serializer);

// Creates the scene object update proto for the given system and application
// limits. Joints are named joint_a1, joint_a2, ..., joint_a6.
intrinsic_proto::scene_object::v1::UpdateJointsRequest ToSceneObjectUpdate(
    const intrinsic::JointLimits& system_limits,
    const intrinsic::JointLimits& application_limits);

}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_UTIL_SCENE_OBJECT_CREATION_H_
